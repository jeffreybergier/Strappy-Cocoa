#ifndef STRAPPY_SESSION_H
#define STRAPPY_SESSION_H

#include "strappy_events.h"
#include "strappy_assistant_sets.h"
#include "strappy_db.h"
#include "strappy_webview.h"

#ifdef __cplusplus
extern "C" {
#endif

void strappy_session_free_string(char *value);

typedef struct strappy_session_webview_render_context
  strappy_session_webview_render_context;

int strappy_session_configure_presentation(const char *font_dir,
                                      char **error_out);

int strappy_session_list_models_matching(
  const char *db_path,
  const char *search_text,
  strappy_model_record_list *list,
  char **error_out);
int strappy_session_list_models_for_configured_providers(
  const char *db_path,
  strappy_model_record_list *list,
  char **error_out);
int strappy_session_list_allowed_models(const char *db_path,
                                        strappy_model_record_list *list,
                                        char **error_out);
int strappy_session_get_default_model(const char *db_path,
                                      char **model_id_out,
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
int strappy_session_list_message_records(
  const char *db_path,
  long long session_id,
  strappy_session_message_record_list *list,
  char **error_out);
int strappy_session_list_message_records_after(
  const char *db_path,
  long long session_id,
  const strappy_response_timeline_cursor *after_cursor,
  strappy_session_message_record_list *list,
  strappy_response_timeline_cursor *next_cursor_out,
  char **error_out);
int strappy_session_load_message_record_by_key(
  const char *db_path,
  long long session_id,
  const char *message_key,
  strappy_session_message_record *record,
  char **error_out);
int strappy_session_load_options(
  const char *db_path,
  long long session_id,
  strappy_session_options *options,
  char **error_out);
int strappy_session_load_default_options(
  const char *db_path,
  const char *fallback_working_directory,
  strappy_session_options *options,
  char **error_out);

int strappy_session_get_working_directory(
  const char *db_path,
  long long session_id,
  char **working_directory_out,
  char **error_out);
int strappy_session_list_assistant_sets(
  const char *resource_dir,
  strappy_assistant_set_record_list *list,
  char **error_out);

char *strappy_session_webview_batched_js(const char *java_script);
char *strappy_session_timeline_cursor_for_session(
  const char *db_path,
  long long session_id,
  char **error_out);
char *strappy_session_webview_messages_page_html_for_session(
  const char *db_path,
  long long session_id,
  const char *resource_dir,
  const char *error_text,
  const char *processing_status_json,
  strappy_webview_palette palette,
  size_t *message_count_out,
  char **timeline_cursor_out,
  char **error_out);
char *strappy_session_webview_refresh_from_request_js_for_session(
  const char *db_path, long long session_id, const char *resource_dir,
  long long minimum_request, char **error_out);
char *strappy_session_webview_refresh_messages_js_for_session(
  const char *db_path, long long session_id, const char *resource_dir,
  char **error_out);
char *strappy_session_webview_append_messages_js_for_session(
  const char *db_path,
  long long session_id,
  const char *resource_dir,
  const char *timeline_cursor,
  size_t *appended_message_count_out,
  char **next_timeline_cursor_out,
  char **error_out);
strappy_session_webview_render_context *
strappy_session_webview_render_context_create(
  const char *db_path,
  const char *resource_dir,
  char **error_out);
void strappy_session_webview_render_context_destroy(
  strappy_session_webview_render_context *context);
char *strappy_session_webview_append_messages_js_with_render_context(
  const strappy_session_webview_render_context *context,
  long long session_id,
  const char *timeline_cursor,
  size_t *appended_message_count_out,
  char **next_timeline_cursor_out,
  char **error_out);
char *strappy_session_webview_reconcile_messages_js_for_session(
  const char *db_path,
  long long session_id,
  const char *resource_dir,
  const char *timeline_cursor,
  size_t *reconciled_message_count_out,
  char **next_timeline_cursor_out,
  char **error_out);
char *strappy_session_webview_reconcile_messages_js_with_render_context(
  const strappy_session_webview_render_context *context,
  long long session_id,
  const char *timeline_cursor,
  size_t *reconciled_message_count_out,
  char **next_timeline_cursor_out,
  char **error_out);
char *strappy_session_webview_set_processing_status_js(
  const char *status_json);
int strappy_session_webview_event_requires_message_update(
  const strappy_responses_event *event);
char *strappy_session_webview_set_round_context_inclusion_js(
  long long model_request_id,
  int include_in_context,
  int animated);
char *strappy_session_webview_message_update_js_for_key(
  const char *db_path,
  long long session_id,
  const char *resource_dir,
  const char *message_key,
  char **error_out);
char *strappy_session_webview_message_update_js_with_render_context(
  const strappy_session_webview_render_context *context,
  long long session_id,
  const char *message_key,
  char **error_out);

#ifdef __cplusplus
}
#endif

#endif
