#define _XOPEN_SOURCE 700

#include <errno.h>
#include <limits.h>
#include <sqlite3.h>
#include <stdarg.h>
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
  const char *session_db;
  long long session_id;
  long long last_logged_call_id;
  char last_status[96];
} hill_event_context;

typedef struct hill_call_summary {
  long long call_id;
  long round_index;
  long attempt_index;
  long http_status;
  double total_seconds;
  char request_kind[64];
  char state[32];
} hill_call_summary;

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

static void hill_log_prefix(unsigned int depth)
{
  unsigned int index;

  if (depth == 0U) {
    depth = 1U;
  }
  for (index = 0U; index < depth; index++) {
    fputc('>', stderr);
  }
  fputc(' ', stderr);
}

static void hill_log_line(unsigned int depth, const char *format, ...)
{
  va_list arguments;

  hill_log_prefix(depth);
  va_start(arguments, format);
  vfprintf(stderr, format, arguments);
  va_end(arguments);
  fputc('\n', stderr);
  fflush(stderr);
}

static void hill_log_multiline(unsigned int depth, const char *text)
{
  const char *cursor;

  if ((text == NULL) || (text[0] == '\0')) {
    hill_log_line(depth, "(empty)");
    return;
  }
  cursor = text;
  while (cursor[0] != '\0') {
    const char *newline;
    size_t length;

    newline = strchr(cursor, '\n');
    length = (newline != NULL) ? (size_t)(newline - cursor) : strlen(cursor);
    hill_log_prefix(depth);
    if (length > 0U) {
      fwrite(cursor, 1U, length, stderr);
    }
    fputc('\n', stderr);
    if (newline == NULL) {
      break;
    }
    cursor = newline + 1;
  }
  fflush(stderr);
}

static size_t hill_utf8_width(unsigned char first)
{
  if ((first & 0x80U) == 0U) {
    return 1U;
  }
  if ((first & 0xE0U) == 0xC0U) {
    return 2U;
  }
  if ((first & 0xF0U) == 0xE0U) {
    return 3U;
  }
  if ((first & 0xF8U) == 0xF0U) {
    return 4U;
  }
  return 1U;
}

static void hill_log_preview(unsigned int depth, const char *text)
{
  enum { HILL_PREVIEW_BYTES = 240 };
  char preview[HILL_PREVIEW_BYTES + 1];
  size_t source_index;
  size_t target_index;
  int last_was_space;
  int truncated;

  if ((text == NULL) || (text[0] == '\0')) {
    hill_log_line(depth, "(empty)");
    return;
  }
  source_index = 0U;
  target_index = 0U;
  last_was_space = 0;
  while (text[source_index] != '\0') {
    unsigned char first;
    size_t width;

    first = (unsigned char)text[source_index];
    if ((first == (unsigned char)' ') ||
        (first == (unsigned char)'\n') ||
        (first == (unsigned char)'\r') ||
        (first == (unsigned char)'\t')) {
      source_index++;
      if (!last_was_space && (target_index > 0U)) {
        if (target_index >= HILL_PREVIEW_BYTES) {
          break;
        }
        preview[target_index++] = ' ';
        last_was_space = 1;
      }
      continue;
    }
    width = hill_utf8_width(first);
    if ((target_index + width) > HILL_PREVIEW_BYTES) {
      break;
    }
    if (width > 1U) {
      size_t continuation;

      for (continuation = 1U; continuation < width; continuation++) {
        unsigned char value;

        value = (unsigned char)text[source_index + continuation];
        if ((value == 0U) || ((value & 0xC0U) != 0x80U)) {
          width = 1U;
          break;
        }
      }
    }
    memcpy(preview + target_index, text + source_index, width);
    target_index += width;
    source_index += width;
    last_was_space = 0;
  }
  while ((target_index > 0U) && (preview[target_index - 1U] == ' ')) {
    target_index--;
  }
  preview[target_index] = '\0';
  truncated = text[source_index] != '\0';
  hill_log_line(depth, "%s%s", preview, truncated ? "..." : "");
}

static const char *hill_request_kind_label(const char *request_kind)
{
  if (request_kind == NULL) {
    return "Unknown";
  }
  if (strcmp(request_kind, "user") == 0) {
    return "User";
  }
  if (strcmp(request_kind, "tool_continuation") == 0) {
    return "Tool Continuation";
  }
  if (strcmp(request_kind, "tool_audit") == 0) {
    return "Harness Audit";
  }
  if (strcmp(request_kind, "retry") == 0) {
    return "Retry";
  }
  return request_kind;
}

static const char *hill_status_label(const char *status_kind)
{
  if (status_kind == NULL) {
    return "Working";
  }
  if (strcmp(status_kind, "thinking") == 0) {
    return "Thinking";
  }
  if (strcmp(status_kind, "tools") == 0) {
    return "Running Tools";
  }
  if (strcmp(status_kind, "retry_wait") == 0) {
    return "Retry Wait";
  }
  if (strcmp(status_kind, "retrying") == 0) {
    return "Retrying";
  }
  return status_kind;
}

static int hill_load_next_call(hill_event_context *context,
                               hill_call_summary *summary)
{
  static const char *sql =
    "SELECT id,request_kind,round_index,attempt_index,state,http_status,"
    "total_seconds FROM response_api_calls "
    "WHERE session_id = ? AND id > ? ORDER BY id LIMIT 1;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  const unsigned char *request_kind;
  const unsigned char *state;
  int rc;

  if ((context == NULL) || (summary == NULL) ||
      (context->session_db == NULL)) {
    return -1;
  }
  db = NULL;
  stmt = NULL;
  rc = sqlite3_open_v2(context->session_db,
                       &db,
                       SQLITE_OPEN_READONLY,
                       NULL);
  if (rc != SQLITE_OK) {
    hill_log_line(3U,
                  "Logging Error | Could not open session database: %s",
                  (db != NULL) ? sqlite3_errmsg(db) : "unknown error");
    sqlite3_close(db);
    return -1;
  }
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt,
                          1,
                          (sqlite3_int64)context->session_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt,
                          2,
                          (sqlite3_int64)context->last_logged_call_id) !=
       SQLITE_OK)) {
    hill_log_line(3U,
                  "Logging Error | Could not query API turns: %s",
                  sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return -1;
  }
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE) {
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }
  if (rc != SQLITE_ROW) {
    hill_log_line(3U,
                  "Logging Error | Could not read API turn: %s",
                  sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return -1;
  }

  memset(summary, 0, sizeof(*summary));
  summary->call_id = (long long)sqlite3_column_int64(stmt, 0);
  request_kind = sqlite3_column_text(stmt, 1);
  summary->round_index = (long)sqlite3_column_int64(stmt, 2);
  summary->attempt_index = (long)sqlite3_column_int64(stmt, 3);
  state = sqlite3_column_text(stmt, 4);
  summary->http_status = (long)sqlite3_column_int64(stmt, 5);
  summary->total_seconds = sqlite3_column_double(stmt, 6);
  snprintf(summary->request_kind,
           sizeof(summary->request_kind),
           "%s",
           (request_kind != NULL) ? (const char *)request_kind : "unknown");
  snprintf(summary->state,
           sizeof(summary->state),
           "%s",
           (state != NULL) ? (const char *)state : "unknown");
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}

static int hill_text_has_value(const char *text)
{
  return (text != NULL) && (text[0] != '\0');
}

static void hill_log_timeline_item(
  const hill_call_summary *summary,
  const strappy_session_message_record *record)
{
  const char *role;
  const char *kind;
  const char *content;

  if ((summary == NULL) || (record == NULL)) {
    return;
  }
  role = (record->render_role != NULL) ? record->render_role : "unknown";
  kind = (record->kind != NULL) ? record->kind : "item";
  content = (record->content != NULL) ? record->content : "";

  if (strcmp(role, "user") == 0) {
    hill_log_line(5U, "User Prompt");
    hill_log_multiline(6U, content);
  } else if (strcmp(role, "harness") == 0) {
    hill_log_line(5U, "Harness Prompt");
    hill_log_multiline(6U, content);
  } else if (strcmp(role, "developer") == 0) {
    hill_log_line(5U,
                  "%s",
                  (strcmp(summary->request_kind, "tool_audit") == 0) ?
                    "Harness Prompt" : "Developer Prompt");
    hill_log_multiline(6U, content);
  } else if (strcmp(role, "assistant") == 0) {
    hill_log_line(5U, "Assistant");
    hill_log_multiline(6U, content);
  } else if (strcmp(role, "api_reasoning") == 0) {
    hill_log_line(5U,
                  "Reasoning | %lu characters",
                  (unsigned long)strlen(content));
    hill_log_preview(6U, content);
  } else if (strcmp(role, "api_function_call") == 0) {
    hill_log_line(5U,
                  "Tool Call | %s",
                  hill_text_has_value(record->tool_name) ?
                    record->tool_name : kind);
    if (hill_text_has_value(record->arguments_json)) {
      hill_log_preview(6U, record->arguments_json);
    }
  } else if (strcmp(role, "api_function_output") == 0) {
    const char *result;

    result = hill_text_has_value(record->result_json) ?
      record->result_json : content;
    hill_log_line(5U,
                  "Tool Result%s | %lu characters",
                  record->is_error ? " | Error" : "",
                  (unsigned long)strlen(result));
    hill_log_preview(6U, result);
  } else if (strcmp(role, "api_item") == 0) {
    hill_log_line(5U, "Server Tool | %s", kind);
    if (hill_text_has_value(content) && (strcmp(content, kind) != 0)) {
      hill_log_preview(6U, content);
    }
  } else if (strcmp(role, "api_error") == 0) {
    hill_log_line(5U, "API Error");
    hill_log_preview(6U, content);
  } else {
    hill_log_line(5U, "%s | %s", role, kind);
    if (hill_text_has_value(content)) {
      hill_log_preview(6U, content);
    }
  }
}

static void hill_log_call(hill_event_context *context,
                          const hill_call_summary *summary)
{
  strappy_session_message_record_list timeline;
  char *error;
  const char *last_direction;
  size_t index;
  int response_heading_logged;

  hill_log_line(3U,
                "API Turn %lld | %s | Round %ld | Attempt %ld",
                summary->call_id,
                hill_request_kind_label(summary->request_kind),
                summary->round_index + 1L,
                summary->attempt_index + 1L);

  error = NULL;
  last_direction = NULL;
  response_heading_logged = 0;
  strappy_session_message_record_list_init(&timeline);
  if (!strappy_db_list_response_timeline(context->session_db,
                                         context->session_id,
                                         &timeline,
                                         &error)) {
    hill_log_line(4U,
                  "Logging Error | %s",
                  (error != NULL) ? error : "Could not load timeline");
    strappy_session_message_record_list_destroy(&timeline);
    strappy_free_string(error);
    return;
  }

  for (index = 0U; index < timeline.count; index++) {
    strappy_session_message_record *record;
    const char *direction;

    record = &timeline.records[index];
    if ((record->turn_id != summary->call_id) ||
        ((record->kind != NULL) &&
         (strcmp(record->kind, "response_api_call") == 0))) {
      continue;
    }
    direction = (record->direction != NULL) ? record->direction : "response";
    if ((last_direction == NULL) ||
        (strcmp(last_direction, direction) != 0)) {
      if (strcmp(direction, "request") == 0) {
        hill_log_line(4U, "Request");
      } else {
        hill_log_line(4U,
                      "Response | %s | HTTP %ld | %.1fs",
                      summary->state,
                      summary->http_status,
                      summary->total_seconds);
        response_heading_logged = 1;
      }
      last_direction = direction;
    }
    hill_log_timeline_item(summary, record);
  }
  if (!response_heading_logged) {
    hill_log_line(4U,
                  "Response | %s | HTTP %ld | %.1fs",
                  summary->state,
                  summary->http_status,
                  summary->total_seconds);
  }
  strappy_session_message_record_list_destroy(&timeline);
  strappy_free_string(error);
}

static void hill_log_completed_calls(hill_event_context *context)
{
  hill_call_summary summary;
  int loaded;

  do {
    loaded = hill_load_next_call(context, &summary);
    if (loaded == 1) {
      hill_log_call(context, &summary);
      context->last_logged_call_id = summary.call_id;
      context->last_status[0] = '\0';
    }
  } while (loaded == 1);
}

static int hill_event_callback(const strappy_responses_event *event,
                               void *user_data)
{
  hill_event_context *context;

  context = (hill_event_context *)user_data;
  if ((event == NULL) || (context == NULL)) {
    return 1;
  }
  if (event->type == STRAPPY_RESPONSES_EVENT_LEDGER_CHANGED) {
    hill_log_completed_calls(context);
  } else if ((event->type == STRAPPY_RESPONSES_EVENT_PROCESSING_STATUS) &&
      (event->status_kind != NULL) &&
      (strcmp(context->last_status, event->status_kind) != 0)) {
    snprintf(context->last_status,
             sizeof(context->last_status),
             "%s",
             event->status_kind);
    if ((strcmp(event->status_kind, "retry_wait") == 0) &&
        (event->retry_attempt > 0U)) {
      hill_log_line(3U,
                    "Activity | %s | Attempt %u of %u | %s",
                    hill_status_label(event->status_kind),
                    event->retry_attempt,
                    event->retry_max_attempts,
                    hill_text_has_value(event->status_reason) ?
                      event->status_reason : "waiting");
    } else {
      hill_log_line(3U,
                    "Activity | %s",
                    hill_status_label(event->status_kind));
    }
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
  events.session_db = options.session_db;
  events.session_id = session_id;
  hill_log_line(2U,
                "Session %lld | %s | Started",
                session_id,
                options.model);
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
    hill_log_line(2U,
                  "Session %lld | Failed",
                  session_id);
    hill_log_line(3U,
                  "%s",
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

  hill_log_line(2U,
                "Session %lld | Complete",
                session_id);
  hill_log_line(3U,
                "Answer | %s",
                options.answer_file);
  return 0;
}
