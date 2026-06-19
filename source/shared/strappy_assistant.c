#include "strappy_assistant.h"

#include "strappy_config.h"
#include "strappy_core.h"
#include "strappy_client.h"
#include "strappy_db.h"
#include "strappy_prompt.h"
#include "strappy_tools.h"

#include <cJSON.h>
#include <stdlib.h>
#include <string.h>

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
  char *tool_call_content;
  char **tool_outputs;
  char **tool_message_jsons;
} strappy_assistant_tool_round;

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
  round->tool_call_content = NULL;
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
  free(round->tool_outputs);
  free(round->tool_message_jsons);
  strappy_assistant_tool_round_init(round);
}

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

static int strappy_assistant_prepare_request_messages(
  const char *prompt,
  const char *system_prompt_template_path,
  const char *webview_user_agent,
  const strappy_session_message_record_list *message_list,
  strappy_assistant_request_messages *request,
  char **error_out)
{
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

  system_prompt =
    strappy_prompt_render_system_prompt(system_prompt_template_path,
                                        webview_user_agent,
                                        error_out);
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
    messages[index + 1U].role = message_list->records[index].role;
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
  const char *prompt,
  const strappy_assistant_request_messages *request_messages,
  const strappy_chat_result *tool_request_result,
  strappy_assistant_tool_round *round,
  char **error_out)
{
  cJSON *root;
  cJSON *tool_calls;
  char *parse_error;
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

  if ((session_db_path == NULL) || (session_db_path[0] == '\0')) {
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
  round->tool_outputs = (char **)calloc((size_t)tool_count, sizeof(char *));
  round->tool_message_jsons = (char **)calloc((size_t)tool_count, sizeof(char *));
  if ((round->followup_messages == NULL) ||
      (round->storage_messages == NULL) ||
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
  round->storage_messages[0].role = "user";
  round->storage_messages[0].content = prompt;
  round->storage_messages[1].role = "tool_call";
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
    size_t followup_index;
    size_t storage_index;

    tool_call = cJSON_GetArrayItem(tool_calls, index);
    tool_call_id = strappy_assistant_tool_call_string(tool_call, "id");
    tool_name =
      strappy_assistant_tool_call_function_string(tool_call, "name");
    arguments =
      strappy_assistant_tool_call_function_string(tool_call, "arguments");

    tool_error = NULL;
    output = strappy_tools_execute(session_db_path,
                                   tool_name,
                                   arguments,
                                   &tool_error);
    if (output == NULL) {
      output = strappy_assistant_tool_error_result(tool_name, tool_error);
      free(tool_error);
      if (output == NULL) {
        cJSON_Delete(root);
        strappy_assistant_tool_round_destroy(round);
        strappy_set_error(error_out, "Could not build tool error result.");
        return 0;
      }
    }

    message_json = strappy_assistant_tool_message_json(tool_call_id,
                                                       tool_name,
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
    round->storage_messages[storage_index].role = "tool";
    round->storage_messages[storage_index].content = output;
    round->storage_messages[storage_index].message_json = message_json;
  }

  cJSON_Delete(root);
  return 1;
}

static int strappy_assistant_store_tool_round(
  const char *session_db_path,
  long long session_id,
  const char *prompt,
  const strappy_assistant_tool_round *round,
  const strappy_chat_result *final_result,
  long long *session_id_out,
  char **error_out)
{
  strappy_session_message_input *messages;
  size_t message_count;
  int ok;

  if ((round == NULL) || (final_result == NULL) ||
      (round->storage_messages == NULL) || (round->storage_count == 0U)) {
    strappy_set_error(error_out, "Tool message sequence is incomplete.");
    return 0;
  }

  if (round->storage_count >= (((size_t)-1) / sizeof(strappy_session_message_input))) {
    strappy_set_error(error_out, "Tool message sequence is too large.");
    return 0;
  }

  message_count = round->storage_count + 1U;
  messages = (strappy_session_message_input *)malloc(
    message_count * sizeof(strappy_session_message_input));
  if (messages == NULL) {
    strappy_set_error(error_out, "Could not allocate tool message sequence.");
    return 0;
  }

  memcpy(messages,
         round->storage_messages,
         round->storage_count * sizeof(strappy_session_message_input));
  memset(&messages[round->storage_count],
         0,
         sizeof(strappy_session_message_input));
  messages[round->storage_count].role = "assistant";
  messages[round->storage_count].content = final_result->response_text;
  messages[round->storage_count].model = final_result->model;
  messages[round->storage_count].http_status = final_result->http_status;
  messages[round->storage_count].metadata_json = final_result->metadata_json;
  messages[round->storage_count].message_json = final_result->message_json;
  messages[round->storage_count].reasoning = final_result->reasoning_text;

  if (session_id > 0) {
    ok = strappy_db_append_message_sequence_to_session(
      session_db_path,
      session_id,
      prompt,
      final_result->response_text,
      final_result->model,
      final_result->http_status,
      messages,
      message_count,
      error_out);
  } else {
    ok = strappy_db_save_message_sequence_with_id(
      session_db_path,
      prompt,
      final_result->response_text,
      final_result->model,
      final_result->http_status,
      messages,
      message_count,
      session_id_out,
      error_out);
  }

  free(messages);
  return ok;
}

static int strappy_assistant_maybe_run_tool_round(
  const strappy_config *config,
  const char *session_db_path,
  const char *prompt,
  const strappy_assistant_request_messages *request_messages,
  const strappy_chat_result *tool_request_result,
  int should_stream,
  strappy_chat_stream_callback callback,
  void *callback_data,
  strappy_assistant_tool_round *round,
  strappy_chat_result *final_result,
  int *did_run_out,
  char **error_out)
{
  if (did_run_out != NULL) {
    *did_run_out = 0;
  }

  if ((round == NULL) || (final_result == NULL)) {
    strappy_set_error(error_out, "Tool round request is incomplete.");
    return 0;
  }

  if (!strappy_assistant_build_tool_round(session_db_path,
                                          prompt,
                                          request_messages,
                                          tool_request_result,
                                          round,
                                          error_out)) {
    return 0;
  }

  if ((round->followup_messages == NULL) || (round->followup_count == 0U)) {
    return 1;
  }

  if (should_stream) {
    if (!strappy_client_stream_messages(config,
                                        round->followup_messages,
                                        round->followup_count,
                                        final_result,
                                        callback,
                                        callback_data,
                                        error_out)) {
      return 0;
    }
  } else if (!strappy_client_send_messages(config,
                                           round->followup_messages,
                                           round->followup_count,
                                           final_result,
                                           error_out)) {
    return 0;
  }

  if (did_run_out != NULL) {
    *did_run_out = 1;
  }

  return 1;
}

static char *strappy_assistant_send_prompt_internal(const char *prompt,
                                                    const char *env_path,
                                                    const char *system_prompt_template_path,
                                                    const char *webview_user_agent,
                                                    const char *session_db_path,
                                                    int should_store,
                                                    long long *session_id_out,
                                                    char **error_out)
{
  strappy_config config;
  strappy_chat_result result;
  strappy_chat_result final_result;
  strappy_assistant_request_messages request_messages;
  strappy_assistant_tool_round tool_round;
  char *response;
  int did_run_tool_round;

  if ((prompt == NULL) || (prompt[0] == '\0')) {
    strappy_set_error(error_out, "Prompt is empty.");
    return NULL;
  }

  if (session_id_out != NULL) {
    *session_id_out = 0;
  }

  strappy_config_init(&config);
  strappy_chat_result_init(&result);
  strappy_chat_result_init(&final_result);
  strappy_assistant_request_messages_init(&request_messages);
  strappy_assistant_tool_round_init(&tool_round);

  if (!strappy_config_load(&config, env_path, error_out)) {
    strappy_assistant_tool_round_destroy(&tool_round);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_chat_result_destroy(&final_result);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_assistant_prepare_request_messages(prompt,
                                                  system_prompt_template_path,
                                                  webview_user_agent,
                                                  NULL,
                                                  &request_messages,
                                                  error_out)) {
    strappy_assistant_tool_round_destroy(&tool_round);
    strappy_chat_result_destroy(&final_result);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_client_send_messages(&config,
                                    request_messages.messages,
                                    request_messages.count,
                                    &result,
                                    error_out)) {
    strappy_assistant_tool_round_destroy(&tool_round);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_chat_result_destroy(&final_result);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  did_run_tool_round = 0;
  if (should_store &&
      !strappy_assistant_maybe_run_tool_round(&config,
                                              session_db_path,
                                              prompt,
                                              &request_messages,
                                              &result,
                                              0,
                                              NULL,
                                              NULL,
                                              &tool_round,
                                              &final_result,
                                              &did_run_tool_round,
                                              error_out)) {
    strappy_assistant_tool_round_destroy(&tool_round);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_chat_result_destroy(&final_result);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  strappy_assistant_request_messages_destroy(&request_messages);

  if (should_store) {
    if (did_run_tool_round) {
      if (!strappy_assistant_store_tool_round(session_db_path,
                                             0LL,
                                             prompt,
                                             &tool_round,
                                             &final_result,
                                             session_id_out,
                                             error_out)) {
        strappy_assistant_tool_round_destroy(&tool_round);
        strappy_chat_result_destroy(&final_result);
        strappy_chat_result_destroy(&result);
        strappy_config_destroy(&config);
        return NULL;
      }
    } else if (!strappy_db_save_exchange_with_id(session_db_path,
                                                prompt,
                                                result.response_text,
                                                result.model,
                                                result.http_status,
                                                result.metadata_json,
                                                result.message_json,
                                                result.reasoning_text,
                                                session_id_out,
                                                error_out)) {
      strappy_assistant_tool_round_destroy(&tool_round);
      strappy_chat_result_destroy(&final_result);
      strappy_chat_result_destroy(&result);
      strappy_config_destroy(&config);
      return NULL;
    }
  }

  if (did_run_tool_round) {
    response = final_result.response_text;
    final_result.response_text = NULL;
  } else {
    response = result.response_text;
    result.response_text = NULL;
  }

  strappy_assistant_tool_round_destroy(&tool_round);
  strappy_chat_result_destroy(&final_result);
  strappy_chat_result_destroy(&result);
  strappy_config_destroy(&config);
  return response;
}

static char *strappy_assistant_send_prompt_for_session_internal(
  const char *prompt,
  const char *env_path,
  const char *system_prompt_template_path,
  const char *webview_user_agent,
  const char *session_db_path,
  long long session_id,
  char **error_out)
{
  strappy_config config;
  strappy_chat_result result;
  strappy_chat_result final_result;
  strappy_session_message_record_list message_list;
  strappy_assistant_request_messages request_messages;
  strappy_assistant_tool_round tool_round;
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
  strappy_chat_result_init(&final_result);
  strappy_session_message_record_list_init(&message_list);
  strappy_assistant_request_messages_init(&request_messages);
  strappy_assistant_tool_round_init(&tool_round);

  if (!strappy_config_load(&config, env_path, error_out)) {
    strappy_assistant_tool_round_destroy(&tool_round);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&final_result);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_db_list_session_messages(session_db_path,
                                        session_id,
                                        &message_list,
                                        error_out)) {
    strappy_assistant_tool_round_destroy(&tool_round);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&final_result);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_assistant_prepare_request_messages(prompt,
                                                  system_prompt_template_path,
                                                  webview_user_agent,
                                                  &message_list,
                                                  &request_messages,
                                                  error_out)) {
    strappy_assistant_tool_round_destroy(&tool_round);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&final_result);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_client_send_messages(&config,
                                    request_messages.messages,
                                    request_messages.count,
                                    &result,
                                    error_out)) {
    strappy_assistant_tool_round_destroy(&tool_round);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&final_result);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  did_run_tool_round = 0;
  if (!strappy_assistant_maybe_run_tool_round(&config,
                                              session_db_path,
                                              prompt,
                                              &request_messages,
                                              &result,
                                              0,
                                              NULL,
                                              NULL,
                                              &tool_round,
                                              &final_result,
                                              &did_run_tool_round,
                                              error_out)) {
    strappy_assistant_tool_round_destroy(&tool_round);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&final_result);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  strappy_assistant_request_messages_destroy(&request_messages);
  strappy_session_message_record_list_destroy(&message_list);

  if (did_run_tool_round) {
    if (!strappy_assistant_store_tool_round(session_db_path,
                                           session_id,
                                           prompt,
                                           &tool_round,
                                           &final_result,
                                           NULL,
                                           error_out)) {
      strappy_assistant_tool_round_destroy(&tool_round);
      strappy_chat_result_destroy(&final_result);
      strappy_chat_result_destroy(&result);
      strappy_config_destroy(&config);
      return NULL;
    }
  } else if (!strappy_db_append_exchange_to_session(session_db_path,
                                                   session_id,
                                                   prompt,
                                                   result.response_text,
                                                   result.model,
                                                   result.http_status,
                                                   result.metadata_json,
                                                   result.message_json,
                                                   result.reasoning_text,
                                                   error_out)) {
    strappy_assistant_tool_round_destroy(&tool_round);
    strappy_chat_result_destroy(&final_result);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (did_run_tool_round) {
    response = final_result.response_text;
    final_result.response_text = NULL;
  } else {
    response = result.response_text;
    result.response_text = NULL;
  }

  strappy_assistant_tool_round_destroy(&tool_round);
  strappy_chat_result_destroy(&final_result);
  strappy_chat_result_destroy(&result);
  strappy_config_destroy(&config);
  return response;
}

static char *strappy_assistant_stream_prompt_internal(
  const char *prompt,
  const char *env_path,
  const char *system_prompt_template_path,
  const char *webview_user_agent,
  const char *session_db_path,
  long long *session_id_out,
  strappy_chat_stream_callback callback,
  void *callback_data,
  char **error_out)
{
  strappy_config config;
  strappy_chat_result result;
  strappy_chat_result final_result;
  strappy_assistant_request_messages request_messages;
  strappy_assistant_tool_round tool_round;
  char *response;
  int did_run_tool_round;

  if ((prompt == NULL) || (prompt[0] == '\0')) {
    strappy_set_error(error_out, "Prompt is empty.");
    return NULL;
  }

  if (session_id_out != NULL) {
    *session_id_out = 0;
  }

  strappy_config_init(&config);
  strappy_chat_result_init(&result);
  strappy_chat_result_init(&final_result);
  strappy_assistant_request_messages_init(&request_messages);
  strappy_assistant_tool_round_init(&tool_round);

  if (!strappy_config_load(&config, env_path, error_out)) {
    strappy_assistant_tool_round_destroy(&tool_round);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_chat_result_destroy(&final_result);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_assistant_prepare_request_messages(prompt,
                                                  system_prompt_template_path,
                                                  webview_user_agent,
                                                  NULL,
                                                  &request_messages,
                                                  error_out)) {
    strappy_assistant_tool_round_destroy(&tool_round);
    strappy_chat_result_destroy(&final_result);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_client_stream_messages(&config,
                                      request_messages.messages,
                                      request_messages.count,
                                      &result,
                                      callback,
                                      callback_data,
                                      error_out)) {
    strappy_assistant_tool_round_destroy(&tool_round);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_chat_result_destroy(&final_result);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  did_run_tool_round = 0;
  if (!strappy_assistant_maybe_run_tool_round(&config,
                                              session_db_path,
                                              prompt,
                                              &request_messages,
                                              &result,
                                              1,
                                              callback,
                                              callback_data,
                                              &tool_round,
                                              &final_result,
                                              &did_run_tool_round,
                                              error_out)) {
    strappy_assistant_tool_round_destroy(&tool_round);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_chat_result_destroy(&final_result);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  strappy_assistant_request_messages_destroy(&request_messages);

  if (did_run_tool_round) {
    if (!strappy_assistant_store_tool_round(session_db_path,
                                           0LL,
                                           prompt,
                                           &tool_round,
                                           &final_result,
                                           session_id_out,
                                           error_out)) {
      strappy_assistant_tool_round_destroy(&tool_round);
      strappy_chat_result_destroy(&final_result);
      strappy_chat_result_destroy(&result);
      strappy_config_destroy(&config);
      return NULL;
    }
  } else if (!strappy_db_save_exchange_with_id(session_db_path,
                                              prompt,
                                              result.response_text,
                                              result.model,
                                              result.http_status,
                                              result.metadata_json,
                                              result.message_json,
                                              result.reasoning_text,
                                              session_id_out,
                                              error_out)) {
    strappy_assistant_tool_round_destroy(&tool_round);
    strappy_chat_result_destroy(&final_result);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (did_run_tool_round) {
    response = final_result.response_text;
    final_result.response_text = NULL;
  } else {
    response = result.response_text;
    result.response_text = NULL;
  }

  strappy_assistant_tool_round_destroy(&tool_round);
  strappy_chat_result_destroy(&final_result);
  strappy_chat_result_destroy(&result);
  strappy_config_destroy(&config);
  return response;
}

static char *strappy_assistant_stream_prompt_for_session_internal(
  const char *prompt,
  const char *env_path,
  const char *system_prompt_template_path,
  const char *webview_user_agent,
  const char *session_db_path,
  long long session_id,
  strappy_chat_stream_callback callback,
  void *callback_data,
  char **error_out)
{
  strappy_config config;
  strappy_chat_result result;
  strappy_chat_result final_result;
  strappy_session_message_record_list message_list;
  strappy_assistant_request_messages request_messages;
  strappy_assistant_tool_round tool_round;
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
  strappy_chat_result_init(&final_result);
  strappy_session_message_record_list_init(&message_list);
  strappy_assistant_request_messages_init(&request_messages);
  strappy_assistant_tool_round_init(&tool_round);

  if (!strappy_config_load(&config, env_path, error_out)) {
    strappy_assistant_tool_round_destroy(&tool_round);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&final_result);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_db_list_session_messages(session_db_path,
                                        session_id,
                                        &message_list,
                                        error_out)) {
    strappy_assistant_tool_round_destroy(&tool_round);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&final_result);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_assistant_prepare_request_messages(prompt,
                                                  system_prompt_template_path,
                                                  webview_user_agent,
                                                  &message_list,
                                                  &request_messages,
                                                  error_out)) {
    strappy_assistant_tool_round_destroy(&tool_round);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&final_result);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_client_stream_messages(&config,
                                      request_messages.messages,
                                      request_messages.count,
                                      &result,
                                      callback,
                                      callback_data,
                                      error_out)) {
    strappy_assistant_tool_round_destroy(&tool_round);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&final_result);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  did_run_tool_round = 0;
  if (!strappy_assistant_maybe_run_tool_round(&config,
                                              session_db_path,
                                              prompt,
                                              &request_messages,
                                              &result,
                                              1,
                                              callback,
                                              callback_data,
                                              &tool_round,
                                              &final_result,
                                              &did_run_tool_round,
                                              error_out)) {
    strappy_assistant_tool_round_destroy(&tool_round);
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_chat_result_destroy(&final_result);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  strappy_assistant_request_messages_destroy(&request_messages);
  strappy_session_message_record_list_destroy(&message_list);

  if (did_run_tool_round) {
    if (!strappy_assistant_store_tool_round(session_db_path,
                                           session_id,
                                           prompt,
                                           &tool_round,
                                           &final_result,
                                           NULL,
                                           error_out)) {
      strappy_assistant_tool_round_destroy(&tool_round);
      strappy_chat_result_destroy(&final_result);
      strappy_chat_result_destroy(&result);
      strappy_config_destroy(&config);
      return NULL;
    }
  } else if (!strappy_db_append_exchange_to_session(session_db_path,
                                                   session_id,
                                                   prompt,
                                                   result.response_text,
                                                   result.model,
                                                   result.http_status,
                                                   result.metadata_json,
                                                   result.message_json,
                                                   result.reasoning_text,
                                                   error_out)) {
    strappy_assistant_tool_round_destroy(&tool_round);
    strappy_chat_result_destroy(&final_result);
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (did_run_tool_round) {
    response = final_result.response_text;
    final_result.response_text = NULL;
  } else {
    response = result.response_text;
    result.response_text = NULL;
  }

  strappy_assistant_tool_round_destroy(&tool_round);
  strappy_chat_result_destroy(&final_result);
  strappy_chat_result_destroy(&result);
  strappy_config_destroy(&config);
  return response;
}

char *strappy_assistant_send_prompt(const char *prompt,
                                    const char *env_path,
                                    const char *system_prompt_template_path,
                                    const char *webview_user_agent,
                                    char **error_out)
{
  return strappy_assistant_send_prompt_internal(prompt,
                                               env_path,
                                               system_prompt_template_path,
                                               webview_user_agent,
                                               NULL,
                                               0,
                                               NULL,
                                               error_out);
}

char *strappy_assistant_send_prompt_and_store(const char *prompt,
                                             const char *env_path,
                                             const char *system_prompt_template_path,
                                             const char *webview_user_agent,
                                             const char *session_db_path,
                                             char **error_out)
{
  return strappy_assistant_send_prompt_internal(prompt,
                                               env_path,
                                               system_prompt_template_path,
                                               webview_user_agent,
                                               session_db_path,
                                               1,
                                               NULL,
                                               error_out);
}

char *strappy_assistant_send_prompt_and_store_with_id(const char *prompt,
                                                     const char *env_path,
                                                     const char *system_prompt_template_path,
                                                     const char *webview_user_agent,
                                                     const char *session_db_path,
                                                     long long *session_id_out,
                                                     char **error_out)
{
  return strappy_assistant_send_prompt_internal(prompt,
                                               env_path,
                                               system_prompt_template_path,
                                               webview_user_agent,
                                               session_db_path,
                                               1,
                                               session_id_out,
                                               error_out);
}

char *strappy_assistant_send_prompt_for_session_and_store(const char *prompt,
                                                         const char *env_path,
                                                         const char *system_prompt_template_path,
                                                         const char *webview_user_agent,
                                                         const char *session_db_path,
                                                         long long session_id,
                                                         char **error_out)
{
  return strappy_assistant_send_prompt_for_session_internal(prompt,
                                                           env_path,
                                                           system_prompt_template_path,
                                                           webview_user_agent,
                                                           session_db_path,
                                                           session_id,
                                                           error_out);
}

char *strappy_assistant_stream_prompt_and_store_with_id(
  const char *prompt,
  const char *env_path,
  const char *system_prompt_template_path,
  const char *webview_user_agent,
  const char *session_db_path,
  long long *session_id_out,
  strappy_chat_stream_callback callback,
  void *callback_data,
  char **error_out)
{
  return strappy_assistant_stream_prompt_internal(prompt,
                                                 env_path,
                                                 system_prompt_template_path,
                                                 webview_user_agent,
                                                 session_db_path,
                                                 session_id_out,
                                                 callback,
                                                 callback_data,
                                                 error_out);
}

char *strappy_assistant_stream_prompt_for_session_and_store(
  const char *prompt,
  const char *env_path,
  const char *system_prompt_template_path,
  const char *webview_user_agent,
  const char *session_db_path,
  long long session_id,
  strappy_chat_stream_callback callback,
  void *callback_data,
  char **error_out)
{
  return strappy_assistant_stream_prompt_for_session_internal(prompt,
                                                             env_path,
                                                             system_prompt_template_path,
                                                             webview_user_agent,
                                                             session_db_path,
                                                             session_id,
                                                             callback,
                                                             callback_data,
                                                             error_out);
}
