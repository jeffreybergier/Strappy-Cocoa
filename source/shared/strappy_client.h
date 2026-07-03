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
  char *usage_json;
  char *generation_metadata_json;
  char *metadata_json;
  char *metadata_text;
  char *reasoning_text;
  long http_status;
  int cancelled;
} strappy_chat_result;

typedef enum strappy_chat_stream_event_type {
  STRAPPY_CHAT_STREAM_EVENT_CONTENT_DELTA = 1,
  STRAPPY_CHAT_STREAM_EVENT_REASONING_DELTA = 2,
  STRAPPY_CHAT_STREAM_EVENT_TOOL_CALL = 3,
  STRAPPY_CHAT_STREAM_EVENT_TOOL_RESULT = 4,
  STRAPPY_CHAT_STREAM_EVENT_TOOL_ERROR = 5,
  STRAPPY_CHAT_STREAM_EVENT_TURN_STARTED = 6,
  STRAPPY_CHAT_STREAM_EVENT_TURN_FINISHED = 7,
  STRAPPY_CHAT_STREAM_EVENT_CONTENT_RETRACTED = 8
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
} strappy_chat_stream_event;

typedef int (*strappy_chat_stream_callback)(
  const strappy_chat_stream_event *event,
  void *user_data);

void strappy_chat_result_init(strappy_chat_result *result);
void strappy_chat_result_destroy(strappy_chat_result *result);
int strappy_client_set_cainfo(const char *path, char **error_out);
int strappy_client_send_messages(const strappy_config *config,
                                 const strappy_chat_message *messages,
                                 size_t message_count,
                                 strappy_chat_result *result,
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

#ifdef __cplusplus
}
#endif

#endif
