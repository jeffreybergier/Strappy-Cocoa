#define _POSIX_C_SOURCE 200809L

#include "strappy_assistant_sets.h"
#include "strappy_bash.h"
#include "strappy_db.h"

#include <cJSON.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct harness_cancel_context {
  unsigned int poll_count;
  unsigned int cancel_at_poll;
} harness_cancel_context;

static long long harness_now_ms(void)
{
  struct timeval value;

  if (gettimeofday(&value, NULL) != 0) {
    return 0LL;
  }
  return ((long long)value.tv_sec * 1000LL) +
    ((long long)value.tv_usec / 1000LL);
}

static void harness_sleep_ms(long milliseconds)
{
  struct timeval timeout;

  timeout.tv_sec = milliseconds / 1000L;
  timeout.tv_usec = (milliseconds % 1000L) * 1000L;
  (void)select(0, NULL, NULL, NULL, &timeout);
}

static int harness_should_continue(void *user_data)
{
  harness_cancel_context *context;

  context = (harness_cancel_context *)user_data;
  if (context == NULL) {
    return 1;
  }
  context->poll_count++;
  return (context->poll_count < context->cancel_at_poll) ? 1 : 0;
}

static int harness_execute_matches(const char *catalog_path,
                                   long long session_id,
                                   const char *arguments_json,
                                   const char *expected,
                                   int expected_output_truncated)
{
  char *error;
  char *output;
  char *result_output;
  int cancelled;
  int output_truncated;
  int ok;

  error = NULL;
  cancelled = 0;
  output_truncated = -1;
  output = strappy_bash_execute(catalog_path,
                                session_id,
                                arguments_json,
                                NULL,
                                NULL,
                                &output_truncated,
                                &cancelled,
                                &error);
  result_output = NULL;
  if (output != NULL) {
    cJSON *root;
    cJSON *text;
    cJSON *truncated;

    root = cJSON_Parse(output);
    text = cJSON_IsObject(root) ?
      cJSON_GetObjectItemCaseSensitive(root, "output") : NULL;
    truncated = cJSON_IsObject(root) ?
      cJSON_GetObjectItemCaseSensitive(root, "output_truncated") : NULL;
    if (cJSON_IsString(text) && (text->valuestring != NULL) &&
        (expected_output_truncated ? cJSON_IsTrue(truncated) :
          cJSON_IsFalse(truncated))) {
      result_output = strdup(text->valuestring);
    }
    cJSON_Delete(root);
  }
  ok = (result_output != NULL) && (strcmp(result_output, expected) == 0) &&
    (output_truncated == expected_output_truncated) && !cancelled &&
    (error == NULL);
  if (!ok) {
    fprintf(stderr,
            "bash output mismatch for %s\nExpected: %s\nActual: %s\n"
            "Truncated: %d\nError: %s\n",
            arguments_json,
            expected,
            (result_output != NULL) ? result_output :
              ((output != NULL) ? output : "(null)"),
            output_truncated,
            (error != NULL) ? error : "(null)");
  }
  free(result_output);
  free(output);
  free(error);
  return ok;
}

static int harness_execute_error_contains(const char *catalog_path,
                                          long long session_id,
                                          const char *arguments_json,
                                          const char *first,
                                          const char *second)
{
  char *error;
  char *output;
  int cancelled;
  int output_truncated;
  int ok;

  error = NULL;
  cancelled = 0;
  output_truncated = -1;
  output = strappy_bash_execute(catalog_path,
                                session_id,
                                arguments_json,
                                NULL,
                                NULL,
                                &output_truncated,
                                &cancelled,
                                &error);
  ok = (output == NULL) && !cancelled && (error != NULL) &&
    (output_truncated == 0) &&
    (strstr(error, first) != NULL) &&
    ((second == NULL) || (strstr(error, second) != NULL));
  if (!ok) {
    fprintf(stderr,
            "bash error mismatch for %s\nOutput: %s\nError: %s\n",
            arguments_json,
            (output != NULL) ? output : "(null)",
            (error != NULL) ? error : "(null)");
  }
  free(output);
  free(error);
  return ok;
}

static char *harness_copy_log_path(const char *text)
{
  static const char full_marker[] = "Full output: ";
  static const char limit_marker[] = "10 MiB: ";
  const char *start;
  const char *end;
  size_t length;
  char *path;

  if (text == NULL) {
    return NULL;
  }
  start = strstr(text, full_marker);
  if (start != NULL) {
    start += sizeof(full_marker) - 1U;
  } else {
    start = strstr(text, limit_marker);
    if (start == NULL) {
      return NULL;
    }
    start += sizeof(limit_marker) - 1U;
  }
  end = strchr(start, ']');
  if ((end == NULL) || (end <= start)) {
    return NULL;
  }
  length = (size_t)(end - start);
  path = (char *)malloc(length + 1U);
  if (path == NULL) {
    return NULL;
  }
  memcpy(path, start, length);
  path[length] = '\0';
  return path;
}

static int harness_log_matches(const char *path, off_t minimum_size)
{
  struct stat info;

  return (path != NULL) && (stat(path, &info) == 0) &&
    S_ISREG(info.st_mode) && ((info.st_mode & 0777) == 0600) &&
    (info.st_size >= minimum_size);
}

static int harness_wait_process_gone(pid_t pid)
{
  int attempt;

  for (attempt = 0; attempt < 100; attempt++) {
    char path[64];
    char line[512];
    char *command_end;
    FILE *file;
    int written;

    if ((kill(pid, 0) != 0) && (errno == ESRCH)) {
      return 1;
    }
    written = snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);
    file = ((written > 0) && ((size_t)written < sizeof(path))) ?
      fopen(path, "rb") : NULL;
    if ((file != NULL) && (fgets(line, sizeof(line), file) != NULL)) {
      command_end = strrchr(line, ')');
      if ((command_end != NULL) && (command_end[1] == ' ') &&
          (command_end[2] == 'Z')) {
        fclose(file);
        return 1;
      }
    }
    if (file != NULL) {
      fclose(file);
    }
    harness_sleep_ms(10L);
  }
  (void)kill(pid, SIGKILL);
  return 0;
}

static int harness_read_pid(const char *path, pid_t *pid_out)
{
  FILE *file;
  long value;
  int ok;

  *pid_out = (pid_t)-1;
  file = fopen(path, "rb");
  if (file == NULL) {
    return 0;
  }
  value = -1L;
  ok = (fscanf(file, "%ld", &value) == 1) && (value > 0L);
  if (fclose(file) != 0) {
    ok = 0;
  }
  if (ok) {
    *pid_out = (pid_t)value;
  }
  return ok;
}

static void harness_unlink_database(const char *catalog_path)
{
  char path[1400];
  int written;

  if (catalog_path == NULL) {
    return;
  }
  unlink(catalog_path);
  written = snprintf(path, sizeof(path), "%s-wal", catalog_path);
  if ((written > 0) && ((size_t)written < sizeof(path))) {
    unlink(path);
  }
  written = snprintf(path, sizeof(path), "%s-shm", catalog_path);
  if ((written > 0) && ((size_t)written < sizeof(path))) {
    unlink(path);
  }
}

static int harness_run(void)
{
  char temp_dir[1024];
  char catalog_path[1200];
  char pid_path[1200];
  char expected_cwd[1100];
  char *error;
  char *output;
  char *result_output;
  char *log_path;
  long long session_id;
  long long started_ms;
  long long elapsed_ms;
  harness_cancel_context cancel_context;
  pid_t background_pid;
  int cancelled;
  int output_truncated;
  int written;
  int ok;

  temp_dir[0] = '\0';
  catalog_path[0] = '\0';
  pid_path[0] = '\0';
  expected_cwd[0] = '\0';
  error = NULL;
  output = NULL;
  result_output = NULL;
  log_path = NULL;
  session_id = 0LL;
  background_pid = (pid_t)-1;
  cancelled = 0;
  output_truncated = -1;
  ok = 0;
  written = snprintf(temp_dir,
                     sizeof(temp_dir),
                     "/tmp/strappy-bash-harness-XXXXXX");
  if ((written <= 0) || ((size_t)written >= sizeof(temp_dir)) ||
      (mkdtemp(temp_dir) == NULL)) {
    perror("Could not create bash harness directory");
    return 0;
  }
  written = snprintf(catalog_path,
                     sizeof(catalog_path),
                     "%s/catalog.sqlite3",
                     temp_dir);
  if ((written <= 0) || ((size_t)written >= sizeof(catalog_path))) {
    fprintf(stderr, "Bash harness catalog path is too long.\n");
    goto cleanup;
  }
  written = snprintf(pid_path,
                     sizeof(pid_path),
                     "%s/bash-child.pid",
                     temp_dir);
  if ((written <= 0) || ((size_t)written >= sizeof(pid_path))) {
    fprintf(stderr, "Bash harness PID path is too long.\n");
    goto cleanup;
  }
  written = snprintf(expected_cwd, sizeof(expected_cwd), "%s\n", temp_dir);
  if ((written <= 0) || ((size_t)written >= sizeof(expected_cwd))) {
    fprintf(stderr, "Bash harness cwd result is too long.\n");
    goto cleanup;
  }

  if (!strappy_db_create_session_with_working_directory(catalog_path,
                                                        temp_dir,
                                                        &session_id,
                                                        &error)) {
    fprintf(stderr,
            "Could not create bash harness session: %s\n",
            (error != NULL) ? error : "unknown");
    goto cleanup;
  }
  output = strappy_bash_execute(catalog_path,
                                session_id,
                                "{\"command\":\"true\"}",
                                NULL,
                                NULL,
                                &output_truncated,
                                &cancelled,
                                &error);
  if ((output != NULL) || (error == NULL) ||
      (output_truncated != 0) ||
      (strstr(error,
              "bash is available only in Coding Assistant sessions") == NULL)) {
    fprintf(stderr, "bash was not isolated to Coding Assistant sessions.\n");
    goto cleanup;
  }
  free(output);
  output = NULL;
  free(error);
  error = NULL;

  if (!strappy_db_update_session_assistant_set(
        catalog_path,
        session_id,
        STRAPPY_ASSISTANT_SET_CODING_ASSISTANT,
        &error)) {
    fprintf(stderr,
            "Could not select Coding Assistant: %s\n",
            (error != NULL) ? error : "unknown");
    goto cleanup;
  }

  output = strappy_bash_execute(catalog_path,
                                session_id,
                                "{\"command\":\"true\"}",
                                NULL,
                                NULL,
                                &output_truncated,
                                &cancelled,
                                &error);
  if ((output != NULL) || (error == NULL) ||
      (output_truncated != 0) ||
      (strstr(error, "bash is disabled for this session") == NULL)) {
    fprintf(stderr, "bash was not disabled by default.\n");
    goto cleanup;
  }
  free(output);
  output = NULL;
  free(error);
  error = NULL;
  if (!strappy_db_update_session_bash_enabled(catalog_path,
                                              session_id,
                                              1,
                                              &error)) {
    fprintf(stderr,
            "Could not enable Bash for the Coding Assistant: %s\n",
            (error != NULL) ? error : "unknown");
    goto cleanup;
  }

  if (!harness_execute_matches(
        catalog_path,
        session_id,
        "{\"command\":\"printf 'stdout\\\\n'; printf 'stderr\\\\n' >&2\"}",
        "stdout\nstderr\n",
        0) ||
      !harness_execute_matches(catalog_path,
                               session_id,
                               "{\"command\":\"pwd\"}",
                               expected_cwd,
                               0) ||
      !harness_execute_matches(catalog_path,
                               session_id,
                               "{\"command\":\"true\"}",
                               "(no output)",
                               0) ||
      !harness_execute_matches(catalog_path,
                               session_id,
                               "{\"command\":\"cd /\"}",
                               "(no output)",
                               0) ||
      !harness_execute_matches(catalog_path,
                               session_id,
                               "{\"command\":\"pwd\"}",
                               expected_cwd,
                               0) ||
      !harness_execute_matches(
        catalog_path,
        session_id,
        "{\"command\":\"if read value; then printf unexpected; else printf closed; fi\"}",
        "closed",
        0)) {
    goto cleanup;
  }

  if (!harness_execute_error_contains(catalog_path,
                                      session_id,
                                      "{}",
                                      "bash requires a non-blank command",
                                      NULL) ||
      !harness_execute_error_contains(
        catalog_path,
        session_id,
        "{\"command\":\" \\t\"}",
        "bash command must be one non-blank string",
        NULL) ||
      !harness_execute_error_contains(
        catalog_path,
        session_id,
        "{\"command\":\"true\",\"timeout\":121}",
        "at most 120 seconds",
        NULL) ||
      !harness_execute_error_contains(
        catalog_path,
        session_id,
        "{\"command\":\"true\",\"extra\":true}",
        "bash accepts only command and timeout",
        NULL) ||
      !harness_execute_error_contains(
        catalog_path,
        session_id,
        "{\"command\":\"printf 'before\\\\n'; exit 7\"}",
        "before\n",
        "Command exited with code 7.")) {
    goto cleanup;
  }

  started_ms = harness_now_ms();
  error = NULL;
  output_truncated = -1;
  output = strappy_bash_execute(
    catalog_path,
    session_id,
    "{\"command\":\"printf 'waiting\\\\n'; sleep 30\",\"timeout\":0.05}",
    NULL,
    NULL,
    &output_truncated,
    &cancelled,
    &error);
  elapsed_ms = harness_now_ms() - started_ms;
  if ((output != NULL) || cancelled || (error == NULL) ||
      (output_truncated != 0) ||
      (strstr(error, "waiting\n") == NULL) ||
      (strstr(error, "Command timed out after 0.05 seconds.") == NULL) ||
      (elapsed_ms > 3000LL)) {
    fprintf(stderr,
            "bash timeout failed after %lld ms: %s\n",
            elapsed_ms,
            (error != NULL) ? error : "unknown");
    goto cleanup;
  }
  free(error);
  error = NULL;

  cancel_context.poll_count = 0U;
  cancel_context.cancel_at_poll = 4U;
  cancelled = 0;
  output_truncated = -1;
  started_ms = harness_now_ms();
  output = strappy_bash_execute(
    catalog_path,
    session_id,
    "{\"command\":\"sleep 30 & child=$!; printf '%s' \\\"$child\\\" > bash-child.pid; wait \\\"$child\\\"\"}",
    harness_should_continue,
    &cancel_context,
    &output_truncated,
    &cancelled,
    &error);
  elapsed_ms = harness_now_ms() - started_ms;
  if ((output != NULL) || !cancelled || (error == NULL) ||
      (output_truncated != 0) ||
      (strstr(error, "Command aborted.") == NULL) ||
      (elapsed_ms > 3000LL) ||
      !harness_read_pid(pid_path, &background_pid) ||
      !harness_wait_process_gone(background_pid)) {
    fprintf(stderr,
            "bash cancellation/process-group cleanup failed after %lld ms: %s\n",
            elapsed_ms,
            (error != NULL) ? error : "unknown");
    goto cleanup;
  }
  free(error);
  error = NULL;
  unlink(pid_path);

  output = strappy_bash_execute(
    catalog_path,
    session_id,
    "{\"command\":\"i=1; while [ \\\"$i\\\" -le 2500 ]; do printf 'line-%04d-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\\\\n' \\\"$i\\\"; i=$((i+1)); done\"}",
    NULL,
    NULL,
    &output_truncated,
    &cancelled,
    &error);
  if (output != NULL) {
    cJSON *root;
    cJSON *text;
    cJSON *truncated;

    root = cJSON_Parse(output);
    text = cJSON_IsObject(root) ?
      cJSON_GetObjectItemCaseSensitive(root, "output") : NULL;
    truncated = cJSON_IsObject(root) ?
      cJSON_GetObjectItemCaseSensitive(root, "output_truncated") : NULL;
    if (cJSON_IsString(text) && (text->valuestring != NULL) &&
        cJSON_IsTrue(truncated)) {
      result_output = strdup(text->valuestring);
    }
    cJSON_Delete(root);
  }
  log_path = harness_copy_log_path(result_output);
  if ((output == NULL) || (result_output == NULL) || (error != NULL) ||
      (output_truncated != 1) || (strlen(result_output) > 53000U) ||
      (strstr(result_output, "line-2500-") == NULL) ||
      !harness_log_matches(log_path, (off_t)100000)) {
    fprintf(stderr,
            "bash display truncation failed: %s\n",
            (error != NULL) ? error : ((output != NULL) ? output : "unknown"));
    goto cleanup;
  }
  unlink(log_path);
  free(log_path);
  log_path = NULL;
  free(output);
  output = NULL;
  free(result_output);
  result_output = NULL;

  output_truncated = -1;
  output = strappy_bash_execute(
    catalog_path,
    session_id,
    "{\"command\":\"head -c 10485761 /dev/zero\"}",
    NULL,
    NULL,
    &output_truncated,
    &cancelled,
    &error);
  log_path = harness_copy_log_path(error);
  if ((output != NULL) || cancelled || (error == NULL) ||
      (output_truncated != 1) ||
      (strstr(error, "exceeded the 10 MiB output limit") == NULL) ||
      !harness_log_matches(log_path, (off_t)(10U * 1024U * 1024U))) {
    fprintf(stderr,
            "bash hard output cap failed: %s\n",
            (error != NULL) ? error : "unknown");
    goto cleanup;
  }
  unlink(log_path);
  free(log_path);
  log_path = NULL;
  free(error);
  error = NULL;
  ok = 1;

cleanup:
  if ((background_pid > 0) &&
      !((kill(background_pid, 0) != 0) && (errno == ESRCH))) {
    (void)kill(background_pid, SIGKILL);
  }
  if (log_path != NULL) {
    unlink(log_path);
  }
  free(log_path);
  free(result_output);
  free(output);
  free(error);
  if (pid_path[0] != '\0') {
    unlink(pid_path);
  }
  if (catalog_path[0] != '\0') {
    harness_unlink_database(catalog_path);
  }
  if (temp_dir[0] != '\0') {
    rmdir(temp_dir);
  }
  return ok;
}

int main(void)
{
  if (!harness_run()) {
    fprintf(stderr, "bash_harness failed.\n");
    return 1;
  }
  printf("bash_harness passed.\n");
  return 0;
}
