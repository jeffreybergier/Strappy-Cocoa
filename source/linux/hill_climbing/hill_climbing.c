#define _XOPEN_SOURCE 700

#include <errno.h>
#include <limits.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "strappy_client.h"
#include "strappy_core.h"
#include "strappy_db.h"
#include "strappy_responses.h"

typedef struct hill_options {
  const char *model;
  const char *session_db;
  const char *media_db;
  const char *env_file;
  const char *system_prompt;
  const char *answer_file;
  const char *prompt;
} hill_options;

typedef struct hill_event_context {
  const char *model;
  char last_status[96];
} hill_event_context;

static const char *HILL_MODELS_JSON =
  "{\"data\":["
  "{\"id\":\"z-ai/glm-5.2\",\"name\":\"GLM 5.2\","
  "\"context_length\":200000,\"supported_parameters\":[\"tools\"]},"
  "{\"id\":\"deepseek/deepseek-v4-pro\",\"name\":\"DeepSeek V4 Pro\","
  "\"context_length\":200000,\"supported_parameters\":[\"tools\"]},"
  "{\"id\":\"google/gemma-4-31b-it\",\"name\":\"Gemma 4 31B IT\","
  "\"context_length\":131072,\"supported_parameters\":[\"tools\"]},"
  "{\"id\":\"qwen/qwen3.6-27b\",\"name\":\"Qwen 3.6 27B\","
  "\"context_length\":131072,\"supported_parameters\":[\"tools\"]}"
  "]}";

static void hill_usage(const char *program)
{
  fprintf(stderr,
          "Usage: %s --model ID --session-db PATH --media-db PATH "
          "--env-file PATH --system-prompt PATH --answer-file PATH "
          "--prompt TEXT\n",
          program);
}

static int hill_parse_options(int argc, char **argv, hill_options *options)
{
  int index;

  memset(options, 0, sizeof(*options));
  for (index = 1; index < argc; index++) {
    const char *name;
    const char *value;

    name = argv[index];
    if ((strcmp(name, "--help") == 0) || (strcmp(name, "-h") == 0)) {
      return -1;
    }
    if ((index + 1) >= argc) {
      fprintf(stderr, "Missing value for %s.\n", name);
      return 0;
    }
    value = argv[++index];
    if (strcmp(name, "--model") == 0) {
      options->model = value;
    } else if (strcmp(name, "--session-db") == 0) {
      options->session_db = value;
    } else if (strcmp(name, "--media-db") == 0) {
      options->media_db = value;
    } else if (strcmp(name, "--env-file") == 0) {
      options->env_file = value;
    } else if (strcmp(name, "--system-prompt") == 0) {
      options->system_prompt = value;
    } else if (strcmp(name, "--answer-file") == 0) {
      options->answer_file = value;
    } else if (strcmp(name, "--prompt") == 0) {
      options->prompt = value;
    } else {
      fprintf(stderr, "Unknown option: %s.\n", name);
      return 0;
    }
  }

  return (options->model != NULL) &&
    (options->session_db != NULL) &&
    (options->media_db != NULL) &&
    (options->env_file != NULL) &&
    (options->system_prompt != NULL) &&
    (options->answer_file != NULL) &&
    (options->prompt != NULL);
}

static void hill_remove_sqlite_family(const char *path)
{
  char sidecar[PATH_MAX];

  if (path == NULL) {
    return;
  }
  unlink(path);
  if (snprintf(sidecar, sizeof(sidecar), "%s-wal", path) > 0) {
    unlink(sidecar);
  }
  if (snprintf(sidecar, sizeof(sidecar), "%s-shm", path) > 0) {
    unlink(sidecar);
  }
}

static int hill_write_answer(const char *path, const char *answer)
{
  FILE *file;
  size_t length;

  file = fopen(path, "wb");
  if (file == NULL) {
    fprintf(stderr, "Could not open answer output %s: %s\n",
            path,
            strerror(errno));
    return 0;
  }
  length = strlen(answer);
  if ((fwrite(answer, 1U, length, file) != length) ||
      (fwrite("\n", 1U, 1U, file) != 1U) ||
      (fclose(file) != 0)) {
    fprintf(stderr, "Could not write answer output %s.\n", path);
    return 0;
  }
  return 1;
}

static int hill_event_callback(const strappy_responses_event *event,
                               void *user_data)
{
  hill_event_context *context;

  context = (hill_event_context *)user_data;
  if ((event == NULL) || (context == NULL)) {
    return 1;
  }
  if ((event->type == STRAPPY_RESPONSES_EVENT_PROCESSING_STATUS) &&
      (event->status_kind != NULL) &&
      (strcmp(context->last_status, event->status_kind) != 0)) {
    snprintf(context->last_status,
             sizeof(context->last_status),
             "%s",
             event->status_kind);
    fprintf(stderr, "[%s] %s\n", context->model, event->status_kind);
    fflush(stderr);
  }
  return 1;
}

static int hill_seed_model(const char *session_db,
                           const char *model,
                           char **error_out)
{
  if (!strappy_db_save_openrouter_models_json(session_db,
                                               HILL_MODELS_JSON,
                                               error_out) ||
      !strappy_db_set_openrouter_model_allowed(session_db,
                                                model,
                                                1,
                                                error_out) ||
      !strappy_db_set_default_openrouter_model(session_db,
                                                model,
                                                error_out)) {
    return 0;
  }
  return 1;
}

static int hill_register_media_database(const char *session_db,
                                        const char *media_db,
                                        char **error_out)
{
  strappy_discovered_database_input input;
  strappy_discovered_database_record_list list;
  struct stat info;
  char resolved[PATH_MAX];
  size_t index;
  long long catalog_id;
  int found;
  int ok;

  if (realpath(media_db, resolved) == NULL) {
    strappy_set_formatted_error(error_out,
                                "Could not resolve MediaLibrary path: %s",
                                strerror(errno));
    return 0;
  }
  if (stat(resolved, &info) != 0) {
    strappy_set_formatted_error(error_out,
                                "Could not stat MediaLibrary fixture: %s",
                                strerror(errno));
    return 0;
  }

  memset(&input, 0, sizeof(input));
  input.path = resolved;
  input.size = (long long)info.st_size;
  input.modified_at = (long long)info.st_mtime;
  input.device = (unsigned long long)info.st_dev;
  input.inode = (unsigned long long)info.st_ino;
  input.is_valid_sqlite = 1;
  input.scan_root = "hill_climbing/private";
  input.app_group_key = "com.apple.mobileipod";
  input.app_name = "Music";
  input.app_bundle_id = "com.apple.mobileipod";
  input.app_source = "hill_climbing private fixture";
  input.origin_kind = "device_fixture";
  input.location_tail = "Media/iTunes_Control/iTunes/MediaLibrary.sqlitedb";
  input.hidden = 0;

  if (!strappy_db_save_discovered_databases(session_db,
                                             &input,
                                             1U,
                                             error_out)) {
    return 0;
  }

  strappy_discovered_database_record_list_init(&list);
  if (!strappy_db_list_discovered_databases(session_db, &list, error_out)) {
    strappy_discovered_database_record_list_destroy(&list);
    return 0;
  }
  found = 0;
  catalog_id = 0LL;
  for (index = 0U; index < list.count; index++) {
    if ((list.records[index].path != NULL) &&
        (strcmp(list.records[index].path, resolved) == 0)) {
      catalog_id = list.records[index].catalog_id;
      found = 1;
      break;
    }
  }
  if (!found) {
    strappy_set_error(error_out,
                      "MediaLibrary fixture disappeared from the catalog.");
    strappy_discovered_database_record_list_destroy(&list);
    return 0;
  }
  ok = strappy_db_update_discovered_database_decision(session_db,
                                                       catalog_id,
                                                       "allowed",
                                                       error_out);
  strappy_discovered_database_record_list_destroy(&list);
  return ok;
}

int main(int argc, char **argv)
{
  hill_options options;
  hill_event_context events;
  char *answer;
  char *error;
  long long session_id;
  int parsed;
  int ok;

  parsed = hill_parse_options(argc, argv, &options);
  if (parsed <= 0) {
    hill_usage(argv[0]);
    return (parsed < 0) ? 0 : 2;
  }

  hill_remove_sqlite_family(options.session_db);
  error = NULL;
  session_id = 0LL;
  ok = strappy_db_initialize(options.session_db, &error) &&
    hill_seed_model(options.session_db, options.model, &error) &&
    strappy_db_create_session(options.session_db, &session_id, &error) &&
    strappy_db_update_session_web_search_enabled(options.session_db,
                                                  session_id,
                                                  1,
                                                  &error) &&
    hill_register_media_database(options.session_db,
                                 options.media_db,
                                 &error);
  if (!ok) {
    fprintf(stderr,
            "Could not prepare hill-climbing run for %s: %s\n",
            options.model,
            (error != NULL) ? error : "unknown error");
    strappy_free_string(error);
    return 1;
  }

  memset(&events, 0, sizeof(events));
  events.model = options.model;
  fprintf(stderr, "[%s] session %lld started\n", options.model, session_id);
  answer = strappy_responses_send_prompt_for_session_and_store_with_events(
    options.prompt,
    options.env_file,
    NULL,
    NULL,
    options.system_prompt,
    options.session_db,
    session_id,
    hill_event_callback,
    &events,
    &error);
  if (answer == NULL) {
    fprintf(stderr,
            "Hill-climbing run failed for %s: %s\n",
            options.model,
            (error != NULL) ? error : "unknown error");
    strappy_free_string(error);
    return 1;
  }

  ok = hill_write_answer(options.answer_file, answer);
  free(answer);
  strappy_free_string(error);
  if (!ok) {
    return 1;
  }

  fprintf(stderr,
          "[%s] session %lld complete; answer saved to %s\n",
          options.model,
          session_id,
          options.answer_file);
  return 0;
}

