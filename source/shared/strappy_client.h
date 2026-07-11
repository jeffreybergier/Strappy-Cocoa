#ifndef STRAPPY_CLIENT_H
#define STRAPPY_CLIENT_H

#include "strappy_config.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct strappy_chat_message {
  const char *role;
  const char *content;
  const char *message_json;
} strappy_chat_message;

typedef struct strappy_chat_result {
  char *response_text;
  char *message_json;
  char *raw_json;
  char *response_id;
  char *finish_reason;
  char *model;
  char *created;
  char *native_finish_reason;
  char *service_tier;
  char *system_fingerprint;
  char *provider_name;
  char *error_code;
  char *error_type;
  char *provider_code;
  char *usage_json;
  char *openrouter_metadata_json;
  char *error_metadata_json;
  char *generation_metadata_json;
  char *metadata_json;
  char *metadata_text;
  char *reasoning_text;
  long http_status;
  long retry_after_seconds;
  int cancelled;
} strappy_chat_result;

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

typedef enum strappy_client_finish_status {
  STRAPPY_CLIENT_FINISH_STATUS_OK = 0,
  STRAPPY_CLIENT_FINISH_STATUS_WARNING = 1,
  STRAPPY_CLIENT_FINISH_STATUS_ERROR = 2
} strappy_client_finish_status;

typedef enum strappy_chat_stream_event_type {
  STRAPPY_CHAT_STREAM_EVENT_CONTENT_DELTA = 1,
  STRAPPY_CHAT_STREAM_EVENT_REASONING_DELTA = 2,
  STRAPPY_CHAT_STREAM_EVENT_TOOL_CALL = 3,
  STRAPPY_CHAT_STREAM_EVENT_TOOL_RESULT = 4,
  STRAPPY_CHAT_STREAM_EVENT_TOOL_ERROR = 5,
  STRAPPY_CHAT_STREAM_EVENT_TURN_STARTED = 6,
  STRAPPY_CHAT_STREAM_EVENT_TURN_FINISHED = 7,
  STRAPPY_CHAT_STREAM_EVENT_CONTENT_RETRACTED = 8,
  STRAPPY_CHAT_STREAM_EVENT_PROCESSING_STATUS = 9,
  STRAPPY_CHAT_STREAM_EVENT_LEDGER_CHANGED = 10
} strappy_chat_stream_event_type;

typedef struct strappy_chat_stream_event {
  strappy_chat_stream_event_type type;
  const char *text;
  const char *turn_key;
  const char *prompt_group_key;
  const char *actor;
  const char *kind;
  const char *message_key;
  const char *target_message_key;
  const char *tool_call_id;
  const char *tool_name;
  const char *arguments_json;
  const char *result_json;
  const char *render_role;
  const char *api_role;
  const char *message_json;
  const char *status_json;
  const char *status_kind;
  const char *status_reason;
  long long status_started_ms;
  long long status_updated_ms;
  long long retry_until_ms;
  unsigned int retry_after_seconds;
  unsigned int retry_attempt;
  unsigned int retry_max_attempts;
} strappy_chat_stream_event;

typedef int (*strappy_chat_stream_callback)(
  const strappy_chat_stream_event *event,
  void *user_data);

void strappy_chat_result_init(strappy_chat_result *result);
void strappy_chat_result_destroy(strappy_chat_result *result);
void strappy_responses_http_result_init(strappy_responses_http_result *result);
void strappy_responses_http_result_destroy(strappy_responses_http_result *result);
strappy_client_finish_status strappy_client_classify_finish_reason(
  const char *finish_reason,
  const char *native_finish_reason);
const char *strappy_client_finish_status_name(
  strappy_client_finish_status status);
int strappy_client_finish_status_is_error(const char *finish_reason,
                                          const char *native_finish_reason);
int strappy_client_set_cainfo(const char *path, char **error_out);
int strappy_client_send_messages(const strappy_config *config,
                                 const strappy_chat_message *messages,
                                 size_t message_count,
                                 strappy_chat_result *result,
                                 char **error_out);
int strappy_client_send_messages_with_events(
  const strappy_config *config,
  const strappy_chat_message *messages,
  size_t message_count,
  strappy_chat_result *result,
  strappy_chat_stream_callback callback,
  void *callback_data,
  char **error_out);
int strappy_client_fetch_openrouter_user_models_json(
  const strappy_config *config,
  char **json_out,
  long *http_status_out,
  char **error_out);
int strappy_client_stream_messages(const strappy_config *config,
                                   const strappy_chat_message *messages,
                                   size_t message_count,
                                   strappy_chat_result *result,
                                   strappy_chat_stream_callback callback,
                                   void *callback_data,
                                   char **error_out);
int strappy_client_send_responses_json_once(
  const strappy_config *config,
  const char *request_json,
  strappy_responses_http_result *result,
  char **error_out);
int strappy_client_send_responses_json_once_with_events(
  const strappy_config *config,
  const char *request_json,
  strappy_responses_http_result *result,
  strappy_chat_stream_callback callback,
  void *callback_data,
  char **error_out);
char *strappy_client_build_responses_url(const char *endpoint);

#ifdef __cplusplus
}
#endif

#endif
