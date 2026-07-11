#ifndef STRAPPY_CLIENT_H
#define STRAPPY_CLIENT_H

#include "strappy_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct strappy_responses_http_result {
  char *response_json;
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
} strappy_responses_http_result;

typedef enum strappy_responses_event_type {
  STRAPPY_RESPONSES_EVENT_CANCELLATION_POLL = 1,
  STRAPPY_RESPONSES_EVENT_PROCESSING_STATUS = 2,
  STRAPPY_RESPONSES_EVENT_LEDGER_CHANGED = 3
} strappy_responses_event_type;

typedef struct strappy_responses_event {
  strappy_responses_event_type type;
  const char *prompt_group_key;
  const char *actor;
  const char *kind;
  const char *message_key;
  const char *render_role;
  const char *status_json;
  const char *status_kind;
  const char *status_reason;
  long long status_started_ms;
  long long status_updated_ms;
  long long retry_until_ms;
  unsigned int retry_after_seconds;
  unsigned int retry_attempt;
  unsigned int retry_max_attempts;
} strappy_responses_event;

typedef int (*strappy_responses_event_callback)(
  const strappy_responses_event *event,
  void *user_data);

void strappy_responses_http_result_init(strappy_responses_http_result *result);
void strappy_responses_http_result_destroy(strappy_responses_http_result *result);
int strappy_client_set_cainfo(const char *path, char **error_out);
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
char *strappy_client_build_responses_url(const char *endpoint);

#ifdef __cplusplus
}
#endif

#endif
