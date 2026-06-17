#ifndef STRAPPY_ASSISTANT_H
#define STRAPPY_ASSISTANT_H

#ifdef __cplusplus
extern "C" {
#endif

char *strappy_assistant_send_prompt(const char *prompt,
                                    const char *env_path,
                                    char **error_out);
char *strappy_assistant_send_prompt_and_store(const char *prompt,
                                             const char *env_path,
                                             const char *session_db_path,
                                             char **error_out);

#ifdef __cplusplus
}
#endif

#endif
