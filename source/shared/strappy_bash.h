#ifndef STRAPPY_BASH_H
#define STRAPPY_BASH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STRAPPY_BASH_DEFAULT_TIMEOUT_SECONDS 120.0
#define STRAPPY_BASH_MAX_TIMEOUT_SECONDS 120.0
#define STRAPPY_BASH_MAX_COMMAND_BYTES (32U * 1024U)
#define STRAPPY_BASH_MAX_DISPLAY_LINES 2000U
#define STRAPPY_BASH_MAX_DISPLAY_BYTES (50U * 1024U)
#define STRAPPY_BASH_MAX_CAPTURE_BYTES (10U * 1024U * 1024U)

typedef int (*strappy_bash_continue_callback)(void *user_data);

char *strappy_bash_result_json(const char *output,
                               int output_truncated,
                               char **error_out);
char *strappy_bash_execute(
  const char *session_db_path,
  long long session_id,
  const char *arguments_json,
  strappy_bash_continue_callback continue_callback,
  void *continue_callback_data,
  int *output_truncated_out,
  int *cancelled_out,
  char **error_out);
/* arguments_json must come from application-owned preflight configuration. */
char *strappy_bash_execute_preflight(
  const char *session_db_path,
  long long session_id,
  const char *arguments_json,
  char **error_out);

#ifdef __cplusplus
}
#endif

#endif
