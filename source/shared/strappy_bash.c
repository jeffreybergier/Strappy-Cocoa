#define _POSIX_C_SOURCE 200809L

#include "strappy_bash.h"

#include "strappy_assistant_sets.h"
#include "strappy_core.h"
#include "strappy_db.h"

#include <cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

#define STRAPPY_BASH_POLL_INTERVAL_MS 100LL
#define STRAPPY_BASH_TERMINATE_GRACE_MS 500LL
#define STRAPPY_BASH_POST_EXIT_IDLE_MS 100LL
#define STRAPPY_BASH_POST_EXIT_MAX_MS 500LL
#define STRAPPY_BASH_READ_BUFFER_BYTES 8192U

extern char **environ;

typedef struct strappy_bash_arguments {
  char *command;
  double timeout_seconds;
} strappy_bash_arguments;

typedef struct strappy_bash_utf8_state {
  unsigned char bytes[4];
  size_t length;
  size_t expected;
} strappy_bash_utf8_state;

typedef struct strappy_bash_output {
  char *tail;
  size_t tail_length;
  unsigned long long total_bytes;
  unsigned long long completed_lines;
  int has_open_line;
  int tail_starts_partial;
  strappy_bash_utf8_state utf8;
} strappy_bash_output;

typedef enum strappy_bash_stop_reason {
  STRAPPY_BASH_STOP_NONE = 0,
  STRAPPY_BASH_STOP_CANCELLED = 1,
  STRAPPY_BASH_STOP_TIMEOUT = 2,
  STRAPPY_BASH_STOP_OUTPUT_LIMIT = 3,
  STRAPPY_BASH_STOP_SUPERVISOR_ERROR = 4
} strappy_bash_stop_reason;

typedef enum strappy_bash_child_error_stage {
  STRAPPY_BASH_CHILD_ERROR_NONE = 0,
  STRAPPY_BASH_CHILD_ERROR_PROCESS_GROUP = 1,
  STRAPPY_BASH_CHILD_ERROR_WORKING_DIRECTORY = 2,
  STRAPPY_BASH_CHILD_ERROR_STDIN = 3,
  STRAPPY_BASH_CHILD_ERROR_STDOUT = 4,
  STRAPPY_BASH_CHILD_ERROR_STDERR = 5,
  STRAPPY_BASH_CHILD_ERROR_EXEC = 6
} strappy_bash_child_error_stage;

typedef struct strappy_bash_child_error {
  int stage;
  int error_number;
} strappy_bash_child_error;

typedef struct strappy_bash_process_result {
  strappy_bash_output output;
  char *log_path;
  int wait_status;
  int has_wait_status;
  strappy_bash_stop_reason stop_reason;
  int capture_limit_reached;
  int supervisor_error_number;
  strappy_bash_child_error child_error;
} strappy_bash_process_result;

static void strappy_bash_arguments_init(strappy_bash_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }
  arguments->command = NULL;
  arguments->timeout_seconds = STRAPPY_BASH_DEFAULT_TIMEOUT_SECONDS;
}

static void strappy_bash_arguments_destroy(strappy_bash_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }
  free(arguments->command);
  strappy_bash_arguments_init(arguments);
}

static int strappy_bash_string_has_non_space(const char *value)
{
  const unsigned char *cursor;

  if (value == NULL) {
    return 0;
  }
  cursor = (const unsigned char *)value;
  while (*cursor != 0U) {
    if ((*cursor != (unsigned char)' ') &&
        (*cursor != (unsigned char)'\t') &&
        (*cursor != (unsigned char)'\n') &&
        (*cursor != (unsigned char)'\r') &&
        (*cursor != (unsigned char)'\f') &&
        (*cursor != (unsigned char)'\v')) {
      return 1;
    }
    cursor++;
  }
  return 0;
}

static int strappy_bash_parse_arguments(const char *arguments_json,
                                        strappy_bash_arguments *arguments,
                                        char **error_out)
{
  cJSON *root;
  cJSON *item;
  int command_count;
  int timeout_count;

  strappy_bash_arguments_init(arguments);
  root = cJSON_Parse((arguments_json != NULL) ? arguments_json : "");
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "bash arguments must be a JSON object.");
    return 0;
  }

  command_count = 0;
  timeout_count = 0;
  for (item = root->child; item != NULL; item = item->next) {
    if ((item->string != NULL) && (strcmp(item->string, "command") == 0)) {
      size_t length;

      command_count++;
      length = (cJSON_IsString(item) && (item->valuestring != NULL)) ?
        strlen(item->valuestring) : 0U;
      if ((command_count != 1) || !cJSON_IsString(item) ||
          (item->valuestring == NULL) ||
          !strappy_bash_string_has_non_space(item->valuestring) ||
          (length > STRAPPY_BASH_MAX_COMMAND_BYTES)) {
        cJSON_Delete(root);
        strappy_bash_arguments_destroy(arguments);
        strappy_set_error(
          error_out,
          "bash command must be one non-blank string of at most 32768 bytes.");
        return 0;
      }
      arguments->command = strappy_string_duplicate(item->valuestring);
      if (arguments->command == NULL) {
        cJSON_Delete(root);
        strappy_set_error(error_out, "Could not allocate the bash command.");
        return 0;
      }
      continue;
    }

    if ((item->string != NULL) && (strcmp(item->string, "timeout") == 0)) {
      double timeout;

      timeout_count++;
      timeout = cJSON_IsNumber(item) ? item->valuedouble : 0.0;
      if ((timeout_count != 1) || !cJSON_IsNumber(item) ||
          !(timeout > 0.0) ||
          !(timeout <= STRAPPY_BASH_MAX_TIMEOUT_SECONDS)) {
        cJSON_Delete(root);
        strappy_bash_arguments_destroy(arguments);
        strappy_set_error(
          error_out,
          "bash timeout must be a number greater than 0 and at most 120 seconds.");
        return 0;
      }
      arguments->timeout_seconds = timeout;
      continue;
    }

    cJSON_Delete(root);
    strappy_bash_arguments_destroy(arguments);
    strappy_set_error(error_out, "bash accepts only command and timeout.");
    return 0;
  }

  cJSON_Delete(root);
  if (command_count != 1) {
    strappy_bash_arguments_destroy(arguments);
    strappy_set_error(error_out, "bash requires a non-blank command string.");
    return 0;
  }
  return 1;
}

static int strappy_bash_path_is_executable(const char *path)
{
  struct stat info;

  return (path != NULL) && (path[0] == '/') &&
    (access(path, X_OK) == 0) && (stat(path, &info) == 0) &&
    S_ISREG(info.st_mode);
}

static char *strappy_bash_path_join(const char *directory,
                                   size_t directory_length,
                                   const char *name)
{
  size_t name_length;
  int needs_separator;
  size_t total_length;
  char *path;

  if ((directory == NULL) || (name == NULL)) {
    return NULL;
  }
  name_length = strlen(name);
  needs_separator = (directory_length > 0U) &&
    (directory[directory_length - 1U] != '/');
  if (directory_length > ((size_t)-1) - name_length - 2U) {
    return NULL;
  }
  total_length = directory_length + name_length +
    (needs_separator ? 1U : 0U);
  path = (char *)malloc(total_length + 1U);
  if (path == NULL) {
    return NULL;
  }
  memcpy(path, directory, directory_length);
  if (needs_separator) {
    path[directory_length++] = '/';
  }
  memcpy(path + directory_length, name, name_length);
  path[total_length] = '\0';
  return path;
}

static char *strappy_bash_find_on_path(const char *name)
{
  const char *path_value;
  const char *cursor;

  path_value = getenv("PATH");
  if ((path_value == NULL) || (path_value[0] == '\0')) {
    return NULL;
  }

  cursor = path_value;
  for (;;) {
    const char *separator;
    size_t length;
    const char *directory;
    char *candidate;

    separator = strchr(cursor, ':');
    length = (separator != NULL) ? (size_t)(separator - cursor) : strlen(cursor);
    directory = (length > 0U) ? cursor : ".";
    if (length == 0U) {
      length = 1U;
    }
    candidate = strappy_bash_path_join(directory, length, name);
    if ((candidate != NULL) && strappy_bash_path_is_executable(candidate)) {
      return candidate;
    }
    free(candidate);
    if (separator == NULL) {
      break;
    }
    cursor = separator + 1;
  }
  return NULL;
}

static char *strappy_bash_resolve_shell(char **error_out)
{
  static const char *bash_paths[] = {
    "/bin/bash",
    "/var/jb/bin/bash",
    "/var/jb/usr/bin/bash",
    "/usr/bin/bash"
  };
  static const char *sh_paths[] = {
    "/bin/sh",
    "/var/jb/bin/sh",
    "/var/jb/usr/bin/sh",
    "/usr/bin/sh"
  };
  size_t index;
  char *path;

  for (index = 0U; index < sizeof(bash_paths) / sizeof(bash_paths[0]); index++) {
    if (strappy_bash_path_is_executable(bash_paths[index])) {
      path = strappy_string_duplicate(bash_paths[index]);
      if (path == NULL) {
        strappy_set_error(error_out,
                          "Could not allocate the bash shell path.");
      }
      return path;
    }
  }
  path = strappy_bash_find_on_path("bash");
  if (path != NULL) {
    return path;
  }
  for (index = 0U; index < sizeof(sh_paths) / sizeof(sh_paths[0]); index++) {
    if (strappy_bash_path_is_executable(sh_paths[index])) {
      path = strappy_string_duplicate(sh_paths[index]);
      if (path == NULL) {
        strappy_set_error(error_out,
                          "Could not allocate the shell path.");
      }
      return path;
    }
  }
  path = strappy_bash_find_on_path("sh");
  if (path == NULL) {
    strappy_set_error(
      error_out,
      "No bash-compatible shell was found in the system paths or PATH.");
  }
  return path;
}

static long long strappy_bash_wall_clock_ms(void)
{
  struct timeval value;

  if (gettimeofday(&value, NULL) != 0) {
    return 0LL;
  }
  return ((long long)value.tv_sec * 1000LL) +
    ((long long)value.tv_usec / 1000LL);
}

static long long strappy_bash_monotonic_ms(void)
{
#if defined(__APPLE__)
  mach_timebase_info_data_t timebase;
  uint64_t absolute;
  long double milliseconds;

  if (mach_timebase_info(&timebase) != KERN_SUCCESS ||
      (timebase.denom == 0U)) {
    return strappy_bash_wall_clock_ms();
  }
  absolute = mach_absolute_time();
  milliseconds = ((long double)absolute * (long double)timebase.numer) /
    ((long double)timebase.denom * 1000000.0L);
  if (milliseconds >= (long double)LLONG_MAX) {
    return LLONG_MAX;
  }
  return (long long)milliseconds;
#else
  struct timespec value;

  if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
    return strappy_bash_wall_clock_ms();
  }
  return ((long long)value.tv_sec * 1000LL) +
    ((long long)value.tv_nsec / 1000000LL);
#endif
}

static void strappy_bash_utf8_state_init(strappy_bash_utf8_state *state)
{
  if (state == NULL) {
    return;
  }
  memset(state->bytes, 0, sizeof(state->bytes));
  state->length = 0U;
  state->expected = 0U;
}

static size_t strappy_bash_utf8_expected(unsigned char byte)
{
  if ((byte >= 0xC2U) && (byte <= 0xDFU)) {
    return 2U;
  }
  if ((byte >= 0xE0U) && (byte <= 0xEFU)) {
    return 3U;
  }
  if ((byte >= 0xF0U) && (byte <= 0xF4U)) {
    return 4U;
  }
  return 0U;
}

static int strappy_bash_utf8_is_continuation(unsigned char byte)
{
  return ((byte & 0xC0U) == 0x80U) ? 1 : 0;
}

static int strappy_bash_utf8_sequence_valid(const unsigned char *bytes,
                                            size_t length,
                                            unsigned int *codepoint_out)
{
  unsigned int codepoint;

  if ((bytes == NULL) || (codepoint_out == NULL)) {
    return 0;
  }
  if ((length == 2U) && (bytes[0] >= 0xC2U) && (bytes[0] <= 0xDFU) &&
      strappy_bash_utf8_is_continuation(bytes[1])) {
    codepoint = ((unsigned int)(bytes[0] & 0x1FU) << 6U) |
      (unsigned int)(bytes[1] & 0x3FU);
  } else if ((length == 3U) &&
             (((bytes[0] == 0xE0U) && (bytes[1] >= 0xA0U) &&
               (bytes[1] <= 0xBFU)) ||
              (((bytes[0] >= 0xE1U) && (bytes[0] <= 0xECU)) &&
               strappy_bash_utf8_is_continuation(bytes[1])) ||
              ((bytes[0] == 0xEDU) && (bytes[1] >= 0x80U) &&
               (bytes[1] <= 0x9FU)) ||
              (((bytes[0] >= 0xEEU) && (bytes[0] <= 0xEFU)) &&
               strappy_bash_utf8_is_continuation(bytes[1]))) &&
             strappy_bash_utf8_is_continuation(bytes[2])) {
    codepoint = ((unsigned int)(bytes[0] & 0x0FU) << 12U) |
      ((unsigned int)(bytes[1] & 0x3FU) << 6U) |
      (unsigned int)(bytes[2] & 0x3FU);
  } else if ((length == 4U) &&
             (((bytes[0] == 0xF0U) && (bytes[1] >= 0x90U) &&
               (bytes[1] <= 0xBFU)) ||
              (((bytes[0] >= 0xF1U) && (bytes[0] <= 0xF3U)) &&
               strappy_bash_utf8_is_continuation(bytes[1])) ||
              ((bytes[0] == 0xF4U) && (bytes[1] >= 0x80U) &&
               (bytes[1] <= 0x8FU))) &&
             strappy_bash_utf8_is_continuation(bytes[2]) &&
             strappy_bash_utf8_is_continuation(bytes[3])) {
    codepoint = ((unsigned int)(bytes[0] & 0x07U) << 18U) |
      ((unsigned int)(bytes[1] & 0x3FU) << 12U) |
      ((unsigned int)(bytes[2] & 0x3FU) << 6U) |
      (unsigned int)(bytes[3] & 0x3FU);
  } else {
    return 0;
  }
  *codepoint_out = codepoint;
  return 1;
}

static int strappy_bash_sanitize_bytes(strappy_bash_utf8_state *state,
                                       const unsigned char *bytes,
                                       size_t length,
                                       int finish,
                                       char **text_out,
                                       size_t *text_length_out)
{
  static const unsigned char replacement[] = { 0xEFU, 0xBFU, 0xBDU };
  size_t capacity;
  char *text;
  size_t output_length;
  size_t index;

  if ((state == NULL) || (text_out == NULL) || (text_length_out == NULL) ||
      ((bytes == NULL) && (length > 0U)) ||
      (length > (((size_t)-1) - 8U) / 3U)) {
    return 0;
  }
  *text_out = NULL;
  *text_length_out = 0U;
  capacity = (length * 3U) + 8U;
  text = (char *)malloc(capacity);
  if (text == NULL) {
    return 0;
  }

  output_length = 0U;
  index = 0U;
  while (index < length) {
    unsigned char byte;

    byte = bytes[index];
    if (state->length > 0U) {
      if (strappy_bash_utf8_is_continuation(byte)) {
        unsigned int codepoint;

        state->bytes[state->length++] = byte;
        index++;
        if (state->length < state->expected) {
          continue;
        }
        if (strappy_bash_utf8_sequence_valid(state->bytes,
                                             state->length,
                                             &codepoint)) {
          if ((codepoint < 0xFFF9U) || (codepoint > 0xFFFBU)) {
            memcpy(text + output_length, state->bytes, state->length);
            output_length += state->length;
          }
        } else {
          memcpy(text + output_length, replacement, sizeof(replacement));
          output_length += sizeof(replacement);
        }
        strappy_bash_utf8_state_init(state);
        continue;
      }
      memcpy(text + output_length, replacement, sizeof(replacement));
      output_length += sizeof(replacement);
      strappy_bash_utf8_state_init(state);
      continue;
    }

    if (byte <= 0x7FU) {
      index++;
      if (((byte >= 0x20U) && (byte != 0x7FU)) ||
          (byte == (unsigned char)'\t') ||
          (byte == (unsigned char)'\n') ||
          (byte == (unsigned char)'\r')) {
        text[output_length++] = (char)byte;
      }
      continue;
    }

    state->expected = strappy_bash_utf8_expected(byte);
    if (state->expected == 0U) {
      memcpy(text + output_length, replacement, sizeof(replacement));
      output_length += sizeof(replacement);
      index++;
      continue;
    }
    state->bytes[0] = byte;
    state->length = 1U;
    index++;
  }

  if (finish && (state->length > 0U)) {
    memcpy(text + output_length, replacement, sizeof(replacement));
    output_length += sizeof(replacement);
    strappy_bash_utf8_state_init(state);
  }
  text[output_length] = '\0';
  *text_out = text;
  *text_length_out = output_length;
  return 1;
}

static void strappy_bash_output_init(strappy_bash_output *output)
{
  if (output == NULL) {
    return;
  }
  output->tail = NULL;
  output->tail_length = 0U;
  output->total_bytes = 0ULL;
  output->completed_lines = 0ULL;
  output->has_open_line = 0;
  output->tail_starts_partial = 0;
  strappy_bash_utf8_state_init(&output->utf8);
}

static void strappy_bash_output_destroy(strappy_bash_output *output)
{
  if (output == NULL) {
    return;
  }
  free(output->tail);
  strappy_bash_output_init(output);
}

static size_t strappy_bash_output_line_count(const char *text, size_t length)
{
  size_t index;
  size_t lines;

  if ((text == NULL) || (length == 0U)) {
    return 0U;
  }
  lines = 0U;
  for (index = 0U; index < length; index++) {
    if (text[index] == '\n') {
      lines++;
    }
  }
  if (text[length - 1U] != '\n') {
    lines++;
  }
  return lines;
}

static void strappy_bash_output_remove_prefix(strappy_bash_output *output,
                                              size_t length,
                                              int starts_partial)
{
  if ((output == NULL) || (length == 0U)) {
    return;
  }
  if (length >= output->tail_length) {
    output->tail_length = 0U;
    output->tail_starts_partial = 0;
    if (output->tail != NULL) {
      output->tail[0] = '\0';
    }
    return;
  }
  memmove(output->tail,
          output->tail + length,
          output->tail_length - length);
  output->tail_length -= length;
  output->tail[output->tail_length] = '\0';
  output->tail_starts_partial = starts_partial;
}

static void strappy_bash_output_trim_bytes(strappy_bash_output *output)
{
  size_t remove_length;
  size_t newline_index;
  int boundary;

  if ((output == NULL) ||
      (output->tail_length <= STRAPPY_BASH_MAX_DISPLAY_BYTES)) {
    return;
  }
  remove_length = output->tail_length - STRAPPY_BASH_MAX_DISPLAY_BYTES;
  while ((remove_length < output->tail_length) &&
         strappy_bash_utf8_is_continuation(
           (unsigned char)output->tail[remove_length])) {
    remove_length++;
  }
  boundary = (remove_length == 0U) ||
    (output->tail[remove_length - 1U] == '\n');
  if (!boundary) {
    newline_index = remove_length;
    while ((newline_index < output->tail_length) &&
           (output->tail[newline_index] != '\n')) {
      newline_index++;
    }
    if ((newline_index < output->tail_length) &&
        ((newline_index + 1U) < output->tail_length)) {
      remove_length = newline_index + 1U;
      boundary = 1;
    }
  }
  strappy_bash_output_remove_prefix(output,
                                    remove_length,
                                    boundary ? 0 : 1);
}

static void strappy_bash_output_trim_lines(strappy_bash_output *output)
{
  size_t line_count;
  size_t remove_lines;
  size_t index;
  size_t seen;

  if ((output == NULL) || (output->tail_length == 0U)) {
    return;
  }
  line_count = strappy_bash_output_line_count(output->tail,
                                               output->tail_length);
  if (line_count <= STRAPPY_BASH_MAX_DISPLAY_LINES) {
    return;
  }
  remove_lines = line_count - STRAPPY_BASH_MAX_DISPLAY_LINES;
  seen = 0U;
  for (index = 0U; index < output->tail_length; index++) {
    if (output->tail[index] == '\n') {
      seen++;
      if (seen == remove_lines) {
        strappy_bash_output_remove_prefix(output, index + 1U, 0);
        return;
      }
    }
  }
}

static int strappy_bash_output_append_text(strappy_bash_output *output,
                                           const char *text,
                                           size_t length)
{
  char *next;
  size_t index;

  if ((output == NULL) || ((text == NULL) && (length > 0U))) {
    return 0;
  }
  if (length == 0U) {
    return 1;
  }
  if ((output->tail_length > ((size_t)-1) - length - 1U) ||
      (output->total_bytes > ULLONG_MAX - (unsigned long long)length)) {
    return 0;
  }
  next = (char *)realloc(output->tail,
                         output->tail_length + length + 1U);
  if (next == NULL) {
    return 0;
  }
  output->tail = next;
  memcpy(output->tail + output->tail_length, text, length);
  output->tail_length += length;
  output->tail[output->tail_length] = '\0';
  output->total_bytes += (unsigned long long)length;

  for (index = 0U; index < length; index++) {
    if (text[index] == '\n') {
      output->completed_lines++;
      output->has_open_line = 0;
    } else {
      output->has_open_line = 1;
    }
  }
  strappy_bash_output_trim_bytes(output);
  strappy_bash_output_trim_lines(output);
  return 1;
}

static int strappy_bash_output_append_raw(strappy_bash_output *output,
                                          const unsigned char *bytes,
                                          size_t length)
{
  char *text;
  size_t text_length;
  int ok;

  text = NULL;
  text_length = 0U;
  if (!strappy_bash_sanitize_bytes(&output->utf8,
                                   bytes,
                                   length,
                                   0,
                                   &text,
                                   &text_length)) {
    return 0;
  }
  ok = strappy_bash_output_append_text(output, text, text_length);
  free(text);
  return ok;
}

static int strappy_bash_output_finish(strappy_bash_output *output)
{
  char *text;
  size_t text_length;
  int ok;

  text = NULL;
  text_length = 0U;
  if (!strappy_bash_sanitize_bytes(&output->utf8,
                                   NULL,
                                   0U,
                                   1,
                                   &text,
                                   &text_length)) {
    return 0;
  }
  ok = strappy_bash_output_append_text(output, text, text_length);
  free(text);
  return ok;
}

static unsigned long long strappy_bash_output_total_lines(
  const strappy_bash_output *output)
{
  if (output == NULL) {
    return 0ULL;
  }
  return output->completed_lines + (output->has_open_line ? 1ULL : 0ULL);
}

static int strappy_bash_output_is_truncated(const strappy_bash_output *output)
{
  return (output != NULL) &&
    ((output->total_bytes > (unsigned long long)STRAPPY_BASH_MAX_DISPLAY_BYTES) ||
     (strappy_bash_output_total_lines(output) >
      (unsigned long long)STRAPPY_BASH_MAX_DISPLAY_LINES));
}

static int strappy_bash_set_close_on_exec(int fd)
{
  int flags;

  flags = fcntl(fd, F_GETFD);
  return (flags >= 0) && (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0);
}

static int strappy_bash_move_fd_above_stdio(int *fd_io)
{
  int duplicate;

  if ((fd_io == NULL) || (*fd_io < 0)) {
    return 0;
  }
  if (*fd_io > STDERR_FILENO) {
    return 1;
  }
  duplicate = fcntl(*fd_io, F_DUPFD, STDERR_FILENO + 1);
  if (duplicate < 0) {
    return 0;
  }
  close(*fd_io);
  *fd_io = duplicate;
  return 1;
}

static int strappy_bash_set_nonblocking(int fd)
{
  int flags;

  flags = fcntl(fd, F_GETFL);
  return (flags >= 0) && (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
}

static int strappy_bash_create_log(char **path_out,
                                   int *fd_out,
                                   char **error_out)
{
  static const char suffix[] = "strappy-bash-XXXXXX";
  const char *directory;
  size_t directory_length;
  size_t total_length;
  char *path;
  int fd;

  *path_out = NULL;
  *fd_out = -1;
  directory = getenv("TMPDIR");
  if ((directory == NULL) || (directory[0] != '/')) {
    directory = "/tmp";
  }
  directory_length = strlen(directory);
  while ((directory_length > 1U) &&
         (directory[directory_length - 1U] == '/')) {
    directory_length--;
  }
  if (directory_length > ((size_t)-1) - sizeof(suffix) - 1U) {
    strappy_set_error(error_out, "The bash output-log path is too long.");
    return 0;
  }
  total_length = directory_length + 1U + sizeof(suffix) - 1U;
  path = (char *)malloc(total_length + 1U);
  if (path == NULL) {
    strappy_set_error(error_out, "Could not allocate the bash output-log path.");
    return 0;
  }
  memcpy(path, directory, directory_length);
  path[directory_length] = '/';
  memcpy(path + directory_length + 1U, suffix, sizeof(suffix));

  fd = mkstemp(path);
  if (fd < 0) {
    strappy_set_formatted_error(error_out,
                                "Could not create the bash output log: %s.",
                                strerror(errno));
    free(path);
    return 0;
  }
  if ((fchmod(fd, S_IRUSR | S_IWUSR) != 0) ||
      !strappy_bash_set_close_on_exec(fd)) {
    int saved_errno;

    saved_errno = errno;
    close(fd);
    unlink(path);
    free(path);
    strappy_set_formatted_error(error_out,
                                "Could not configure the bash output log: %s.",
                                strerror(saved_errno));
    return 0;
  }
  *path_out = path;
  *fd_out = fd;
  return 1;
}

static int strappy_bash_write_all(int fd,
                                  const unsigned char *bytes,
                                  size_t length,
                                  int *error_number_out)
{
  size_t written;

  written = 0U;
  while (written < length) {
    ssize_t result;

    result = write(fd, bytes + written, length - written);
    if (result > 0) {
      written += (size_t)result;
      continue;
    }
    if ((result < 0) && (errno == EINTR)) {
      continue;
    }
    if (error_number_out != NULL) {
      *error_number_out = (result < 0) ? errno : EIO;
    }
    return 0;
  }
  return 1;
}

static void strappy_bash_child_report_error(int fd, int stage)
{
  strappy_bash_child_error error;
  const unsigned char *bytes;
  size_t written;

  error.stage = stage;
  error.error_number = errno;
  bytes = (const unsigned char *)&error;
  written = 0U;
  while (written < sizeof(error)) {
    ssize_t result;

    result = write(fd, bytes + written, sizeof(error) - written);
    if (result > 0) {
      written += (size_t)result;
    } else if ((result < 0) && (errno == EINTR)) {
      continue;
    } else {
      break;
    }
  }
}

static void strappy_bash_kill_process_group(pid_t pid, int signal_number)
{
  if (pid <= 0) {
    return;
  }
  if ((kill(-pid, signal_number) != 0) && (errno != ESRCH)) {
    (void)kill(pid, signal_number);
  }
}

static void strappy_bash_process_result_init(
  strappy_bash_process_result *result)
{
  if (result == NULL) {
    return;
  }
  strappy_bash_output_init(&result->output);
  result->log_path = NULL;
  result->wait_status = 0;
  result->has_wait_status = 0;
  result->stop_reason = STRAPPY_BASH_STOP_NONE;
  result->capture_limit_reached = 0;
  result->supervisor_error_number = 0;
  result->child_error.stage = STRAPPY_BASH_CHILD_ERROR_NONE;
  result->child_error.error_number = 0;
}

static void strappy_bash_process_result_destroy(
  strappy_bash_process_result *result)
{
  if (result == NULL) {
    return;
  }
  strappy_bash_output_destroy(&result->output);
  free(result->log_path);
  strappy_bash_process_result_init(result);
}

static int strappy_bash_append_process_output(
  strappy_bash_process_result *result,
  int log_fd,
  unsigned long long *captured_bytes,
  const unsigned char *bytes,
  size_t length)
{
  size_t accepted;
  unsigned long long remaining;
  int write_error;

  if ((result == NULL) || (captured_bytes == NULL) ||
      ((bytes == NULL) && (length > 0U))) {
    return 0;
  }
  remaining = (*captured_bytes <
               (unsigned long long)STRAPPY_BASH_MAX_CAPTURE_BYTES) ?
    ((unsigned long long)STRAPPY_BASH_MAX_CAPTURE_BYTES - *captured_bytes) :
    0ULL;
  accepted = (remaining < (unsigned long long)length) ?
    (size_t)remaining : length;
  write_error = 0;
  if ((accepted > 0U) &&
      !strappy_bash_write_all(log_fd, bytes, accepted, &write_error)) {
    if (result->stop_reason == STRAPPY_BASH_STOP_NONE) {
      result->stop_reason = STRAPPY_BASH_STOP_SUPERVISOR_ERROR;
      result->supervisor_error_number = write_error;
    }
    return 0;
  }
  if ((accepted > 0U) &&
      !strappy_bash_output_append_raw(&result->output, bytes, accepted)) {
    if (result->stop_reason == STRAPPY_BASH_STOP_NONE) {
      result->stop_reason = STRAPPY_BASH_STOP_SUPERVISOR_ERROR;
      result->supervisor_error_number = ENOMEM;
    }
    return 0;
  }
  *captured_bytes += (unsigned long long)accepted;
  if (accepted < length) {
    result->capture_limit_reached = 1;
    if (result->stop_reason == STRAPPY_BASH_STOP_NONE) {
      result->stop_reason = STRAPPY_BASH_STOP_OUTPUT_LIMIT;
    }
    return 0;
  }
  return 1;
}

static long long strappy_bash_select_wait_ms(
  long long now_ms,
  long long deadline_ms,
  int child_exited,
  strappy_bash_stop_reason stop_reason,
  int terminate_sent,
  int kill_sent,
  long long terminate_sent_ms)
{
  long long wait_ms;
  long long remaining;

  wait_ms = STRAPPY_BASH_POLL_INTERVAL_MS;
  if (!child_exited && (stop_reason == STRAPPY_BASH_STOP_NONE)) {
    remaining = deadline_ms - now_ms;
    if (remaining < wait_ms) {
      wait_ms = remaining;
    }
  }
  if (terminate_sent && !kill_sent) {
    remaining = (terminate_sent_ms + STRAPPY_BASH_TERMINATE_GRACE_MS) -
      now_ms;
    if (remaining < wait_ms) {
      wait_ms = remaining;
    }
  }
  return (wait_ms > 0LL) ? wait_ms : 0LL;
}

static int strappy_bash_read_child_error(int fd,
                                         strappy_bash_child_error *error_out)
{
  unsigned char *bytes;
  size_t length;

  bytes = (unsigned char *)error_out;
  length = 0U;
  while (length < sizeof(*error_out)) {
    ssize_t result;

    result = read(fd, bytes + length, sizeof(*error_out) - length);
    if (result > 0) {
      length += (size_t)result;
      continue;
    }
    if ((result < 0) && (errno == EINTR)) {
      continue;
    }
    if ((result < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK))) {
      break;
    }
    break;
  }
  return (length == sizeof(*error_out)) ? 1 : 0;
}

static int strappy_bash_supervise(const char *shell_path,
                                  const char *working_directory,
                                  const strappy_bash_arguments *arguments,
                                  strappy_bash_continue_callback continue_callback,
                                  void *continue_callback_data,
                                  strappy_bash_process_result *result,
                                  char **error_out)
{
  int output_pipe[2];
  int child_error_pipe[2];
  int null_fd;
  int log_fd;
  pid_t pid;
  const char *shell_name;
  long long started_ms;
  long long timeout_ms;
  long long deadline_ms;
  long long terminate_sent_ms;
  long long child_exited_ms;
  long long last_output_ms;
  unsigned long long captured_bytes;
  int output_eof;
  int child_exited;
  int terminate_sent;
  int kill_sent;
  int group_cleanup_sent;

  output_pipe[0] = -1;
  output_pipe[1] = -1;
  child_error_pipe[0] = -1;
  child_error_pipe[1] = -1;
  log_fd = -1;
  if (!strappy_bash_create_log(&result->log_path, &log_fd, error_out)) {
    return 0;
  }
  if ((pipe(output_pipe) != 0) || (pipe(child_error_pipe) != 0)) {
    int saved_errno;

    saved_errno = errno;
    if (output_pipe[0] >= 0) {
      close(output_pipe[0]);
      close(output_pipe[1]);
    }
    if (child_error_pipe[0] >= 0) {
      close(child_error_pipe[0]);
      close(child_error_pipe[1]);
    }
    close(log_fd);
    unlink(result->log_path);
    strappy_set_formatted_error(error_out,
                                "Could not create bash process pipes: %s.",
                                strerror(saved_errno));
    return 0;
  }
  null_fd = open("/dev/null", O_RDONLY);
  if ((null_fd < 0) ||
      !strappy_bash_move_fd_above_stdio(&output_pipe[0]) ||
      !strappy_bash_move_fd_above_stdio(&output_pipe[1]) ||
      !strappy_bash_move_fd_above_stdio(&child_error_pipe[0]) ||
      !strappy_bash_move_fd_above_stdio(&child_error_pipe[1]) ||
      !strappy_bash_move_fd_above_stdio(&null_fd) ||
      !strappy_bash_set_close_on_exec(child_error_pipe[1]) ||
      !strappy_bash_set_nonblocking(output_pipe[0]) ||
      !strappy_bash_set_nonblocking(child_error_pipe[0])) {
    int saved_errno;

    saved_errno = errno;
    if (null_fd >= 0) {
      close(null_fd);
    }
    close(output_pipe[0]);
    close(output_pipe[1]);
    close(child_error_pipe[0]);
    close(child_error_pipe[1]);
    close(log_fd);
    unlink(result->log_path);
    strappy_set_formatted_error(error_out,
                                "Could not configure bash process pipes: %s.",
                                strerror(saved_errno));
    return 0;
  }

  shell_name = strrchr(shell_path, '/');
  shell_name = (shell_name != NULL) ? shell_name + 1 : shell_path;
  pid = fork();
  if (pid < 0) {
    int saved_errno;

    saved_errno = errno;
    close(null_fd);
    close(output_pipe[0]);
    close(output_pipe[1]);
    close(child_error_pipe[0]);
    close(child_error_pipe[1]);
    close(log_fd);
    unlink(result->log_path);
    strappy_set_formatted_error(error_out,
                                "Could not start bash: %s.",
                                strerror(saved_errno));
    return 0;
  }
  if (pid == 0) {
    char *const child_arguments[] = {
      (char *)shell_name,
      (char *)"-c",
      arguments->command,
      NULL
    };

    close(output_pipe[0]);
    close(child_error_pipe[0]);
    close(log_fd);
    if (setpgid(0, 0) != 0) {
      strappy_bash_child_report_error(
        child_error_pipe[1],
        STRAPPY_BASH_CHILD_ERROR_PROCESS_GROUP);
      _exit(127);
    }
    if (chdir(working_directory) != 0) {
      strappy_bash_child_report_error(
        child_error_pipe[1],
        STRAPPY_BASH_CHILD_ERROR_WORKING_DIRECTORY);
      _exit(127);
    }
    if (dup2(null_fd, STDIN_FILENO) < 0) {
      strappy_bash_child_report_error(child_error_pipe[1],
                                      STRAPPY_BASH_CHILD_ERROR_STDIN);
      _exit(127);
    }
    if (dup2(output_pipe[1], STDOUT_FILENO) < 0) {
      strappy_bash_child_report_error(child_error_pipe[1],
                                      STRAPPY_BASH_CHILD_ERROR_STDOUT);
      _exit(127);
    }
    if (dup2(output_pipe[1], STDERR_FILENO) < 0) {
      strappy_bash_child_report_error(child_error_pipe[1],
                                      STRAPPY_BASH_CHILD_ERROR_STDERR);
      _exit(127);
    }
    close(null_fd);
    close(output_pipe[1]);
    execve(shell_path, child_arguments, environ);
    strappy_bash_child_report_error(child_error_pipe[1],
                                    STRAPPY_BASH_CHILD_ERROR_EXEC);
    _exit(127);
  }

  close(null_fd);
  close(output_pipe[1]);
  close(child_error_pipe[1]);
  if ((setpgid(pid, pid) != 0) && (errno != EACCES) && (errno != ESRCH)) {
    result->stop_reason = STRAPPY_BASH_STOP_SUPERVISOR_ERROR;
    result->supervisor_error_number = errno;
  }

  started_ms = strappy_bash_monotonic_ms();
  timeout_ms = (long long)((arguments->timeout_seconds * 1000.0) + 0.999);
  deadline_ms = (started_ms > (LLONG_MAX - timeout_ms)) ?
    LLONG_MAX : started_ms + timeout_ms;
  terminate_sent_ms = 0LL;
  child_exited_ms = 0LL;
  last_output_ms = started_ms;
  captured_bytes = 0ULL;
  output_eof = 0;
  child_exited = 0;
  terminate_sent = 0;
  kill_sent = 0;
  group_cleanup_sent = 0;

  while (!child_exited || !output_eof) {
    long long now_ms;
    long long wait_ms;
    struct timeval timeout;
    fd_set read_fds;
    int selected;

    now_ms = strappy_bash_monotonic_ms();
    if (!child_exited && (result->stop_reason == STRAPPY_BASH_STOP_NONE)) {
      if ((continue_callback != NULL) &&
          !continue_callback(continue_callback_data)) {
        result->stop_reason = STRAPPY_BASH_STOP_CANCELLED;
      } else if (now_ms >= deadline_ms) {
        result->stop_reason = STRAPPY_BASH_STOP_TIMEOUT;
      }
    }
    if (!child_exited && (result->stop_reason != STRAPPY_BASH_STOP_NONE) &&
        !terminate_sent) {
      strappy_bash_kill_process_group(pid, SIGTERM);
      terminate_sent = 1;
      terminate_sent_ms = now_ms;
    }
    if (!child_exited && terminate_sent && !kill_sent &&
        ((now_ms - terminate_sent_ms) >= STRAPPY_BASH_TERMINATE_GRACE_MS)) {
      strappy_bash_kill_process_group(pid, SIGKILL);
      kill_sent = 1;
    }

    if (child_exited && !group_cleanup_sent &&
        (((now_ms - last_output_ms) >= STRAPPY_BASH_POST_EXIT_IDLE_MS) ||
         ((now_ms - child_exited_ms) >= STRAPPY_BASH_POST_EXIT_MAX_MS))) {
      strappy_bash_kill_process_group(pid, SIGKILL);
      group_cleanup_sent = 1;
    }

    wait_ms = strappy_bash_select_wait_ms(now_ms,
                                           deadline_ms,
                                           child_exited,
                                           result->stop_reason,
                                           terminate_sent,
                                           kill_sent,
                                           terminate_sent_ms);
    FD_ZERO(&read_fds);
    if (!output_eof) {
      FD_SET(output_pipe[0], &read_fds);
    }
    timeout.tv_sec = (long)(wait_ms / 1000LL);
    timeout.tv_usec = (long)((wait_ms % 1000LL) * 1000LL);
    selected = select(output_eof ? 0 : output_pipe[0] + 1,
                      output_eof ? NULL : &read_fds,
                      NULL,
                      NULL,
                      &timeout);
    if (selected < 0) {
      if (errno != EINTR) {
        result->stop_reason = STRAPPY_BASH_STOP_SUPERVISOR_ERROR;
        result->supervisor_error_number = errno;
      }
    } else if ((selected > 0) && !output_eof &&
               FD_ISSET(output_pipe[0], &read_fds)) {
      for (;;) {
        unsigned char buffer[STRAPPY_BASH_READ_BUFFER_BYTES];
        ssize_t bytes_read;

        bytes_read = read(output_pipe[0], buffer, sizeof(buffer));
        if (bytes_read > 0) {
          last_output_ms = strappy_bash_monotonic_ms();
          if (!result->capture_limit_reached &&
              (result->stop_reason != STRAPPY_BASH_STOP_OUTPUT_LIMIT) &&
              (result->stop_reason != STRAPPY_BASH_STOP_SUPERVISOR_ERROR) &&
              !strappy_bash_append_process_output(result,
                                                  log_fd,
                                                  &captured_bytes,
                                                  buffer,
                                                  (size_t)bytes_read)) {
            close(output_pipe[0]);
            output_pipe[0] = -1;
            output_eof = 1;
            break;
          }
          continue;
        }
        if (bytes_read == 0) {
          close(output_pipe[0]);
          output_pipe[0] = -1;
          output_eof = 1;
        } else if ((errno != EAGAIN) && (errno != EWOULDBLOCK) &&
                   (errno != EINTR)) {
          result->stop_reason = STRAPPY_BASH_STOP_SUPERVISOR_ERROR;
          result->supervisor_error_number = errno;
          close(output_pipe[0]);
          output_pipe[0] = -1;
          output_eof = 1;
        }
        break;
      }
    }

    if (!child_exited) {
      pid_t waited;

      waited = waitpid(pid, &result->wait_status, WNOHANG);
      if (waited == pid) {
        child_exited = 1;
        result->has_wait_status = 1;
        child_exited_ms = strappy_bash_monotonic_ms();
      } else if ((waited < 0) && (errno != EINTR)) {
        child_exited = 1;
        child_exited_ms = strappy_bash_monotonic_ms();
        if (errno != ECHILD) {
          result->stop_reason = STRAPPY_BASH_STOP_SUPERVISOR_ERROR;
          result->supervisor_error_number = errno;
        }
      }
    }

    now_ms = strappy_bash_monotonic_ms();
    if (child_exited && group_cleanup_sent && !output_eof &&
        ((now_ms - last_output_ms) >= STRAPPY_BASH_POST_EXIT_IDLE_MS)) {
      close(output_pipe[0]);
      output_pipe[0] = -1;
      output_eof = 1;
    }
  }

  strappy_bash_kill_process_group(pid, SIGKILL);
  if (!child_exited) {
    pid_t waited;

    strappy_bash_kill_process_group(pid, SIGKILL);
    do {
      waited = waitpid(pid, &result->wait_status, 0);
    } while ((waited < 0) && (errno == EINTR));
    if (waited == pid) {
      result->has_wait_status = 1;
    }
  }
  if (output_pipe[0] >= 0) {
    close(output_pipe[0]);
  }
  (void)strappy_bash_read_child_error(child_error_pipe[0],
                                      &result->child_error);
  close(child_error_pipe[0]);
  if (!strappy_bash_output_finish(&result->output) &&
      (result->stop_reason == STRAPPY_BASH_STOP_NONE)) {
    result->stop_reason = STRAPPY_BASH_STOP_SUPERVISOR_ERROR;
    result->supervisor_error_number = ENOMEM;
  }
  if ((close(log_fd) != 0) &&
      (result->stop_reason == STRAPPY_BASH_STOP_NONE)) {
    result->stop_reason = STRAPPY_BASH_STOP_SUPERVISOR_ERROR;
    result->supervisor_error_number = errno;
  }
  return 1;
}

static char *strappy_bash_format_output(
  const strappy_bash_process_result *result,
  const char *empty_text,
  int log_is_complete)
{
  const char *base;
  size_t base_length;
  size_t path_length;
  size_t capacity;
  char *text;
  size_t used;
  int truncated;

  base = ((result->output.tail != NULL) &&
          (result->output.tail_length > 0U)) ?
    result->output.tail : ((empty_text != NULL) ? empty_text : "");
  base_length = strlen(base);
  path_length = (result->log_path != NULL) ? strlen(result->log_path) : 0U;
  if ((base_length > ((size_t)-1) - path_length - 512U)) {
    return NULL;
  }
  capacity = base_length + path_length + 512U;
  text = (char *)malloc(capacity);
  if (text == NULL) {
    return NULL;
  }
  memcpy(text, base, base_length);
  used = base_length;
  text[used] = '\0';

  truncated = strappy_bash_output_is_truncated(&result->output) ||
    result->capture_limit_reached;
  if (truncated && (result->log_path != NULL)) {
    unsigned long long total_lines;
    size_t output_lines;
    int written;

    if (used > 0U) {
      memcpy(text + used, "\n\n", 2U);
      used += 2U;
      text[used] = '\0';
    }
    total_lines = strappy_bash_output_total_lines(&result->output);
    output_lines = strappy_bash_output_line_count(result->output.tail,
                                                   result->output.tail_length);
    if (!log_is_complete) {
      written = snprintf(
        text + used,
        capacity - used,
        "[Showing captured output tail. Captured output is limited to 10 MiB: %s]",
        result->log_path);
    } else if ((total_lines >
                (unsigned long long)STRAPPY_BASH_MAX_DISPLAY_LINES) &&
               (result->output.total_bytes <=
                (unsigned long long)STRAPPY_BASH_MAX_DISPLAY_BYTES)) {
      unsigned long long start_line;

      start_line = total_lines - (unsigned long long)output_lines + 1ULL;
      written = snprintf(text + used,
                         capacity - used,
                         "[Showing lines %llu-%llu of %llu. Full output: %s]",
                         start_line,
                         total_lines,
                         total_lines,
                         result->log_path);
    } else {
      written = snprintf(
        text + used,
        capacity - used,
        "[Showing the last %lu bytes of %llu bytes (50 KiB limit). Full output: %s]",
        (unsigned long)result->output.tail_length,
        result->output.total_bytes,
        result->log_path);
    }
    if ((written < 0) || ((size_t)written >= (capacity - used))) {
      free(text);
      return NULL;
    }
    used += (size_t)written;
  }
  text[used] = '\0';
  return text;
}

static char *strappy_bash_append_status(const char *output,
                                        const char *status)
{
  size_t output_length;
  size_t status_length;
  size_t separator_length;
  char *message;

  output = (output != NULL) ? output : "";
  status = (status != NULL) ? status : "";
  output_length = strlen(output);
  status_length = strlen(status);
  separator_length = (output_length > 0U) ? 2U : 0U;
  if (output_length > ((size_t)-1) - status_length - separator_length - 1U) {
    return NULL;
  }
  message = (char *)malloc(output_length + separator_length +
                           status_length + 1U);
  if (message == NULL) {
    return NULL;
  }
  memcpy(message, output, output_length);
  if (separator_length > 0U) {
    memcpy(message + output_length, "\n\n", separator_length);
  }
  memcpy(message + output_length + separator_length,
         status,
         status_length + 1U);
  return message;
}

static void strappy_bash_set_owned_error(char **error_out, char *message)
{
  if (error_out != NULL) {
    *error_out = message;
  } else {
    free(message);
  }
}

static const char *strappy_bash_child_error_action(int stage)
{
  if (stage == STRAPPY_BASH_CHILD_ERROR_PROCESS_GROUP) {
    return "create the bash process group";
  }
  if (stage == STRAPPY_BASH_CHILD_ERROR_WORKING_DIRECTORY) {
    return "enter the session working directory";
  }
  if (stage == STRAPPY_BASH_CHILD_ERROR_STDIN) {
    return "redirect bash stdin";
  }
  if (stage == STRAPPY_BASH_CHILD_ERROR_STDOUT) {
    return "capture bash stdout";
  }
  if (stage == STRAPPY_BASH_CHILD_ERROR_STDERR) {
    return "capture bash stderr";
  }
  return "execute the bash shell";
}

static char *strappy_bash_status_message(
  const strappy_bash_process_result *result,
  const strappy_bash_arguments *arguments)
{
  char buffer[512];

  if (result->child_error.stage != STRAPPY_BASH_CHILD_ERROR_NONE) {
    snprintf(buffer,
             sizeof(buffer),
             "Could not %s: %s.",
             strappy_bash_child_error_action(result->child_error.stage),
             strerror(result->child_error.error_number));
    return strappy_string_duplicate(buffer);
  }
  if (result->stop_reason == STRAPPY_BASH_STOP_CANCELLED) {
    return strappy_string_duplicate("Command aborted.");
  }
  if (result->stop_reason == STRAPPY_BASH_STOP_TIMEOUT) {
    snprintf(buffer,
             sizeof(buffer),
             "Command timed out after %.3g seconds.",
             arguments->timeout_seconds);
    return strappy_string_duplicate(buffer);
  }
  if (result->stop_reason == STRAPPY_BASH_STOP_OUTPUT_LIMIT) {
    return strappy_string_duplicate(
      "Command exceeded the 10 MiB output limit and was terminated.");
  }
  if (result->stop_reason == STRAPPY_BASH_STOP_SUPERVISOR_ERROR) {
    snprintf(buffer,
             sizeof(buffer),
             "Could not supervise the bash command: %s.",
             strerror((result->supervisor_error_number != 0) ?
                      result->supervisor_error_number : EIO));
    return strappy_string_duplicate(buffer);
  }
  if (!result->has_wait_status) {
    return strappy_string_duplicate(
      "Could not determine the bash command exit status.");
  }
  if (WIFEXITED(result->wait_status)) {
    int exit_code;

    exit_code = WEXITSTATUS(result->wait_status);
    if (exit_code == 0) {
      return NULL;
    }
    snprintf(buffer,
             sizeof(buffer),
             "Command exited with code %d.",
             exit_code);
    return strappy_string_duplicate(buffer);
  }
  if (WIFSIGNALED(result->wait_status)) {
    snprintf(buffer,
             sizeof(buffer),
             "Command terminated by signal %d.",
             WTERMSIG(result->wait_status));
    return strappy_string_duplicate(buffer);
  }
  return strappy_string_duplicate("Command did not exit normally.");
}

static int strappy_bash_process_succeeded(
  const strappy_bash_process_result *result)
{
  return (result != NULL) &&
    (result->child_error.stage == STRAPPY_BASH_CHILD_ERROR_NONE) &&
    (result->stop_reason == STRAPPY_BASH_STOP_NONE) &&
    result->has_wait_status && WIFEXITED(result->wait_status) &&
    (WEXITSTATUS(result->wait_status) == 0);
}

char *strappy_bash_result_json(const char *output,
                               int output_truncated,
                               char **error_out)
{
  cJSON *root;
  char *json;

  root = cJSON_CreateObject();
  if ((root == NULL) ||
      (cJSON_AddStringToObject(root,
                               "output",
                               (output != NULL) ? output : "") == NULL) ||
      (cJSON_AddBoolToObject(root,
                            "output_truncated",
                            output_truncated ? 1 : 0) == NULL)) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Could not build the bash result.");
    return NULL;
  }
  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize the bash result.");
  }
  return json;
}

char *strappy_bash_execute(
  const char *session_db_path,
  long long session_id,
  const char *arguments_json,
  strappy_bash_continue_callback continue_callback,
  void *continue_callback_data,
  int *output_truncated_out,
  int *cancelled_out,
  char **error_out)
{
  strappy_bash_arguments arguments;
  strappy_bash_process_result process;
  char *assistant_set_id;
  char *working_directory;
  char *shell_path;
  char *output;
  char *result_json;
  char *status;
  char *message;
  int bash_enabled;
  int keep_log;
  int success;

  if (output_truncated_out != NULL) {
    *output_truncated_out = 0;
  }
  if (cancelled_out != NULL) {
    *cancelled_out = 0;
  }
  if ((session_db_path == NULL) || (session_db_path[0] == '\0') ||
      (session_id <= 0LL)) {
    strappy_set_error(error_out, "bash requires an active session.");
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
      "bash is available only in Coding Assistant sessions.");
    return NULL;
  }
  free(assistant_set_id);

  bash_enabled = 0;
  if (!strappy_db_get_session_bash_enabled(session_db_path,
                                           session_id,
                                           &bash_enabled,
                                           error_out)) {
    return NULL;
  }
  if (!bash_enabled) {
    strappy_set_error(error_out, "bash is disabled for this session.");
    return NULL;
  }

  if (!strappy_bash_parse_arguments(arguments_json, &arguments, error_out)) {
    return NULL;
  }
  if ((continue_callback != NULL) &&
      !continue_callback(continue_callback_data)) {
    strappy_bash_arguments_destroy(&arguments);
    if (cancelled_out != NULL) {
      *cancelled_out = 1;
    }
    strappy_set_error(error_out, "Command aborted.");
    return NULL;
  }

  working_directory = NULL;
  shell_path = NULL;
  if (!strappy_db_get_session_working_directory(session_db_path,
                                                 session_id,
                                                 &working_directory,
                                                 error_out)) {
    strappy_bash_arguments_destroy(&arguments);
    return NULL;
  }
  shell_path = strappy_bash_resolve_shell(error_out);
  if (shell_path == NULL) {
    free(working_directory);
    strappy_bash_arguments_destroy(&arguments);
    return NULL;
  }

  strappy_bash_process_result_init(&process);
  if (!strappy_bash_supervise(shell_path,
                              working_directory,
                              &arguments,
                              continue_callback,
                              continue_callback_data,
                              &process,
                              error_out)) {
    strappy_bash_process_result_destroy(&process);
    free(shell_path);
    free(working_directory);
    strappy_bash_arguments_destroy(&arguments);
    return NULL;
  }
  free(shell_path);
  free(working_directory);

  success = strappy_bash_process_succeeded(&process);
  status = success ? NULL :
    strappy_bash_status_message(&process, &arguments);
  if (!success && (status == NULL)) {
    if (process.log_path != NULL) {
      unlink(process.log_path);
    }
    strappy_bash_process_result_destroy(&process);
    strappy_bash_arguments_destroy(&arguments);
    strappy_set_error(error_out, "Could not allocate the bash status result.");
    return NULL;
  }
  keep_log = strappy_bash_output_is_truncated(&process.output) ||
    process.capture_limit_reached;
  if (output_truncated_out != NULL) {
    *output_truncated_out = keep_log;
  }
  output = strappy_bash_format_output(
    &process,
    success ? "(no output)" :
      (((process.stop_reason == STRAPPY_BASH_STOP_TIMEOUT) ||
        (process.stop_reason == STRAPPY_BASH_STOP_CANCELLED)) ? "" :
       "(no output)"),
    !process.capture_limit_reached);
  if (output == NULL) {
    free(status);
    if (process.log_path != NULL) {
      unlink(process.log_path);
    }
    strappy_bash_process_result_destroy(&process);
    strappy_bash_arguments_destroy(&arguments);
    strappy_set_error(error_out, "Could not allocate the bash result.");
    return NULL;
  }

  if (!keep_log && (process.log_path != NULL)) {
    unlink(process.log_path);
  }
  if (success) {
    result_json = strappy_bash_result_json(output,
                                           keep_log,
                                           error_out);
    free(output);
    if ((result_json == NULL) && keep_log &&
        (process.log_path != NULL)) {
      unlink(process.log_path);
    }
    strappy_bash_process_result_destroy(&process);
    strappy_bash_arguments_destroy(&arguments);
    return result_json;
  }

  message = strappy_bash_append_status(output, status);
  free(output);
  free(status);
  if (process.stop_reason == STRAPPY_BASH_STOP_CANCELLED) {
    if (cancelled_out != NULL) {
      *cancelled_out = 1;
    }
  }
  strappy_bash_process_result_destroy(&process);
  strappy_bash_arguments_destroy(&arguments);
  if (message == NULL) {
    strappy_set_error(error_out, "Could not allocate the bash error result.");
    return NULL;
  }
  strappy_bash_set_owned_error(error_out, message);
  return NULL;
}
