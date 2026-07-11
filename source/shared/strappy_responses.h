#ifndef STRAPPY_RESPONSES_H
#define STRAPPY_RESPONSES_H

#include "strappy_client.h"

#ifdef __cplusplus
extern "C" {
#endif

char *strappy_responses_send_prompt_for_session_and_store(
  const char *prompt,
  const char *env_path,
  const char *fallback_api_endpoint,
  const char *fallback_api_token,
  const char *system_prompt_template_path,
  const char *session_db_path,
  long long session_id,
  char **error_out);

char *strappy_responses_send_prompt_for_session_and_store_with_events(
  const char *prompt,
  const char *env_path,
  const char *fallback_api_endpoint,
  const char *fallback_api_token,
  const char *system_prompt_template_path,
  const char *session_db_path,
  long long session_id,
  strappy_responses_event_callback callback,
  void *callback_data,
  char **error_out);

#ifdef __cplusplus
}
#endif

#endif
