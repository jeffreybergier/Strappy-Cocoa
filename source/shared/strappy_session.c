#include "strappy_session.h"

#include "strappy_assistant_sets.h"
#include "strappy_core.h"
#include "strappy_model_catalog.h"
#include "strappy_responses.h"
#include "strappy_tools.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int strappy_session_ensure_directory_component(const char *path,
                                                      char **error_out)
{
  struct stat directory_stat;

  errno = 0;
  if (mkdir(path, 0755) == 0) {
    return 1;
  }
  if (errno != EEXIST) {
    strappy_set_formatted_error(error_out,
                                "Could not create working directory %s: %s.",
                                path,
                                strerror(errno));
    return 0;
  }
  errno = 0;
  if (stat(path, &directory_stat) != 0) {
    strappy_set_formatted_error(error_out,
                                "Could not inspect working directory %s: %s.",
                                path,
                                strerror(errno));
    return 0;
  }
  if (!S_ISDIR(directory_stat.st_mode)) {
    strappy_set_formatted_error(error_out,
                                "Working directory path is not a directory: %s.",
                                path);
    return 0;
  }
  return 1;
}

static int strappy_session_ensure_working_directory(
  const char *working_directory,
  char **error_out)
{
  char *path;
  char *cursor;
  size_t length;
  int ok;

  if ((working_directory == NULL) || (working_directory[0] == '\0')) {
    strappy_set_error(error_out, "Session working directory is empty.");
    return 0;
  }
  if (working_directory[0] != '/') {
    strappy_set_error(error_out,
                      "Session working directory must be an absolute path.");
    return 0;
  }

  path = strappy_string_duplicate(working_directory);
  if (path == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate the session working directory.");
    return 0;
  }
  length = strlen(path);
  while ((length > 1U) && (path[length - 1U] == '/')) {
    path[--length] = '\0';
  }

  ok = 1;
  cursor = path + 1;
  while (ok) {
    char saved;

    while ((*cursor != '\0') && (*cursor != '/')) {
      cursor++;
    }
    saved = *cursor;
    *cursor = '\0';
    if (path[1] != '\0') {
      ok = strappy_session_ensure_directory_component(path, error_out);
    }
    *cursor = saved;
    if (saved == '\0') {
      break;
    }
    cursor++;
    while (*cursor == '/') {
      cursor++;
    }
  }
  free(path);
  return ok;
}

void strappy_session_free_string(char *value)
{
  strappy_free_string(value);
}

int strappy_session_configure_process(const char *ca_cert_path,
                                      const char *font_dir,
                                      char **error_out)
{
  if (!strappy_webview_configure_localized_labels(error_out)) {
    return 0;
  }
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

int strappy_session_create_with_working_directory(
  const char *db_path,
  const char *working_directory,
  long long *session_id_out,
  char **error_out)
{
  if (session_id_out != NULL) {
    *session_id_out = 0LL;
  }
  if (!strappy_session_ensure_working_directory(working_directory,
                                                error_out)) {
    return 0;
  }
  return strappy_db_create_session_with_working_directory(db_path,
                                                          working_directory,
                                                          session_id_out,
                                                          error_out);
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

int strappy_session_load_list_record(const char *db_path,
                                     long long session_id,
                                     strappy_session_record *record,
                                     char **error_out)
{
  return strappy_db_load_session_list_record(db_path,
                                             session_id,
                                             record,
                                             error_out);
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

int strappy_session_list_message_records_after(
  const char *db_path,
  long long session_id,
  const strappy_response_timeline_cursor *after_cursor,
  strappy_session_message_record_list *list,
  strappy_response_timeline_cursor *next_cursor_out,
  char **error_out)
{
  return strappy_db_list_response_timeline_after(db_path,
                                                 session_id,
                                                 after_cursor,
                                                 list,
                                                 next_cursor_out,
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

int strappy_session_update_model_request_include_in_context(
  const char *db_path,
  long long session_id,
  long long model_request_id,
  int include_in_context,
  char **error_out)
{
  return strappy_db_update_model_request_include_in_context(
    db_path,
    session_id,
    model_request_id,
    include_in_context,
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

int strappy_session_update_web_provider(const char *db_path,
                                        long long session_id,
                                        strappy_web_provider web_provider,
                                        char **error_out)
{
  return strappy_db_update_session_web_provider(db_path,
                                                session_id,
                                                web_provider,
                                                error_out);
}

int strappy_session_update_bash_enabled(const char *db_path,
                                        long long session_id,
                                        int bash_enabled,
                                        char **error_out)
{
  return strappy_db_update_session_bash_enabled(db_path,
                                               session_id,
                                               bash_enabled,
                                               error_out);
}

int strappy_session_get_working_directory(
  const char *db_path,
  long long session_id,
  char **working_directory_out,
  char **error_out)
{
  return strappy_db_get_session_working_directory(db_path,
                                                  session_id,
                                                  working_directory_out,
                                                  error_out);
}

int strappy_session_update_working_directory(
  const char *db_path,
  long long session_id,
  const char *working_directory,
  char **error_out)
{
  if (!strappy_session_ensure_working_directory(working_directory,
                                                error_out)) {
    return 0;
  }
  return strappy_db_update_session_working_directory(db_path,
                                                     session_id,
                                                     working_directory,
                                                     error_out);
}

int strappy_session_list_assistant_sets(
  const char *resource_dir,
  strappy_assistant_set_record_list *list,
  char **error_out)
{
  return strappy_assistant_sets_list(resource_dir, list, error_out);
}

int strappy_session_update_assistant_set(const char *db_path,
                                         long long session_id,
                                         const char *resource_dir,
                                         const char *assistant_set_id,
                                         char **error_out)
{
  strappy_assistant_set_profile profile;
  int ok;

  strappy_assistant_set_profile_init(&profile);
  if (!strappy_assistant_sets_load_profile(resource_dir,
                                           assistant_set_id,
                                           &profile,
                                           error_out)) {
    return 0;
  }
  if (!strappy_assistant_set_profile_is_available(&profile)) {
    strappy_set_formatted_error(error_out,
                                "Assistant set is not available: %s",
                                assistant_set_id);
    strappy_assistant_set_profile_destroy(&profile);
    return 0;
  }
  ok = strappy_db_update_session_assistant_set(db_path,
                                               session_id,
                                               assistant_set_id,
                                               error_out);
  strappy_assistant_set_profile_destroy(&profile);
  return ok;
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

int strappy_session_send_prompt_with_events_and_load(
  const char *prompt,
  const char *api_endpoint,
  const char *api_token,
  const char *guidance_resource_dir,
  const char *db_path,
  long long session_id,
  strappy_responses_event_callback callback,
  void *callback_data,
  strappy_session_record *record,
  char **error_out)
{
  char *response;

  response = strappy_responses_send_prompt_for_session_and_store_with_events(
    prompt,
    NULL,
    api_endpoint,
    api_token,
    guidance_resource_dir,
    db_path,
    session_id,
    callback,
    callback_data,
    error_out);

  return strappy_session_load_after_prompt(db_path,
                                           session_id,
                                           response,
                                           record,
                                           error_out);
}

int strappy_session_send_isolated_prompt_with_events_and_load(
  const char *prompt,
  const char *api_endpoint,
  const char *api_token,
  const char *guidance_resource_dir,
  const char *db_path,
  long long session_id,
  strappy_responses_event_callback callback,
  void *callback_data,
  strappy_session_record *record,
  char **error_out)
{
  char *response;

  response =
    strappy_responses_send_isolated_prompt_for_session_and_store_with_events(
      prompt,
      NULL,
      api_endpoint,
      api_token,
      guidance_resource_dir,
      db_path,
      session_id,
      callback,
      callback_data,
      error_out);
  return strappy_session_load_after_prompt(db_path,
                                           session_id,
                                           response,
                                           record,
                                           error_out);
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

typedef struct strappy_session_database_display_context {
  strappy_database_display_name_record_list records;
  strappy_webview_database_display_name *names;
  size_t count;
} strappy_session_database_display_context;

static void strappy_session_database_display_context_init(
  strappy_session_database_display_context *context)
{
  if (context == NULL) {
    return;
  }
  strappy_database_display_name_record_list_init(&context->records);
  context->names = NULL;
  context->count = 0U;
}

static void strappy_session_database_display_context_destroy(
  strappy_session_database_display_context *context)
{
  if (context == NULL) {
    return;
  }
  free(context->names);
  strappy_database_display_name_record_list_destroy(&context->records);
  strappy_session_database_display_context_init(context);
}

static int strappy_session_database_display_context_load(
  const char *db_path,
  strappy_session_database_display_context *context,
  char **error_out)
{
  size_t index;

  if (context == NULL) {
    strappy_set_error(error_out,
                      "Database display context output is missing.");
    return 0;
  }
  strappy_session_database_display_context_init(context);
  if (!strappy_db_list_approved_database_display_names(
        db_path,
        &context->records,
        error_out)) {
    strappy_session_database_display_context_destroy(context);
    return 0;
  }
  if (context->records.count == 0U) {
    return 1;
  }
  if (context->records.count >
      (((size_t)-1) / sizeof(*context->names))) {
    strappy_session_database_display_context_destroy(context);
    strappy_set_error(error_out,
                      "Database display name list is too large.");
    return 0;
  }
  context->names = (strappy_webview_database_display_name *)calloc(
    context->records.count,
    sizeof(*context->names));
  if (context->names == NULL) {
    strappy_session_database_display_context_destroy(context);
    strappy_set_error(error_out,
                      "Could not allocate database display names.");
    return 0;
  }

  for (index = 0U; index < context->records.count; index++) {
    const strappy_database_display_name_record *record;

    record = &context->records.records[index];
    context->names[context->count].database_id =
      record->assistant_database_id;
    context->names[context->count].filename = record->filename;
    context->count++;
  }
  return 1;
}

static char *strappy_session_webview_messages_page_html_with_content(
  const char *messages_html,
  const char *resource_dir,
  const strappy_webview_database_display_name *database_display_names,
  size_t database_display_name_count,
  const char *error_text,
  const char *processing_status_json)
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
    database_display_names,
    database_display_name_count,
    error_text,
    processing_status_json);
  strappy_free_string(display_registry_json);
  strappy_free_string(display_error);
  return page_html;
}

char *strappy_session_webview_set_processing_status_js(
  const char *status_json)
{
  return strappy_webview_set_processing_status_js(status_json);
}

char *strappy_session_webview_set_round_context_inclusion_js(
  long long model_request_id,
  int include_in_context,
  int animated)
{
  return strappy_webview_set_round_context_inclusion_js(
    model_request_id,
    include_in_context,
    animated);
}

static void strappy_session_webview_message_from_record(
  const strappy_session_message_record *record,
  strappy_webview_message *message,
  const strappy_webview_database_display_name *database_display_names,
  size_t database_display_name_count)
{
  if (message == NULL) {
    return;
  }

  memset(message, 0, sizeof(*message));
  message->database_display_names = database_display_names;
  message->database_display_name_count = database_display_name_count;
  if (record == NULL) {
    return;
  }

  message->message_id = record->message_id;
  message->round_id = record->model_request_id;
  message->api_call_id = record->http_attempt_id;
  message->round_number = record->round_index + 1L;
  message->attempt_number = record->attempt_index + 1L;
  message->http_status = record->http_status;
  message->attempt_state = record->attempt_state;
  message->cumulative_usage_cost = record->cumulative_usage_cost;
  message->has_cumulative_usage_cost = record->has_cumulative_usage_cost;
  if ((record->render_role != NULL) &&
      (strcmp(record->render_role, "answer_quality") == 0)) {
    message->element_id = record->message_key;
  }
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
  message->response_item_action_json = record->response_item_action_json;
  message->response_item_url = record->response_item_url;
  message->response_item_title = record->response_item_title;
  message->response_item_status = record->response_item_status;
  message->response_item_http_status = record->response_item_http_status;
  message->request_method = record->request_method;
  message->request_endpoint = record->request_endpoint;
  message->text = record->content;
  message->reasoning = record->reasoning;
  message->metadata_json = record->metadata_json;
  message->render_state_json = record->render_state_json;
  message->created_at = record->created_at;
  message->can_include_in_context =
    record->can_include_in_context ? 1 : 0;
  message->include_in_context = record->include_in_context ? 1 : 0;
  message->is_error = record->is_error ? 1 : 0;
}

static char *strappy_session_webview_messages_html_for_record_range(
  const strappy_session_message_record_list *list,
  size_t start_index,
  size_t end_index,
  const strappy_webview_database_display_name *database_display_names,
  size_t database_display_name_count,
  char **error_out)
{
  strappy_webview_message *messages;
  char *messages_html;
  size_t count;
  size_t index;

  if (list == NULL) {
    strappy_set_error(error_out, "WebView message records are missing.");
    return NULL;
  }
  if ((start_index > end_index) || (end_index > list->count)) {
    strappy_set_error(error_out, "WebView message range is invalid.");
    return NULL;
  }

  count = end_index - start_index;
  if (count == 0U) {
    messages_html = strappy_webview_messages_html(
      NULL,
      0U,
      strappy_webview_localized_labels());
    if (messages_html == NULL) {
      strappy_set_error(error_out, "Could not allocate empty WebView HTML.");
    }
    return messages_html;
  }
  if (list->records == NULL) {
    strappy_set_error(error_out, "WebView message records are missing.");
    return NULL;
  }
  if (count > (((size_t)-1) / sizeof(*messages))) {
    strappy_set_error(error_out, "WebView message range is too large.");
    return NULL;
  }

  messages = (strappy_webview_message *)calloc(count, sizeof(*messages));
  if (messages == NULL) {
    strappy_set_error(error_out, "Could not allocate WebView messages.");
    return NULL;
  }

  for (index = 0U; index < count; index++) {
    strappy_session_webview_message_from_record(
      &list->records[start_index + index],
      &messages[index],
      database_display_names,
      database_display_name_count);
  }
  messages_html = strappy_webview_messages_html(
    messages,
    count,
    strappy_webview_localized_labels());
  free(messages);
  if (messages_html == NULL) {
    strappy_set_error(error_out, "Could not render WebView messages.");
  }
  return messages_html;
}

static char *strappy_session_timeline_cursor_string(
  const strappy_response_timeline_cursor *cursor,
  char **error_out)
{
  char buffer[256];
  int written;

  if ((cursor == NULL) || (cursor->session_id <= 0LL)) {
    strappy_set_error(error_out, "WebView timeline cursor is not valid.");
    return NULL;
  }
  written = snprintf(buffer,
                     sizeof(buffer),
                     "v1:%lld:%d:%lld:%lld:%lld:%lld:%lld:%lld:%lld",
                     cursor->session_id,
                     cursor->valid ? 1 : 0,
                     cursor->request_id,
                     cursor->group_phase,
                     cursor->attempt_index,
                     cursor->attempt_phase,
                     cursor->item_index,
                     cursor->entry_type,
                     cursor->row_id);
  if ((written < 0) || ((size_t)written >= sizeof(buffer))) {
    strappy_set_error(error_out, "Could not encode WebView timeline cursor.");
    return NULL;
  }
  return strappy_string_duplicate(buffer);
}

static int strappy_session_parse_timeline_cursor(
  const char *value,
  long long session_id,
  strappy_response_timeline_cursor *cursor,
  char **error_out)
{
  int consumed;
  int valid;

  if (cursor == NULL) {
    strappy_set_error(error_out, "WebView timeline cursor has no output.");
    return 0;
  }
  strappy_response_timeline_cursor_init(cursor);
  if ((value == NULL) || (value[0] == '\0')) {
    strappy_set_error(error_out, "WebView timeline cursor is empty.");
    return 0;
  }
  consumed = 0;
  valid = 0;
  if ((sscanf(value,
              "v1:%lld:%d:%lld:%lld:%lld:%lld:%lld:%lld:%lld%n",
              &cursor->session_id,
              &valid,
              &cursor->request_id,
              &cursor->group_phase,
              &cursor->attempt_index,
              &cursor->attempt_phase,
              &cursor->item_index,
              &cursor->entry_type,
              &cursor->row_id,
              &consumed) != 9) ||
      (value[consumed] != '\0') ||
      ((valid != 0) && (valid != 1)) ||
      (cursor->session_id != session_id)) {
    strappy_response_timeline_cursor_init(cursor);
    strappy_set_error(error_out, "WebView timeline cursor is not valid.");
    return 0;
  }
  cursor->valid = valid;
  if ((!cursor->valid &&
       ((cursor->request_id != 0LL) || (cursor->group_phase != 0LL) ||
        (cursor->attempt_index != 0LL) || (cursor->attempt_phase != 0LL) ||
        (cursor->item_index != 0LL) || (cursor->entry_type != 0LL) ||
        (cursor->row_id != 0LL))) ||
      (cursor->valid &&
       ((cursor->request_id <= 0LL) || (cursor->group_phase < 0LL) ||
        (cursor->attempt_index < -1LL) || (cursor->attempt_phase < 0LL) ||
        (cursor->item_index < -1LL) || (cursor->entry_type < 0LL) ||
        (cursor->entry_type > 2LL) || (cursor->row_id <= 0LL)))) {
    strappy_response_timeline_cursor_init(cursor);
    strappy_set_error(error_out, "WebView timeline cursor is not valid.");
    return 0;
  }
  return 1;
}

char *strappy_session_webview_messages_page_html_for_session(
  const char *db_path,
  long long session_id,
  const char *resource_dir,
  const char *error_text,
  const char *processing_status_json,
  size_t *message_count_out,
  char **timeline_cursor_out,
  char **error_out)
{
  strappy_session_database_display_context database_display;
  strappy_session_message_record_list list;
  const char *display_error;
  char *list_error;
  char *messages_html;
  char *page_html;
  strappy_response_timeline_cursor timeline_cursor;

  if (message_count_out != NULL) {
    *message_count_out = 0U;
  }
  if (timeline_cursor_out != NULL) {
    *timeline_cursor_out = NULL;
  }
  strappy_response_timeline_cursor_init(&timeline_cursor);
  timeline_cursor.session_id = session_id;

  strappy_session_database_display_context_init(&database_display);
  strappy_session_message_record_list_init(&list);
  list_error = NULL;
  if (!strappy_session_list_message_records(db_path,
                                            session_id,
                                            &list,
                                            &list_error)) {
    display_error = ((error_text != NULL) && (error_text[0] != '\0')) ?
      error_text : list_error;
    page_html = strappy_session_webview_messages_page_html_with_content(
      "",
      resource_dir,
      NULL,
      0U,
      display_error,
      processing_status_json);
    if (page_html == NULL) {
      strappy_set_error(error_out,
                        (list_error != NULL) ? list_error :
                          "Could not render the WebView error page.");
    }
    strappy_free_string(list_error);
    strappy_session_message_record_list_destroy(&list);
    strappy_session_database_display_context_destroy(&database_display);
    return page_html;
  }

  if (message_count_out != NULL) {
    *message_count_out = list.count;
  }
  if (list.count > 0U) {
    timeline_cursor = list.records[list.count - 1U].timeline_cursor;
  }
  if (timeline_cursor_out != NULL) {
    *timeline_cursor_out =
      strappy_session_timeline_cursor_string(&timeline_cursor, error_out);
    if (*timeline_cursor_out == NULL) {
      strappy_session_message_record_list_destroy(&list);
      strappy_session_database_display_context_destroy(&database_display);
      return NULL;
    }
  }
  if (!strappy_session_database_display_context_load(
        db_path,
        &database_display,
        error_out)) {
    if (timeline_cursor_out != NULL) {
      free(*timeline_cursor_out);
      *timeline_cursor_out = NULL;
    }
    strappy_session_message_record_list_destroy(&list);
    return NULL;
  }
  messages_html = strappy_session_webview_messages_html_for_record_range(
    &list,
    0U,
    list.count,
    database_display.names,
    database_display.count,
    error_out);
  strappy_session_message_record_list_destroy(&list);
  if (messages_html == NULL) {
    if (timeline_cursor_out != NULL) {
      free(*timeline_cursor_out);
      *timeline_cursor_out = NULL;
    }
    strappy_session_database_display_context_destroy(&database_display);
    return NULL;
  }

  page_html = strappy_session_webview_messages_page_html_with_content(
    messages_html,
    resource_dir,
    database_display.names,
    database_display.count,
    error_text,
    processing_status_json);
  free(messages_html);
  strappy_session_database_display_context_destroy(&database_display);
  if (page_html == NULL) {
    if (timeline_cursor_out != NULL) {
      free(*timeline_cursor_out);
      *timeline_cursor_out = NULL;
    }
    strappy_set_error(error_out, "Could not render the WebView page.");
  }
  return page_html;
}

char *strappy_session_webview_append_messages_js_for_session(
  const char *db_path,
  long long session_id,
  const char *timeline_cursor,
  size_t *appended_message_count_out,
  char **next_timeline_cursor_out,
  char **error_out)
{
  strappy_session_database_display_context database_display;
  strappy_session_message_record_list list;
  strappy_response_timeline_cursor after_cursor;
  strappy_response_timeline_cursor next_cursor;
  char *messages_html;
  char *script;

  if (appended_message_count_out != NULL) {
    *appended_message_count_out = 0U;
  }
  if (next_timeline_cursor_out != NULL) {
    *next_timeline_cursor_out = NULL;
  }

  strappy_session_database_display_context_init(&database_display);
  strappy_session_message_record_list_init(&list);
  strappy_response_timeline_cursor_init(&after_cursor);
  strappy_response_timeline_cursor_init(&next_cursor);
  if (!strappy_session_parse_timeline_cursor(timeline_cursor,
                                             session_id,
                                             &after_cursor,
                                             error_out) ||
      !strappy_session_list_message_records_after(
        db_path,
        session_id,
        after_cursor.valid ? &after_cursor : NULL,
        &list,
        &next_cursor,
        error_out)) {
    strappy_session_message_record_list_destroy(&list);
    strappy_session_database_display_context_destroy(&database_display);
    return NULL;
  }

  if (appended_message_count_out != NULL) {
    *appended_message_count_out = list.count;
  }
  if (next_timeline_cursor_out != NULL) {
    *next_timeline_cursor_out =
      strappy_session_timeline_cursor_string(&next_cursor, error_out);
    if (*next_timeline_cursor_out == NULL) {
      strappy_session_message_record_list_destroy(&list);
      strappy_session_database_display_context_destroy(&database_display);
      return NULL;
    }
  }
  if (list.count == 0U) {
    strappy_session_message_record_list_destroy(&list);
    strappy_session_database_display_context_destroy(&database_display);
    script = strappy_string_duplicate("");
    if (script == NULL) {
      if (next_timeline_cursor_out != NULL) {
        free(*next_timeline_cursor_out);
        *next_timeline_cursor_out = NULL;
      }
      strappy_set_error(error_out, "Could not allocate empty WebView JavaScript.");
    }
    return script;
  }

  if (!strappy_session_database_display_context_load(
        db_path,
        &database_display,
        error_out)) {
    if (next_timeline_cursor_out != NULL) {
      free(*next_timeline_cursor_out);
      *next_timeline_cursor_out = NULL;
    }
    strappy_session_message_record_list_destroy(&list);
    return NULL;
  }
  messages_html = strappy_session_webview_messages_html_for_record_range(
    &list,
    0U,
    list.count,
    database_display.names,
    database_display.count,
    error_out);
  strappy_session_message_record_list_destroy(&list);
  strappy_session_database_display_context_destroy(&database_display);
  if (messages_html == NULL) {
    if (next_timeline_cursor_out != NULL) {
      free(*next_timeline_cursor_out);
      *next_timeline_cursor_out = NULL;
    }
    return NULL;
  }

  script = strappy_webview_append_message_js(messages_html);
  free(messages_html);
  if (script == NULL) {
    if (next_timeline_cursor_out != NULL) {
      free(*next_timeline_cursor_out);
      *next_timeline_cursor_out = NULL;
    }
    strappy_set_error(error_out, "Could not render WebView append JavaScript.");
  }
  return script;
}

char *strappy_session_webview_message_update_js_for_key(
  const char *db_path,
  long long session_id,
  const char *message_key,
  char **error_out)
{
  strappy_session_database_display_context database_display;
  strappy_session_message_record record;
  strappy_webview_message message;
  char *script;

  strappy_session_database_display_context_init(&database_display);
  strappy_session_message_record_init(&record);
  if (!strappy_session_load_message_record_by_key(db_path,
                                                  session_id,
                                                  message_key,
                                                  &record,
                                                  error_out)) {
    strappy_session_message_record_destroy(&record);
    strappy_session_database_display_context_destroy(&database_display);
    return NULL;
  }

  if (!strappy_session_database_display_context_load(
        db_path,
        &database_display,
        error_out)) {
    strappy_session_message_record_destroy(&record);
    return NULL;
  }
  strappy_session_webview_message_from_record(
    &record,
    &message,
    database_display.names,
    database_display.count);
  script = strappy_webview_message_update_js(
    &message,
    strappy_webview_localized_labels());
  strappy_session_database_display_context_destroy(&database_display);
  strappy_session_message_record_destroy(&record);
  return script;
}
