#ifndef STRAPPY_TOOLS_H
#define STRAPPY_TOOLS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STRAPPY_TOOL_DATABASE_LIST_INFO "database_list_info"
#define STRAPPY_TOOL_DATABASE_QUERY "database_query"
#define STRAPPY_TOOL_HELPER_CONVERT_DATES "helper_convert_dates"
#define STRAPPY_TOOL_HELPER_USER_INFO_READ "helper_user_info_read"
#define STRAPPY_TOOL_HELPER_USER_INFO_REMEMBER "helper_user_info_remember"
#define STRAPPY_TOOL_HELPER_USER_INFO_FORGET "helper_user_info_forget"
#define STRAPPY_TOOL_DATABASE_CONTEXT_READ "database_context_read"
#define STRAPPY_TOOL_HELPER_DATABASE_INFO_REMEMBER "helper_database_info_remember"
#define STRAPPY_TOOL_HELPER_DATABASE_INFO_FORGET "helper_database_info_forget"

char *strappy_tools_request_json(const char *resource_dir,
                                 char **error_out);
char *strappy_tools_request_json_filtered(const char *resource_dir,
                                          const char * const *allowed_names,
                                          size_t allowed_name_count,
                                          char **error_out);
int strappy_tools_is_helper(const char *tool_name);
char *strappy_tools_execute(const char *session_db_path,
                            const char *resource_dir,
                            const char *tool_name,
                            const char *arguments_json,
                            char **error_out);

#ifdef __cplusplus
}
#endif

#endif
