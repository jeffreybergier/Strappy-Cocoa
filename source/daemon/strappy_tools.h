#ifndef STRAPPY_TOOLS_H
#define STRAPPY_TOOLS_H
#include "strappy_tool_metadata.h"
typedef int (*strappy_tools_continue_callback)(void *user_data);
char *strappy_tools_execute(const char *session_db_path,
                            long long active_session_id,
                            const char *resource_dir,
                            const char *tool_name,
                            const char *arguments_json,
                            char **error_out);
/* arguments_json must come from application-owned preflight configuration. */
char *strappy_tools_execute_preflight(const char *session_db_path,
                                      long long active_session_id,
                                      const char *resource_dir,
                                      const char *tool_name,
                                      const char *arguments_json,
                                      char **error_out);
char *strappy_tools_execute_for_function_call(
  const char *session_db_path,
  long long active_session_id,
  const char *resource_dir,
  const char *provider_call_id,
  const char *tool_name,
  const char *arguments_json,
  char **error_out);
char *strappy_tools_execute_for_function_call_with_cancellation(
  const char *session_db_path,
  long long active_session_id,
  const char *resource_dir,
  const char *provider_call_id,
  const char *tool_name,
  const char *arguments_json,
  strappy_tools_continue_callback continue_callback,
  void *continue_callback_data,
  int *output_truncated_out,
  int *cancelled_out,
  char **error_out);

#endif
