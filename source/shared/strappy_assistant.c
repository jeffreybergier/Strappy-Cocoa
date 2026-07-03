#include "strappy_assistant.h"

#include "strappy_config.h"
#include "strappy_core.h"
#include "strappy_client.h"
#include "strappy_db.h"
#include "strappy_prompt.h"
#include "strappy_tools.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define STRAPPY_ASSISTANT_MAX_TOOL_ROUNDS 50U
#define STRAPPY_ASSISTANT_GUIDANCE_PROMPTS "assistant_prompts"
#define STRAPPY_ASSISTANT_GUIDANCE_MESSAGES "assistant_messages"
#define STRAPPY_ASSISTANT_GUIDANCE_TOOL_ERRORS "tool_errors"
#define STRAPPY_ASSISTANT_GUIDANCE_LEARNING_SUMMARY "learning_summary"
#define STRAPPY_ASSISTANT_GUIDANCE_LEARNING_SUMMARY_COMPLETION \
  "learning_summary_completion"
#define STRAPPY_ASSISTANT_GUIDANCE_TOOL_ROUND_LIMIT "tool_round_limit"
#define STRAPPY_ASSISTANT_GUIDANCE_TOOL_ERROR_DEFAULT "default"
#define STRAPPY_ASSISTANT_GUIDANCE_TOOL_ERROR_NOT_ALLOWED "not_allowed_for_turn"
#define STRAPPY_ASSISTANT_STREAM_FLUSH_BYTES 512U
#define STRAPPY_ASSISTANT_STREAM_FLUSH_MS 80LL
#define STRAPPY_ASSISTANT_STREAM_BUFFER_BYTES 2048U

typedef struct strappy_assistant_guidance {
  char *learning_summary_prompt;
  char *learning_summary_completion_text;
  char *tool_round_limit_message;
  char *tool_error_default;
  char *tool_error_not_allowed_for_turn;
} strappy_assistant_guidance;

typedef struct strappy_assistant_request_messages {
  strappy_chat_message *messages;
  size_t count;
  char *system_prompt;
} strappy_assistant_request_messages;

typedef struct strappy_assistant_tool_round {
  strappy_chat_message *followup_messages;
  size_t followup_count;
  strappy_session_message_input *storage_messages;
  size_t storage_count;
  size_t tool_count;
  int store_prompt_message;
  int post_final;
  char *tool_call_content;
  char *storage_prompt_message_json;
  char *tool_call_message_key;
  char **tool_result_message_keys;
  char **tool_call_ids;
  char **tool_names;
  char **tool_arguments;
  char **tool_outputs;
  char **tool_message_jsons;
} strappy_assistant_tool_round;

typedef enum strappy_assistant_tool_policy {
  STRAPPY_ASSISTANT_TOOL_POLICY_NORMAL = 0,
  STRAPPY_ASSISTANT_TOOL_POLICY_LEARNING_SUMMARY = 1
} strappy_assistant_tool_policy;

typedef struct strappy_assistant_turn_spec {
  const char *turn_key;
  const char *prompt_group_key;
  const char *actor;
  const char *render_role;
  const char *api_role;
  const char *context_policy;
  const char *prompt;
  const char *prompt_message_key;
  const char *assistant_message_key;
  long long session_id;
  strappy_assistant_tool_policy tool_policy;
} strappy_assistant_turn_spec;

typedef struct strappy_assistant_turn_keys {
  char *turn_key;
  char *prompt_group_key;
  char *prompt_message_key;
  char *assistant_message_key;
} strappy_assistant_turn_keys;

typedef struct strappy_assistant_tool_sequence {
  strappy_assistant_tool_round rounds[STRAPPY_ASSISTANT_MAX_TOOL_ROUNDS];
  strappy_chat_result results[STRAPPY_ASSISTANT_MAX_TOOL_ROUNDS + 1U];
  strappy_chat_result learning_summary_result;
  size_t round_count;
  strappy_chat_result *final_result;
  strappy_chat_result *learning_summary_final_result;
} strappy_assistant_tool_sequence;

typedef struct strappy_assistant_stream_turn_context {
  strappy_chat_stream_callback callback;
  void *callback_data;
  const strappy_assistant_turn_spec *turn;
} strappy_assistant_stream_turn_context;

typedef struct strappy_assistant_stream_store_context {
  const char *session_db_path;
  long long session_id;
  strappy_chat_stream_callback callback;
  void *callback_data;
  char **error_out;
  strappy_chat_stream_event pending_delta_event;
  char pending_content[STRAPPY_ASSISTANT_STREAM_BUFFER_BYTES + 1U];
  char pending_reasoning[STRAPPY_ASSISTANT_STREAM_BUFFER_BYTES + 1U];
  size_t pending_content_length;
  size_t pending_reasoning_length;
  long long pending_started_ms;
} strappy_assistant_stream_store_context;

static void strappy_assistant_request_messages_init(
  strappy_assistant_request_messages *request)
{
  if (request == NULL) {
    return;
  }

  request->messages = NULL;
  request->count = 0U;
  request->system_prompt = NULL;
}

static void strappy_assistant_request_messages_destroy(
  strappy_assistant_request_messages *request)
{
  if (request == NULL) {
    return;
  }

  free(request->system_prompt);
  free(request->messages);
  strappy_assistant_request_messages_init(request);
}

static void strappy_assistant_guidance_init(
  strappy_assistant_guidance *guidance)
{
  if (guidance == NULL) {
    return;
  }

  guidance->learning_summary_prompt = NULL;
  guidance->learning_summary_completion_text = NULL;
  guidance->tool_round_limit_message = NULL;
  guidance->tool_error_default = NULL;
  guidance->tool_error_not_allowed_for_turn = NULL;
}

static void strappy_assistant_guidance_destroy(
  strappy_assistant_guidance *guidance)
{
  if (guidance == NULL) {
    return;
  }

  free(guidance->learning_summary_prompt);
  free(guidance->learning_summary_completion_text);
  free(guidance->tool_round_limit_message);
  free(guidance->tool_error_default);
  free(guidance->tool_error_not_allowed_for_turn);
  strappy_assistant_guidance_init(guidance);
}

static int strappy_assistant_guidance_load_string(
  const char *resource_dir,
  const char *section_name,
  const char *key,
  char **target,
  char **error_out)
{
  if (target == NULL) {
    strappy_set_error(error_out, "Assistant guidance output is missing.");
    return 0;
  }

  *target = strappy_tools_tool_guidance_string(resource_dir,
                                               section_name,
                                               key,
                                               error_out);
  return (*target != NULL) ? 1 : 0;
}

static int strappy_assistant_guidance_load(
  strappy_assistant_guidance *guidance,
  const char *resource_dir,
  char **error_out)
{
  if (guidance == NULL) {
    strappy_set_error(error_out, "Assistant guidance output is missing.");
    return 0;
  }

  strappy_assistant_guidance_destroy(guidance);
  if (!strappy_assistant_guidance_load_string(
        resource_dir,
        STRAPPY_ASSISTANT_GUIDANCE_PROMPTS,
        STRAPPY_ASSISTANT_GUIDANCE_LEARNING_SUMMARY,
        &guidance->learning_summary_prompt,
        error_out) ||
      !strappy_assistant_guidance_load_string(
        resource_dir,
        STRAPPY_ASSISTANT_GUIDANCE_PROMPTS,
        STRAPPY_ASSISTANT_GUIDANCE_LEARNING_SUMMARY_COMPLETION,
        &guidance->learning_summary_completion_text,
        error_out) ||
      !strappy_assistant_guidance_load_string(
        resource_dir,
        STRAPPY_ASSISTANT_GUIDANCE_MESSAGES,
        STRAPPY_ASSISTANT_GUIDANCE_TOOL_ROUND_LIMIT,
        &guidance->tool_round_limit_message,
        error_out) ||
      !strappy_assistant_guidance_load_string(
        resource_dir,
        STRAPPY_ASSISTANT_GUIDANCE_TOOL_ERRORS,
        STRAPPY_ASSISTANT_GUIDANCE_TOOL_ERROR_DEFAULT,
        &guidance->tool_error_default,
        error_out) ||
      !strappy_assistant_guidance_load_string(
        resource_dir,
        STRAPPY_ASSISTANT_GUIDANCE_TOOL_ERRORS,
        STRAPPY_ASSISTANT_GUIDANCE_TOOL_ERROR_NOT_ALLOWED,
        &guidance->tool_error_not_allowed_for_turn,
        error_out)) {
    strappy_assistant_guidance_destroy(guidance);
    return 0;
  }

  return 1;
}

static void strappy_assistant_tool_round_init(
  strappy_assistant_tool_round *round)
{
  if (round == NULL) {
    return;
  }

  round->followup_messages = NULL;
  round->followup_count = 0U;
  round->storage_messages = NULL;
  round->storage_count = 0U;
  round->tool_count = 0U;
  round->store_prompt_message = 0;
  round->post_final = 0;
  round->tool_call_content = NULL;
  round->storage_prompt_message_json = NULL;
  round->tool_call_message_key = NULL;
  round->tool_result_message_keys = NULL;
  round->tool_call_ids = NULL;
  round->tool_names = NULL;
  round->tool_arguments = NULL;
  round->tool_outputs = NULL;
  round->tool_message_jsons = NULL;
}

static void strappy_assistant_tool_round_destroy(
  strappy_assistant_tool_round *round)
{
  size_t index;

  if (round == NULL) {
    return;
  }

  free(round->followup_messages);
  free(round->storage_messages);
  free(round->tool_call_content);
  free(round->storage_prompt_message_json);
  free(round->tool_call_message_key);
  if (round->tool_result_message_keys != NULL) {
    for (index = 0U; index < round->tool_count; index++) {
      free(round->tool_result_message_keys[index]);
    }
  }
  if (round->tool_call_ids != NULL) {
    for (index = 0U; index < round->tool_count; index++) {
      free(round->tool_call_ids[index]);
    }
  }
  if (round->tool_names != NULL) {
    for (index = 0U; index < round->tool_count; index++) {
      free(round->tool_names[index]);
    }
  }
  if (round->tool_arguments != NULL) {
    for (index = 0U; index < round->tool_count; index++) {
      free(round->tool_arguments[index]);
    }
  }
  if (round->tool_outputs != NULL) {
    for (index = 0U; index < round->tool_count; index++) {
      free(round->tool_outputs[index]);
    }
  }
  if (round->tool_message_jsons != NULL) {
    for (index = 0U; index < round->tool_count; index++) {
      free(round->tool_message_jsons[index]);
    }
  }
  free(round->tool_result_message_keys);
  free(round->tool_call_ids);
  free(round->tool_names);
  free(round->tool_arguments);
  free(round->tool_outputs);
  free(round->tool_message_jsons);
  strappy_assistant_tool_round_init(round);
}

static void strappy_assistant_tool_sequence_init(
  strappy_assistant_tool_sequence *sequence)
{
  size_t index;

  if (sequence == NULL) {
    return;
  }

  for (index = 0U; index < STRAPPY_ASSISTANT_MAX_TOOL_ROUNDS; index++) {
    strappy_assistant_tool_round_init(&sequence->rounds[index]);
  }

  for (index = 0U; index <= STRAPPY_ASSISTANT_MAX_TOOL_ROUNDS; index++) {
    strappy_chat_result_init(&sequence->results[index]);
  }
  strappy_chat_result_init(&sequence->learning_summary_result);

  sequence->round_count = 0U;
  sequence->final_result = NULL;
  sequence->learning_summary_final_result = NULL;
}

static void strappy_assistant_tool_sequence_destroy(
  strappy_assistant_tool_sequence *sequence)
{
  size_t index;

  if (sequence == NULL) {
    return;
  }

  for (index = 0U; index < STRAPPY_ASSISTANT_MAX_TOOL_ROUNDS; index++) {
    strappy_assistant_tool_round_destroy(&sequence->rounds[index]);
  }

  for (index = 0U; index <= STRAPPY_ASSISTANT_MAX_TOOL_ROUNDS; index++) {
    strappy_chat_result_destroy(&sequence->results[index]);
  }
  strappy_chat_result_destroy(&sequence->learning_summary_result);

  sequence->round_count = 0U;
  sequence->final_result = NULL;
  sequence->learning_summary_final_result = NULL;
}

static int strappy_assistant_emit_turn_event(
  strappy_chat_stream_event_type type,
  const strappy_assistant_turn_spec *turn,
  const char *text,
  strappy_chat_stream_callback callback,
  void *callback_data,
  char **error_out)
{
  strappy_chat_stream_event event;

  if (callback == NULL) {
    return 1;
  }

  memset(&event, 0, sizeof(event));
  event.type = type;
  event.text = (text != NULL) ? text : "";
  if (turn != NULL) {
    event.turn_key = turn->turn_key;
    event.prompt_group_key = turn->prompt_group_key;
    event.actor = turn->actor;
    if (type == STRAPPY_CHAT_STREAM_EVENT_TURN_FINISHED) {
      event.render_role = "assistant";
      event.api_role = "assistant";
      event.kind = "assistant";
      event.message_key = turn->assistant_message_key;
    } else {
      event.render_role = turn->render_role;
      event.api_role = turn->api_role;
      event.kind = "prompt";
      event.message_key = turn->prompt_message_key;
      event.target_message_key = turn->assistant_message_key;
    }
  }
  if (!callback(&event, callback_data)) {
    strappy_set_error(error_out, "Stream callback rejected turn event.");
    return 0;
  }

  return 1;
}

static int strappy_assistant_emit_tool_event(
  strappy_chat_stream_event_type type,
  const strappy_assistant_turn_spec *turn,
  const char *message_key,
  const char *tool_call_id,
  const char *tool_name,
  const char *arguments_json,
  const char *result_json,
  const char *message_json,
  strappy_chat_stream_callback callback,
  void *callback_data,
  char **error_out)
{
  strappy_chat_stream_event event;

  if (callback == NULL) {
    return 1;
  }

  memset(&event, 0, sizeof(event));
  event.type = type;
  event.text = tool_name;
  if (turn != NULL) {
    event.turn_key = turn->turn_key;
    event.prompt_group_key = turn->prompt_group_key;
    event.actor = turn->actor;
    event.render_role =
      (type == STRAPPY_CHAT_STREAM_EVENT_TOOL_CALL) ? "tool_call" : "tool";
    event.api_role =
      (type == STRAPPY_CHAT_STREAM_EVENT_TOOL_CALL) ? "assistant" : "tool";
    if (type == STRAPPY_CHAT_STREAM_EVENT_TOOL_CALL) {
      event.kind = "tool_call";
    } else if (type == STRAPPY_CHAT_STREAM_EVENT_TOOL_ERROR) {
      event.kind = "tool_error";
    } else {
      event.kind = "tool_result";
    }
    event.message_key = message_key;
    event.target_message_key = turn->assistant_message_key;
  }
  event.tool_call_id = tool_call_id;
  event.tool_name = tool_name;
  event.arguments_json = arguments_json;
  event.result_json = result_json;
  event.message_json = message_json;
  if (!callback(&event, callback_data)) {
    strappy_set_error(error_out, "Stream callback rejected tool event.");
    return 0;
  }

  return 1;
}

static int strappy_assistant_emit_content_retracted_event(
  const strappy_assistant_turn_spec *turn,
  strappy_chat_stream_callback callback,
  void *callback_data,
  char **error_out)
{
  strappy_chat_stream_event event;

  if ((turn == NULL) || (callback == NULL)) {
    return 1;
  }

  memset(&event, 0, sizeof(event));
  event.type = STRAPPY_CHAT_STREAM_EVENT_CONTENT_RETRACTED;
  event.text = "";
  event.turn_key = turn->turn_key;
  event.prompt_group_key = turn->prompt_group_key;
  event.actor = turn->actor;
  event.kind = "assistant";
  event.message_key = turn->assistant_message_key;
  event.render_role = "assistant";
  event.api_role = "assistant";

  if (!callback(&event, callback_data)) {
    strappy_set_error(error_out,
                      "Stream callback rejected content correction event.");
    return 0;
  }

  return 1;
}

static int strappy_assistant_emit_assistant_placeholder_event(
  const strappy_assistant_turn_spec *turn,
  strappy_chat_stream_callback callback,
  void *callback_data,
  char **error_out)
{
  strappy_chat_stream_event event;

  if ((turn == NULL) || (callback == NULL)) {
    return 1;
  }

  memset(&event, 0, sizeof(event));
  event.type = STRAPPY_CHAT_STREAM_EVENT_TURN_STARTED;
  event.text = "";
  event.turn_key = turn->turn_key;
  event.prompt_group_key = turn->prompt_group_key;
  event.actor = turn->actor;
  event.kind = "assistant";
  event.message_key = turn->assistant_message_key;
  event.render_role = "assistant";
  event.api_role = "assistant";

  if (!callback(&event, callback_data)) {
    strappy_set_error(error_out,
                      "Stream callback rejected assistant placeholder event.");
    return 0;
  }

  return 1;
}

static int strappy_assistant_stream_turn_callback(
  const strappy_chat_stream_event *event,
  void *callback_data)
{
  strappy_assistant_stream_turn_context *context;
  strappy_chat_stream_event turn_event;

  if (callback_data == NULL) {
    return 1;
  }

  context = (strappy_assistant_stream_turn_context *)callback_data;
  if (context->callback == NULL) {
    return 1;
  }

  if (event == NULL) {
    return context->callback(event, context->callback_data);
  }

  turn_event = *event;
  if (context->turn != NULL) {
    if (turn_event.turn_key == NULL) {
      turn_event.turn_key = context->turn->turn_key;
    }
    if (turn_event.prompt_group_key == NULL) {
      turn_event.prompt_group_key = context->turn->prompt_group_key;
    }
    if (turn_event.actor == NULL) {
      turn_event.actor = context->turn->actor;
    }
    if (turn_event.render_role == NULL) {
      turn_event.render_role = "assistant";
    }
    if (turn_event.api_role == NULL) {
      turn_event.api_role = "assistant";
    }
    if (turn_event.kind == NULL) {
      turn_event.kind = "assistant";
    }
    if (turn_event.message_key == NULL) {
      turn_event.message_key = context->turn->assistant_message_key;
    }
  }
  return context->callback(&turn_event, context->callback_data);
}

static int strappy_assistant_stream_event_included_in_context(
  const strappy_chat_stream_event *event)
{
  if ((event != NULL) &&
      (event->actor != NULL) &&
      (strcmp(event->actor, "harness") == 0)) {
    return 0;
  }
  return 1;
}

static const char *strappy_assistant_stream_event_kind(
  const strappy_chat_stream_event *event)
{
  if ((event != NULL) &&
      (event->kind != NULL) &&
      (event->kind[0] != '\0')) {
    return event->kind;
  }

  if (event == NULL) {
    return "message";
  }
  if (event->type == STRAPPY_CHAT_STREAM_EVENT_TURN_STARTED) {
    return "prompt";
  }
  if ((event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_CALL) ||
      (event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_RESULT) ||
      (event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_ERROR)) {
    return (event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_CALL) ?
      "tool_call" :
      ((event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_ERROR) ?
        "tool_error" : "tool_result");
  }
  return "assistant";
}

static const char *strappy_assistant_stream_event_api_role(
  const strappy_chat_stream_event *event)
{
  if ((event != NULL) &&
      (event->api_role != NULL) &&
      (event->api_role[0] != '\0')) {
    return event->api_role;
  }
  if ((event != NULL) &&
      (event->type == STRAPPY_CHAT_STREAM_EVENT_TURN_STARTED)) {
    return "user";
  }
  if ((event != NULL) &&
      (event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_RESULT ||
       event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_ERROR)) {
    return "tool";
  }
  return "assistant";
}

static const char *strappy_assistant_stream_event_render_role(
  const strappy_chat_stream_event *event)
{
  if ((event != NULL) &&
      (event->render_role != NULL) &&
      (event->render_role[0] != '\0')) {
    return event->render_role;
  }
  if ((event != NULL) &&
      (event->type == STRAPPY_CHAT_STREAM_EVENT_TURN_STARTED)) {
    return (event->actor != NULL) ? event->actor : "user";
  }
  if ((event != NULL) &&
      (event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_CALL)) {
    return "tool_call";
  }
  if ((event != NULL) &&
      (event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_RESULT ||
       event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_ERROR)) {
    return "tool";
  }
  return "assistant";
}

static const char *strappy_assistant_stream_event_render_state_json(
  const strappy_chat_stream_event *event)
{
  static const char *reasoning_streaming_state =
    "{\"streaming\":true,\"reasoning_render_when_empty\":true,"
    "\"reasoning_collapsed\":false,\"tool_column_collapsed\":true}";
  static const char *content_streaming_state =
    "{\"streaming\":true,\"reasoning_render_when_empty\":true,"
    "\"reasoning_collapsed\":false,\"tool_column_collapsed\":true,"
    "\"content_started\":true}";
  static const char *tool_streaming_state =
    "{\"streaming\":true}";

  if (event == NULL) {
    return NULL;
  }

  if (event->type == STRAPPY_CHAT_STREAM_EVENT_REASONING_DELTA) {
    return reasoning_streaming_state;
  }
  if (event->type == STRAPPY_CHAT_STREAM_EVENT_CONTENT_RETRACTED) {
    return reasoning_streaming_state;
  }
  if (event->type == STRAPPY_CHAT_STREAM_EVENT_CONTENT_DELTA) {
    return content_streaming_state;
  }
  if ((event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_CALL) ||
      (event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_RESULT) ||
      (event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_ERROR)) {
    return tool_streaming_state;
  }
  if ((event->type == STRAPPY_CHAT_STREAM_EVENT_TURN_STARTED) &&
      (event->kind != NULL) &&
      (strcmp(event->kind, "assistant") == 0)) {
    return reasoning_streaming_state;
  }

  return NULL;
}

static void strappy_assistant_stream_message_input_from_event(
  const strappy_chat_stream_event *event,
  const char *message_key,
  const char *content,
  strappy_session_message_input *message)
{
  int include_in_context;

  if (message == NULL) {
    return;
  }

  memset(message, 0, sizeof(*message));
  include_in_context = strappy_assistant_stream_event_included_in_context(event);

  if (event != NULL) {
    message->turn_key = event->turn_key;
    message->prompt_group_key = event->prompt_group_key;
    message->actor = event->actor;
    message->message_key = message_key;
    message->target_message_key = event->target_message_key;
    message->tool_call_id = event->tool_call_id;
    message->tool_name = event->tool_name;
    message->arguments_json = event->arguments_json;
    message->result_json = event->result_json;
    message->message_json = event->message_json;
    message->is_error =
      (event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_ERROR) ? 1 : 0;
  }
  message->context_policy = include_in_context ? "full" : "omit";
  message->kind = strappy_assistant_stream_event_kind(event);
  message->api_role = strappy_assistant_stream_event_api_role(event);
  message->render_role = strappy_assistant_stream_event_render_role(event);
  message->role = message->render_role;
  message->content = (content != NULL) ? content : "";
  message->render_state_json =
    strappy_assistant_stream_event_render_state_json(event);
  message->include_in_context = include_in_context;
}

static int strappy_assistant_store_stream_event(
  const strappy_chat_stream_event *event,
  const strappy_assistant_stream_store_context *context)
{
  strappy_session_message_input message;
  const char *content;
  const char *message_key;

  if ((event == NULL) || (context == NULL) ||
      (context->session_db_path == NULL) ||
      (context->session_db_path[0] == '\0') ||
      (context->session_id <= 0LL)) {
    return 1;
  }

  message_key = event->message_key;
  content = (event->text != NULL) ? event->text : "";

  if (event->type == STRAPPY_CHAT_STREAM_EVENT_TURN_STARTED) {
    strappy_assistant_stream_message_input_from_event(event,
                                                      message_key,
                                                      content,
                                                      &message);
    if (!strappy_db_upsert_session_message(context->session_db_path,
                                           context->session_id,
                                           &message,
                                           context->error_out)) {
      return 0;
    }
    return 1;
  }

  if (event->type == STRAPPY_CHAT_STREAM_EVENT_TURN_FINISHED) {
    strappy_assistant_stream_message_input_from_event(event,
                                                      message_key,
                                                      content,
                                                      &message);
    return strappy_db_upsert_session_message(context->session_db_path,
                                             context->session_id,
                                             &message,
                                             context->error_out);
  }

  if ((event->type == STRAPPY_CHAT_STREAM_EVENT_CONTENT_DELTA) ||
      (event->type == STRAPPY_CHAT_STREAM_EVENT_REASONING_DELTA)) {
    strappy_assistant_stream_message_input_from_event(event,
                                                      message_key,
                                                      "",
                                                      &message);
    return strappy_db_append_session_message_content(
      context->session_db_path,
      context->session_id,
      &message,
      (event->type == STRAPPY_CHAT_STREAM_EVENT_CONTENT_DELTA) ? content : "",
      (event->type == STRAPPY_CHAT_STREAM_EVENT_REASONING_DELTA) ? content : "",
      context->error_out);
  }

  if (event->type == STRAPPY_CHAT_STREAM_EVENT_CONTENT_RETRACTED) {
    strappy_assistant_stream_message_input_from_event(event,
                                                      message_key,
                                                      "",
                                                      &message);
    return strappy_db_move_session_message_content_to_reasoning(
      context->session_db_path,
      context->session_id,
      &message,
      context->error_out);
  }

  if ((event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_CALL) ||
      (event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_RESULT) ||
      (event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_ERROR)) {
    if ((message_key == NULL) || (message_key[0] == '\0')) {
      return 1;
    }
    if (event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_CALL) {
      content = (event->arguments_json != NULL) ? event->arguments_json : content;
    } else {
      content = (event->result_json != NULL) ? event->result_json : content;
    }
    strappy_assistant_stream_message_input_from_event(event,
                                                      message_key,
                                                      content,
                                                      &message);
    return strappy_db_upsert_session_message(context->session_db_path,
                                             context->session_id,
                                             &message,
                                             context->error_out);
  }

  return 1;
}

static long long strappy_assistant_stream_now_ms(void)
{
  struct timeval now;

  if (gettimeofday(&now, NULL) != 0) {
    return 0LL;
  }

  return ((long long)now.tv_sec * 1000LL) + ((long long)now.tv_usec / 1000LL);
}

static int strappy_assistant_stream_event_is_delta(
  const strappy_chat_stream_event *event)
{
  return ((event != NULL) &&
          ((event->type == STRAPPY_CHAT_STREAM_EVENT_CONTENT_DELTA) ||
           (event->type == STRAPPY_CHAT_STREAM_EVENT_REASONING_DELTA))) ? 1 : 0;
}

static int strappy_assistant_strings_equal(const char *left,
                                           const char *right)
{
  if (left == right) {
    return 1;
  }
  if ((left == NULL) || (right == NULL)) {
    return 0;
  }
  return (strcmp(left, right) == 0) ? 1 : 0;
}

static size_t strappy_assistant_stream_pending_length(
  const strappy_assistant_stream_store_context *context)
{
  if (context == NULL) {
    return 0U;
  }
  return context->pending_content_length + context->pending_reasoning_length;
}

static void strappy_assistant_stream_clear_pending(
  strappy_assistant_stream_store_context *context)
{
  if (context == NULL) {
    return;
  }

  memset(&context->pending_delta_event, 0, sizeof(context->pending_delta_event));
  context->pending_content_length = 0U;
  context->pending_reasoning_length = 0U;
  context->pending_content[0] = '\0';
  context->pending_reasoning[0] = '\0';
  context->pending_started_ms = 0LL;
}

static int strappy_assistant_stream_pending_matches_event(
  const strappy_assistant_stream_store_context *context,
  const strappy_chat_stream_event *event)
{
  const strappy_chat_stream_event *pending;

  if ((context == NULL) || (event == NULL) ||
      (strappy_assistant_stream_pending_length(context) == 0U)) {
    return 1;
  }

  pending = &context->pending_delta_event;
  return
    strappy_assistant_strings_equal(pending->message_key, event->message_key) &&
    strappy_assistant_strings_equal(pending->turn_key, event->turn_key) &&
    strappy_assistant_strings_equal(pending->prompt_group_key,
                                    event->prompt_group_key) &&
    strappy_assistant_strings_equal(pending->actor, event->actor) &&
    strappy_assistant_strings_equal(pending->kind, event->kind) &&
    strappy_assistant_strings_equal(pending->target_message_key,
                                    event->target_message_key) &&
    strappy_assistant_strings_equal(pending->render_role,
                                    event->render_role) &&
    strappy_assistant_strings_equal(pending->api_role, event->api_role);
}

static int strappy_assistant_stream_append_pending(
  strappy_assistant_stream_store_context *context,
  const strappy_chat_stream_event *event)
{
  const char *text;
  char *target;
  size_t *length;
  size_t text_length;

  if ((context == NULL) || !strappy_assistant_stream_event_is_delta(event)) {
    return 0;
  }

  text = (event->text != NULL) ? event->text : "";
  text_length = strlen(text);
  if (text_length == 0U) {
    return 1;
  }

  if (event->type == STRAPPY_CHAT_STREAM_EVENT_CONTENT_DELTA) {
    target = context->pending_content;
    length = &context->pending_content_length;
  } else {
    target = context->pending_reasoning;
    length = &context->pending_reasoning_length;
  }

  if (text_length >
      (STRAPPY_ASSISTANT_STREAM_BUFFER_BYTES - *length)) {
    return 0;
  }

  if (strappy_assistant_stream_pending_length(context) == 0U) {
    context->pending_delta_event = *event;
    context->pending_started_ms = strappy_assistant_stream_now_ms();
  }

  memcpy(target + *length, text, text_length);
  *length += text_length;
  target[*length] = '\0';
  return 1;
}

static int strappy_assistant_stream_store_and_forward_event(
  const strappy_chat_stream_event *event,
  strappy_assistant_stream_store_context *context)
{
  if (!strappy_assistant_store_stream_event(event, context)) {
    return 0;
  }

  if ((context == NULL) || (context->callback == NULL)) {
    return 1;
  }

  return context->callback(event, context->callback_data);
}

static int strappy_assistant_stream_flush_pending(
  strappy_assistant_stream_store_context *context,
  int forward_events)
{
  strappy_chat_stream_event event;
  strappy_session_message_input message;

  if ((context == NULL) ||
      (strappy_assistant_stream_pending_length(context) == 0U)) {
    return 1;
  }

  event = context->pending_delta_event;
  event.type = (context->pending_content_length > 0U) ?
    STRAPPY_CHAT_STREAM_EVENT_CONTENT_DELTA :
    STRAPPY_CHAT_STREAM_EVENT_REASONING_DELTA;
  event.text = "";

  strappy_assistant_stream_message_input_from_event(&event,
                                                    event.message_key,
                                                    "",
                                                    &message);
  if (!strappy_db_append_session_message_content(
        context->session_db_path,
        context->session_id,
        &message,
        context->pending_content,
        context->pending_reasoning,
        context->error_out)) {
    return 0;
  }

  if (forward_events && (context->callback != NULL)) {
    if (context->pending_reasoning_length > 0U) {
      event = context->pending_delta_event;
      event.type = STRAPPY_CHAT_STREAM_EVENT_REASONING_DELTA;
      event.text = context->pending_reasoning;
      if (!context->callback(&event, context->callback_data)) {
        return 0;
      }
    }
    if (context->pending_content_length > 0U) {
      event = context->pending_delta_event;
      event.type = STRAPPY_CHAT_STREAM_EVENT_CONTENT_DELTA;
      event.text = context->pending_content;
      if (!context->callback(&event, context->callback_data)) {
        return 0;
      }
    }
  }

  strappy_assistant_stream_clear_pending(context);
  return 1;
}

static int strappy_assistant_stream_should_flush_pending(
  const strappy_assistant_stream_store_context *context)
{
  long long now_ms;
  size_t pending_length;

  pending_length = strappy_assistant_stream_pending_length(context);
  if (pending_length == 0U) {
    return 0;
  }
  if (pending_length >= STRAPPY_ASSISTANT_STREAM_FLUSH_BYTES) {
    return 1;
  }
  if ((context == NULL) || (context->pending_started_ms <= 0LL)) {
    return 0;
  }

  now_ms = strappy_assistant_stream_now_ms();
  return ((now_ms > 0LL) &&
          ((now_ms - context->pending_started_ms) >=
           STRAPPY_ASSISTANT_STREAM_FLUSH_MS)) ? 1 : 0;
}

static int strappy_assistant_stream_store_callback(
  const strappy_chat_stream_event *event,
  void *callback_data)
{
  strappy_assistant_stream_store_context *context;
  const char *text;
  size_t text_length;

  if (callback_data == NULL) {
    return 1;
  }

  context = (strappy_assistant_stream_store_context *)callback_data;
  if (strappy_assistant_stream_event_is_delta(event)) {
    text = (event->text != NULL) ? event->text : "";
    text_length = strlen(text);
    if (text_length == 0U) {
      return 1;
    }

    if (!strappy_assistant_stream_pending_matches_event(context, event) &&
        !strappy_assistant_stream_flush_pending(context, 1)) {
      return 0;
    }

    if (text_length > STRAPPY_ASSISTANT_STREAM_BUFFER_BYTES) {
      if (!strappy_assistant_stream_flush_pending(context, 1)) {
        return 0;
      }
      return strappy_assistant_stream_store_and_forward_event(event, context);
    }

    if (!strappy_assistant_stream_append_pending(context, event)) {
      if (!strappy_assistant_stream_flush_pending(context, 1)) {
        return 0;
      }
      if (!strappy_assistant_stream_append_pending(context, event)) {
        return strappy_assistant_stream_store_and_forward_event(event, context);
      }
    }

    if (strappy_assistant_stream_should_flush_pending(context)) {
      return strappy_assistant_stream_flush_pending(context, 1);
    }
    return 1;
  }

  if ((event != NULL) &&
      (event->type == STRAPPY_CHAT_STREAM_EVENT_CONTENT_RETRACTED)) {
    if (!strappy_assistant_stream_flush_pending(context, 0)) {
      return 0;
    }
    return strappy_assistant_stream_store_and_forward_event(event, context);
  }

  if (!strappy_assistant_stream_flush_pending(context, 1)) {
    return 0;
  }
  return strappy_assistant_stream_store_and_forward_event(event, context);
}

static char *strappy_assistant_create_basic_message_json(const char *role,
                                                         const char *content,
                                                         char **error_out);

static char *strappy_assistant_join_strings(const char *first,
                                            const char *second)
{
  size_t first_length;
  size_t second_length;
  char *joined;

  if ((first == NULL) || (second == NULL)) {
    return NULL;
  }

  first_length = strlen(first);
  second_length = strlen(second);
  if (first_length > (((size_t)-1) - second_length - 1U)) {
    return NULL;
  }

  joined = (char *)malloc(first_length + second_length + 1U);
  if (joined == NULL) {
    return NULL;
  }

  memcpy(joined, first, first_length);
  memcpy(joined + first_length, second, second_length);
  joined[first_length + second_length] = '\0';
  return joined;
}

static char *strappy_assistant_make_key(const char *prefix)
{
  char buffer[96];
  struct timeval now;
  unsigned long stack_marker;
  int result;

  if (gettimeofday(&now, NULL) != 0) {
    now.tv_sec = 0;
    now.tv_usec = 0;
  }
  stack_marker = (unsigned long)(uintptr_t)&buffer;
  result = snprintf(buffer,
                    sizeof(buffer),
                    "%s-%lld-%ld-%lx",
                    (prefix != NULL) ? prefix : "turn",
                    (long long)now.tv_sec,
                    (long)now.tv_usec,
                    stack_marker);
  if ((result <= 0) || ((size_t)result >= sizeof(buffer))) {
    return NULL;
  }

  return strappy_string_duplicate(buffer);
}

static char *strappy_assistant_make_child_key(const char *parent,
                                              const char *suffix)
{
  char *with_dash;
  char *joined;

  if ((parent == NULL) || (suffix == NULL)) {
    return NULL;
  }

  with_dash = strappy_assistant_join_strings(parent, "-");
  if (with_dash == NULL) {
    return NULL;
  }
  joined = strappy_assistant_join_strings(with_dash, suffix);
  free(with_dash);
  return joined;
}

static void strappy_assistant_turn_keys_init(
  strappy_assistant_turn_keys *keys)
{
  if (keys == NULL) {
    return;
  }

  keys->turn_key = NULL;
  keys->prompt_group_key = NULL;
  keys->prompt_message_key = NULL;
  keys->assistant_message_key = NULL;
}

static void strappy_assistant_turn_keys_destroy(
  strappy_assistant_turn_keys *keys)
{
  if (keys == NULL) {
    return;
  }

  free(keys->turn_key);
  free(keys->prompt_group_key);
  free(keys->prompt_message_key);
  free(keys->assistant_message_key);
  strappy_assistant_turn_keys_init(keys);
}

static int strappy_assistant_turn_keys_make(
  strappy_assistant_turn_keys *keys,
  const char *prefix,
  char **error_out)
{
  if (keys == NULL) {
    strappy_set_error(error_out, "Turn key output is missing.");
    return 0;
  }

  strappy_assistant_turn_keys_init(keys);
  keys->turn_key = strappy_assistant_make_key(prefix);
  keys->prompt_group_key = strappy_assistant_make_key("prompt-group");
  if (keys->turn_key != NULL) {
    keys->prompt_message_key =
      strappy_assistant_make_child_key(keys->turn_key, "prompt");
  }
  if (keys->turn_key != NULL) {
    keys->assistant_message_key =
      strappy_assistant_make_child_key(keys->turn_key, "assistant");
  }

  if ((keys->turn_key == NULL) ||
      (keys->prompt_group_key == NULL) ||
      (keys->prompt_message_key == NULL) ||
      (keys->assistant_message_key == NULL)) {
    strappy_assistant_turn_keys_destroy(keys);
    strappy_set_error(error_out, "Could not allocate turn message keys.");
    return 0;
  }

  return 1;
}

static void strappy_assistant_turn_spec_set(
  strappy_assistant_turn_spec *turn,
  const strappy_assistant_turn_keys *keys,
  long long session_id,
  const char *prompt_group_key,
  const char *actor,
  const char *render_role,
  const char *api_role,
  const char *context_policy,
  const char *prompt,
  strappy_assistant_tool_policy policy)
{
  if (turn == NULL) {
    return;
  }

  turn->turn_key = (keys != NULL) ? keys->turn_key : NULL;
  turn->prompt_group_key =
    (prompt_group_key != NULL) ? prompt_group_key :
      ((keys != NULL) ? keys->prompt_group_key : NULL);
  turn->actor = actor;
  turn->render_role = render_role;
  turn->api_role = api_role;
  turn->context_policy = context_policy;
  turn->prompt = prompt;
  turn->prompt_message_key = (keys != NULL) ? keys->prompt_message_key : NULL;
  turn->assistant_message_key =
    (keys != NULL) ? keys->assistant_message_key : NULL;
  turn->session_id = session_id;
  turn->tool_policy = policy;
}

static int strappy_assistant_make_default_turns(
  const char *prompt,
  long long session_id,
  const strappy_assistant_guidance *guidance,
  strappy_assistant_turn_keys *main_keys,
  strappy_assistant_turn_keys *helper_keys,
  strappy_assistant_turn_spec *main_turn,
  strappy_assistant_turn_spec *helper_turn,
  char **error_out)
{
  if ((guidance == NULL) ||
      (guidance->learning_summary_prompt == NULL) ||
      (main_keys == NULL) || (helper_keys == NULL) ||
      (main_turn == NULL) || (helper_turn == NULL)) {
    strappy_set_error(error_out, "Turn outputs are missing.");
    return 0;
  }

  if (!strappy_assistant_turn_keys_make(main_keys, "user-turn", error_out) ||
      !strappy_assistant_turn_keys_make(helper_keys, "harness-turn", error_out)) {
    strappy_assistant_turn_keys_destroy(helper_keys);
    strappy_assistant_turn_keys_destroy(main_keys);
    return 0;
  }

  strappy_assistant_turn_spec_set(main_turn,
                                  main_keys,
                                  session_id,
                                  NULL,
                                  "user",
                                  "user",
                                  "user",
                                  "full",
                                  prompt,
                                  STRAPPY_ASSISTANT_TOOL_POLICY_NORMAL);
  strappy_assistant_turn_spec_set(helper_turn,
                                  helper_keys,
                                  session_id,
                                  main_keys->prompt_group_key,
                                  "harness",
                                  "harness",
                                  "user",
                                  "omit",
                                  guidance->learning_summary_prompt,
                                  STRAPPY_ASSISTANT_TOOL_POLICY_LEARNING_SUMMARY);
  return 1;
}

static int strappy_assistant_context_is_included(
  const strappy_assistant_turn_spec *turn)
{
  if ((turn != NULL) &&
      (turn->context_policy != NULL) &&
      (strcmp(turn->context_policy, "omit") == 0)) {
    return 0;
  }
  return 1;
}

static void strappy_assistant_storage_message_set_turn(
  strappy_session_message_input *message,
  const strappy_assistant_turn_spec *turn,
  const char *kind,
  const char *api_role,
  const char *render_role,
  const char *message_key,
  const char *target_message_key)
{
  if (message == NULL) {
    return;
  }

  message->turn_key = (turn != NULL) ? turn->turn_key : NULL;
  message->prompt_group_key =
    (turn != NULL) ? turn->prompt_group_key : NULL;
  message->actor = (turn != NULL) ? turn->actor : NULL;
  message->context_policy = (turn != NULL) ? turn->context_policy : "full";
  message->kind = kind;
  message->api_role = api_role;
  message->render_role = render_role;
  message->role = render_role;
  message->message_key = message_key;
  message->target_message_key = target_message_key;
  message->include_in_context = strappy_assistant_context_is_included(turn);
}

static int strappy_assistant_tool_is_allowed(
  strappy_assistant_tool_policy policy,
  const char *tool_name)
{
  if (policy == STRAPPY_ASSISTANT_TOOL_POLICY_LEARNING_SUMMARY) {
    return ((tool_name != NULL) &&
            ((strcmp(tool_name, STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE) == 0) ||
             (strcmp(tool_name, STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER) == 0) ||
             (strcmp(tool_name, STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER) == 0))) ? 1 : 0;
  }

  return 1;
}

static int strappy_assistant_tool_sequence_has_tool(
  const strappy_assistant_tool_sequence *sequence,
  const char *tool_name)
{
  size_t round_index;

  if ((sequence == NULL) || (tool_name == NULL) || (tool_name[0] == '\0')) {
    return 0;
  }

  for (round_index = 0U; round_index < sequence->round_count; round_index++) {
    const strappy_assistant_tool_round *round;
    size_t tool_index;

    round = &sequence->rounds[round_index];
    if (round->tool_names == NULL) {
      continue;
    }

    for (tool_index = 0U; tool_index < round->tool_count; tool_index++) {
      if ((round->tool_names[tool_index] != NULL) &&
          (strcmp(round->tool_names[tool_index], tool_name) == 0)) {
        return 1;
      }
    }
  }

  return 0;
}

static int strappy_assistant_final_request_context(
  const strappy_assistant_request_messages *request_messages,
  const strappy_assistant_tool_sequence *sequence,
  int did_run_tool_round,
  const strappy_chat_message **messages_out,
  size_t *count_out,
  char **error_out)
{
  const strappy_assistant_tool_round *round;

  if ((messages_out == NULL) || (count_out == NULL)) {
    strappy_set_error(error_out, "Assistant context output is missing.");
    return 0;
  }
  *messages_out = NULL;
  *count_out = 0U;

  if (did_run_tool_round) {
    if ((sequence == NULL) || (sequence->round_count == 0U)) {
      strappy_set_error(error_out, "Assistant tool context is incomplete.");
      return 0;
    }

    round = &sequence->rounds[sequence->round_count - 1U];
    if ((round->followup_messages == NULL) || (round->followup_count == 0U)) {
      strappy_set_error(error_out, "Assistant tool context is incomplete.");
      return 0;
    }

    *messages_out = round->followup_messages;
    *count_out = round->followup_count;
    return 1;
  }

  if ((request_messages == NULL) ||
      (request_messages->messages == NULL) ||
      (request_messages->count == 0U)) {
    strappy_set_error(error_out, "Assistant request context is incomplete.");
    return 0;
  }

  *messages_out = request_messages->messages;
  *count_out = request_messages->count;
  return 1;
}

static int strappy_assistant_build_learning_summary_request(
  const strappy_chat_message *base_messages,
  size_t base_count,
  const strappy_chat_result *final_result,
  const strappy_assistant_guidance *guidance,
  strappy_assistant_request_messages *helper_request,
  char **error_out)
{
  strappy_chat_message *messages;
  size_t request_count;

  if (helper_request == NULL) {
    strappy_set_error(error_out, "Learning summary request output is missing.");
    return 0;
  }
  strappy_assistant_request_messages_init(helper_request);

  if ((base_messages == NULL) || (base_count == 0U) ||
      (final_result == NULL) ||
      (final_result->response_text == NULL) ||
      (guidance == NULL) ||
      (guidance->learning_summary_prompt == NULL)) {
    strappy_set_error(error_out, "Learning summary request is incomplete.");
    return 0;
  }

  if (base_count > (((size_t)-1) - 2U)) {
    strappy_set_error(error_out, "Learning summary request is too large.");
    return 0;
  }
  request_count = base_count + 2U;
  if (request_count > (((size_t)-1) / sizeof(strappy_chat_message))) {
    strappy_set_error(error_out, "Learning summary request is too large.");
    return 0;
  }

  messages = (strappy_chat_message *)malloc(
    request_count * sizeof(strappy_chat_message));
  if (messages == NULL) {
    strappy_set_error(error_out, "Could not allocate learning summary request.");
    return 0;
  }

  memcpy(messages, base_messages, base_count * sizeof(strappy_chat_message));
  messages[base_count].role = "assistant";
  messages[base_count].content = final_result->response_text;
  messages[base_count].message_json = final_result->message_json;
  messages[base_count + 1U].role = "user";
  messages[base_count + 1U].content = guidance->learning_summary_prompt;
  messages[base_count + 1U].message_json = NULL;

  helper_request->messages = messages;
  helper_request->count = request_count;
  helper_request->system_prompt = NULL;
  return 1;
}

static int strappy_assistant_prepare_request_messages(
  const char *prompt,
  const char *system_prompt_template_path,
  strappy_config *config,
  const strappy_session_message_record_list *message_list,
  strappy_assistant_request_messages *request,
  char **error_out)
{
  char *resource_dir;
  char *system_prompt;
  strappy_chat_message *messages;
  size_t history_count;
  size_t request_count;
  size_t index;

  if (request == NULL) {
    strappy_set_error(error_out, "Assistant request message output is missing.");
    return 0;
  }
  strappy_assistant_request_messages_init(request);

  if (config == NULL) {
    strappy_set_error(error_out, "Assistant guidance configuration is missing.");
    return 0;
  }

  if ((prompt == NULL) || (prompt[0] == '\0')) {
    strappy_set_error(error_out, "Prompt is empty.");
    return 0;
  }

  history_count = 0U;
  if (message_list != NULL) {
    history_count = message_list->count;
  }

  if (history_count > (((size_t)-1) - 2U)) {
    strappy_set_error(error_out, "Session message list is too large.");
    return 0;
  }

  request_count = history_count + 2U;
  if (request_count > (((size_t)-1) / sizeof(strappy_chat_message))) {
    strappy_set_error(error_out, "Session message list is too large.");
    return 0;
  }

  resource_dir =
    strappy_prompt_resource_directory_from_template_path(
      system_prompt_template_path,
      error_out);
  if (resource_dir == NULL) {
    return 0;
  }

  if (!strappy_config_set_guidance_resource_dir(config,
                                                resource_dir,
                                                error_out)) {
    free(resource_dir);
    return 0;
  }

  system_prompt =
    strappy_prompt_render_system_prompt(system_prompt_template_path,
                                        error_out);
  free(resource_dir);
  if (system_prompt == NULL) {
    return 0;
  }

  messages = (strappy_chat_message *)malloc(
    request_count * sizeof(strappy_chat_message));
  if (messages == NULL) {
    free(system_prompt);
    strappy_set_error(error_out, "Could not allocate assistant request messages.");
    return 0;
  }

  messages[0].role = "system";
  messages[0].content = system_prompt;
  messages[0].message_json = NULL;

  for (index = 0U; index < history_count; index++) {
    messages[index + 1U].role =
      (message_list->records[index].api_role != NULL) ?
        message_list->records[index].api_role :
        message_list->records[index].role;
    messages[index + 1U].content = message_list->records[index].content;
    messages[index + 1U].message_json = message_list->records[index].message_json;
  }

  messages[history_count + 1U].role = "user";
  messages[history_count + 1U].content = prompt;
  messages[history_count + 1U].message_json = NULL;

  request->messages = messages;
  request->count = request_count;
  request->system_prompt = system_prompt;
  return 1;
}

static cJSON *strappy_assistant_tool_calls_from_result(
  const strappy_chat_result *result,
  cJSON **message_root_out,
  char **error_out)
{
  cJSON *root;
  cJSON *tool_calls;

  if (message_root_out != NULL) {
    *message_root_out = NULL;
  }

  if ((result == NULL) ||
      (result->message_json == NULL) ||
      (result->message_json[0] == '\0')) {
    return NULL;
  }

  root = cJSON_Parse(result->message_json);
  if (root == NULL) {
    strappy_set_error(error_out, "Assistant tool-call message JSON is invalid.");
    return NULL;
  }

  tool_calls = cJSON_GetObjectItem(root, "tool_calls");
  if (!cJSON_IsArray(tool_calls) || (cJSON_GetArraySize(tool_calls) <= 0)) {
    cJSON_Delete(root);
    return NULL;
  }

  if (message_root_out != NULL) {
    *message_root_out = root;
  } else {
    cJSON_Delete(root);
  }

  return tool_calls;
}

static const char *strappy_assistant_tool_call_string(cJSON *tool_call,
                                                      const char *key)
{
  cJSON *value;

  if (!cJSON_IsObject(tool_call) || (key == NULL)) {
    return NULL;
  }

  value = cJSON_GetObjectItem(tool_call, key);
  if (!cJSON_IsString(value) || (value->valuestring == NULL)) {
    return NULL;
  }

  return value->valuestring;
}

static const char *strappy_assistant_tool_call_function_string(
  cJSON *tool_call,
  const char *key)
{
  cJSON *function;

  if (!cJSON_IsObject(tool_call) || (key == NULL)) {
    return NULL;
  }

  function = cJSON_GetObjectItem(tool_call, "function");
  if (!cJSON_IsObject(function)) {
    return NULL;
  }

  return strappy_assistant_tool_call_string(function, key);
}

static char *strappy_assistant_tool_call_content(cJSON *tool_calls,
                                                 char **error_out)
{
  char *json;
  char *content;

  json = cJSON_Print(tool_calls);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize tool-call inputs.");
    return NULL;
  }

  content = strappy_assistant_join_strings("Tool call input:\n", json);
  free(json);
  if (content == NULL) {
    strappy_set_error(error_out, "Could not allocate tool-call input text.");
    return NULL;
  }

  return content;
}

static char *strappy_assistant_tool_error_result(const char *tool_name,
                                                 const char *message,
                                                 const char *default_message)
{
  cJSON *root;
  char *json;
  const char *text;

  root = cJSON_CreateObject();
  if (root == NULL) {
    return NULL;
  }

  text = (message != NULL) ? message : default_message;
  if (text == NULL) {
    text = "";
  }

  if (((tool_name != NULL) &&
       (cJSON_AddStringToObject(root, "tool_name", tool_name) == NULL)) ||
      (cJSON_AddStringToObject(root, "error", text) == NULL)) {
    cJSON_Delete(root);
    return NULL;
  }

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return json;
}

static char *strappy_assistant_tool_message_json(const char *tool_call_id,
                                                 const char *tool_name,
                                                 const char *content,
                                                 char **error_out)
{
  cJSON *root;
  char *json;

  (void)tool_name;

  root = cJSON_CreateObject();
  if (root == NULL) {
    strappy_set_error(error_out, "Could not allocate tool result message.");
    return NULL;
  }

  if ((cJSON_AddStringToObject(root, "role", "tool") == NULL) ||
      ((tool_call_id != NULL) &&
       (cJSON_AddStringToObject(root,
                                "tool_call_id",
                                tool_call_id) == NULL)) ||
      (cJSON_AddStringToObject(root,
                               "content",
                               (content != NULL) ? content : "") == NULL)) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Could not build tool result message.");
    return NULL;
  }

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize tool result message.");
    return NULL;
  }

  return json;
}

static int strappy_assistant_build_tool_round(
  const char *session_db_path,
  const char *resource_dir,
  const strappy_assistant_guidance *guidance,
  const strappy_assistant_turn_spec *turn,
  size_t round_number,
  int store_prompt_message,
  int post_final,
  const strappy_assistant_request_messages *request_messages,
  const strappy_chat_result *tool_request_result,
  int should_stream,
  strappy_chat_stream_callback callback,
  void *callback_data,
  int requires_catalog,
  strappy_assistant_tool_round *round,
  char **error_out)
{
  cJSON *root;
  cJSON *tool_calls;
  char *parse_error;
  char tool_call_key[96];
  int tool_count;
  int index;
  size_t followup_count;
  size_t storage_count;

  if ((guidance == NULL) ||
      (guidance->tool_error_default == NULL) ||
      (guidance->tool_error_not_allowed_for_turn == NULL)) {
    strappy_set_error(error_out, "Assistant tool error guidance is missing.");
    return 0;
  }

  if (round == NULL) {
    strappy_set_error(error_out, "Tool round output is missing.");
    return 0;
  }
  strappy_assistant_tool_round_init(round);

  root = NULL;
  parse_error = NULL;
  tool_calls =
    strappy_assistant_tool_calls_from_result(tool_request_result,
                                             &root,
                                             &parse_error);
  if (tool_calls == NULL) {
    if (parse_error != NULL) {
      strappy_set_error(error_out, parse_error);
      free(parse_error);
      return 0;
    }
    return 1;
  }
  free(parse_error);

  if (requires_catalog &&
      ((session_db_path == NULL) || (session_db_path[0] == '\0'))) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Tool execution requires a catalog database.");
    return 0;
  }

  tool_count = cJSON_GetArraySize(tool_calls);
  if ((tool_count <= 0) ||
      (request_messages == NULL) ||
      (request_messages->messages == NULL)) {
    cJSON_Delete(root);
    return 1;
  }

  if ((size_t)tool_count > (((size_t)-1) - request_messages->count - 1U)) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Tool call list is too large.");
    return 0;
  }

  followup_count = request_messages->count + 1U + (size_t)tool_count;
  storage_count = 2U + (size_t)tool_count;
  if ((followup_count > (((size_t)-1) / sizeof(strappy_chat_message))) ||
      (storage_count > (((size_t)-1) / sizeof(strappy_session_message_input))) ||
      ((size_t)tool_count > (((size_t)-1) / sizeof(char *)))) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Tool call list is too large.");
    return 0;
  }

  round->followup_messages = (strappy_chat_message *)malloc(
    followup_count * sizeof(strappy_chat_message));
  round->storage_messages = (strappy_session_message_input *)malloc(
    storage_count * sizeof(strappy_session_message_input));
  round->tool_result_message_keys =
    (char **)calloc((size_t)tool_count, sizeof(char *));
  round->tool_call_ids = (char **)calloc((size_t)tool_count, sizeof(char *));
  round->tool_names = (char **)calloc((size_t)tool_count, sizeof(char *));
  round->tool_arguments = (char **)calloc((size_t)tool_count, sizeof(char *));
  round->tool_outputs = (char **)calloc((size_t)tool_count, sizeof(char *));
  round->tool_message_jsons = (char **)calloc((size_t)tool_count, sizeof(char *));
  if ((round->followup_messages == NULL) ||
      (round->storage_messages == NULL) ||
      (round->tool_result_message_keys == NULL) ||
      (round->tool_call_ids == NULL) ||
      (round->tool_names == NULL) ||
      (round->tool_arguments == NULL) ||
      (round->tool_outputs == NULL) ||
      (round->tool_message_jsons == NULL)) {
    cJSON_Delete(root);
    strappy_assistant_tool_round_destroy(round);
    strappy_set_error(error_out, "Could not allocate tool messages.");
    return 0;
  }

  round->tool_count = (size_t)tool_count;
  round->followup_count = followup_count;
  round->storage_count = storage_count;
  round->store_prompt_message = store_prompt_message ? 1 : 0;
  round->post_final = post_final ? 1 : 0;
  memcpy(round->followup_messages,
         request_messages->messages,
         request_messages->count * sizeof(strappy_chat_message));

  round->tool_call_content =
    strappy_assistant_tool_call_content(tool_calls, error_out);
  if (round->tool_call_content == NULL) {
    cJSON_Delete(root);
    strappy_assistant_tool_round_destroy(round);
    return 0;
  }

  round->followup_messages[request_messages->count].role = "assistant";
  round->followup_messages[request_messages->count].content =
    (tool_request_result->response_text != NULL) ?
      tool_request_result->response_text : "";
  round->followup_messages[request_messages->count].message_json =
    tool_request_result->message_json;

  memset(round->storage_messages,
         0,
         storage_count * sizeof(strappy_session_message_input));
  strappy_assistant_storage_message_set_turn(&round->storage_messages[0],
                                             turn,
                                             "prompt",
                                             (turn != NULL) ?
                                               turn->api_role : "user",
                                             (turn != NULL) ?
                                               turn->render_role : "user",
                                             (turn != NULL) ?
                                               turn->prompt_message_key : NULL,
                                             NULL);
  round->storage_messages[0].content =
    ((turn != NULL) && (turn->prompt != NULL)) ? turn->prompt : "";
  snprintf(tool_call_key,
           sizeof(tool_call_key),
           "%s-tool-call-%lu",
           ((turn != NULL) && (turn->turn_key != NULL)) ?
             turn->turn_key : "turn",
           (unsigned long)round_number);
  round->tool_call_message_key = strappy_string_duplicate(tool_call_key);
  if (round->tool_call_message_key == NULL) {
    cJSON_Delete(root);
    strappy_assistant_tool_round_destroy(round);
    strappy_set_error(error_out, "Could not allocate tool-call message key.");
    return 0;
  }
  strappy_assistant_storage_message_set_turn(&round->storage_messages[1],
                                             turn,
                                             "tool_call",
                                             "assistant",
                                             "tool_call",
                                             round->tool_call_message_key,
                                             (turn != NULL) ?
                                               turn->assistant_message_key :
                                               NULL);
  round->storage_messages[1].content = round->tool_call_content;
  round->storage_messages[1].model = tool_request_result->model;
  round->storage_messages[1].http_status = tool_request_result->http_status;
  round->storage_messages[1].metadata_json = tool_request_result->metadata_json;
  round->storage_messages[1].message_json = tool_request_result->message_json;
  round->storage_messages[1].reasoning = tool_request_result->reasoning_text;

  if (should_stream &&
      !strappy_assistant_emit_tool_event(STRAPPY_CHAT_STREAM_EVENT_TOOL_CALL,
                                         turn,
                                         round->tool_call_message_key,
                                         NULL,
                                         NULL,
                                         round->tool_call_content,
                                         NULL,
                                         tool_request_result->message_json,
                                         callback,
                                         callback_data,
                                         error_out)) {
    cJSON_Delete(root);
    strappy_assistant_tool_round_destroy(round);
    return 0;
  }

  for (index = 0; index < tool_count; index++) {
    cJSON *tool_call;
    const char *tool_call_id;
    const char *tool_name;
    const char *arguments;
    char *tool_error;
    char *output;
    char *message_json;
    char tool_result_key[96];
    size_t followup_index;
    size_t storage_index;
    int had_tool_error;

    tool_call = cJSON_GetArrayItem(tool_calls, index);
    tool_call_id = strappy_assistant_tool_call_string(tool_call, "id");
    tool_name =
      strappy_assistant_tool_call_function_string(tool_call, "name");
    arguments =
      strappy_assistant_tool_call_function_string(tool_call, "arguments");

    if ((tool_call_id != NULL) &&
        ((round->tool_call_ids[index] =
            strappy_string_duplicate(tool_call_id)) == NULL)) {
      cJSON_Delete(root);
      strappy_assistant_tool_round_destroy(round);
      strappy_set_error(error_out, "Could not allocate tool call id.");
      return 0;
    }
    if ((tool_name != NULL) &&
        ((round->tool_names[index] =
            strappy_string_duplicate(tool_name)) == NULL)) {
      cJSON_Delete(root);
      strappy_assistant_tool_round_destroy(round);
      strappy_set_error(error_out, "Could not allocate tool name.");
      return 0;
    }
    if ((arguments != NULL) &&
        ((round->tool_arguments[index] =
            strappy_string_duplicate(arguments)) == NULL)) {
      cJSON_Delete(root);
      strappy_assistant_tool_round_destroy(round);
      strappy_set_error(error_out, "Could not allocate tool arguments.");
      return 0;
    }

    tool_error = NULL;
    had_tool_error = 0;
    if (!strappy_assistant_tool_is_allowed((turn != NULL) ?
                                             turn->tool_policy :
                                             STRAPPY_ASSISTANT_TOOL_POLICY_NORMAL,
                                           round->tool_names[index])) {
      had_tool_error = 1;
      output = strappy_assistant_tool_error_result(
        round->tool_names[index],
        guidance->tool_error_not_allowed_for_turn,
        guidance->tool_error_default);
    } else {
      output = strappy_tools_execute(session_db_path,
                                     (turn != NULL) ? turn->session_id : 0LL,
                                     resource_dir,
                                     round->tool_names[index],
                                     round->tool_arguments[index],
                                     &tool_error);
    }
    if (output == NULL) {
      had_tool_error = 1;
      output = strappy_assistant_tool_error_result(round->tool_names[index],
                                                   tool_error,
                                                   guidance->tool_error_default);
      free(tool_error);
      tool_error = NULL;
      if (output == NULL) {
        cJSON_Delete(root);
        strappy_assistant_tool_round_destroy(round);
        strappy_set_error(error_out, "Could not build tool error result.");
        return 0;
      }
    }

    storage_index = 2U + (size_t)index;
    snprintf(tool_result_key,
             sizeof(tool_result_key),
             "%s-tool-result-%lu-%d",
             ((turn != NULL) && (turn->turn_key != NULL)) ?
               turn->turn_key : "turn",
             (unsigned long)round_number,
             index);
    round->tool_result_message_keys[index] =
      strappy_string_duplicate(tool_result_key);
    if (round->tool_result_message_keys[index] == NULL) {
      free(output);
      free(tool_error);
      cJSON_Delete(root);
      strappy_assistant_tool_round_destroy(round);
      strappy_set_error(error_out, "Could not allocate tool-result message key.");
      return 0;
    }

    message_json = strappy_assistant_tool_message_json(round->tool_call_ids[index],
                                                       round->tool_names[index],
                                                       output,
                                                       error_out);
    if (message_json == NULL) {
      free(output);
      cJSON_Delete(root);
      strappy_assistant_tool_round_destroy(round);
      return 0;
    }

    if (should_stream &&
        !strappy_assistant_emit_tool_event(
          had_tool_error ? STRAPPY_CHAT_STREAM_EVENT_TOOL_ERROR :
                           STRAPPY_CHAT_STREAM_EVENT_TOOL_RESULT,
          turn,
          round->tool_result_message_keys[index],
          round->tool_call_ids[index],
          round->tool_names[index],
          round->tool_arguments[index],
          output,
          message_json,
          callback,
          callback_data,
          error_out)) {
      free(message_json);
      free(output);
      free(tool_error);
      cJSON_Delete(root);
      strappy_assistant_tool_round_destroy(round);
      return 0;
    }

    round->tool_outputs[index] = output;
    round->tool_message_jsons[index] = message_json;

    followup_index = request_messages->count + 1U + (size_t)index;
    round->followup_messages[followup_index].role = "tool";
    round->followup_messages[followup_index].content = output;
    round->followup_messages[followup_index].message_json = message_json;

    strappy_assistant_storage_message_set_turn(
      &round->storage_messages[storage_index],
      turn,
      had_tool_error ? "tool_error" : "tool_result",
      "tool",
      "tool",
      round->tool_result_message_keys[index],
      (turn != NULL) ? turn->assistant_message_key : NULL);
    round->storage_messages[storage_index].content = output;
    round->storage_messages[storage_index].message_json = message_json;
    round->storage_messages[storage_index].tool_call_id =
      round->tool_call_ids[index];
    round->storage_messages[storage_index].tool_name =
      round->tool_names[index];
    round->storage_messages[storage_index].arguments_json =
      round->tool_arguments[index];
    round->storage_messages[storage_index].result_json = output;
    round->storage_messages[storage_index].is_error = had_tool_error;
  }

  cJSON_Delete(root);
  return 1;
}

static int strappy_assistant_result_has_tool_calls(
  const strappy_chat_result *result,
  int *has_tool_calls_out,
  char **error_out)
{
  cJSON *root;
  cJSON *tool_calls;
  char *parse_error;

  if (has_tool_calls_out == NULL) {
    strappy_set_error(error_out, "Tool-call check output is missing.");
    return 0;
  }
  *has_tool_calls_out = 0;

  root = NULL;
  parse_error = NULL;
  tool_calls =
    strappy_assistant_tool_calls_from_result(result, &root, &parse_error);
  if (tool_calls == NULL) {
    if (parse_error != NULL) {
      strappy_set_error(error_out, parse_error);
      free(parse_error);
      return 0;
    }
    return 1;
  }

  *has_tool_calls_out = 1;
  cJSON_Delete(root);
  return 1;
}

static int strappy_assistant_emit_result_delta_event(
  strappy_chat_stream_event_type type,
  const strappy_assistant_turn_spec *turn,
  const char *text,
  strappy_chat_stream_callback callback,
  void *callback_data,
  char **error_out)
{
  strappy_chat_stream_event event;

  if ((callback == NULL) || (text == NULL) || (text[0] == '\0')) {
    return 1;
  }

  memset(&event, 0, sizeof(event));
  event.type = type;
  event.text = text;
  if (turn != NULL) {
    event.turn_key = turn->turn_key;
    event.prompt_group_key = turn->prompt_group_key;
    event.actor = turn->actor;
    event.kind = "assistant";
    event.render_role = "assistant";
    event.api_role = "assistant";
    event.message_key = turn->assistant_message_key;
  }

  if (!callback(&event, callback_data)) {
    strappy_set_error(error_out,
                      "Stream callback rejected assistant response event.");
    return 0;
  }

  return 1;
}

static int strappy_assistant_emit_complete_result_events(
  const strappy_assistant_turn_spec *turn,
  const strappy_chat_result *result,
  int has_tool_calls,
  strappy_chat_stream_callback callback,
  void *callback_data,
  char **error_out)
{
  if ((callback == NULL) || (result == NULL) || result->cancelled) {
    return 1;
  }

  if (!strappy_assistant_emit_result_delta_event(
        STRAPPY_CHAT_STREAM_EVENT_REASONING_DELTA,
        turn,
        result->reasoning_text,
        callback,
        callback_data,
        error_out)) {
    return 0;
  }

  if (!has_tool_calls &&
      !strappy_assistant_emit_result_delta_event(
        STRAPPY_CHAT_STREAM_EVENT_CONTENT_DELTA,
        turn,
        result->response_text,
        callback,
        callback_data,
        error_out)) {
    return 0;
  }

  return 1;
}

static char *strappy_assistant_create_basic_message_json(const char *role,
                                                         const char *content,
                                                         char **error_out)
{
  cJSON *root;
  char *json;

  root = cJSON_CreateObject();
  if (root == NULL) {
    strappy_set_error(error_out, "Could not allocate assistant message JSON.");
    return NULL;
  }

  if ((cJSON_AddStringToObject(root,
                               "role",
                               (role != NULL) ? role : "assistant") == NULL) ||
      (cJSON_AddStringToObject(root,
                               "content",
                               (content != NULL) ? content : "") == NULL)) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Could not build assistant message JSON.");
    return NULL;
  }

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize assistant message JSON.");
    return NULL;
  }

  return json;
}

static char *strappy_assistant_create_local_message_json(const char *content,
                                                         char **error_out)
{
  return strappy_assistant_create_basic_message_json("assistant",
                                                    content,
                                                    error_out);
}

static int strappy_assistant_json_add_string_if_present(cJSON *object,
                                                        const char *key,
                                                        const char *value)
{
  if ((object == NULL) || (key == NULL)) {
    return 0;
  }
  if ((value == NULL) || (value[0] == '\0')) {
    return 1;
  }
  return (cJSON_AddStringToObject(object, key, value) != NULL) ? 1 : 0;
}

static char *strappy_assistant_create_error_metadata_json(
  const char *message,
  const strappy_chat_result *result,
  char **error_out)
{
  cJSON *root;
  char *json;
  long http_status;
  int ok;

  root = cJSON_CreateObject();
  if (root == NULL) {
    strappy_set_error(error_out, "Could not allocate assistant error metadata.");
    return NULL;
  }

  http_status = (result != NULL) ? result->http_status : 0L;
  ok = (cJSON_AddStringToObject(root, "finish_reason", "error") != NULL) &&
       (cJSON_AddStringToObject(root, "native_finish_reason", "error") != NULL) &&
       (cJSON_AddNumberToObject(root, "http_status", (double)http_status) != NULL) &&
       (cJSON_AddStringToObject(root,
                                "error",
                                ((message != NULL) && (message[0] != '\0')) ?
                                  message :
                                  "Prompt failed before the assistant turn completed.") != NULL) &&
       strappy_assistant_json_add_string_if_present(root,
                                                    "response_id",
                                                    (result != NULL) ?
                                                      result->response_id : NULL) &&
       strappy_assistant_json_add_string_if_present(root,
                                                    "model",
                                                    (result != NULL) ?
                                                      result->model : NULL) &&
       strappy_assistant_json_add_string_if_present(root,
                                                    "created",
                                                    (result != NULL) ?
                                                      result->created : NULL) &&
       strappy_assistant_json_add_string_if_present(root,
                                                    "provider_finish_reason",
                                                    (result != NULL) ?
                                                      result->finish_reason : NULL) &&
       strappy_assistant_json_add_string_if_present(root,
                                                    "provider_native_finish_reason",
                                                    (result != NULL) ?
                                                      result->native_finish_reason : NULL);

  if (!ok) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Could not build assistant error metadata.");
    return NULL;
  }

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize assistant error metadata.");
    return NULL;
  }

  return json;
}

static int strappy_assistant_store_turn_error(
  const char *session_db_path,
  long long session_id,
  const strappy_assistant_turn_spec *turn,
  const strappy_chat_result *result,
  const char *message,
  char **error_out)
{
  strappy_session_message_input input;
  char *metadata_json;
  char *message_json;
  const char *content;
  int ok;

  if ((session_db_path == NULL) || (session_db_path[0] == '\0') ||
      (session_id <= 0LL) || (turn == NULL) ||
      (turn->assistant_message_key == NULL) ||
      (turn->assistant_message_key[0] == '\0')) {
    return 1;
  }

  content = ((message != NULL) && (message[0] != '\0')) ?
    message :
    "Prompt failed before the assistant turn completed.";
  metadata_json =
    strappy_assistant_create_error_metadata_json(content, result, error_out);
  if (metadata_json == NULL) {
    return 0;
  }

  message_json = strappy_assistant_create_basic_message_json("assistant",
                                                            content,
                                                            error_out);
  if (message_json == NULL) {
    free(metadata_json);
    return 0;
  }

  memset(&input, 0, sizeof(input));
  strappy_assistant_storage_message_set_turn(&input,
                                             turn,
                                             "assistant",
                                             "assistant",
                                             "assistant",
                                             turn->assistant_message_key,
                                             NULL);
  input.context_policy = "omit";
  input.content = content;
  input.model = (result != NULL) ? result->model : NULL;
  input.http_status = (result != NULL) ? result->http_status : 0L;
  input.metadata_json = metadata_json;
  input.message_json = message_json;
  input.include_in_context = 0;
  input.is_error = 1;

  ok = strappy_db_upsert_session_message(session_db_path,
                                         session_id,
                                         &input,
                                         error_out);
  free(message_json);
  free(metadata_json);
  return ok;
}

static int strappy_assistant_store_turn_result(
  const char *session_db_path,
  long long session_id,
  const strappy_assistant_turn_spec *turn,
  const strappy_chat_result *result,
  char **error_out)
{
  strappy_session_message_input input;

  if ((session_db_path == NULL) || (session_db_path[0] == '\0') ||
      (session_id <= 0LL) || (turn == NULL) || (result == NULL) ||
      result->cancelled || (turn->assistant_message_key == NULL) ||
      (turn->assistant_message_key[0] == '\0')) {
    return 1;
  }

  memset(&input, 0, sizeof(input));
  strappy_assistant_storage_message_set_turn(&input,
                                             turn,
                                             "assistant",
                                             "assistant",
                                             "assistant",
                                             turn->assistant_message_key,
                                             NULL);
  input.content = (result->response_text != NULL) ? result->response_text : "";
  input.model = result->model;
  input.http_status = result->http_status;
  input.metadata_json = result->metadata_json;
  input.message_json = result->message_json;
  input.reasoning = result->reasoning_text;

  return strappy_db_upsert_session_message(session_db_path,
                                           session_id,
                                           &input,
                                           error_out);
}

static void strappy_assistant_try_store_turn_error(
  const char *session_db_path,
  long long session_id,
  const strappy_assistant_turn_spec *turn,
  const strappy_chat_result *result,
  char **error_out)
{
  const char *message;
  char *store_error;

  message = ((error_out != NULL) && (*error_out != NULL) &&
             ((*error_out)[0] != '\0')) ?
    *error_out :
    "Prompt failed before the assistant turn completed.";
  store_error = NULL;
  if (!strappy_assistant_store_turn_error(session_db_path,
                                          session_id,
                                          turn,
                                          result,
                                          message,
                                          &store_error)) {
    free(store_error);
  }
}

static int strappy_assistant_validate_final_result(
  const strappy_chat_result *result,
  char **error_out)
{
  if (result == NULL) {
    strappy_set_error(error_out, "Assistant final result is missing.");
    return 0;
  }
  if (result->cancelled) {
    return 1;
  }
  if ((result->response_text != NULL) && (result->response_text[0] != '\0')) {
    return 1;
  }

  strappy_set_error(error_out,
                    "OpenRouter returned a final assistant message without content.");
  return 0;
}

static int strappy_assistant_set_local_limit_result(
  strappy_chat_result *result,
  const strappy_chat_result *last_result,
  const char *message,
  const char *finish_reason,
  char **error_out)
{
  if (result == NULL) {
    strappy_set_error(error_out, "Assistant limit result output is missing.");
    return 0;
  }

  if ((message == NULL) || (finish_reason == NULL)) {
    strappy_set_error(error_out, "Assistant limit result request is incomplete.");
    return 0;
  }

  strappy_chat_result_destroy(result);
  result->response_text = strappy_string_duplicate(message);
  result->message_json =
    strappy_assistant_create_local_message_json(message, error_out);
  result->finish_reason = strappy_string_duplicate(finish_reason);
  result->native_finish_reason = strappy_string_duplicate(finish_reason);
  if ((last_result != NULL) && (last_result->model != NULL)) {
    result->model = strappy_string_duplicate(last_result->model);
  }
  if (last_result != NULL) {
    result->http_status = last_result->http_status;
  }

  if ((result->response_text == NULL) || (result->message_json == NULL) ||
      (result->finish_reason == NULL) ||
      (result->native_finish_reason == NULL) ||
      ((last_result != NULL) && (last_result->model != NULL) &&
       (result->model == NULL))) {
    strappy_chat_result_destroy(result);
    strappy_set_error(error_out, "Could not allocate assistant limit result.");
    return 0;
  }

  return 1;
}

static int strappy_assistant_set_tool_limit_result(
  strappy_chat_result *result,
  const strappy_chat_result *last_result,
  const strappy_assistant_guidance *guidance,
  char **error_out)
{
  if ((guidance == NULL) || (guidance->tool_round_limit_message == NULL)) {
    strappy_set_error(error_out, "Assistant tool limit guidance is missing.");
    return 0;
  }

  return strappy_assistant_set_local_limit_result(result,
                                                  last_result,
                                                  guidance->tool_round_limit_message,
                                                  "tool_round_limit",
                                                  error_out);
}

static int strappy_assistant_store_tool_sequence(
  const char *session_db_path,
  long long session_id,
  const char *prompt,
  const strappy_assistant_guidance *guidance,
  const strappy_assistant_turn_spec *main_turn,
  const strappy_assistant_turn_spec *helper_turn,
  const strappy_assistant_tool_sequence *sequence,
  const strappy_chat_result *final_result,
  char **error_out)
{
  strappy_session_message_input *messages;
  size_t message_count;
  size_t message_index;
  size_t round_index;
  int has_post_final_rounds;
  int has_learning_summary_turn;
  int needs_learning_summary_prompt;
  char *learning_summary_completion_json;
  const strappy_chat_result *learning_summary_final_result;
  int ok;

  if ((main_turn == NULL) || (sequence == NULL) || (final_result == NULL) ||
      (guidance == NULL) ||
      (guidance->learning_summary_prompt == NULL) ||
      (guidance->learning_summary_completion_text == NULL)) {
    strappy_set_error(error_out, "Tool message sequence is incomplete.");
    return 0;
  }
  if (session_id <= 0) {
    strappy_set_error(error_out, "Session id is not valid.");
    return 0;
  }

  learning_summary_final_result = sequence->learning_summary_final_result;
  has_learning_summary_turn = (learning_summary_final_result != NULL) ? 1 : 0;
  message_count = 1U;
  has_post_final_rounds = 0;
  for (round_index = 0U; round_index < sequence->round_count; round_index++) {
    const strappy_assistant_tool_round *round;
    size_t copy_start;
    size_t addition;

    round = &sequence->rounds[round_index];
    if ((round->storage_messages == NULL) || (round->storage_count < 2U)) {
      strappy_set_error(error_out, "Tool message sequence is incomplete.");
      return 0;
    }

    if (round->post_final) {
      has_post_final_rounds = 1;
      continue;
    }

    copy_start = round->store_prompt_message ? 0U : 1U;
    if (round->storage_count < copy_start) {
      strappy_set_error(error_out, "Tool message sequence is incomplete.");
      return 0;
    }
    addition = round->storage_count - copy_start;
    if (message_count > (((size_t)-1) - addition - 1U)) {
      strappy_set_error(error_out, "Tool message sequence is too large.");
      return 0;
    }
    message_count += addition;
  }

  if (message_count > (((size_t)-1) - 1U)) {
    strappy_set_error(error_out, "Tool message sequence is too large.");
    return 0;
  }
  message_count++;

  for (round_index = 0U; round_index < sequence->round_count; round_index++) {
    const strappy_assistant_tool_round *round;
    size_t copy_start;
    size_t addition;

    round = &sequence->rounds[round_index];
    if (!round->post_final) {
      continue;
    }

    copy_start = round->store_prompt_message ? 0U : 1U;
    if (round->storage_count < copy_start) {
      strappy_set_error(error_out, "Tool message sequence is incomplete.");
      return 0;
    }
    addition = round->storage_count - copy_start;
    if (message_count > (((size_t)-1) - addition - 1U)) {
      strappy_set_error(error_out, "Tool message sequence is too large.");
      return 0;
    }
    message_count += addition;
  }

  needs_learning_summary_prompt =
    (has_learning_summary_turn && !has_post_final_rounds) ? 1 : 0;
  if (has_learning_summary_turn) {
    if (needs_learning_summary_prompt &&
        (message_count > (((size_t)-1) - 1U))) {
      strappy_set_error(error_out, "Tool message sequence is too large.");
      return 0;
    }
    if (needs_learning_summary_prompt) {
      message_count++;
    }
    if (message_count > (((size_t)-1) - 1U)) {
      strappy_set_error(error_out, "Tool message sequence is too large.");
      return 0;
    }
    message_count++;
  }

  if (message_count > (((size_t)-1) / sizeof(strappy_session_message_input))) {
    strappy_set_error(error_out, "Tool message sequence is too large.");
    return 0;
  }

  learning_summary_completion_json = NULL;
  if (has_learning_summary_turn) {
    learning_summary_completion_json =
      strappy_assistant_create_basic_message_json(
        "assistant",
        guidance->learning_summary_completion_text,
        error_out);
    if (learning_summary_completion_json == NULL) {
      return 0;
    }
  }

  messages = (strappy_session_message_input *)malloc(
    message_count * sizeof(strappy_session_message_input));
  if (messages == NULL) {
    free(learning_summary_completion_json);
    strappy_set_error(error_out, "Could not allocate tool message sequence.");
    return 0;
  }

  memset(messages, 0, message_count * sizeof(strappy_session_message_input));
  message_index = 0U;
  strappy_assistant_storage_message_set_turn(&messages[message_index],
                                             main_turn,
                                             "prompt",
                                             main_turn->api_role,
                                             main_turn->render_role,
                                             main_turn->prompt_message_key,
                                             NULL);
  messages[message_index].content = prompt;
  message_index++;

  for (round_index = 0U; round_index < sequence->round_count; round_index++) {
    const strappy_assistant_tool_round *round;
    size_t copy_start;
    size_t copy_count;

    round = &sequence->rounds[round_index];
    if (round->post_final) {
      continue;
    }
    copy_start = round->store_prompt_message ? 0U : 1U;
    copy_count = round->storage_count - copy_start;
    memcpy(&messages[message_index],
           &round->storage_messages[copy_start],
           copy_count * sizeof(strappy_session_message_input));
    message_index += copy_count;
  }

  strappy_assistant_storage_message_set_turn(&messages[message_index],
                                             main_turn,
                                             "assistant",
                                             "assistant",
                                             "assistant",
                                             main_turn->assistant_message_key,
                                             NULL);
  messages[message_index].content = final_result->response_text;
  messages[message_index].model = final_result->model;
  messages[message_index].http_status = final_result->http_status;
  messages[message_index].metadata_json = final_result->metadata_json;
  messages[message_index].message_json = final_result->message_json;
  messages[message_index].reasoning = final_result->reasoning_text;
  message_index++;

  for (round_index = 0U; round_index < sequence->round_count; round_index++) {
    const strappy_assistant_tool_round *round;
    size_t copy_start;
    size_t copy_count;

    round = &sequence->rounds[round_index];
    if (!round->post_final) {
      continue;
    }
    copy_start = round->store_prompt_message ? 0U : 1U;
    copy_count = round->storage_count - copy_start;
    memcpy(&messages[message_index],
           &round->storage_messages[copy_start],
           copy_count * sizeof(strappy_session_message_input));
    message_index += copy_count;
  }

  if (needs_learning_summary_prompt) {
    strappy_assistant_storage_message_set_turn(
      &messages[message_index],
      helper_turn,
      "prompt",
      (helper_turn != NULL) ? helper_turn->api_role : "user",
      (helper_turn != NULL) ? helper_turn->render_role : "harness",
      (helper_turn != NULL) ? helper_turn->prompt_message_key : NULL,
      NULL);
    messages[message_index].content =
      ((helper_turn != NULL) && (helper_turn->prompt != NULL)) ?
        helper_turn->prompt :
        ((guidance != NULL) ? guidance->learning_summary_prompt : "");
    message_index++;
  }

  if (has_learning_summary_turn) {
    strappy_assistant_storage_message_set_turn(
      &messages[message_index],
      helper_turn,
      "assistant",
      "assistant",
      "assistant",
      (helper_turn != NULL) ? helper_turn->assistant_message_key : NULL,
      NULL);
    messages[message_index].content =
      guidance->learning_summary_completion_text;
    if (learning_summary_final_result != NULL) {
      messages[message_index].model = learning_summary_final_result->model;
      messages[message_index].http_status =
        learning_summary_final_result->http_status;
      messages[message_index].metadata_json =
        learning_summary_final_result->metadata_json;
      messages[message_index].reasoning =
        learning_summary_final_result->reasoning_text;
    }
    messages[message_index].message_json = learning_summary_completion_json;
    message_index++;
  }

  ok = strappy_db_append_message_sequence_to_session(session_db_path,
                                                     session_id,
                                                     prompt,
                                                     final_result->response_text,
                                                     final_result->model,
                                                     final_result->http_status,
                                                     messages,
                                                     message_count,
                                                     error_out);

  free(learning_summary_completion_json);
  free(messages);
  return ok;
}

static int strappy_assistant_run_tool_sequence(
  const strappy_config *config,
  const char *session_db_path,
  const strappy_assistant_guidance *guidance,
  const strappy_assistant_turn_spec *turn,
  int store_prompt_message,
  int post_final,
  const strappy_assistant_request_messages *request_messages,
  const strappy_chat_result *tool_request_result,
  int stream_tool_events,
  int stream_model_responses,
  int emit_complete_model_events,
  strappy_chat_stream_callback callback,
  void *callback_data,
  strappy_assistant_tool_sequence *sequence,
  int *did_run_out,
  char **error_out)
{
  const strappy_assistant_request_messages *current_request;
  strappy_assistant_request_messages followup_request;
  const strappy_chat_result *current_result;
  int stored_prompt_message;

  if (did_run_out != NULL) {
    *did_run_out = 0;
  }

  if ((guidance == NULL) ||
      (guidance->tool_round_limit_message == NULL)) {
    strappy_set_error(error_out, "Assistant tool guidance is missing.");
    return 0;
  }

  if (sequence == NULL) {
    strappy_set_error(error_out, "Tool sequence request is incomplete.");
    return 0;
  }

  current_request = request_messages;
  current_result = tool_request_result;
  stored_prompt_message = 0;
  strappy_assistant_request_messages_init(&followup_request);

  for (;;) {
    strappy_assistant_tool_round *round;
    strappy_chat_result *next_result;
    int has_tool_calls;

    if ((current_result != NULL) && current_result->cancelled) {
      sequence->final_result = (strappy_chat_result *)current_result;
      return 1;
    }

    if (!strappy_assistant_result_has_tool_calls(current_result,
                                                &has_tool_calls,
                                                error_out)) {
      return 0;
    }

    if (!has_tool_calls) {
      sequence->final_result = (strappy_chat_result *)current_result;
      return 1;
    }

    if (stream_model_responses &&
        !strappy_assistant_emit_content_retracted_event(turn,
                                                        callback,
                                                        callback_data,
                                                        error_out)) {
      return 0;
    }

    if (sequence->round_count >= STRAPPY_ASSISTANT_MAX_TOOL_ROUNDS) {
      next_result = &sequence->results[sequence->round_count];
      if (!strappy_assistant_set_tool_limit_result(next_result,
                                                   current_result,
                                                   guidance,
                                                   error_out)) {
        return 0;
      }
      if ((stream_model_responses || emit_complete_model_events) &&
          !strappy_assistant_emit_complete_result_events(turn,
                                                         next_result,
                                                         0,
                                                         callback,
                                                         callback_data,
                                                         error_out)) {
        return 0;
      }
      sequence->final_result = next_result;
      return 1;
    }

    round = &sequence->rounds[sequence->round_count];
    if (!strappy_assistant_build_tool_round(session_db_path,
                                            config->guidance_resource_dir,
                                            guidance,
                                            turn,
                                            sequence->round_count,
                                            store_prompt_message &&
                                              !stored_prompt_message,
                                            post_final,
                                            current_request,
                                            current_result,
                                            stream_tool_events,
                                            callback,
                                            callback_data,
                                            1,
                                            round,
                                            error_out)) {
      return 0;
    }

    if ((round->followup_messages == NULL) || (round->followup_count == 0U)) {
      sequence->final_result = (strappy_chat_result *)current_result;
      return 1;
    }

    next_result = &sequence->results[sequence->round_count];
    sequence->round_count++;
    if (round->store_prompt_message) {
      stored_prompt_message = 1;
    }
    if (did_run_out != NULL) {
      *did_run_out = 1;
    }

    if (stream_model_responses) {
      strappy_assistant_stream_turn_context turn_context;
      strappy_chat_stream_callback stream_callback;
      void *stream_callback_data;

      stream_callback = callback;
      stream_callback_data = callback_data;
      if ((turn != NULL) && (callback != NULL)) {
        turn_context.callback = callback;
        turn_context.callback_data = callback_data;
        turn_context.turn = turn;
        stream_callback = strappy_assistant_stream_turn_callback;
        stream_callback_data = &turn_context;
      }

      if (!strappy_client_stream_messages(config,
                                          round->followup_messages,
                                          round->followup_count,
                                          next_result,
                                          stream_callback,
                                          stream_callback_data,
                                          error_out)) {
        return 0;
      }
      if (next_result->cancelled) {
        sequence->final_result = next_result;
        return 1;
      }
    } else if (!strappy_client_send_messages(config,
                                             round->followup_messages,
                                             round->followup_count,
                                             next_result,
                                             error_out)) {
      return 0;
    } else if (emit_complete_model_events) {
      int next_has_tool_calls;

      next_has_tool_calls = 0;
      if (!strappy_assistant_result_has_tool_calls(next_result,
                                                   &next_has_tool_calls,
                                                   error_out) ||
          !strappy_assistant_emit_complete_result_events(turn,
                                                         next_result,
                                                         next_has_tool_calls,
                                                         callback,
                                                         callback_data,
                                                         error_out)) {
        return 0;
      }
    }

    followup_request.messages = round->followup_messages;
    followup_request.count = round->followup_count;
    current_request = &followup_request;
    current_result = next_result;
  }
}

static int strappy_assistant_run_learning_summary(
  const strappy_config *config,
  const char *session_db_path,
  const strappy_assistant_guidance *guidance,
  const strappy_assistant_turn_spec *helper_turn,
  const strappy_assistant_request_messages *request_messages,
  strappy_assistant_tool_sequence *sequence,
  const strappy_chat_result *final_result,
  int original_did_run_tool_round,
  int should_stream,
  strappy_chat_stream_callback callback,
  void *callback_data,
  int *did_run_tool_round_in_out,
  char **error_out)
{
  const strappy_chat_message *base_messages;
  strappy_assistant_request_messages helper_request;
  size_t base_count;
  int helper_has_tool_calls;
  int did_run_helper_tool_round;
  int emit_events;
  const char *helper_tool_allowlist[3];
  strappy_config helper_config;
  strappy_assistant_stream_turn_context turn_context;

  if ((config == NULL) || (helper_turn == NULL) ||
      (guidance == NULL) ||
      (guidance->learning_summary_prompt == NULL) ||
      (guidance->learning_summary_completion_text == NULL) ||
      (sequence == NULL) || (final_result == NULL)) {
    strappy_set_error(error_out, "Learning summary request is incomplete.");
    return 0;
  }

  if ((session_db_path == NULL) || (session_db_path[0] == '\0')) {
    return 1;
  }

  /* This flag belongs to the current user turn; older session tool rows do
     not participate in deciding whether this harness turn is worth running. */
  if (!original_did_run_tool_round) {
    return 1;
  }

  if (strappy_assistant_tool_sequence_has_tool(
        sequence,
        STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE)) {
    return 1;
  }

  if (sequence->round_count >= STRAPPY_ASSISTANT_MAX_TOOL_ROUNDS) {
    return 1;
  }

  base_messages = NULL;
  base_count = 0U;
  if (!strappy_assistant_final_request_context(request_messages,
                                               sequence,
                                               original_did_run_tool_round,
                                               &base_messages,
                                               &base_count,
                                               error_out)) {
    return 0;
  }

  strappy_assistant_request_messages_init(&helper_request);
  if (!strappy_assistant_build_learning_summary_request(base_messages,
                                                    base_count,
                                                    final_result,
                                                    guidance,
                                                    &helper_request,
                                                    error_out)) {
    return 0;
  }

  helper_tool_allowlist[0] = STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE;
  helper_tool_allowlist[1] = STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER;
  helper_tool_allowlist[2] = STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER;
  helper_config = *config;
  helper_config.tool_allowlist = helper_tool_allowlist;
  helper_config.tool_allowlist_count =
    sizeof(helper_tool_allowlist) / sizeof(helper_tool_allowlist[0]);
  emit_events = (callback != NULL) ? 1 : 0;

  if (emit_events &&
      !strappy_assistant_emit_turn_event(STRAPPY_CHAT_STREAM_EVENT_TURN_STARTED,
                                         helper_turn,
                                         helper_turn->prompt,
                                         callback,
                                         callback_data,
                                         error_out)) {
    strappy_assistant_request_messages_destroy(&helper_request);
    return 0;
  }
  if (emit_events &&
      !strappy_assistant_emit_assistant_placeholder_event(helper_turn,
                                                         callback,
                                                         callback_data,
                                                         error_out)) {
    strappy_assistant_request_messages_destroy(&helper_request);
    return 0;
  }

  sequence->learning_summary_final_result = NULL;
  strappy_chat_result_destroy(&sequence->learning_summary_result);
  if (should_stream) {
    turn_context.callback = callback;
    turn_context.callback_data = callback_data;
    turn_context.turn = helper_turn;
    if (!strappy_client_stream_messages(&helper_config,
                                        helper_request.messages,
                                        helper_request.count,
                                        &sequence->learning_summary_result,
                                        strappy_assistant_stream_turn_callback,
                                        &turn_context,
                                        error_out)) {
      strappy_assistant_request_messages_destroy(&helper_request);
      return 0;
    }
  } else {
    if (!strappy_client_send_messages(&helper_config,
                                      helper_request.messages,
                                      helper_request.count,
                                      &sequence->learning_summary_result,
                                      error_out)) {
      strappy_assistant_request_messages_destroy(&helper_request);
      return 0;
    }
  }

  helper_has_tool_calls = 0;
  if (!strappy_assistant_result_has_tool_calls(&sequence->learning_summary_result,
                                               &helper_has_tool_calls,
                                               error_out)) {
    strappy_assistant_request_messages_destroy(&helper_request);
    return 0;
  }

  if (!should_stream &&
      !strappy_assistant_emit_complete_result_events(helper_turn,
                                                     &sequence->learning_summary_result,
                                                     helper_has_tool_calls,
                                                     callback,
                                                     callback_data,
                                                     error_out)) {
    strappy_assistant_request_messages_destroy(&helper_request);
    return 0;
  }

  did_run_helper_tool_round = 0;
  if (!strappy_assistant_run_tool_sequence(&helper_config,
                                           session_db_path,
                                           guidance,
                                           helper_turn,
                                           1,
                                           1,
                                           &helper_request,
                                           &sequence->learning_summary_result,
                                           emit_events && helper_has_tool_calls,
                                           should_stream && helper_has_tool_calls,
                                           (!should_stream && emit_events &&
                                            helper_has_tool_calls),
                                           callback,
                                           callback_data,
                                           sequence,
                                           &did_run_helper_tool_round,
                                           error_out)) {
    strappy_assistant_request_messages_destroy(&helper_request);
    return 0;
  }

  if (emit_events &&
      !strappy_assistant_emit_turn_event(STRAPPY_CHAT_STREAM_EVENT_TURN_FINISHED,
                                         helper_turn,
                                         guidance->learning_summary_completion_text,
                                         callback,
                                         callback_data,
                                         error_out)) {
    strappy_assistant_request_messages_destroy(&helper_request);
    return 0;
  }

  sequence->learning_summary_final_result = did_run_helper_tool_round ?
    sequence->final_result : &sequence->learning_summary_result;
  sequence->final_result = (strappy_chat_result *)final_result;
  if (did_run_helper_tool_round && (did_run_tool_round_in_out != NULL)) {
    *did_run_tool_round_in_out = 1;
  }

  strappy_assistant_request_messages_destroy(&helper_request);
  return 1;
}

static int strappy_assistant_apply_selected_model(strappy_config *config,
                                                  const char *session_db_path,
                                                  long long session_id,
                                                  char **error_out)
{
  char *selected_model;
  int ok;

  if (config == NULL) {
    strappy_set_error(error_out, "Assistant configuration is missing.");
    return 0;
  }

  selected_model = NULL;
  if (!strappy_db_get_session_model(session_db_path,
                                    session_id,
                                    &selected_model,
                                    error_out)) {
    return 0;
  }

  ok = 1;
  if ((selected_model != NULL) && (selected_model[0] != '\0')) {
    ok = strappy_config_set_api_model(config, selected_model, error_out);
  }
  free(selected_model);
  return ok;
}

static char *strappy_assistant_send_prompt_for_session_internal(
  const char *prompt,
  const char *env_path,
  const char *system_prompt_template_path,
  const char *session_db_path,
  long long session_id,
  strappy_chat_stream_callback callback,
  void *callback_data,
  char **error_out)
{
  strappy_config config;
  strappy_assistant_guidance guidance;
  strappy_chat_result result;
  strappy_session_message_record_list message_list;
  strappy_assistant_request_messages request_messages;
  strappy_assistant_tool_sequence tool_sequence;
  strappy_assistant_turn_keys main_keys;
  strappy_assistant_turn_keys helper_keys;
  strappy_assistant_turn_spec main_turn;
  strappy_assistant_turn_spec helper_turn;
  strappy_assistant_stream_store_context store_context;
  strappy_chat_stream_callback event_callback;
  void *event_callback_data;
  const strappy_chat_result *final_result;
  char *response;
  int initial_has_tool_calls;
  int did_run_tool_round;

  if ((prompt == NULL) || (prompt[0] == '\0')) {
    strappy_set_error(error_out, "Prompt is empty.");
    return NULL;
  }

  if (session_id <= 0) {
    strappy_set_error(error_out, "Session id is not valid.");
    return NULL;
  }

  strappy_config_init(&config);
  strappy_assistant_guidance_init(&guidance);
  strappy_chat_result_init(&result);
  strappy_session_message_record_list_init(&message_list);
  strappy_assistant_request_messages_init(&request_messages);
  strappy_assistant_tool_sequence_init(&tool_sequence);
  strappy_assistant_turn_keys_init(&main_keys);
  strappy_assistant_turn_keys_init(&helper_keys);
  memset(&main_turn, 0, sizeof(main_turn));
  memset(&helper_turn, 0, sizeof(helper_turn));
  memset(&store_context, 0, sizeof(store_context));
  event_callback = callback;
  event_callback_data = callback_data;
  if (callback != NULL) {
    store_context.session_db_path = session_db_path;
    store_context.session_id = session_id;
    store_context.callback = callback;
    store_context.callback_data = callback_data;
    store_context.error_out = error_out;
    event_callback = strappy_assistant_stream_store_callback;
    event_callback_data = &store_context;
  }

  if (!strappy_config_load(&config, env_path, error_out)) {
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_assistant_apply_selected_model(&config,
                                              session_db_path,
                                              session_id,
                                              error_out)) {
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_db_list_session_context_messages(session_db_path,
                                                session_id,
                                                &message_list,
                                                error_out)) {
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_assistant_prepare_request_messages(prompt,
                                                  system_prompt_template_path,
                                                  &config,
                                                  &message_list,
                                                  &request_messages,
                                                  error_out)) {
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_assistant_guidance_load(&guidance,
                                       config.guidance_resource_dir,
                                       error_out)) {
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  did_run_tool_round = 0;
  if (!strappy_assistant_make_default_turns(prompt,
                                            session_id,
                                            &guidance,
                                            &main_keys,
                                            &helper_keys,
                                            &main_turn,
                                            &helper_turn,
                                            error_out)) {
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  if ((event_callback != NULL) &&
      !strappy_assistant_emit_turn_event(STRAPPY_CHAT_STREAM_EVENT_TURN_STARTED,
                                         &main_turn,
                                         prompt,
                                         event_callback,
                                         event_callback_data,
                                         error_out)) {
    strappy_assistant_turn_keys_destroy(&helper_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  if ((event_callback != NULL) &&
      !strappy_assistant_emit_assistant_placeholder_event(&main_turn,
                                                         event_callback,
                                                         event_callback_data,
                                                         error_out)) {
    strappy_assistant_turn_keys_destroy(&helper_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_client_send_messages(&config,
                                    request_messages.messages,
                                    request_messages.count,
                                    &result,
                                    error_out)) {
    if (event_callback != NULL) {
      strappy_assistant_try_store_turn_error(session_db_path,
                                             session_id,
                                             &main_turn,
                                             &result,
                                             error_out);
    }
    strappy_assistant_turn_keys_destroy(&helper_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  initial_has_tool_calls = 0;
  if (!strappy_assistant_result_has_tool_calls(&result,
                                               &initial_has_tool_calls,
                                               error_out) ||
      !strappy_assistant_emit_complete_result_events(&main_turn,
                                                     &result,
                                                     initial_has_tool_calls,
                                                     event_callback,
                                                     event_callback_data,
                                                     error_out)) {
    if (event_callback != NULL) {
      strappy_assistant_try_store_turn_error(session_db_path,
                                             session_id,
                                             &main_turn,
                                             &result,
                                             error_out);
    }
    strappy_assistant_turn_keys_destroy(&helper_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }
  if (!strappy_assistant_stream_flush_pending(&store_context, 1)) {
    if (event_callback != NULL) {
      strappy_assistant_try_store_turn_error(session_db_path,
                                             session_id,
                                             &main_turn,
                                             &result,
                                             error_out);
    }
    strappy_assistant_turn_keys_destroy(&helper_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_assistant_run_tool_sequence(&config,
                                           session_db_path,
                                           &guidance,
                                           &main_turn,
                                           0,
                                           0,
                                           &request_messages,
                                           &result,
                                           (event_callback != NULL) ? 1 : 0,
                                           0,
                                           (event_callback != NULL) ? 1 : 0,
                                           event_callback,
                                           event_callback_data,
                                           &tool_sequence,
                                           &did_run_tool_round,
                                           error_out)) {
    if (event_callback != NULL) {
      strappy_assistant_try_store_turn_error(session_db_path,
                                             session_id,
                                             &main_turn,
                                             &result,
                                             error_out);
    }
    strappy_assistant_turn_keys_destroy(&helper_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }
  if (!strappy_assistant_stream_flush_pending(&store_context, 1)) {
    if (event_callback != NULL) {
      strappy_assistant_try_store_turn_error(session_db_path,
                                             session_id,
                                             &main_turn,
                                             &result,
                                             error_out);
    }
    strappy_assistant_turn_keys_destroy(&helper_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  final_result = did_run_tool_round ? tool_sequence.final_result : &result;
  if (!strappy_assistant_validate_final_result(final_result, error_out)) {
    if (event_callback != NULL) {
      strappy_assistant_try_store_turn_error(session_db_path,
                                             session_id,
                                             &main_turn,
                                             final_result,
                                             error_out);
    }
    strappy_assistant_turn_keys_destroy(&helper_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }
  if ((event_callback != NULL) && !final_result->cancelled &&
      !strappy_assistant_emit_turn_event(STRAPPY_CHAT_STREAM_EVENT_TURN_FINISHED,
                                         &main_turn,
                                         final_result->response_text,
                                         event_callback,
                                         event_callback_data,
                                         error_out)) {
    strappy_assistant_try_store_turn_error(session_db_path,
                                           session_id,
                                           &main_turn,
                                           final_result,
                                           error_out);
    strappy_assistant_turn_keys_destroy(&helper_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }
  if (!strappy_assistant_stream_flush_pending(&store_context, 1)) {
    if (event_callback != NULL) {
      strappy_assistant_try_store_turn_error(session_db_path,
                                             session_id,
                                             &main_turn,
                                             final_result,
                                             error_out);
    }
    strappy_assistant_turn_keys_destroy(&helper_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }
  if ((event_callback != NULL) && !final_result->cancelled &&
      !strappy_assistant_store_turn_result(session_db_path,
                                           session_id,
                                           &main_turn,
                                           final_result,
                                           error_out)) {
    strappy_assistant_turn_keys_destroy(&helper_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_assistant_run_learning_summary(&config,
                                          session_db_path,
                                          &guidance,
                                          &helper_turn,
                                          &request_messages,
                                          &tool_sequence,
                                          final_result,
                                          did_run_tool_round,
                                          0,
                                          event_callback,
                                          event_callback_data,
                                          &did_run_tool_round,
                                          error_out)) {
    strappy_assistant_turn_keys_destroy(&helper_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }
  if (!strappy_assistant_stream_flush_pending(&store_context, 1)) {
    strappy_assistant_turn_keys_destroy(&helper_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  strappy_assistant_request_messages_destroy(&request_messages);
  strappy_session_message_record_list_destroy(&message_list);

  final_result = did_run_tool_round ? tool_sequence.final_result : &result;
  if (!strappy_assistant_store_tool_sequence(session_db_path,
                                            session_id,
                                            prompt,
                                            &guidance,
                                            &main_turn,
                                            &helper_turn,
                                            &tool_sequence,
                                            final_result,
                                            error_out)) {
    strappy_assistant_turn_keys_destroy(&helper_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (did_run_tool_round) {
    response = tool_sequence.final_result->response_text;
    tool_sequence.final_result->response_text = NULL;
  } else {
    response = result.response_text;
    result.response_text = NULL;
  }

  strappy_assistant_tool_sequence_destroy(&tool_sequence);
  strappy_assistant_turn_keys_destroy(&helper_keys);
  strappy_assistant_turn_keys_destroy(&main_keys);
  strappy_chat_result_destroy(&result);
  strappy_assistant_guidance_destroy(&guidance);
  strappy_config_destroy(&config);
  return response;
}

static char *strappy_assistant_stream_prompt_for_session_internal(
  const char *prompt,
  const char *env_path,
  const char *system_prompt_template_path,
  const char *session_db_path,
  long long session_id,
  strappy_chat_stream_callback callback,
  void *callback_data,
  char **error_out)
{
  strappy_config config;
  strappy_assistant_guidance guidance;
  strappy_chat_result result;
  strappy_session_message_record_list message_list;
  strappy_assistant_request_messages request_messages;
  strappy_assistant_tool_sequence tool_sequence;
  strappy_assistant_turn_keys main_keys;
  strappy_assistant_turn_keys helper_keys;
  strappy_assistant_turn_spec main_turn;
  strappy_assistant_turn_spec helper_turn;
  strappy_assistant_stream_turn_context turn_context;
  strappy_assistant_stream_store_context store_context;
  const strappy_chat_result *final_result;
  char *response;
  int did_run_tool_round;

  if ((prompt == NULL) || (prompt[0] == '\0')) {
    strappy_set_error(error_out, "Prompt is empty.");
    return NULL;
  }

  if (session_id <= 0) {
    strappy_set_error(error_out, "Session id is not valid.");
    return NULL;
  }

  strappy_config_init(&config);
  strappy_assistant_guidance_init(&guidance);
  strappy_chat_result_init(&result);
  strappy_session_message_record_list_init(&message_list);
  strappy_assistant_request_messages_init(&request_messages);
  strappy_assistant_tool_sequence_init(&tool_sequence);
  strappy_assistant_turn_keys_init(&main_keys);
  strappy_assistant_turn_keys_init(&helper_keys);
  memset(&main_turn, 0, sizeof(main_turn));
  memset(&helper_turn, 0, sizeof(helper_turn));
  memset(&store_context, 0, sizeof(store_context));

  if (!strappy_config_load(&config, env_path, error_out)) {
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_assistant_apply_selected_model(&config,
                                              session_db_path,
                                              session_id,
                                              error_out)) {
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_db_list_session_context_messages(session_db_path,
                                                session_id,
                                                &message_list,
                                                error_out)) {
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_assistant_prepare_request_messages(prompt,
                                                  system_prompt_template_path,
                                                  &config,
                                                  &message_list,
                                                  &request_messages,
                                                  error_out)) {
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_assistant_guidance_load(&guidance,
                                       config.guidance_resource_dir,
                                       error_out)) {
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_assistant_make_default_turns(prompt,
                                            session_id,
                                            &guidance,
                                            &main_keys,
                                            &helper_keys,
                                            &main_turn,
                                            &helper_turn,
                                            error_out)) {
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  store_context.session_db_path = session_db_path;
  store_context.session_id = session_id;
  store_context.callback = callback;
  store_context.callback_data = callback_data;
  store_context.error_out = error_out;

  if (!strappy_assistant_emit_turn_event(STRAPPY_CHAT_STREAM_EVENT_TURN_STARTED,
                                         &main_turn,
                                         prompt,
                                         strappy_assistant_stream_store_callback,
                                         &store_context,
                                         error_out)) {
    strappy_assistant_turn_keys_destroy(&helper_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_assistant_emit_assistant_placeholder_event(
        &main_turn,
        strappy_assistant_stream_store_callback,
        &store_context,
        error_out)) {
    strappy_assistant_turn_keys_destroy(&helper_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  turn_context.callback = strappy_assistant_stream_store_callback;
  turn_context.callback_data = &store_context;
  turn_context.turn = &main_turn;
  if (!strappy_client_stream_messages(&config,
                                      request_messages.messages,
                                      request_messages.count,
                                      &result,
                                      strappy_assistant_stream_turn_callback,
                                      &turn_context,
                                      error_out)) {
    strappy_assistant_try_store_turn_error(session_db_path,
                                           session_id,
                                           &main_turn,
                                           &result,
                                           error_out);
    strappy_assistant_turn_keys_destroy(&helper_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }
  if (!strappy_assistant_stream_flush_pending(&store_context, 1)) {
    strappy_assistant_try_store_turn_error(session_db_path,
                                           session_id,
                                           &main_turn,
                                           &result,
                                           error_out);
    strappy_assistant_turn_keys_destroy(&helper_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  did_run_tool_round = 0;
  if (!result.cancelled &&
      !strappy_assistant_run_tool_sequence(&config,
                                           session_db_path,
                                           &guidance,
                                           &main_turn,
                                           0,
                                           0,
                                           &request_messages,
                                           &result,
                                           1,
                                           1,
                                           0,
                                           strappy_assistant_stream_store_callback,
                                           &store_context,
                                           &tool_sequence,
                                           &did_run_tool_round,
                                           error_out)) {
    strappy_assistant_try_store_turn_error(session_db_path,
                                           session_id,
                                           &main_turn,
                                           &result,
                                           error_out);
    strappy_assistant_turn_keys_destroy(&helper_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }
  if (!strappy_assistant_stream_flush_pending(&store_context, 1)) {
    strappy_assistant_try_store_turn_error(session_db_path,
                                           session_id,
                                           &main_turn,
                                           &result,
                                           error_out);
    strappy_assistant_turn_keys_destroy(&helper_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  final_result = did_run_tool_round ? tool_sequence.final_result : &result;
  if (!strappy_assistant_validate_final_result(final_result, error_out)) {
    strappy_assistant_try_store_turn_error(session_db_path,
                                           session_id,
                                           &main_turn,
                                           final_result,
                                           error_out);
    strappy_assistant_turn_keys_destroy(&helper_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }
  if (!final_result->cancelled) {
    if (!strappy_assistant_emit_turn_event(STRAPPY_CHAT_STREAM_EVENT_TURN_FINISHED,
                                           &main_turn,
                                           final_result->response_text,
                                           strappy_assistant_stream_store_callback,
                                           &store_context,
                                           error_out)) {
      strappy_assistant_try_store_turn_error(session_db_path,
                                             session_id,
                                             &main_turn,
                                             final_result,
                                             error_out);
      strappy_assistant_turn_keys_destroy(&helper_keys);
      strappy_assistant_turn_keys_destroy(&main_keys);
      strappy_assistant_tool_sequence_destroy(&tool_sequence);
      strappy_assistant_request_messages_destroy(&request_messages);
      strappy_session_message_record_list_destroy(&message_list);
      strappy_chat_result_destroy(&result);
      strappy_assistant_guidance_destroy(&guidance);
      strappy_config_destroy(&config);
      return NULL;
    }

    if (!strappy_assistant_store_turn_result(session_db_path,
                                             session_id,
                                             &main_turn,
                                             final_result,
                                             error_out)) {
      strappy_assistant_turn_keys_destroy(&helper_keys);
      strappy_assistant_turn_keys_destroy(&main_keys);
      strappy_assistant_tool_sequence_destroy(&tool_sequence);
      strappy_assistant_request_messages_destroy(&request_messages);
      strappy_session_message_record_list_destroy(&message_list);
      strappy_chat_result_destroy(&result);
      strappy_assistant_guidance_destroy(&guidance);
      strappy_config_destroy(&config);
      return NULL;
    }

    if (!strappy_assistant_run_learning_summary(&config,
                                            session_db_path,
                                            &guidance,
                                            &helper_turn,
                                            &request_messages,
                                            &tool_sequence,
                                            final_result,
                                            did_run_tool_round,
                                            1,
                                            strappy_assistant_stream_store_callback,
                                            &store_context,
                                            &did_run_tool_round,
                                            error_out)) {
      strappy_assistant_turn_keys_destroy(&helper_keys);
      strappy_assistant_turn_keys_destroy(&main_keys);
      strappy_assistant_tool_sequence_destroy(&tool_sequence);
      strappy_assistant_request_messages_destroy(&request_messages);
      strappy_session_message_record_list_destroy(&message_list);
      strappy_chat_result_destroy(&result);
      strappy_assistant_guidance_destroy(&guidance);
      strappy_config_destroy(&config);
      return NULL;
    }
  }
  if (!strappy_assistant_stream_flush_pending(&store_context, 1)) {
    strappy_assistant_turn_keys_destroy(&helper_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  strappy_assistant_request_messages_destroy(&request_messages);
  strappy_session_message_record_list_destroy(&message_list);

  final_result = did_run_tool_round ? tool_sequence.final_result : &result;
  if (!strappy_assistant_store_tool_sequence(session_db_path,
                                            session_id,
                                            prompt,
                                            &guidance,
                                            &main_turn,
                                            &helper_turn,
                                            &tool_sequence,
                                            final_result,
                                            error_out)) {
    strappy_assistant_turn_keys_destroy(&helper_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_chat_result_destroy(&result);
    strappy_assistant_guidance_destroy(&guidance);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (did_run_tool_round) {
    response = tool_sequence.final_result->response_text;
    tool_sequence.final_result->response_text = NULL;
  } else {
    response = result.response_text;
    result.response_text = NULL;
  }

  strappy_assistant_tool_sequence_destroy(&tool_sequence);
  strappy_assistant_turn_keys_destroy(&helper_keys);
  strappy_assistant_turn_keys_destroy(&main_keys);
  strappy_chat_result_destroy(&result);
  strappy_assistant_guidance_destroy(&guidance);
  strappy_config_destroy(&config);
  return response;
}

char *strappy_assistant_send_prompt_for_session_and_store(const char *prompt,
                                                         const char *env_path,
                                                         const char *system_prompt_template_path,
                                                         const char *session_db_path,
                                                         long long session_id,
                                                         char **error_out)
{
  return strappy_assistant_send_prompt_for_session_internal(prompt,
                                                           env_path,
                                                           system_prompt_template_path,
                                                           session_db_path,
                                                           session_id,
                                                           NULL,
                                                           NULL,
                                                           error_out);
}

char *strappy_assistant_send_prompt_for_session_and_store_with_events(
  const char *prompt,
  const char *env_path,
  const char *system_prompt_template_path,
  const char *session_db_path,
  long long session_id,
  strappy_chat_stream_callback callback,
  void *callback_data,
  char **error_out)
{
  return strappy_assistant_send_prompt_for_session_internal(prompt,
                                                           env_path,
                                                           system_prompt_template_path,
                                                           session_db_path,
                                                           session_id,
                                                           callback,
                                                           callback_data,
                                                           error_out);
}

char *strappy_assistant_stream_prompt_for_session_and_store(
  const char *prompt,
  const char *env_path,
  const char *system_prompt_template_path,
  const char *session_db_path,
  long long session_id,
  strappy_chat_stream_callback callback,
  void *callback_data,
  char **error_out)
{
  return strappy_assistant_stream_prompt_for_session_internal(prompt,
                                                             env_path,
                                                             system_prompt_template_path,
                                                             session_db_path,
                                                             session_id,
                                                             callback,
                                                             callback_data,
                                                             error_out);
}
