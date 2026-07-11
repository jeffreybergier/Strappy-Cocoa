#include "strappy_session.h"

#include "strappy_core.h"
#include "strappy_model_catalog.h"
#include "strappy_prompt.h"
#include "strappy_responses.h"
#include "strappy_tools.h"

#include <string.h>

void strappy_session_free_string(char *value)
{
  strappy_free_string(value);
}

const char *strappy_session_prompt_template_resource_name(void)
{
  return STRAPPY_PROMPT_TEMPLATE_RESOURCE_NAME;
}

const char *strappy_session_prompt_template_resource_type(void)
{
  return STRAPPY_PROMPT_TEMPLATE_RESOURCE_TYPE;
}

int strappy_session_configure_process(const char *ca_cert_path,
                                      const char *font_dir,
                                      char **error_out)
{
  if (!strappy_client_set_cainfo(ca_cert_path, error_out)) {
    return 0;
  }

  strappy_webview_set_font_dir(font_dir);
  return 1;
}

int strappy_session_initialize_store(const char *db_path,
                                     char **error_out)
{
  return strappy_db_initialize(db_path, error_out);
}

int strappy_session_list_openrouter_models_matching(
  const char *db_path,
  const char *search_text,
  strappy_openrouter_model_record_list *list,
  char **error_out)
{
  return strappy_db_list_openrouter_models_matching(db_path,
                                                   search_text,
                                                   list,
                                                   error_out);
}

int strappy_session_list_allowed_openrouter_models(
  const char *db_path,
  strappy_openrouter_model_record_list *list,
  char **error_out)
{
  return strappy_db_list_allowed_openrouter_models(db_path, list, error_out);
}

int strappy_session_get_default_openrouter_model(const char *db_path,
                                                 char **model_id_out,
                                                 char **error_out)
{
  return strappy_db_get_default_openrouter_model(db_path,
                                                model_id_out,
                                                error_out);
}

int strappy_session_set_default_openrouter_model(const char *db_path,
                                                 const char *model_id,
                                                 char **error_out)
{
  return strappy_db_set_default_openrouter_model(db_path,
                                                model_id,
                                                error_out);
}

int strappy_session_set_openrouter_model_allowed(const char *db_path,
                                                 const char *model_id,
                                                 int allowed,
                                                 char **error_out)
{
  return strappy_db_set_openrouter_model_allowed(db_path,
                                                model_id,
                                                allowed,
                                                error_out);
}

int strappy_session_refresh_openrouter_user_models(
  const char *api_endpoint,
  const char *api_token,
  const char *db_path,
  char **error_out)
{
  return strappy_model_catalog_refresh_openrouter_user_models(NULL,
                                                             api_endpoint,
                                                             api_token,
                                                             db_path,
                                                             error_out);
}

int strappy_session_create(const char *db_path,
                           long long *session_id_out,
                           char **error_out)
{
  return strappy_db_create_session(db_path, session_id_out, error_out);
}

int strappy_session_list_records(const char *db_path,
                                 strappy_session_record_list *list,
                                 char **error_out)
{
  return strappy_db_list_sessions(db_path, list, error_out);
}

int strappy_session_load_record(const char *db_path,
                                long long session_id,
                                strappy_session_record *record,
                                char **error_out)
{
  return strappy_db_load_session(db_path, session_id, record, error_out);
}

int strappy_session_delete(const char *db_path,
                           long long session_id,
                           char **error_out)
{
  return strappy_db_delete_session(db_path, session_id, error_out);
}

int strappy_session_list_message_records(
  const char *db_path,
  long long session_id,
  strappy_session_message_record_list *list,
  char **error_out)
{
  return strappy_db_list_response_timeline(db_path,
                                          session_id,
                                          list,
                                          error_out);
}

int strappy_session_load_message_record_by_key(
  const char *db_path,
  long long session_id,
  const char *message_key,
  strappy_session_message_record *record,
  char **error_out)
{
  return strappy_db_load_session_message_by_key(db_path,
                                               session_id,
                                               message_key,
                                               record,
                                               error_out);
}

int strappy_session_update_streaming_enabled(const char *db_path,
                                             long long session_id,
                                             int streaming_enabled,
                                             char **error_out)
{
  return strappy_db_update_session_streaming_enabled(db_path,
                                                    session_id,
                                                    streaming_enabled,
                                                    error_out);
}

int strappy_session_update_web_search_enabled(const char *db_path,
                                              long long session_id,
                                              int web_search_enabled,
                                              char **error_out)
{
  return strappy_db_update_session_web_search_enabled(db_path,
                                                     session_id,
                                                     web_search_enabled,
                                                     error_out);
}

int strappy_session_get_model(const char *db_path,
                              long long session_id,
                              char **model_id_out,
                              char **error_out)
{
  return strappy_db_get_session_model(db_path,
                                     session_id,
                                     model_id_out,
                                     error_out);
}

int strappy_session_update_model(const char *db_path,
                                 long long session_id,
                                 const char *model_id,
                                 char **error_out)
{
  return strappy_db_update_session_model(db_path,
                                        session_id,
                                        model_id,
                                        error_out);
}

static int strappy_session_load_after_prompt(const char *db_path,
                                             long long session_id,
                                             char *response,
                                             strappy_session_record *record,
                                             char **error_out)
{
  if (response == NULL) {
    return 0;
  }

  strappy_free_string(response);
  return strappy_session_load_record(db_path, session_id, record, error_out);
}

int strappy_session_send_prompt_and_load(
  const char *prompt,
  const char *api_endpoint,
  const char *api_token,
  const char *system_prompt_template_path,
  const char *db_path,
  long long session_id,
  strappy_session_record *record,
  char **error_out)
{
  char *response;

  response =
    strappy_responses_send_prompt_for_session_and_store(
      prompt,
      NULL,
      api_endpoint,
      api_token,
      system_prompt_template_path,
      db_path,
      session_id,
      error_out);
  return strappy_session_load_after_prompt(db_path,
                                           session_id,
                                           response,
                                           record,
                                           error_out);
}

int strappy_session_submit_prompt_with_events_and_load(
  const char *prompt,
  const char *api_endpoint,
  const char *api_token,
  const char *system_prompt_template_path,
  const char *db_path,
  long long session_id,
  int streaming,
  strappy_chat_stream_callback callback,
  void *callback_data,
  strappy_session_record *record,
  char **error_out)
{
  char *response;

  if (streaming) {
    (void)callback;
    (void)callback_data;
    strappy_set_error(error_out,
                      "Streaming Responses API support is intentionally disabled.");
    return 0;
  } else {
    response =
      strappy_responses_send_prompt_for_session_and_store_with_events(
      prompt,
      NULL,
      api_endpoint,
      api_token,
      system_prompt_template_path,
      db_path,
      session_id,
      callback,
      callback_data,
      error_out);
  }

  return strappy_session_load_after_prompt(db_path,
                                           session_id,
                                           response,
                                           record,
                                           error_out);
}

char *strappy_session_webview_message_html(
  const strappy_webview_message *message,
  const strappy_webview_labels *labels,
  const char *state,
  const char *status_html)
{
  return strappy_webview_message_html(message, labels, state, status_html);
}

void strappy_session_webview_message_init(strappy_webview_message *message)
{
  if (message == NULL) {
    return;
  }

  memset(message, 0, sizeof(*message));
}

char *strappy_session_webview_append_messages_js(const char *messages_html)
{
  return strappy_webview_append_message_js(messages_html);
}

char *strappy_session_webview_batched_js(const char *java_script)
{
  strappy_webview_script_batch *batch;
  char *script;

  if ((java_script == NULL) || (java_script[0] == '\0')) {
    return NULL;
  }

  batch = strappy_webview_script_batch_create();
  if (batch == NULL) {
    return NULL;
  }

  if (!strappy_webview_script_batch_append_js(batch, java_script)) {
    strappy_webview_script_batch_destroy(batch);
    return NULL;
  }

  script = strappy_webview_script_batch_finish_js(batch);
  strappy_webview_script_batch_destroy(batch);
  return script;
}

char *strappy_session_webview_messages_page_html(const char *messages_html,
                                                 const char *resource_dir,
                                                 const char *error_text)
{
  char *display_registry_json;
  char *display_error;
  char *page_html;

  display_error = NULL;
  display_registry_json =
    strappy_tools_display_registry_json(resource_dir, &display_error);
  page_html = strappy_webview_messages_page_html(
    messages_html,
    (display_registry_json != NULL) ? display_registry_json : "{}",
    error_text);
  strappy_free_string(display_registry_json);
  strappy_free_string(display_error);
  return page_html;
}

char *strappy_session_webview_append_message_text_by_key_js(
  const char *message_key,
  const char *delta)
{
  return strappy_webview_append_message_text_by_key_js(message_key, delta);
}

char *strappy_session_webview_append_reasoning_text_by_key_js(
  const char *message_key,
  const char *delta)
{
  return strappy_webview_append_reasoning_text_by_key_js(message_key, delta);
}

char *strappy_session_webview_move_message_text_to_reasoning_by_key_js(
  const char *message_key)
{
  return strappy_webview_move_message_text_to_reasoning_by_key_js(message_key);
}

char *strappy_session_webview_set_processing_status_js(
  const char *status_json)
{
  return strappy_webview_set_processing_status_js(status_json);
}

char *strappy_session_webview_clear_processing_status_js(void)
{
  return strappy_webview_clear_processing_status_js();
}

char *strappy_session_webview_message_update_js(
  const strappy_webview_message *message,
  const strappy_webview_labels *labels)
{
  return strappy_webview_message_update_js(message, labels);
}

static void strappy_session_webview_message_from_record(
  const strappy_session_message_record *record,
  strappy_webview_message *message)
{
  if (message == NULL) {
    return;
  }

  memset(message, 0, sizeof(*message));
  if (record == NULL) {
    return;
  }

  message->message_id = record->message_id;
  message->api_call_id = record->turn_id;
  message->round_number = record->round_index + 1L;
  message->attempt_number = record->attempt_index + 1L;
  message->http_status = record->http_status;
  message->role = record->role;
  message->kind = record->kind;
  message->actor = record->actor;
  message->prompt_group_key = record->prompt_group_key;
  message->message_key = record->message_key;
  message->target_message_key = record->target_message_key;
  message->direction = record->direction;
  message->tool_call_id = record->tool_call_id;
  message->tool_name = record->tool_name;
  message->arguments_json = record->arguments_json;
  message->result_json = record->result_json;
  message->text = record->content;
  message->reasoning = record->reasoning;
  message->metadata_json = record->metadata_json;
  message->render_state_json = record->render_state_json;
  message->created_at = record->created_at;
  message->is_error = record->is_error ? 1 : 0;
}

char *strappy_session_webview_message_update_js_for_key(
  const char *db_path,
  long long session_id,
  const char *message_key,
  const strappy_webview_labels *labels,
  char **error_out)
{
  strappy_session_message_record record;
  strappy_webview_message message;
  char *script;

  strappy_session_message_record_init(&record);
  if (!strappy_session_load_message_record_by_key(db_path,
                                                  session_id,
                                                  message_key,
                                                  &record,
                                                  error_out)) {
    strappy_session_message_record_destroy(&record);
    return NULL;
  }

  strappy_session_webview_message_from_record(&record, &message);
  script = strappy_webview_message_update_js(&message, labels);
  strappy_session_message_record_destroy(&record);
  return script;
}
