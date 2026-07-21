#ifndef STRAPPY_TOOLS_H
#define STRAPPY_TOOLS_H

#include "strappy_config.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STRAPPY_TOOL_DATABASE_LIST "database_list"
#define STRAPPY_TOOL_DATABASE_QUERY "database_query"
#define STRAPPY_TOOL_BASH "bash"
#define STRAPPY_TOOL_FILE_READ "file_read"
#define STRAPPY_TOOL_FILE_WRITE "file_write"
#define STRAPPY_TOOL_FILE_EDIT "file_edit"
#define STRAPPY_TOOL_DATETIME_TO_ISO8601 "datetime_to_iso8601"
#define STRAPPY_TOOL_DATETIME_FROM_ISO8601 "datetime_from_iso8601"
#define STRAPPY_TOOL_MEMORY_READ "memory_read"
#define STRAPPY_TOOL_MEMORY_SAVE "memory_save"
#define STRAPPY_TOOL_MEMORY_DELETE "memory_delete"
#define STRAPPY_TOOL_SESSION_RENAME "session_rename"
#define STRAPPY_TOOL_DATABASE_CONTEXT "database_context"
#define STRAPPY_TOOL_DATABASE_STUDY "database_study"
#define STRAPPY_TOOL_FONTAWESOME_SEARCH "fontawesome_search"
#define STRAPPY_TOOL_FONTAWESOME_CONFIRM "fontawesome_confirm"
#define STRAPPY_TOOL_OPENROUTER_WEB_SEARCH "openrouter:web_search"
#define STRAPPY_TOOL_OPENROUTER_WEB_FETCH "openrouter:web_fetch"

typedef int (*strappy_tools_continue_callback)(void *user_data);

char *strappy_tools_request_json(const char *resource_dir,
                                 char **error_out);
char *strappy_tools_request_json_filtered(const char *resource_dir,
                                          const char * const *allowed_names,
                                          size_t allowed_name_count,
                                          char **error_out);
char *strappy_tools_display_registry_json(const char *resource_dir,
                                          char **error_out);
char *strappy_tools_responses_request_json(
  const char *resource_dir,
  strappy_web_provider web_provider,
  char **error_out);
char *strappy_tools_responses_request_json_filtered(
  const char *resource_dir,
  const char * const *allowed_names,
  size_t allowed_name_count,
  strappy_web_provider web_provider,
  char **error_out);
char *strappy_tools_prompt_markdown_filtered(
  const char *resource_dir,
  const char * const *allowed_names,
  size_t allowed_name_count,
  strappy_web_provider web_provider,
  char **error_out);
char *strappy_tools_tool_guidance_string(const char *resource_dir,
                                         const char *section_name,
                                         const char *key,
                                         char **error_out);
int strappy_tools_is_registered(const char *tool_name);
int strappy_tools_is_server(const char *tool_name);
int strappy_tools_is_helper(const char *tool_name);
char *strappy_tools_execute(const char *session_db_path,
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

#ifdef __cplusplus
}
#endif

#endif
