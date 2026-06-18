#ifndef STRAPPY_ASSISTANT_H
#define STRAPPY_ASSISTANT_H

#include "strappy_client.h"

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
char *strappy_assistant_send_prompt_and_store_with_id(const char *prompt,
                                                     const char *env_path,
                                                     const char *session_db_path,
                                                     long long *session_id_out,
                                                     char **error_out);
char *strappy_assistant_send_prompt_for_session_and_store(const char *prompt,
                                                         const char *env_path,
                                                         const char *session_db_path,
                                                         long long session_id,
                                                         char **error_out);
char *strappy_assistant_stream_prompt_and_store_with_id(
  const char *prompt,
  const char *env_path,
  const char *session_db_path,
  long long *session_id_out,
  strappy_chat_stream_callback callback,
  void *callback_data,
  char **error_out);
char *strappy_assistant_stream_prompt_for_session_and_store(
  const char *prompt,
  const char *env_path,
  const char *session_db_path,
  long long session_id,
  strappy_chat_stream_callback callback,
  void *callback_data,
  char **error_out);

#ifdef __cplusplus
}
#endif

#endif
