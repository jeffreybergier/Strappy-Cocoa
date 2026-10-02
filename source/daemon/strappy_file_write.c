#include "strappy_file_write.h"

#include "strappy_core.h"
#include "strappy_file_mutation.h"

#include <cJSON.h>
#include <stdlib.h>
#include <string.h>

typedef struct strappy_file_write_arguments {
  char *path;
  char *content;
  size_t content_length;
} strappy_file_write_arguments;

static void strappy_file_write_arguments_init(
  strappy_file_write_arguments *arguments)
{
  arguments->path = NULL;
  arguments->content = NULL;
  arguments->content_length = 0U;
}

static void strappy_file_write_arguments_destroy(
  strappy_file_write_arguments *arguments)
{
  free(arguments->path);
  free(arguments->content);
  strappy_file_write_arguments_init(arguments);
}

static int strappy_file_write_parse_arguments(
  const char *arguments_json,
  strappy_file_write_arguments *arguments,
  char **error_out)
{
  cJSON *root;
  cJSON *item;
  int path_count;
  int content_count;

  strappy_file_write_arguments_init(arguments);
  if (strappy_file_mutation_json_has_null_escape(arguments_json)) {
    strappy_set_error(error_out,
                      "file_write arguments cannot contain U+0000.");
    return 0;
  }
  root = cJSON_Parse((arguments_json != NULL) ? arguments_json : "");
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    strappy_set_error(error_out,
                      "file_write arguments must be a JSON object.");
    return 0;
  }

  path_count = 0;
  content_count = 0;
  for (item = root->child; item != NULL; item = item->next) {
    if ((item->string != NULL) && (strcmp(item->string, "path") == 0)) {
      size_t path_length;

      path_count++;
      path_length = (cJSON_IsString(item) && (item->valuestring != NULL)) ?
        strlen(item->valuestring) : 0U;
      if ((path_count != 1) || !cJSON_IsString(item) ||
          (item->valuestring == NULL) || (path_length == 0U) ||
          (path_length > STRAPPY_FILE_MUTATION_MAX_PATH_BYTES) ||
          !strappy_file_mutation_validate_utf8(item->valuestring,
                                               path_length)) {
        cJSON_Delete(root);
        strappy_file_write_arguments_destroy(arguments);
        strappy_set_error(
          error_out,
          "file_write path must be one non-empty UTF-8 string of at most "
          "4096 bytes.");
        return 0;
      }
      arguments->path = strappy_string_duplicate(item->valuestring);
      if (arguments->path == NULL) {
        cJSON_Delete(root);
        strappy_file_write_arguments_destroy(arguments);
        strappy_set_error(error_out,
                          "Could not allocate the file_write path.");
        return 0;
      }
      continue;
    }
    if ((item->string != NULL) && (strcmp(item->string, "content") == 0)) {
      size_t content_length;

      content_count++;
      content_length = (cJSON_IsString(item) && (item->valuestring != NULL)) ?
        strlen(item->valuestring) : 0U;
      if ((content_count != 1) || !cJSON_IsString(item) ||
          (item->valuestring == NULL) ||
          (content_length > STRAPPY_FILE_MUTATION_MAX_CONTENT_BYTES) ||
          !strappy_file_mutation_validate_utf8(item->valuestring,
                                               content_length)) {
        cJSON_Delete(root);
        strappy_file_write_arguments_destroy(arguments);
        strappy_set_error(
          error_out,
          "file_write content must be one valid UTF-8 string of at most "
          "1 MiB.");
        return 0;
      }
      arguments->content = strappy_string_duplicate(item->valuestring);
      if (arguments->content == NULL) {
        cJSON_Delete(root);
        strappy_file_write_arguments_destroy(arguments);
        strappy_set_error(error_out,
                          "Could not allocate the file_write content.");
        return 0;
      }
      arguments->content_length = content_length;
      continue;
    }

    cJSON_Delete(root);
    strappy_file_write_arguments_destroy(arguments);
    strappy_set_error(error_out,
                      "file_write accepts only path and content.");
    return 0;
  }

  cJSON_Delete(root);
  if ((path_count != 1) || (content_count != 1)) {
    strappy_file_write_arguments_destroy(arguments);
    strappy_set_error(error_out,
                      "file_write requires path and content strings.");
    return 0;
  }
  return 1;
}

char *strappy_file_write_execute(const char *session_db_path,
                                 long long session_id,
                                 const char *arguments_json,
                                 char **error_out)
{
  strappy_file_write_arguments arguments;
  char *resolved_path;
  char *result;
  int locked;
  int ok;

  if (!strappy_file_mutation_require_coding_session(session_db_path,
                                                    session_id,
                                                    "file_write",
                                                    error_out)) {
    return NULL;
  }
  if (!strappy_file_write_parse_arguments(arguments_json,
                                           &arguments,
                                           error_out)) {
    return NULL;
  }
  resolved_path = strappy_file_mutation_resolve_session_path(
    session_db_path,
    session_id,
    "file_write",
    arguments.path,
    error_out);
  if (resolved_path == NULL) {
    strappy_file_write_arguments_destroy(&arguments);
    return NULL;
  }
  result = strappy_string_duplicate("{}");
  if (result == NULL) {
    free(resolved_path);
    strappy_file_write_arguments_destroy(&arguments);
    strappy_set_error(error_out,
                      "Could not allocate the file_write result.");
    return NULL;
  }

  locked = strappy_file_mutation_lock(error_out);
  ok = locked && strappy_file_mutation_write_text(
    "file_write",
    resolved_path,
    arguments.content,
    arguments.content_length,
    1,
    0,
    error_out);
  if (locked) {
    strappy_file_mutation_unlock();
  }
  free(resolved_path);
  strappy_file_write_arguments_destroy(&arguments);
  if (!ok) {
    free(result);
    return NULL;
  }
  return result;
}
