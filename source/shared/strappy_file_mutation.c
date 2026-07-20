#include "strappy_file_mutation.h"

#include "strappy_assistant_sets.h"
#include "strappy_core.h"
#include "strappy_db.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef struct strappy_file_mutation_utf8_state {
  unsigned int remaining;
  unsigned char next_minimum;
  unsigned char next_maximum;
} strappy_file_mutation_utf8_state;

static pthread_mutex_t strappy_file_mutation_mutex =
  PTHREAD_MUTEX_INITIALIZER;

static void strappy_file_mutation_utf8_state_init(
  strappy_file_mutation_utf8_state *state)
{
  state->remaining = 0U;
  state->next_minimum = 0x80U;
  state->next_maximum = 0xBFU;
}

static int strappy_file_mutation_utf8_consume(
  strappy_file_mutation_utf8_state *state,
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
    return 1;
  }
  if (byte == 0xE0U) {
    state->remaining = 2U;
    state->next_minimum = 0xA0U;
    return 1;
  }
  if (((byte >= 0xE1U) && (byte <= 0xECU)) ||
      ((byte >= 0xEEU) && (byte <= 0xEFU))) {
    state->remaining = 2U;
    return 1;
  }
  if (byte == 0xEDU) {
    state->remaining = 2U;
    state->next_maximum = 0x9FU;
    return 1;
  }
  if (byte == 0xF0U) {
    state->remaining = 3U;
    state->next_minimum = 0x90U;
    return 1;
  }
  if ((byte >= 0xF1U) && (byte <= 0xF3U)) {
    state->remaining = 3U;
    return 1;
  }
  if (byte == 0xF4U) {
    state->remaining = 3U;
    state->next_maximum = 0x8FU;
    return 1;
  }
  return 0;
}

int strappy_file_mutation_validate_utf8(const char *text, size_t length)
{
  strappy_file_mutation_utf8_state state;
  size_t index;

  if ((text == NULL) && (length != 0U)) {
    return 0;
  }
  strappy_file_mutation_utf8_state_init(&state);
  for (index = 0U; index < length; index++) {
    if (!strappy_file_mutation_utf8_consume(
          &state,
          (unsigned char)text[index])) {
      return 0;
    }
  }
  return (state.remaining == 0U) ? 1 : 0;
}

int strappy_file_mutation_json_has_null_escape(const char *json)
{
  size_t index;

  if (json == NULL) {
    return 0;
  }
  index = 0U;
  while (json[index] != '\0') {
    size_t slash_count;

    if (json[index] != '\\') {
      index++;
      continue;
    }
    slash_count = 0U;
    while (json[index + slash_count] == '\\') {
      slash_count++;
    }
    if (((slash_count % 2U) != 0U) &&
        (strncmp(json + index + slash_count, "u0000", 5U) == 0)) {
      return 1;
    }
    index += slash_count;
  }
  return 0;
}

int strappy_file_mutation_require_coding_session(
  const char *session_db_path,
  long long session_id,
  const char *tool_name,
  char **error_out)
{
  char *assistant_set_id;

  if ((session_db_path == NULL) || (session_db_path[0] == '\0') ||
      (session_id <= 0LL)) {
    strappy_set_formatted_error(error_out,
                                "%s requires an active session.",
                                tool_name);
    return 0;
  }

  assistant_set_id = NULL;
  if (!strappy_db_get_session_assistant_set(session_db_path,
                                            session_id,
                                            &assistant_set_id,
                                            error_out)) {
    return 0;
  }
  if ((assistant_set_id == NULL) ||
      (strcmp(assistant_set_id,
              STRAPPY_ASSISTANT_SET_CODING_ASSISTANT) != 0)) {
    free(assistant_set_id);
    strappy_set_formatted_error(
      error_out,
      "%s is available only in Coding Assistant sessions.",
      tool_name);
    return 0;
  }
  free(assistant_set_id);
  return 1;
}

char *strappy_file_mutation_resolve_session_path(
  const char *session_db_path,
  long long session_id,
  const char *tool_name,
  const char *path,
  char **error_out)
{
  char *working_directory;
  size_t directory_length;
  size_t path_length;
  size_t total_length;
  int needs_separator;
  char *resolved;

  if ((path == NULL) || (path[0] == '\0')) {
    strappy_set_formatted_error(error_out,
                                "%s path is empty.",
                                tool_name);
    return NULL;
  }
  if (path[0] == '/') {
    resolved = strappy_string_duplicate(path);
    if (resolved == NULL) {
      strappy_set_formatted_error(error_out,
                                  "Could not allocate the %s path.",
                                  tool_name);
    }
    return resolved;
  }

  working_directory = NULL;
  if (!strappy_db_get_session_working_directory(session_db_path,
                                                 session_id,
                                                 &working_directory,
                                                 error_out)) {
    return NULL;
  }
  directory_length = strlen(working_directory);
  path_length = strlen(path);
  needs_separator = (directory_length > 0U) &&
    (working_directory[directory_length - 1U] != '/');
  if (directory_length > ((size_t)-1) - path_length - 2U) {
    free(working_directory);
    strappy_set_formatted_error(error_out,
                                "The resolved %s path is too long.",
                                tool_name);
    return NULL;
  }
  total_length = directory_length + path_length +
    (needs_separator ? 1U : 0U);
  resolved = (char *)malloc(total_length + 1U);
  if (resolved == NULL) {
    free(working_directory);
    strappy_set_formatted_error(error_out,
                                "Could not allocate the resolved %s path.",
                                tool_name);
    return NULL;
  }
  memcpy(resolved, working_directory, directory_length);
  if (needs_separator) {
    resolved[directory_length++] = '/';
  }
  memcpy(resolved + directory_length, path, path_length);
  resolved[total_length] = '\0';
  free(working_directory);
  return resolved;
}

int strappy_file_mutation_lock(char **error_out)
{
  int result;

  result = pthread_mutex_lock(&strappy_file_mutation_mutex);
  if (result != 0) {
    strappy_set_formatted_error(error_out,
                                "Could not lock file mutations: %s.",
                                strerror(result));
    return 0;
  }
  return 1;
}

void strappy_file_mutation_unlock(void)
{
  pthread_mutex_unlock(&strappy_file_mutation_mutex);
}

static int strappy_file_mutation_directory_is_valid(
  const char *tool_name,
  const char *path,
  char **error_out)
{
  struct stat directory_stat;

  errno = 0;
  if (mkdir(path, 0777) == 0) {
    return 1;
  }
  if (errno != EEXIST) {
    strappy_set_formatted_error(error_out,
                                "%s could not create directory %s: %s.",
                                tool_name,
                                path,
                                strerror(errno));
    return 0;
  }
  errno = 0;
  if ((stat(path, &directory_stat) != 0) ||
      !S_ISDIR(directory_stat.st_mode)) {
    strappy_set_formatted_error(error_out,
                                "%s parent path is not a directory: %s.",
                                tool_name,
                                path);
    return 0;
  }
  return 1;
}

static int strappy_file_mutation_create_parent_directories(
  const char *tool_name,
  const char *resolved_path,
  char **error_out)
{
  const char *last_separator;
  char *parent;
  size_t parent_length;
  size_t index;
  int ok;

  last_separator = strrchr(resolved_path, '/');
  if (last_separator == NULL) {
    return 1;
  }
  parent_length = (size_t)(last_separator - resolved_path);
  if (parent_length == 0U) {
    return 1;
  }
  parent = (char *)malloc(parent_length + 1U);
  if (parent == NULL) {
    strappy_set_formatted_error(error_out,
                                "%s could not allocate the parent path.",
                                tool_name);
    return 0;
  }
  memcpy(parent, resolved_path, parent_length);
  parent[parent_length] = '\0';

  ok = 1;
  for (index = 1U; index < parent_length; index++) {
    if ((parent[index] != '/') || (parent[index - 1U] == '/')) {
      continue;
    }
    parent[index] = '\0';
    if ((parent[0] != '\0') &&
        !strappy_file_mutation_directory_is_valid(tool_name,
                                                  parent,
                                                  error_out)) {
      ok = 0;
      parent[index] = '/';
      break;
    }
    parent[index] = '/';
  }
  if (ok) {
    ok = strappy_file_mutation_directory_is_valid(tool_name,
                                                  parent,
                                                  error_out);
  }
  free(parent);
  return ok;
}

char *strappy_file_mutation_read_text(const char *tool_name,
                                      const char *resolved_path,
                                      size_t *length_out,
                                      char **error_out)
{
  struct stat file_stat;
  FILE *file;
  char *content;
  size_t length;
  int close_result;

  if (length_out != NULL) {
    *length_out = 0U;
  }
  errno = 0;
  if (stat(resolved_path, &file_stat) != 0) {
    if (errno == ENOENT) {
      strappy_set_formatted_error(error_out,
                                  "File not found: %s.",
                                  resolved_path);
    } else {
      strappy_set_formatted_error(error_out,
                                  "%s could not inspect file %s: %s.",
                                  tool_name,
                                  resolved_path,
                                  strerror(errno));
    }
    return NULL;
  }
  if (!S_ISREG(file_stat.st_mode)) {
    strappy_set_formatted_error(error_out,
                                "Path is not a regular file: %s.",
                                resolved_path);
    return NULL;
  }
  if ((file_stat.st_size < 0) ||
      (file_stat.st_size >
       (off_t)STRAPPY_FILE_MUTATION_MAX_CONTENT_BYTES)) {
    strappy_set_formatted_error(
      error_out,
      "%s target exceeds the 1 MiB file size limit: %s.",
      tool_name,
      resolved_path);
    return NULL;
  }

  errno = 0;
  file = fopen(resolved_path, "rb");
  if (file == NULL) {
    strappy_set_formatted_error(error_out,
                                "%s could not open file %s: %s.",
                                tool_name,
                                resolved_path,
                                strerror(errno));
    return NULL;
  }
  content = (char *)malloc(STRAPPY_FILE_MUTATION_MAX_CONTENT_BYTES + 1U);
  if (content == NULL) {
    fclose(file);
    strappy_set_formatted_error(error_out,
                                "%s could not allocate file content.",
                                tool_name);
    return NULL;
  }
  length = fread(content,
                 1U,
                 STRAPPY_FILE_MUTATION_MAX_CONTENT_BYTES + 1U,
                 file);
  if (ferror(file)) {
    int saved_errno;

    saved_errno = (errno != 0) ? errno : EIO;
    free(content);
    fclose(file);
    strappy_set_formatted_error(error_out,
                                "%s could not read file %s: %s.",
                                tool_name,
                                resolved_path,
                                strerror(saved_errno));
    return NULL;
  }
  close_result = fclose(file);
  if (close_result != 0) {
    free(content);
    strappy_set_formatted_error(error_out,
                                "%s could not close file %s: %s.",
                                tool_name,
                                resolved_path,
                                strerror(errno));
    return NULL;
  }
  if (length > STRAPPY_FILE_MUTATION_MAX_CONTENT_BYTES) {
    free(content);
    strappy_set_formatted_error(
      error_out,
      "%s target exceeds the 1 MiB file size limit: %s.",
      tool_name,
      resolved_path);
    return NULL;
  }
  if (!strappy_file_mutation_validate_utf8(content, length)) {
    free(content);
    strappy_set_formatted_error(error_out,
                                "File is not valid UTF-8 text: %s.",
                                resolved_path);
    return NULL;
  }
  content[length] = '\0';
  if (length_out != NULL) {
    *length_out = length;
  }
  return content;
}

int strappy_file_mutation_write_text(const char *tool_name,
                                     const char *resolved_path,
                                     const char *text,
                                     size_t length,
                                     int create_parent_directories,
                                     int require_existing_file,
                                     char **error_out)
{
  struct stat file_stat;
  FILE *file;
  size_t written;
  int exists;
  int saved_errno;

  if ((length > STRAPPY_FILE_MUTATION_MAX_CONTENT_BYTES) ||
      !strappy_file_mutation_validate_utf8(text, length)) {
    strappy_set_formatted_error(
      error_out,
      "%s content must be valid UTF-8 of at most 1 MiB.",
      tool_name);
    return 0;
  }
  if (create_parent_directories &&
      !strappy_file_mutation_create_parent_directories(tool_name,
                                                       resolved_path,
                                                       error_out)) {
    return 0;
  }

  errno = 0;
  exists = (stat(resolved_path, &file_stat) == 0) ? 1 : 0;
  if (!exists && (errno != ENOENT)) {
    strappy_set_formatted_error(error_out,
                                "%s could not inspect file %s: %s.",
                                tool_name,
                                resolved_path,
                                strerror(errno));
    return 0;
  }
  if (exists && !S_ISREG(file_stat.st_mode)) {
    strappy_set_formatted_error(error_out,
                                "Path is not a regular file: %s.",
                                resolved_path);
    return 0;
  }
  if (!exists && require_existing_file) {
    strappy_set_formatted_error(error_out,
                                "File not found: %s.",
                                resolved_path);
    return 0;
  }

  errno = 0;
  file = fopen(resolved_path, "wb");
  if (file == NULL) {
    strappy_set_formatted_error(error_out,
                                "%s could not open file %s for writing: %s.",
                                tool_name,
                                resolved_path,
                                strerror(errno));
    return 0;
  }
  written = 0U;
  while (written < length) {
    size_t amount;

    amount = fwrite(text + written, 1U, length - written, file);
    if (amount == 0U) {
      saved_errno = (errno != 0) ? errno : EIO;
      fclose(file);
      strappy_set_formatted_error(error_out,
                                  "%s could not write file %s: %s.",
                                  tool_name,
                                  resolved_path,
                                  strerror(saved_errno));
      return 0;
    }
    written += amount;
  }
  if (fflush(file) != 0) {
    saved_errno = (errno != 0) ? errno : EIO;
    fclose(file);
    strappy_set_formatted_error(error_out,
                                "%s could not flush file %s: %s.",
                                tool_name,
                                resolved_path,
                                strerror(saved_errno));
    return 0;
  }
  if (fclose(file) != 0) {
    strappy_set_formatted_error(error_out,
                                "%s could not close file %s: %s.",
                                tool_name,
                                resolved_path,
                                strerror(errno));
    return 0;
  }
  return 1;
}
