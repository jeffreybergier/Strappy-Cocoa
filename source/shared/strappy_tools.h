#ifndef STRAPPY_TOOLS_H
#define STRAPPY_TOOLS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STRAPPY_TOOL_DATABASE_LIST_INFO "database_list_info"
#define STRAPPY_TOOL_DATABASE_QUERY "database_query"
#define STRAPPY_TOOL_HELPER_DATETIME_TO_ISO8601 "helper_datetime_to_iso8601"
#define STRAPPY_TOOL_HELPER_DATETIME_FROM_ISO8601 "helper_datetime_from_iso8601"
#define STRAPPY_TOOL_MEMORY_USER_FACT_READ "memory_user_fact_read"
#define STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER "memory_user_fact_remember"
#define STRAPPY_TOOL_MEMORY_USER_FACT_FORGET "memory_user_fact_forget"
#define STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE "helper_session_name_write"
#define STRAPPY_TOOL_DATABASE_CONTEXT_READ "database_context_read"
#define STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER "memory_database_hint_remember"
#define STRAPPY_TOOL_MEMORY_DATABASE_HINT_FORGET "memory_database_hint_forget"
#define STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_SEARCH "helper_fontawesome_shortcode_search"
#define STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_CONFIRM "helper_fontawesome_shortcode_confirm"

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
  int web_search_enabled,
  char **error_out);
char *strappy_tools_responses_request_json_filtered(
  const char *resource_dir,
  const char * const *allowed_names,
  size_t allowed_name_count,
  int web_search_enabled,
  char **error_out);
char *strappy_tools_tool_guidance_string(const char *resource_dir,
                                         const char *section_name,
                                         const char *key,
                                         char **error_out);
int strappy_tools_is_helper(const char *tool_name);
char *strappy_tools_execute(const char *session_db_path,
                            long long active_session_id,
                            const char *resource_dir,
                            const char *tool_name,
                            const char *arguments_json,
                            char **error_out);

#ifdef __cplusplus
}
#endif

#endif
