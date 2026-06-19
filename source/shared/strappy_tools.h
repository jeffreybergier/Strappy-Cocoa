#ifndef STRAPPY_TOOLS_H
#define STRAPPY_TOOLS_H

#ifdef __cplusplus
extern "C" {
#endif

#define STRAPPY_TOOL_DATABASE_LIST "database_list"

char *strappy_tools_request_json(char **error_out);
char *strappy_tools_prompt_fragment(char **error_out);
char *strappy_tools_execute(const char *session_db_path,
                            const char *tool_name,
                            const char *arguments_json,
                            char **error_out);

#ifdef __cplusplus
}
#endif

#endif
