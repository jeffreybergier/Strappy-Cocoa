#ifndef STRAPPY_RESPONSES_H
#define STRAPPY_RESPONSES_H

#include "strappy_client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*strappy_chatgpt_credentials_callback)(
  int force_refresh,
  char **access_token_out,
  char **account_id_out,
  void *user_data,
  char **error_out);

/* Cocoa installs a process-lifetime Keychain bridge at startup. Portable
 * harnesses may install a deterministic callback; no credential is persisted
 * by the shared Responses core. */
void strappy_responses_set_chatgpt_credentials_callback(
  strappy_chatgpt_credentials_callback callback,
  void *user_data);

char *strappy_responses_send_prompt_for_session_and_store(
  const char *prompt,
  const char *env_path,
  const char *fallback_api_endpoint,
  const char *fallback_api_token,
  const char *guidance_resource_dir,
  const char *session_db_path,
  long long session_id,
  char **error_out);

char *strappy_responses_send_prompt_for_session_and_store_with_events(
  const char *prompt,
  const char *env_path,
  const char *fallback_api_endpoint,
  const char *fallback_api_token,
  const char *guidance_resource_dir,
  const char *session_db_path,
  long long session_id,
  strappy_responses_event_callback callback,
  void *callback_data,
  char **error_out);

char *strappy_responses_send_isolated_prompt_for_session_and_store_with_events(
  const char *prompt,
  const char *env_path,
  const char *fallback_api_endpoint,
  const char *fallback_api_token,
  const char *guidance_resource_dir,
  const char *session_db_path,
  long long session_id,
  strappy_responses_event_callback callback,
  void *callback_data,
  char **error_out);

#ifdef __cplusplus
}
#endif

#endif
