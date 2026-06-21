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
  STRAPPY_ASSISTANT_TOOL_POLICY_MEMORY_AUDIT = 1
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
  strappy_chat_result memory_audit_result;
  size_t round_count;
  strappy_chat_result *final_result;
  strappy_chat_result *memory_audit_final_result;
} strappy_assistant_tool_sequence;

typedef struct strappy_assistant_stream_turn_context {
  strappy_chat_stream_callback callback;
  void *callback_data;
  const strappy_assistant_turn_spec *turn;
} strappy_assistant_stream_turn_context;

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
  strappy_chat_result_init(&sequence->memory_audit_result);

  sequence->round_count = 0U;
  sequence->final_result = NULL;
  sequence->memory_audit_final_result = NULL;
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
  strappy_chat_result_destroy(&sequence->memory_audit_result);

  sequence->round_count = 0U;
  sequence->final_result = NULL;
  sequence->memory_audit_final_result = NULL;
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
  const char *tool_call_id,
  const char *tool_name,
  const char *arguments_json,
  const char *result_json,
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
    event.render_role = "tool";
    event.api_role = "tool";
    event.kind = (type == STRAPPY_CHAT_STREAM_EVENT_TOOL_CALL) ?
      "tool_call" : "tool_result";
    event.target_message_key = turn->assistant_message_key;
  }
  event.tool_call_id = tool_call_id;
  event.tool_name = tool_name;
  event.arguments_json = arguments_json;
  event.result_json = result_json;
  if (!callback(&event, callback_data)) {
    strappy_set_error(error_out, "Stream callback rejected tool event.");
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

static char *strappy_assistant_create_basic_message_json(const char *role,
                                                         const char *content,
                                                         char **error_out);
static const char *strappy_assistant_memory_audit_prompt(void);

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
  strappy_assistant_turn_keys *main_keys,
  strappy_assistant_turn_keys *audit_keys,
  strappy_assistant_turn_spec *main_turn,
  strappy_assistant_turn_spec *audit_turn,
  char **error_out)
{
  if ((main_keys == NULL) || (audit_keys == NULL) ||
      (main_turn == NULL) || (audit_turn == NULL)) {
    strappy_set_error(error_out, "Turn outputs are missing.");
    return 0;
  }

  if (!strappy_assistant_turn_keys_make(main_keys, "user-turn", error_out) ||
      !strappy_assistant_turn_keys_make(audit_keys, "harness-turn", error_out)) {
    strappy_assistant_turn_keys_destroy(audit_keys);
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
  strappy_assistant_turn_spec_set(audit_turn,
                                  audit_keys,
                                  session_id,
                                  main_keys->prompt_group_key,
                                  "harness",
                                  "harness",
                                  "user",
                                  "omit",
                                  strappy_assistant_memory_audit_prompt(),
                                  STRAPPY_ASSISTANT_TOOL_POLICY_MEMORY_AUDIT);
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
  if (policy == STRAPPY_ASSISTANT_TOOL_POLICY_MEMORY_AUDIT) {
    return ((tool_name != NULL) &&
            ((strcmp(tool_name, STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE) == 0) ||
             (strcmp(tool_name, STRAPPY_TOOL_HELPER_USER_INFO_REMEMBER) == 0) ||
             (strcmp(tool_name, STRAPPY_TOOL_HELPER_DATABASE_INFO_REMEMBER) == 0))) ? 1 : 0;
  }

  return 1;
}

static const char *strappy_assistant_memory_audit_prompt(void)
{
  return
    "Learning Summary:\n"
    "- Do not revise, summarize, or add commentary to the final answer you just "
    "wrote.\n"
    "- Call helper_session_name_write once with the shortest useful title "
    "summarizing the user's request. Use only a few words, no quotes, and no "
    "punctuation unless necessary. The tool only writes if the session is "
    "currently untitled.\n"
    "- If the user prompt, tool results, database queries, or what you learned "
    "while answering revealed any new small, stable, future-useful user fact, "
    "call helper_user_info_remember now.\n"
    "- If tool results or database queries verified any reusable database "
    "schema, join, timestamp, flag, caveat, or query-pattern hint, call "
    "helper_database_info_remember now.\n"
    "- Store only information that is explicit or strongly implied and useful "
    "later. Never store secrets, credentials, sensitive identifiers, long "
    "copied content, or private row contents.\n"
    "- If there is nothing worth remembering beyond the session title, do not "
    "call a remember tool.\n"
    "After any remember tool calls complete, respond with exactly: "
    "Learning Summary Complete";
}

static const char *strappy_assistant_memory_audit_completion_text(void)
{
  return "Learning Summary Complete";
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

static int strappy_assistant_build_memory_audit_request(
  const strappy_chat_message *base_messages,
  size_t base_count,
  const strappy_chat_result *final_result,
  strappy_assistant_request_messages *audit_request,
  char **error_out)
{
  strappy_chat_message *messages;
  size_t request_count;

  if (audit_request == NULL) {
    strappy_set_error(error_out, "Learning summary request output is missing.");
    return 0;
  }
  strappy_assistant_request_messages_init(audit_request);

  if ((base_messages == NULL) || (base_count == 0U) ||
      (final_result == NULL) ||
      (final_result->response_text == NULL)) {
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
  messages[base_count + 1U].content =
    strappy_assistant_memory_audit_prompt();
  messages[base_count + 1U].message_json = NULL;

  audit_request->messages = messages;
  audit_request->count = request_count;
  audit_request->system_prompt = NULL;
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
                                                 const char *message)
{
  cJSON *root;
  char *json;

  root = cJSON_CreateObject();
  if (root == NULL) {
    return NULL;
  }

  if (((tool_name != NULL) &&
       (cJSON_AddStringToObject(root, "tool_name", tool_name) == NULL)) ||
      (cJSON_AddStringToObject(root,
                               "error",
                               (message != NULL) ? message : "Tool failed.") == NULL)) {
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
    if (should_stream &&
        !strappy_assistant_emit_tool_event(STRAPPY_CHAT_STREAM_EVENT_TOOL_CALL,
                                           turn,
                                           round->tool_call_ids[index],
                                           round->tool_names[index],
                                           round->tool_arguments[index],
                                           NULL,
                                           callback,
                                           callback_data,
                                           error_out)) {
      cJSON_Delete(root);
      strappy_assistant_tool_round_destroy(round);
      return 0;
    }

    if (!strappy_assistant_tool_is_allowed((turn != NULL) ?
                                             turn->tool_policy :
                                             STRAPPY_ASSISTANT_TOOL_POLICY_NORMAL,
                                           round->tool_names[index])) {
      had_tool_error = 1;
      output = strappy_assistant_tool_error_result(
        round->tool_names[index],
        "Tool is not allowed for this turn.");
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
                                                   tool_error);
      free(tool_error);
      tool_error = NULL;
      if (output == NULL) {
        cJSON_Delete(root);
        strappy_assistant_tool_round_destroy(round);
        strappy_set_error(error_out, "Could not build tool error result.");
        return 0;
      }
    }

    if (should_stream &&
        !strappy_assistant_emit_tool_event(
          had_tool_error ? STRAPPY_CHAT_STREAM_EVENT_TOOL_ERROR :
                           STRAPPY_CHAT_STREAM_EVENT_TOOL_RESULT,
          turn,
          round->tool_call_ids[index],
          round->tool_names[index],
          round->tool_arguments[index],
          output,
          callback,
          callback_data,
          error_out)) {
      free(output);
      free(tool_error);
      cJSON_Delete(root);
      strappy_assistant_tool_round_destroy(round);
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

    round->tool_outputs[index] = output;
    round->tool_message_jsons[index] = message_json;

    followup_index = request_messages->count + 1U + (size_t)index;
    round->followup_messages[followup_index].role = "tool";
    round->followup_messages[followup_index].content = output;
    round->followup_messages[followup_index].message_json = message_json;

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
      cJSON_Delete(root);
      strappy_assistant_tool_round_destroy(round);
      strappy_set_error(error_out, "Could not allocate tool-result message key.");
      return 0;
    }
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
  char **error_out)
{
  static const char *message =
    "I reached the 50-round tool limit while inspecting local data, "
    "so I could not finish a reliable answer. Please ask a narrower question "
    "or continue with a more specific follow-up.";

  return strappy_assistant_set_local_limit_result(result,
                                                  last_result,
                                                  message,
                                                  "tool_round_limit",
                                                  error_out);
}

static int strappy_assistant_store_tool_sequence(
  const char *session_db_path,
  long long session_id,
  const char *prompt,
  const strappy_assistant_turn_spec *main_turn,
  const strappy_assistant_turn_spec *audit_turn,
  const strappy_assistant_tool_sequence *sequence,
  const strappy_chat_result *final_result,
  char **error_out)
{
  strappy_session_message_input *messages;
  size_t message_count;
  size_t message_index;
  size_t round_index;
  int has_post_final_rounds;
  int has_memory_audit_turn;
  int needs_memory_audit_prompt;
  char *memory_audit_completion_json;
  const strappy_chat_result *memory_audit_final_result;
  int ok;

  if ((main_turn == NULL) || (sequence == NULL) || (final_result == NULL)) {
    strappy_set_error(error_out, "Tool message sequence is incomplete.");
    return 0;
  }
  if (session_id <= 0) {
    strappy_set_error(error_out, "Session id is not valid.");
    return 0;
  }

  memory_audit_final_result = sequence->memory_audit_final_result;
  has_memory_audit_turn = (memory_audit_final_result != NULL) ? 1 : 0;
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

  needs_memory_audit_prompt =
    (has_memory_audit_turn && !has_post_final_rounds) ? 1 : 0;
  if (has_memory_audit_turn) {
    if (needs_memory_audit_prompt &&
        (message_count > (((size_t)-1) - 1U))) {
      strappy_set_error(error_out, "Tool message sequence is too large.");
      return 0;
    }
    if (needs_memory_audit_prompt) {
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

  memory_audit_completion_json = NULL;
  if (has_memory_audit_turn) {
    memory_audit_completion_json =
      strappy_assistant_create_basic_message_json(
        "assistant",
        strappy_assistant_memory_audit_completion_text(),
        error_out);
    if (memory_audit_completion_json == NULL) {
      return 0;
    }
  }

  messages = (strappy_session_message_input *)malloc(
    message_count * sizeof(strappy_session_message_input));
  if (messages == NULL) {
    free(memory_audit_completion_json);
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

  if (needs_memory_audit_prompt) {
    strappy_assistant_storage_message_set_turn(
      &messages[message_index],
      audit_turn,
      "prompt",
      (audit_turn != NULL) ? audit_turn->api_role : "user",
      (audit_turn != NULL) ? audit_turn->render_role : "harness",
      (audit_turn != NULL) ? audit_turn->prompt_message_key : NULL,
      NULL);
    messages[message_index].content =
      ((audit_turn != NULL) && (audit_turn->prompt != NULL)) ?
        audit_turn->prompt : strappy_assistant_memory_audit_prompt();
    message_index++;
  }

  if (has_memory_audit_turn) {
    strappy_assistant_storage_message_set_turn(
      &messages[message_index],
      audit_turn,
      "assistant",
      "assistant",
      "assistant",
      (audit_turn != NULL) ? audit_turn->assistant_message_key : NULL,
      NULL);
    messages[message_index].content =
      strappy_assistant_memory_audit_completion_text();
    if (memory_audit_final_result != NULL) {
      messages[message_index].model = memory_audit_final_result->model;
      messages[message_index].http_status =
        memory_audit_final_result->http_status;
      messages[message_index].metadata_json =
        memory_audit_final_result->metadata_json;
      messages[message_index].reasoning =
        memory_audit_final_result->reasoning_text;
    }
    messages[message_index].message_json = memory_audit_completion_json;
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

  free(memory_audit_completion_json);
  free(messages);
  return ok;
}

static int strappy_assistant_run_tool_sequence(
  const strappy_config *config,
  const char *session_db_path,
  const strappy_assistant_turn_spec *turn,
  int store_prompt_message,
  int post_final,
  const strappy_assistant_request_messages *request_messages,
  const strappy_chat_result *tool_request_result,
  int stream_tool_events,
  int stream_model_responses,
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

    if (!strappy_assistant_result_has_tool_calls(current_result,
                                                &has_tool_calls,
                                                error_out)) {
      return 0;
    }

    if (!has_tool_calls) {
      sequence->final_result = (strappy_chat_result *)current_result;
      return 1;
    }

    if (sequence->round_count >= STRAPPY_ASSISTANT_MAX_TOOL_ROUNDS) {
      next_result = &sequence->results[sequence->round_count];
      if (!strappy_assistant_set_tool_limit_result(next_result,
                                                   current_result,
                                                   error_out)) {
        return 0;
      }
      if (stream_model_responses && (callback != NULL)) {
        strappy_chat_stream_event event;

        memset(&event, 0, sizeof(event));
        event.type = STRAPPY_CHAT_STREAM_EVENT_CONTENT_DELTA;
        event.text = next_result->response_text;
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
                            "Stream callback rejected assistant limit message.");
          return 0;
        }
      }
      sequence->final_result = next_result;
      return 1;
    }

    round = &sequence->rounds[sequence->round_count];
    if (!strappy_assistant_build_tool_round(session_db_path,
                                            config->guidance_resource_dir,
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
    } else if (!strappy_client_send_messages(config,
                                             round->followup_messages,
                                             round->followup_count,
                                             next_result,
                                             error_out)) {
      return 0;
    }

    followup_request.messages = round->followup_messages;
    followup_request.count = round->followup_count;
    current_request = &followup_request;
    current_result = next_result;
  }
}

static int strappy_assistant_run_memory_audit(
  const strappy_config *config,
  const char *session_db_path,
  const strappy_assistant_turn_spec *audit_turn,
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
  strappy_assistant_request_messages audit_request;
  size_t base_count;
  int audit_has_tool_calls;
  int did_run_audit_tool_round;
  const char *audit_tool_allowlist[3];
  strappy_config audit_config;
  strappy_assistant_stream_turn_context turn_context;

  if ((config == NULL) || (audit_turn == NULL) ||
      (sequence == NULL) || (final_result == NULL)) {
    strappy_set_error(error_out, "Learning summary request is incomplete.");
    return 0;
  }

  if ((session_db_path == NULL) || (session_db_path[0] == '\0')) {
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

  strappy_assistant_request_messages_init(&audit_request);
  if (!strappy_assistant_build_memory_audit_request(base_messages,
                                                    base_count,
                                                    final_result,
                                                    &audit_request,
                                                    error_out)) {
    return 0;
  }

  audit_tool_allowlist[0] = STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE;
  audit_tool_allowlist[1] = STRAPPY_TOOL_HELPER_USER_INFO_REMEMBER;
  audit_tool_allowlist[2] = STRAPPY_TOOL_HELPER_DATABASE_INFO_REMEMBER;
  audit_config = *config;
  audit_config.tool_allowlist = audit_tool_allowlist;
  audit_config.tool_allowlist_count =
    sizeof(audit_tool_allowlist) / sizeof(audit_tool_allowlist[0]);

  if (should_stream &&
      !strappy_assistant_emit_turn_event(STRAPPY_CHAT_STREAM_EVENT_TURN_STARTED,
                                         audit_turn,
                                         audit_turn->prompt,
                                         callback,
                                         callback_data,
                                         error_out)) {
    strappy_assistant_request_messages_destroy(&audit_request);
    return 0;
  }

  sequence->memory_audit_final_result = NULL;
  strappy_chat_result_destroy(&sequence->memory_audit_result);
  if (should_stream) {
    turn_context.callback = callback;
    turn_context.callback_data = callback_data;
    turn_context.turn = audit_turn;
    if (!strappy_client_stream_messages(&audit_config,
                                        audit_request.messages,
                                        audit_request.count,
                                        &sequence->memory_audit_result,
                                        strappy_assistant_stream_turn_callback,
                                        &turn_context,
                                        error_out)) {
      strappy_assistant_request_messages_destroy(&audit_request);
      return 0;
    }
  } else {
    if (!strappy_client_send_messages(&audit_config,
                                      audit_request.messages,
                                      audit_request.count,
                                      &sequence->memory_audit_result,
                                      error_out)) {
      strappy_assistant_request_messages_destroy(&audit_request);
      return 0;
    }
  }

  audit_has_tool_calls = 0;
  if (!strappy_assistant_result_has_tool_calls(&sequence->memory_audit_result,
                                               &audit_has_tool_calls,
                                               error_out)) {
    strappy_assistant_request_messages_destroy(&audit_request);
    return 0;
  }

  did_run_audit_tool_round = 0;
  if (!strappy_assistant_run_tool_sequence(&audit_config,
                                           session_db_path,
                                           audit_turn,
                                           1,
                                           1,
                                           &audit_request,
                                           &sequence->memory_audit_result,
                                           should_stream && audit_has_tool_calls,
                                           should_stream && audit_has_tool_calls,
                                           callback,
                                           callback_data,
                                           sequence,
                                           &did_run_audit_tool_round,
                                           error_out)) {
    strappy_assistant_request_messages_destroy(&audit_request);
    return 0;
  }

  if (should_stream &&
      !strappy_assistant_emit_turn_event(STRAPPY_CHAT_STREAM_EVENT_TURN_FINISHED,
                                         audit_turn,
                                         strappy_assistant_memory_audit_completion_text(),
                                         callback,
                                         callback_data,
                                         error_out)) {
    strappy_assistant_request_messages_destroy(&audit_request);
    return 0;
  }

  sequence->memory_audit_final_result = did_run_audit_tool_round ?
    sequence->final_result : &sequence->memory_audit_result;
  sequence->final_result = (strappy_chat_result *)final_result;
  if (did_run_audit_tool_round && (did_run_tool_round_in_out != NULL)) {
    *did_run_tool_round_in_out = 1;
  }

  strappy_assistant_request_messages_destroy(&audit_request);
  return 1;
}

static char *strappy_assistant_send_prompt_for_session_internal(
  const char *prompt,
  const char *env_path,
  const char *system_prompt_template_path,
  const char *session_db_path,
  long long session_id,
  char **error_out)
{
  strappy_config config;
  strappy_chat_result result;
  strappy_session_message_record_list message_list;
  strappy_assistant_request_messages request_messages;
  strappy_assistant_tool_sequence tool_sequence;
  strappy_assistant_turn_keys main_keys;
  strappy_assistant_turn_keys audit_keys;
  strappy_assistant_turn_spec main_turn;
  strappy_assistant_turn_spec audit_turn;
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
  strappy_chat_result_init(&result);
  strappy_session_message_record_list_init(&message_list);
  strappy_assistant_request_messages_init(&request_messages);
  strappy_assistant_tool_sequence_init(&tool_sequence);
  strappy_assistant_turn_keys_init(&main_keys);
  strappy_assistant_turn_keys_init(&audit_keys);
  memset(&main_turn, 0, sizeof(main_turn));
  memset(&audit_turn, 0, sizeof(audit_turn));

  if (!strappy_config_load(&config, env_path, error_out)) {
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
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
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_client_send_messages(&config,
                                    request_messages.messages,
                                    request_messages.count,
                                    &result,
                                    error_out)) {
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  did_run_tool_round = 0;
  if (!strappy_assistant_turn_keys_make(&main_keys, "user-turn", error_out) ||
      !strappy_assistant_turn_keys_make(&audit_keys, "harness-turn", error_out)) {
    strappy_assistant_turn_keys_destroy(&audit_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }
  strappy_assistant_turn_spec_set(&main_turn,
                                  &main_keys,
                                  session_id,
                                  NULL,
                                  "user",
                                  "user",
                                  "user",
                                  "full",
                                  prompt,
                                  STRAPPY_ASSISTANT_TOOL_POLICY_NORMAL);
  strappy_assistant_turn_spec_set(&audit_turn,
                                  &audit_keys,
                                  session_id,
                                  main_keys.prompt_group_key,
                                  "harness",
                                  "harness",
                                  "user",
                                  "omit",
                                  strappy_assistant_memory_audit_prompt(),
                                  STRAPPY_ASSISTANT_TOOL_POLICY_MEMORY_AUDIT);

  if (!strappy_assistant_run_tool_sequence(&config,
                                           session_db_path,
                                           &main_turn,
                                           0,
                                           0,
                                           &request_messages,
                                           &result,
                                           0,
                                           0,
                                           NULL,
                                           NULL,
                                           &tool_sequence,
                                           &did_run_tool_round,
                                           error_out)) {
    strappy_assistant_turn_keys_destroy(&audit_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  final_result = did_run_tool_round ? tool_sequence.final_result : &result;
  if (!strappy_assistant_run_memory_audit(&config,
                                          session_db_path,
                                          &audit_turn,
                                          &request_messages,
                                          &tool_sequence,
                                          final_result,
                                          did_run_tool_round,
                                          0,
                                          NULL,
                                          NULL,
                                          &did_run_tool_round,
                                          error_out)) {
    strappy_assistant_turn_keys_destroy(&audit_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  strappy_assistant_request_messages_destroy(&request_messages);
  strappy_session_message_record_list_destroy(&message_list);

  final_result = did_run_tool_round ? tool_sequence.final_result : &result;
  if (!strappy_assistant_store_tool_sequence(session_db_path,
                                            session_id,
                                            prompt,
                                            &main_turn,
                                            &audit_turn,
                                            &tool_sequence,
                                            final_result,
                                            error_out)) {
    strappy_assistant_turn_keys_destroy(&audit_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_chat_result_destroy(&result);
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
  strappy_assistant_turn_keys_destroy(&audit_keys);
  strappy_assistant_turn_keys_destroy(&main_keys);
  strappy_chat_result_destroy(&result);
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
  strappy_chat_result result;
  strappy_session_message_record_list message_list;
  strappy_assistant_request_messages request_messages;
  strappy_assistant_tool_sequence tool_sequence;
  strappy_assistant_turn_keys main_keys;
  strappy_assistant_turn_keys audit_keys;
  strappy_assistant_turn_spec main_turn;
  strappy_assistant_turn_spec audit_turn;
  strappy_assistant_stream_turn_context turn_context;
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
  strappy_chat_result_init(&result);
  strappy_session_message_record_list_init(&message_list);
  strappy_assistant_request_messages_init(&request_messages);
  strappy_assistant_tool_sequence_init(&tool_sequence);
  strappy_assistant_turn_keys_init(&main_keys);
  strappy_assistant_turn_keys_init(&audit_keys);
  memset(&main_turn, 0, sizeof(main_turn));
  memset(&audit_turn, 0, sizeof(audit_turn));

  if (!strappy_config_load(&config, env_path, error_out)) {
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
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
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_assistant_make_default_turns(prompt,
                                            session_id,
                                            &main_keys,
                                            &audit_keys,
                                            &main_turn,
                                            &audit_turn,
                                            error_out)) {
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_assistant_emit_turn_event(STRAPPY_CHAT_STREAM_EVENT_TURN_STARTED,
                                         &main_turn,
                                         prompt,
                                         callback,
                                         callback_data,
                                         error_out)) {
    strappy_assistant_turn_keys_destroy(&audit_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  turn_context.callback = callback;
  turn_context.callback_data = callback_data;
  turn_context.turn = &main_turn;
  if (!strappy_client_stream_messages(&config,
                                      request_messages.messages,
                                      request_messages.count,
                                      &result,
                                      strappy_assistant_stream_turn_callback,
                                      &turn_context,
                                      error_out)) {
    strappy_assistant_turn_keys_destroy(&audit_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  did_run_tool_round = 0;
  if (!strappy_assistant_run_tool_sequence(&config,
                                           session_db_path,
                                           &main_turn,
                                           0,
                                           0,
                                           &request_messages,
                                           &result,
                                           1,
                                           1,
                                           callback,
                                           callback_data,
                                           &tool_sequence,
                                           &did_run_tool_round,
                                           error_out)) {
    strappy_assistant_turn_keys_destroy(&audit_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  final_result = did_run_tool_round ? tool_sequence.final_result : &result;
  if (!strappy_assistant_emit_turn_event(STRAPPY_CHAT_STREAM_EVENT_TURN_FINISHED,
                                         &main_turn,
                                         final_result->response_text,
                                         callback,
                                         callback_data,
                                         error_out)) {
    strappy_assistant_turn_keys_destroy(&audit_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_assistant_run_memory_audit(&config,
                                          session_db_path,
                                          &audit_turn,
                                          &request_messages,
                                          &tool_sequence,
                                          final_result,
                                          did_run_tool_round,
                                          1,
                                          callback,
                                          callback_data,
                                          &did_run_tool_round,
                                          error_out)) {
    strappy_assistant_turn_keys_destroy(&audit_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  strappy_assistant_request_messages_destroy(&request_messages);
  strappy_session_message_record_list_destroy(&message_list);

  final_result = did_run_tool_round ? tool_sequence.final_result : &result;
  if (!strappy_assistant_store_tool_sequence(session_db_path,
                                            session_id,
                                            prompt,
                                            &main_turn,
                                            &audit_turn,
                                            &tool_sequence,
                                            final_result,
                                            error_out)) {
    strappy_assistant_turn_keys_destroy(&audit_keys);
    strappy_assistant_turn_keys_destroy(&main_keys);
    strappy_assistant_tool_sequence_destroy(&tool_sequence);
    strappy_chat_result_destroy(&result);
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
  strappy_assistant_turn_keys_destroy(&audit_keys);
  strappy_assistant_turn_keys_destroy(&main_keys);
  strappy_chat_result_destroy(&result);
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
