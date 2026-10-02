#include "strappy_file_read.h"

#include "strappy_assistant_sets.h"
#include "strappy_core.h"
#include "strappy_db.h"

#include <cJSON.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define STRAPPY_FILE_READ_DEFAULT_LIMIT 500
#define STRAPPY_FILE_READ_MAX_LINES 500
#define STRAPPY_FILE_READ_MAX_CONTENT_BYTES (25U * 1024U)
#define STRAPPY_FILE_READ_MAX_PATH_BYTES 4096U
#define STRAPPY_FILE_READ_MAX_OFFSET 2147483647LL

typedef struct strappy_file_read_arguments {
  char *path;
  long long offset;
  int limit;
} strappy_file_read_arguments;

typedef struct strappy_file_read_utf8_state {
  unsigned int remaining;
  unsigned char next_minimum;
  unsigned char next_maximum;
} strappy_file_read_utf8_state;

static void strappy_file_read_arguments_init(
  strappy_file_read_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }
  arguments->path = NULL;
  arguments->offset = 1LL;
  arguments->limit = STRAPPY_FILE_READ_DEFAULT_LIMIT;
}

static void strappy_file_read_arguments_destroy(
  strappy_file_read_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }
  free(arguments->path);
  strappy_file_read_arguments_init(arguments);
}

static int strappy_file_read_copy_integer(cJSON *value,
                                          const char *name,
                                          long long maximum,
                                          long long *result_out,
                                          char **error_out)
{
  double number;
  long long result;

  if (!cJSON_IsNumber(value)) {
    strappy_set_formatted_error(error_out,
                                "file_read %s must be an integer.",
                                name);
    return 0;
  }
  number = value->valuedouble;
  if ((number < 1.0) || (number > (double)maximum)) {
    strappy_set_formatted_error(error_out,
                                "file_read %s must be between 1 and %lld.",
                                name,
                                maximum);
    return 0;
  }
  result = (long long)number;
  if ((double)result != number) {
    strappy_set_formatted_error(error_out,
                                "file_read %s must be an integer.",
                                name);
    return 0;
  }
  *result_out = result;
  return 1;
}

static int strappy_file_read_parse_arguments(
  const char *arguments_json,
  strappy_file_read_arguments *arguments,
  char **error_out)
{
  cJSON *root;
  cJSON *item;
  int path_count;
  int offset_count;
  int limit_count;

  strappy_file_read_arguments_init(arguments);
  root = cJSON_Parse((arguments_json != NULL) ? arguments_json : "");
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    strappy_set_error(error_out,
                      "file_read arguments must be a JSON object.");
    return 0;
  }

  path_count = 0;
  offset_count = 0;
  limit_count = 0;
  for (item = root->child; item != NULL; item = item->next) {
    long long integer_value;

    if ((item->string != NULL) && (strcmp(item->string, "path") == 0)) {
      path_count++;
      if ((path_count != 1) || !cJSON_IsString(item) ||
          (item->valuestring == NULL) || (item->valuestring[0] == '\0') ||
          (strlen(item->valuestring) > STRAPPY_FILE_READ_MAX_PATH_BYTES)) {
        cJSON_Delete(root);
        strappy_file_read_arguments_destroy(arguments);
        strappy_set_error(
          error_out,
          "file_read path must be one non-empty string of at most 4096 bytes.");
        return 0;
      }
      arguments->path = strappy_string_duplicate(item->valuestring);
      if (arguments->path == NULL) {
        cJSON_Delete(root);
        strappy_set_error(error_out,
                          "Could not allocate the file_read path.");
        return 0;
      }
      continue;
    }
    if ((item->string != NULL) && (strcmp(item->string, "offset") == 0)) {
      offset_count++;
      if (offset_count != 1) {
        cJSON_Delete(root);
        strappy_file_read_arguments_destroy(arguments);
        strappy_set_error(error_out,
                          "file_read offset may be provided only once.");
        return 0;
      }
      if (!strappy_file_read_copy_integer(item,
                                          "offset",
                                          STRAPPY_FILE_READ_MAX_OFFSET,
                                          &integer_value,
                                          error_out)) {
        cJSON_Delete(root);
        strappy_file_read_arguments_destroy(arguments);
        return 0;
      }
      arguments->offset = integer_value;
      continue;
    }
    if ((item->string != NULL) && (strcmp(item->string, "limit") == 0)) {
      limit_count++;
      if (limit_count != 1) {
        cJSON_Delete(root);
        strappy_file_read_arguments_destroy(arguments);
        strappy_set_error(error_out,
                          "file_read limit may be provided only once.");
        return 0;
      }
      if (!strappy_file_read_copy_integer(item,
                                          "limit",
                                          STRAPPY_FILE_READ_MAX_LINES,
                                          &integer_value,
                                          error_out)) {
        cJSON_Delete(root);
        strappy_file_read_arguments_destroy(arguments);
        return 0;
      }
      arguments->limit = (int)integer_value;
      continue;
    }

    cJSON_Delete(root);
    strappy_file_read_arguments_destroy(arguments);
    strappy_set_error(error_out,
                      "file_read accepts only path, offset, and limit.");
    return 0;
  }

  cJSON_Delete(root);
  if (path_count != 1) {
    strappy_file_read_arguments_destroy(arguments);
    strappy_set_error(error_out,
                      "file_read requires a non-empty path string.");
    return 0;
  }
  return 1;
}

static char *strappy_file_read_resolve_path(const char *working_directory,
                                            const char *path,
                                            char **error_out)
{
  size_t directory_length;
  size_t path_length;
  size_t total_length;
  int needs_separator;
  char *resolved;

  if (path[0] == '/') {
    resolved = strappy_string_duplicate(path);
    if (resolved == NULL) {
      strappy_set_error(error_out,
                        "Could not allocate the file_read path.");
    }
    return resolved;
  }

  directory_length = strlen(working_directory);
  path_length = strlen(path);
  needs_separator = (directory_length > 0U) &&
    (working_directory[directory_length - 1U] != '/');
  if ((directory_length > ((size_t)-1) - path_length - 2U)) {
    strappy_set_error(error_out, "The resolved file_read path is too long.");
    return NULL;
  }
  total_length = directory_length + path_length +
    (needs_separator ? 1U : 0U);
  resolved = (char *)malloc(total_length + 1U);
  if (resolved == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate the resolved file_read path.");
    return NULL;
  }
  memcpy(resolved, working_directory, directory_length);
  if (needs_separator) {
    resolved[directory_length] = '/';
    directory_length++;
  }
  memcpy(resolved + directory_length, path, path_length);
  resolved[total_length] = '\0';
  return resolved;
}

static void strappy_file_read_utf8_state_init(
  strappy_file_read_utf8_state *state)
{
  state->remaining = 0U;
  state->next_minimum = 0x80U;
  state->next_maximum = 0xBFU;
}

static int strappy_file_read_utf8_consume(
  strappy_file_read_utf8_state *state,
  unsigned char byte)
{
  if (state->remaining > 0U) {
    if ((byte < state->next_minimum) || (byte > state->next_maximum)) {
      return 0;
    }
    state->remaining--;
    state->next_minimum = 0x80U;
    state->next_maximum = 0xBFU;
    return 1;
  }

  if (byte == 0U) {
    return 0;
  }
  if (byte <= 0x7FU) {
    return 1;
  }
  if ((byte >= 0xC2U) && (byte <= 0xDFU)) {
    state->remaining = 1U;
    state->next_minimum = 0x80U;
    state->next_maximum = 0xBFU;
    return 1;
  }
  if (byte == 0xE0U) {
    state->remaining = 2U;
    state->next_minimum = 0xA0U;
    state->next_maximum = 0xBFU;
    return 1;
  }
  if (((byte >= 0xE1U) && (byte <= 0xECU)) ||
      ((byte >= 0xEEU) && (byte <= 0xEFU))) {
    state->remaining = 2U;
    state->next_minimum = 0x80U;
    state->next_maximum = 0xBFU;
    return 1;
  }
  if (byte == 0xEDU) {
    state->remaining = 2U;
    state->next_minimum = 0x80U;
    state->next_maximum = 0x9FU;
    return 1;
  }
  if (byte == 0xF0U) {
    state->remaining = 3U;
    state->next_minimum = 0x90U;
    state->next_maximum = 0xBFU;
    return 1;
  }
  if ((byte >= 0xF1U) && (byte <= 0xF3U)) {
    state->remaining = 3U;
    state->next_minimum = 0x80U;
    state->next_maximum = 0xBFU;
    return 1;
  }
  if (byte == 0xF4U) {
    state->remaining = 3U;
    state->next_minimum = 0x80U;
    state->next_maximum = 0x8FU;
    return 1;
  }
  return 0;
}

static int strappy_file_read_set_file_error(const char *resolved_path,
                                            char **error_out)
{
  strappy_set_formatted_error(error_out,
                              "Could not read file %s: %s.",
                              resolved_path,
                              strerror(errno));
  return 0;
}

static int strappy_file_read_set_utf8_error(const char *resolved_path,
                                            char **error_out)
{
  strappy_set_formatted_error(error_out,
                              "File is not valid UTF-8 text: %s.",
                              resolved_path);
  return 0;
}

static int strappy_file_read_skip_to_offset(
  FILE *file,
  const char *resolved_path,
  long long offset,
  strappy_file_read_utf8_state *utf8_state,
  char **error_out)
{
  long long current_line;

  current_line = 1LL;
  while (current_line < offset) {
    int character;
    unsigned char byte;

    errno = 0;
    character = fgetc(file);
    if (character == EOF) {
      if (ferror(file)) {
        return strappy_file_read_set_file_error(resolved_path, error_out);
      }
      strappy_set_formatted_error(error_out,
                                  "Offset %lld is beyond end of file.",
                                  offset);
      return 0;
    }
    byte = (unsigned char)character;
    if (!strappy_file_read_utf8_consume(utf8_state, byte)) {
      return strappy_file_read_set_utf8_error(resolved_path, error_out);
    }
    if (byte == (unsigned char)'\n') {
      current_line++;
    }
  }
  return 1;
}

static char *strappy_file_read_result_json(const char *content,
                                           long long offset,
                                           int line_count,
                                           int has_next_offset,
                                           char **error_out)
{
  cJSON *root;
  char *json;

  root = cJSON_CreateObject();
  if ((root == NULL) ||
      (cJSON_AddStringToObject(root, "content", content) == NULL) ||
      (cJSON_AddNumberToObject(root, "offset", (double)offset) == NULL) ||
      (cJSON_AddNumberToObject(root,
                              "line_count",
                              (double)line_count) == NULL)) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Could not build the file_read result.");
    return NULL;
  }
  if (has_next_offset) {
    if (cJSON_AddNumberToObject(root,
                               "next_offset",
                               (double)(offset + (long long)line_count)) == NULL) {
      cJSON_Delete(root);
      strappy_set_error(error_out, "Could not build the file_read result.");
      return NULL;
    }
  } else if (cJSON_AddNullToObject(root, "next_offset") == NULL) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Could not build the file_read result.");
    return NULL;
  }

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize the file_read result.");
  }
  return json;
}

static char *strappy_file_read_stream(FILE *file,
                                      const char *resolved_path,
                                      const strappy_file_read_arguments *arguments,
                                      char **error_out)
{
  strappy_file_read_utf8_state utf8_state;
  char *content;
  size_t content_length;
  int line_count;
  int has_unterminated_line;
  int stopped_at_limit;
  int saw_content;
  int has_next_offset;
  char *result;

  strappy_file_read_utf8_state_init(&utf8_state);
  if (!strappy_file_read_skip_to_offset(file,
                                        resolved_path,
                                        arguments->offset,
                                        &utf8_state,
                                        error_out)) {
    return NULL;
  }
  if (utf8_state.remaining != 0U) {
    strappy_file_read_set_utf8_error(resolved_path, error_out);
    return NULL;
  }

  content = (char *)malloc(STRAPPY_FILE_READ_MAX_CONTENT_BYTES + 1U);
  if (content == NULL) {
    strappy_set_error(error_out, "Could not allocate the file_read result.");
    return NULL;
  }
  content_length = 0U;
  line_count = 0;
  has_unterminated_line = 0;
  stopped_at_limit = 0;
  saw_content = 0;
  has_next_offset = 0;

  while (line_count < arguments->limit) {
    int character;
    unsigned char byte;

    errno = 0;
    character = fgetc(file);
    if (character == EOF) {
      if (ferror(file)) {
        free(content);
        strappy_file_read_set_file_error(resolved_path, error_out);
        return NULL;
      }
      break;
    }
    byte = (unsigned char)character;
    saw_content = 1;
    if (!strappy_file_read_utf8_consume(&utf8_state, byte)) {
      free(content);
      strappy_file_read_set_utf8_error(resolved_path, error_out);
      return NULL;
    }
    if (content_length == STRAPPY_FILE_READ_MAX_CONTENT_BYTES) {
      free(content);
      strappy_set_formatted_error(
        error_out,
        "Requested %d lines starting at line %lld exceeds the 25 KiB output "
        "limit. Retry file_read with a smaller limit.",
        arguments->limit,
        arguments->offset);
      return NULL;
    }
    content[content_length++] = (char)byte;
    if (byte == (unsigned char)'\n') {
      line_count++;
      has_unterminated_line = 0;
      if (line_count == arguments->limit) {
        stopped_at_limit = 1;
      }
    } else {
      has_unterminated_line = 1;
    }
  }

  if (utf8_state.remaining != 0U) {
    free(content);
    strappy_file_read_set_utf8_error(resolved_path, error_out);
    return NULL;
  }
  if (!saw_content && (arguments->offset > 1LL)) {
    free(content);
    strappy_set_formatted_error(error_out,
                                "Offset %lld is beyond end of file.",
                                arguments->offset);
    return NULL;
  }
  if (has_unterminated_line) {
    line_count++;
  }

  if (stopped_at_limit) {
    int character;

    errno = 0;
    character = fgetc(file);
    if ((character == EOF) && ferror(file)) {
      free(content);
      strappy_file_read_set_file_error(resolved_path, error_out);
      return NULL;
    }
    has_next_offset = (character != EOF) ? 1 : 0;
  }

  content[content_length] = '\0';
  result = strappy_file_read_result_json(content,
                                         arguments->offset,
                                         line_count,
                                         has_next_offset,
                                         error_out);
  free(content);
  return result;
}

char *strappy_file_read_execute(const char *session_db_path,
                                long long session_id,
                                const char *arguments_json,
                                char **error_out)
{
  strappy_file_read_arguments arguments;
  char *assistant_set_id;
  char *working_directory;
  char *resolved_path;
  struct stat file_stat;
  FILE *file;
  char *result;

  if ((session_db_path == NULL) || (session_db_path[0] == '\0') ||
      (session_id <= 0LL)) {
    strappy_set_error(error_out,
                      "file_read requires an active session.");
    return NULL;
  }

  assistant_set_id = NULL;
  if (!strappy_db_get_session_assistant_set(session_db_path,
                                            session_id,
                                            &assistant_set_id,
                                            error_out)) {
    return NULL;
  }
  if ((assistant_set_id == NULL) ||
      (strcmp(assistant_set_id,
              STRAPPY_ASSISTANT_SET_CODING_ASSISTANT) != 0)) {
    free(assistant_set_id);
    strappy_set_error(
      error_out,
      "file_read is available only in Coding Assistant sessions.");
    return NULL;
  }
  free(assistant_set_id);

  if (!strappy_file_read_parse_arguments(arguments_json,
                                          &arguments,
                                          error_out)) {
    return NULL;
  }

  working_directory = NULL;
  if (!strappy_db_get_session_working_directory(session_db_path,
                                                 session_id,
                                                 &working_directory,
                                                 error_out)) {
    strappy_file_read_arguments_destroy(&arguments);
    return NULL;
  }
  resolved_path = strappy_file_read_resolve_path(working_directory,
                                                 arguments.path,
                                                 error_out);
  free(working_directory);
  if (resolved_path == NULL) {
    strappy_file_read_arguments_destroy(&arguments);
    return NULL;
  }

  errno = 0;
  if (stat(resolved_path, &file_stat) != 0) {
    if (errno == ENOENT) {
      strappy_set_formatted_error(error_out,
                                  "File not found: %s.",
                                  resolved_path);
    } else {
      strappy_set_formatted_error(error_out,
                                  "Could not inspect file %s: %s.",
                                  resolved_path,
                                  strerror(errno));
    }
    free(resolved_path);
    strappy_file_read_arguments_destroy(&arguments);
    return NULL;
  }
  if (!S_ISREG(file_stat.st_mode)) {
    strappy_set_formatted_error(error_out,
                                "Path is not a regular file: %s.",
                                resolved_path);
    free(resolved_path);
    strappy_file_read_arguments_destroy(&arguments);
    return NULL;
  }

  errno = 0;
  file = fopen(resolved_path, "rb");
  if (file == NULL) {
    strappy_set_formatted_error(error_out,
                                "File is not readable: %s: %s.",
                                resolved_path,
                                strerror(errno));
    free(resolved_path);
    strappy_file_read_arguments_destroy(&arguments);
    return NULL;
  }

  result = strappy_file_read_stream(file,
                                    resolved_path,
                                    &arguments,
                                    error_out);
  fclose(file);
  free(resolved_path);
  strappy_file_read_arguments_destroy(&arguments);
  return result;
}
