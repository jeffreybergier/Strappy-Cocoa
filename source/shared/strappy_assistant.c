#include "strappy_assistant.h"

#include "strappy_config.h"
#include "strappy_core.h"
#include "strappy_client.h"
#include "strappy_db.h"
#include "strappy_prompt.h"

#include <stdlib.h>

typedef struct strappy_assistant_request_messages {
  strappy_chat_message *messages;
  size_t count;
  char *system_prompt;
} strappy_assistant_request_messages;

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
  strappy_assistant_request_messages request_messages;
  char *response;

  if ((prompt == NULL) || (prompt[0] == '\0')) {
    strappy_set_error(error_out, "Prompt is empty.");
    return NULL;
  }

  if (session_id_out != NULL) {
    *session_id_out = 0;
  }

  strappy_config_init(&config);
  strappy_chat_result_init(&result);
  strappy_assistant_request_messages_init(&request_messages);

  if (!strappy_config_load(&config, env_path, error_out)) {
    return NULL;
  }

  if (!strappy_assistant_prepare_request_messages(prompt,
                                                  system_prompt_template_path,
                                                  webview_user_agent,
                                                  NULL,
                                                  &request_messages,
                                                  error_out)) {
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_client_send_messages(&config,
                                    request_messages.messages,
                                    request_messages.count,
                                    &result,
                                    error_out)) {
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_config_destroy(&config);
    return NULL;
  }
  strappy_assistant_request_messages_destroy(&request_messages);

  if (should_store &&
      !strappy_db_save_exchange_with_id(session_db_path,
                                        prompt,
                                        result.response_text,
                                        result.model,
                                        result.http_status,
                                        result.metadata_json,
                                        result.message_json,
                                        result.reasoning_text,
                                        session_id_out,
                                        error_out)) {
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  response = result.response_text;
  result.response_text = NULL;

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
  strappy_session_message_record_list message_list;
  strappy_assistant_request_messages request_messages;
  char *response;

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

  if (!strappy_config_load(&config, env_path, error_out)) {
    return NULL;
  }

  if (!strappy_db_list_session_messages(session_db_path,
                                        session_id,
                                        &message_list,
                                        error_out)) {
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_assistant_prepare_request_messages(prompt,
                                                  system_prompt_template_path,
                                                  webview_user_agent,
                                                  &message_list,
                                                  &request_messages,
                                                  error_out)) {
    strappy_session_message_record_list_destroy(&message_list);
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_client_send_messages(&config,
                                    request_messages.messages,
                                    request_messages.count,
                                    &result,
                                    error_out)) {
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_config_destroy(&config);
    return NULL;
  }

  strappy_assistant_request_messages_destroy(&request_messages);
  strappy_session_message_record_list_destroy(&message_list);

  if (!strappy_db_append_exchange_to_session(session_db_path,
                                             session_id,
                                             prompt,
                                             result.response_text,
                                             result.model,
                                             result.http_status,
                                             result.metadata_json,
                                             result.message_json,
                                             result.reasoning_text,
                                             error_out)) {
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  response = result.response_text;
  result.response_text = NULL;

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
  strappy_assistant_request_messages request_messages;
  char *response;

  if ((prompt == NULL) || (prompt[0] == '\0')) {
    strappy_set_error(error_out, "Prompt is empty.");
    return NULL;
  }

  if (session_id_out != NULL) {
    *session_id_out = 0;
  }

  strappy_config_init(&config);
  strappy_chat_result_init(&result);
  strappy_assistant_request_messages_init(&request_messages);

  if (!strappy_config_load(&config, env_path, error_out)) {
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_assistant_prepare_request_messages(prompt,
                                                  system_prompt_template_path,
                                                  webview_user_agent,
                                                  NULL,
                                                  &request_messages,
                                                  error_out)) {
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
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_config_destroy(&config);
    return NULL;
  }
  strappy_assistant_request_messages_destroy(&request_messages);

  if (!strappy_db_save_exchange_with_id(session_db_path,
                                        prompt,
                                        result.response_text,
                                        result.model,
                                        result.http_status,
                                        result.metadata_json,
                                        result.message_json,
                                        result.reasoning_text,
                                        session_id_out,
                                        error_out)) {
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  response = result.response_text;
  result.response_text = NULL;

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
  strappy_session_message_record_list message_list;
  strappy_assistant_request_messages request_messages;
  char *response;

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

  if (!strappy_config_load(&config, env_path, error_out)) {
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_db_list_session_messages(session_db_path,
                                        session_id,
                                        &message_list,
                                        error_out)) {
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_assistant_prepare_request_messages(prompt,
                                                  system_prompt_template_path,
                                                  webview_user_agent,
                                                  &message_list,
                                                  &request_messages,
                                                  error_out)) {
    strappy_session_message_record_list_destroy(&message_list);
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
    strappy_assistant_request_messages_destroy(&request_messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_config_destroy(&config);
    return NULL;
  }

  strappy_assistant_request_messages_destroy(&request_messages);
  strappy_session_message_record_list_destroy(&message_list);

  if (!strappy_db_append_exchange_to_session(session_db_path,
                                             session_id,
                                             prompt,
                                             result.response_text,
                                             result.model,
                                             result.http_status,
                                             result.metadata_json,
                                             result.message_json,
                                             result.reasoning_text,
                                             error_out)) {
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  response = result.response_text;
  result.response_text = NULL;

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
