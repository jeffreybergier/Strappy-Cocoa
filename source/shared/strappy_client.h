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

typedef enum strappy_responses_event_type {
  STRAPPY_RESPONSES_EVENT_CANCELLATION_POLL = 1,
  STRAPPY_RESPONSES_EVENT_PROCESSING_STATUS = 2,
  /* Ledger events invalidate a DB-backed timeline range. One event may make
   * multiple committed changes visible when adjacent round transitions are
   * deliberately coalesced. */
  STRAPPY_RESPONSES_EVENT_LEDGER_CHANGED = 3,
  STRAPPY_RESPONSES_EVENT_LEDGER_UPDATED = 4,
  /* A durable session field changed outside the conversation ledger.
   * Consumers should reload the session summary from SQLite. */
  STRAPPY_RESPONSES_EVENT_SESSION_UPDATED = 5
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
  /* The committed ledger range may be held briefly so a fast continuation
   * can share one WebView mutation without hiding a long-running tool call. */
  int coalesce_with_next_ledger_change;
  /* A completed, tool-free response that ends the prompt loop. */
  int is_terminal;
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
