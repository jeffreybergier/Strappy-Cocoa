#ifndef STRAPPY_SESSION_H
#define STRAPPY_SESSION_H

#include "strappy_client.h"
#include "strappy_assistant_sets.h"
#include "strappy_db.h"
#include "strappy_webview.h"

#ifdef __cplusplus
extern "C" {
#endif

void strappy_session_free_string(char *value);
const char *strappy_session_prompt_template_resource_name(void);
const char *strappy_session_prompt_template_resource_type(void);

int strappy_session_configure_process(const char *ca_cert_path,
                                      const char *font_dir,
                                      char **error_out);
int strappy_session_initialize_store(const char *db_path,
                                     char **error_out);

int strappy_session_list_openrouter_models_matching(
  const char *db_path,
  const char *search_text,
  strappy_openrouter_model_record_list *list,
  char **error_out);
int strappy_session_list_allowed_openrouter_models(
  const char *db_path,
  strappy_openrouter_model_record_list *list,
  char **error_out);
int strappy_session_get_default_openrouter_model(const char *db_path,
                                                 char **model_id_out,
                                                 char **error_out);
int strappy_session_set_default_openrouter_model(const char *db_path,
                                                 const char *model_id,
                                                 char **error_out);
int strappy_session_set_openrouter_model_allowed(const char *db_path,
                                                 const char *model_id,
                                                 int allowed,
                                                 char **error_out);
int strappy_session_refresh_openrouter_user_models(
  const char *api_endpoint,
  const char *api_token,
  const char *db_path,
  char **error_out);

int strappy_session_create(const char *db_path,
                           long long *session_id_out,
                           char **error_out);
int strappy_session_list_records(const char *db_path,
                                 strappy_session_record_list *list,
                                 char **error_out);
int strappy_session_load_record(const char *db_path,
                                long long session_id,
                                strappy_session_record *record,
                                char **error_out);
int strappy_session_load_list_record(const char *db_path,
                                     long long session_id,
                                     strappy_session_record *record,
                                     char **error_out);
int strappy_session_delete(const char *db_path,
                           long long session_id,
                           char **error_out);
int strappy_session_list_message_records(
  const char *db_path,
  long long session_id,
  strappy_session_message_record_list *list,
  char **error_out);
int strappy_session_list_message_records_from_index(
  const char *db_path,
  long long session_id,
  size_t start_index,
  strappy_session_message_record_list *list,
  size_t *total_count_out,
  char **error_out);
int strappy_session_load_message_record_by_key(
  const char *db_path,
  long long session_id,
  const char *message_key,
  strappy_session_message_record *record,
  char **error_out);
int strappy_session_update_streaming_enabled(const char *db_path,
                                             long long session_id,
                                             int streaming_enabled,
                                             char **error_out);
int strappy_session_update_web_search_enabled(const char *db_path,
                                              long long session_id,
                                              int web_search_enabled,
                                              char **error_out);
int strappy_session_list_assistant_sets(
  const char *resource_dir,
  strappy_assistant_set_record_list *list,
  char **error_out);
int strappy_session_update_assistant_set(const char *db_path,
                                         long long session_id,
                                         const char *resource_dir,
                                         const char *assistant_set_id,
                                         char **error_out);
int strappy_session_get_model(const char *db_path,
                              long long session_id,
                              char **model_id_out,
                              char **error_out);
int strappy_session_update_model(const char *db_path,
                                 long long session_id,
                                 const char *model_id,
                                 char **error_out);

int strappy_session_send_prompt_with_events_and_load(
  const char *prompt,
  const char *api_endpoint,
  const char *api_token,
  const char *system_prompt_template_path,
  const char *db_path,
  long long session_id,
  strappy_responses_event_callback callback,
  void *callback_data,
  strappy_session_record *record,
  char **error_out);

char *strappy_session_webview_batched_js(const char *java_script);
char *strappy_session_webview_messages_page_html_for_session(
  const char *db_path,
  long long session_id,
  const char *resource_dir,
  const char *error_text,
  size_t *message_count_out,
  char **error_out);
char *strappy_session_webview_append_messages_js_for_session(
  const char *db_path,
  long long session_id,
  size_t start_index,
  size_t *message_count_out,
  char **error_out);
char *strappy_session_webview_set_processing_status_js(
  const char *status_json);
char *strappy_session_webview_message_update_js_for_key(
  const char *db_path,
  long long session_id,
  const char *message_key,
  char **error_out);

#ifdef __cplusplus
}
#endif

#endif
