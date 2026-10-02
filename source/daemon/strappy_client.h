#ifndef STRAPPY_CLIENT_H
#define STRAPPY_CLIENT_H

#include "strappy_config.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct strappy_responses_http_result {
  char *response_json;
  char *raw_response_body;
  size_t raw_response_body_length;
  char *response_headers;
  char *effective_url;
  char *transport_error;
  char *content_type;
  char *request_id;
  char *generation_id;
  char *rate_limit_limit;
  char *rate_limit_remaining;
  char *rate_limit_reset;
  char *rate_limit_limit_requests;
  char *rate_limit_remaining_requests;
  char *rate_limit_reset_requests;
  char *rate_limit_limit_tokens;
  char *rate_limit_remaining_tokens;
  char *rate_limit_reset_tokens;
  long http_status;
  long retry_after_seconds;
  long curl_code;
  long long started_at_ms;
  long long completed_at_ms;
  long long request_bytes;
  long long response_bytes;
  double name_lookup_seconds;
  double connect_seconds;
  double start_transfer_seconds;
  double total_seconds;
  int cancelled;
  /* True once at least one syntactically valid SSE data event arrived. */
  int response_event_received;
} strappy_responses_http_result;

typedef enum strappy_responses_response_transport {
  STRAPPY_RESPONSES_RESPONSE_TRANSPORT_JSON = 0,
  STRAPPY_RESPONSES_RESPONSE_TRANSPORT_SSE = 1
} strappy_responses_response_transport;

#include "strappy_events.h"

void strappy_responses_http_result_init(strappy_responses_http_result *result);
void strappy_responses_http_result_destroy(strappy_responses_http_result *result);
int strappy_client_set_cainfo(const char *path, char **error_out);
int strappy_client_fetch_openrouter_user_models_json_with_cancel(
  const strappy_config *config, char **json_out, long *http_status_out,
  int (*should_cancel)(void *), void *context, char **error_out);
int strappy_client_fetch_openrouter_user_models_json(
  const strappy_config *config,
  char **json_out,
  long *http_status_out,
  char **error_out);
int strappy_client_send_responses_json(
  const strappy_config *config,
  const char *request_json,
  strappy_responses_http_result *result,
  strappy_responses_event_callback callback,
  void *callback_data,
  char **error_out);
int strappy_client_send_provider_responses_json(
  const strappy_config *config,
  strappy_provider_kind provider,
  const char *bearer_token,
  const char *chatgpt_account_id,
  const char *session_request_id,
  const char *request_json,
  strappy_responses_http_result *result,
  strappy_responses_event_callback callback,
  void *callback_data,
  char **error_out);
int strappy_client_send_provider_responses_json_with_transport(
  const strappy_config *config,
  strappy_provider_kind provider,
  strappy_responses_response_transport response_transport,
  const char *bearer_token,
  const char *chatgpt_account_id,
  const char *session_request_id,
  const char *request_json,
  strappy_responses_http_result *result,
  strappy_responses_event_callback callback,
  void *callback_data,
  char **error_out);
char *strappy_client_build_responses_url(const char *endpoint);

#ifdef __cplusplus
}
#endif

#endif
