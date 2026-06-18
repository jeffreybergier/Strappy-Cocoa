#include "strappy_assistant.h"

#include "strappy_config.h"
#include "strappy_core.h"
#include "strappy_client.h"
#include "strappy_db.h"

#include <stdlib.h>

static char *strappy_assistant_send_prompt_internal(const char *prompt,
                                                    const char *env_path,
                                                    const char *session_db_path,
                                                    int should_store,
                                                    long long *session_id_out,
                                                    char **error_out)
{
  strappy_config config;
  strappy_chat_result result;
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

  if (!strappy_config_load(&config, env_path, error_out)) {
    return NULL;
  }

  if (!strappy_client_send_prompt(&config, prompt, &result, error_out)) {
    strappy_config_destroy(&config);
    return NULL;
  }

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
  const char *session_db_path,
  long long session_id,
  char **error_out)
{
  strappy_config config;
  strappy_chat_result result;
  strappy_session_message_record_list message_list;
  strappy_chat_message *messages;
  char *response;
  size_t index;

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
  messages = NULL;

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

  if (message_list.count >= (((size_t)-1) / sizeof(strappy_chat_message))) {
    strappy_session_message_record_list_destroy(&message_list);
    strappy_config_destroy(&config);
    strappy_set_error(error_out, "Session message list is too large.");
    return NULL;
  }

  messages = (strappy_chat_message *)malloc(
    (message_list.count + 1U) * sizeof(strappy_chat_message));
  if (messages == NULL) {
    strappy_session_message_record_list_destroy(&message_list);
    strappy_config_destroy(&config);
    strappy_set_error(error_out, "Could not allocate session request messages.");
    return NULL;
  }

  for (index = 0U; index < message_list.count; index++) {
    messages[index].role = message_list.records[index].role;
    messages[index].content = message_list.records[index].content;
    messages[index].message_json = message_list.records[index].message_json;
  }
  messages[message_list.count].role = "user";
  messages[message_list.count].content = prompt;
  messages[message_list.count].message_json = NULL;

  if (!strappy_client_send_messages(&config,
                                    messages,
                                    message_list.count + 1U,
                                    &result,
                                    error_out)) {
    free(messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_config_destroy(&config);
    return NULL;
  }

  free(messages);
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
  const char *session_db_path,
  long long *session_id_out,
  strappy_chat_stream_callback callback,
  void *callback_data,
  char **error_out)
{
  strappy_config config;
  strappy_chat_result result;
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

  if (!strappy_config_load(&config, env_path, error_out)) {
    strappy_config_destroy(&config);
    return NULL;
  }

  if (!strappy_client_stream_prompt(&config,
                                    prompt,
                                    &result,
                                    callback,
                                    callback_data,
                                    error_out)) {
    strappy_config_destroy(&config);
    return NULL;
  }

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
  const char *session_db_path,
  long long session_id,
  strappy_chat_stream_callback callback,
  void *callback_data,
  char **error_out)
{
  strappy_config config;
  strappy_chat_result result;
  strappy_session_message_record_list message_list;
  strappy_chat_message *messages;
  char *response;
  size_t index;

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
  messages = NULL;

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

  if (message_list.count >= (((size_t)-1) / sizeof(strappy_chat_message))) {
    strappy_session_message_record_list_destroy(&message_list);
    strappy_config_destroy(&config);
    strappy_set_error(error_out, "Session message list is too large.");
    return NULL;
  }

  messages = (strappy_chat_message *)malloc(
    (message_list.count + 1U) * sizeof(strappy_chat_message));
  if (messages == NULL) {
    strappy_session_message_record_list_destroy(&message_list);
    strappy_config_destroy(&config);
    strappy_set_error(error_out, "Could not allocate session request messages.");
    return NULL;
  }

  for (index = 0U; index < message_list.count; index++) {
    messages[index].role = message_list.records[index].role;
    messages[index].content = message_list.records[index].content;
    messages[index].message_json = message_list.records[index].message_json;
  }
  messages[message_list.count].role = "user";
  messages[message_list.count].content = prompt;
  messages[message_list.count].message_json = NULL;

  if (!strappy_client_stream_messages(&config,
                                      messages,
                                      message_list.count + 1U,
                                      &result,
                                      callback,
                                      callback_data,
                                      error_out)) {
    free(messages);
    strappy_session_message_record_list_destroy(&message_list);
    strappy_config_destroy(&config);
    return NULL;
  }

  free(messages);
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
                                    char **error_out)
{
  return strappy_assistant_send_prompt_internal(prompt,
                                               env_path,
                                               NULL,
                                               0,
                                               NULL,
                                               error_out);
}

char *strappy_assistant_send_prompt_and_store(const char *prompt,
                                             const char *env_path,
                                             const char *session_db_path,
                                             char **error_out)
{
  return strappy_assistant_send_prompt_internal(prompt,
                                               env_path,
                                               session_db_path,
                                               1,
                                               NULL,
                                               error_out);
}

char *strappy_assistant_send_prompt_and_store_with_id(const char *prompt,
                                                     const char *env_path,
                                                     const char *session_db_path,
                                                     long long *session_id_out,
                                                     char **error_out)
{
  return strappy_assistant_send_prompt_internal(prompt,
                                               env_path,
                                               session_db_path,
                                               1,
                                               session_id_out,
                                               error_out);
}

char *strappy_assistant_send_prompt_for_session_and_store(const char *prompt,
                                                         const char *env_path,
                                                         const char *session_db_path,
                                                         long long session_id,
                                                         char **error_out)
{
  return strappy_assistant_send_prompt_for_session_internal(prompt,
                                                           env_path,
                                                           session_db_path,
                                                           session_id,
                                                           error_out);
}

char *strappy_assistant_stream_prompt_and_store_with_id(
  const char *prompt,
  const char *env_path,
  const char *session_db_path,
  long long *session_id_out,
  strappy_chat_stream_callback callback,
  void *callback_data,
  char **error_out)
{
  return strappy_assistant_stream_prompt_internal(prompt,
                                                 env_path,
                                                 session_db_path,
                                                 session_id_out,
                                                 callback,
                                                 callback_data,
                                                 error_out);
}

char *strappy_assistant_stream_prompt_for_session_and_store(
  const char *prompt,
  const char *env_path,
  const char *session_db_path,
  long long session_id,
  strappy_chat_stream_callback callback,
  void *callback_data,
  char **error_out)
{
  return strappy_assistant_stream_prompt_for_session_internal(prompt,
                                                             env_path,
                                                             session_db_path,
                                                             session_id,
                                                             callback,
                                                             callback_data,
                                                             error_out);
}
