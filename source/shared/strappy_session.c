#include "strappy_session.h"

#include "strappy_assistant_sets.h"
#include "strappy_core.h"
#include "strappy_tool_metadata.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void strappy_session_free_string(char *value)
{
  strappy_free_string(value);
}

int strappy_session_configure_presentation(const char *font_dir,
                                      char **error_out)
{
  if (!strappy_webview_configure_localized_labels(error_out)) {
    return 0;
  }
  strappy_webview_set_font_dir(font_dir);
  return 1;
}

int strappy_session_list_models_matching(
  const char *db_path,
  const char *search_text,
  strappy_model_record_list *list,
  char **error_out)
{
  return strappy_db_list_models_matching(db_path,
                                         search_text,
                                         list,
                                         error_out);
}

int strappy_session_list_models_for_configured_providers(
  const char *db_path,
  strappy_model_record_list *list,
  char **error_out)
{
  return strappy_db_list_models_for_configured_providers(db_path,
                                                          list,
                                                          error_out);
}

int strappy_session_list_allowed_models(
  const char *db_path,
  strappy_model_record_list *list,
  char **error_out)
{
  return strappy_db_list_allowed_models(db_path, list, error_out);
}

int strappy_session_get_default_model(const char *db_path,
                                      char **model_id_out,
                                      char **error_out)
{
  return strappy_db_get_default_model(db_path, model_id_out, error_out);
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
  return strappy_session_get_default_model(db_path,
                                           model_id_out,
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

int strappy_session_load_options(
  const char *db_path,
  long long session_id,
  strappy_session_options *options,
  char **error_out)
{
  return strappy_db_load_session_options(db_path,
                                         session_id,
                                         options,
                                         error_out);
}

int strappy_session_load_default_options(
  const char *db_path,
  const char *fallback_working_directory,
  strappy_session_options *options,
  char **error_out)
{
  return strappy_db_load_default_session_options(db_path,
                                                 fallback_working_directory,
                                                 options,
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

int strappy_session_list_assistant_sets(
  const char *resource_dir,
  strappy_assistant_set_record_list *list,
  char **error_out)
{
  return strappy_assistant_sets_list(resource_dir, list, error_out);
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

struct strappy_session_webview_render_context {
  char *db_path;
  strappy_session_database_display_context database_display;
  strappy_webview_render_context *webview;
};

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

strappy_session_webview_render_context *
strappy_session_webview_render_context_create(
  const char *db_path,
  const char *resource_dir,
  char **error_out)
{
  strappy_session_webview_render_context *context;
  char *display_registry_error;
  char *display_registry_json;

  if ((db_path == NULL) || (db_path[0] == '\0')) {
    strappy_set_error(error_out, "WebView render database path is empty.");
    return NULL;
  }
  context = (strappy_session_webview_render_context *)calloc(
    1U,
    sizeof(*context));
  if (context == NULL) {
    strappy_set_error(error_out, "Could not allocate WebView render context.");
    return NULL;
  }
  strappy_session_database_display_context_init(&context->database_display);
  context->db_path = strappy_string_duplicate(db_path);
  if ((context->db_path == NULL) ||
      !strappy_session_database_display_context_load(
        db_path,
        &context->database_display,
        error_out)) {
    strappy_session_webview_render_context_destroy(context);
    if ((error_out == NULL) || (*error_out == NULL)) {
      strappy_set_error(error_out, "Could not prepare WebView render context.");
    }
    return NULL;
  }

  display_registry_error = NULL;
  display_registry_json =
    strappy_tools_display_registry_json(resource_dir, &display_registry_error);
  context->webview = strappy_webview_render_context_create(
    (display_registry_json != NULL) ? display_registry_json : "{}");
  strappy_free_string(display_registry_json);
  strappy_free_string(display_registry_error);
  if (context->webview == NULL) {
    strappy_session_webview_render_context_destroy(context);
    strappy_set_error(error_out,
                      "Could not allocate the WebView display registry.");
    return NULL;
  }
  return context;
}

void strappy_session_webview_render_context_destroy(
  strappy_session_webview_render_context *context)
{
  if (context == NULL) {
    return;
  }
  free(context->db_path);
  strappy_session_database_display_context_destroy(&context->database_display);
  strappy_webview_render_context_destroy(context->webview);
  free(context);
}

static char *strappy_session_webview_messages_page_html_with_content(
  const char *messages_html,
  const char *tool_display_registry_json,
  const strappy_webview_database_display_name *database_display_names,
  size_t database_display_name_count,
  const char *error_text,
  const char *processing_status_json,
  strappy_webview_palette palette)
{
  return strappy_webview_messages_page_html(
    messages_html,
    (tool_display_registry_json != NULL) ? tool_display_registry_json : "{}",
    database_display_names,
    database_display_name_count,
    error_text,
    processing_status_json,
    palette);
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
  size_t database_display_name_count,
  const char *tool_display_registry_json)
{
  if (message == NULL) {
    return;
  }

  memset(message, 0, sizeof(*message));
  message->database_display_names = database_display_names;
  message->database_display_name_count = database_display_name_count;
  message->tool_display_registry_json = tool_display_registry_json;
  if (record == NULL) {
    return;
  }

  message->message_id = record->message_id;
  message->round_id = record->model_request_id;
  message->api_call_id = record->http_attempt_id;
  message->prompt_number = record->prompt_index + 1L;
  message->round_number = record->round_index + 1L;
  message->attempt_number = record->attempt_index + 1L;
  message->http_status = record->http_status;
  message->cumulative_usage_cost = record->cumulative_usage_cost;
  message->has_cumulative_usage_cost = record->has_cumulative_usage_cost;
  message->cumulative_wait_ms = record->cumulative_wait_ms;
  message->has_cumulative_wait_ms = record->has_cumulative_wait_ms;
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
  message->reasoning_encrypted = record->reasoning_encrypted;
  message->metadata_json = record->metadata_json;
  message->render_state_json = record->render_state_json;
  message->created_at = record->created_at;
  message->can_include_in_context =
    record->can_include_in_context ? 1 : 0;
  message->include_in_context = record->include_in_context ? 1 : 0;
  message->is_error = record->is_error ? 1 : 0;
}

static int strappy_session_webview_messages_for_record_range(
  const strappy_session_message_record_list *list,
  size_t start_index,
  size_t end_index,
  const strappy_webview_database_display_name *database_display_names,
  size_t database_display_name_count,
  const char *tool_display_registry_json,
  strappy_webview_message **messages_out,
  size_t *message_count_out,
  char **error_out)
{
  strappy_webview_message *messages;
  size_t count;
  size_t index;

  if ((messages_out == NULL) || (message_count_out == NULL)) {
    strappy_set_error(error_out, "WebView messages have no output.");
    return 0;
  }
  *messages_out = NULL;
  *message_count_out = 0U;
  if (list == NULL) {
    strappy_set_error(error_out, "WebView message records are missing.");
    return 0;
  }
  if ((start_index > end_index) || (end_index > list->count)) {
    strappy_set_error(error_out, "WebView message range is invalid.");
    return 0;
  }

  count = end_index - start_index;
  if (count == 0U) {
    return 1;
  }
  if (list->records == NULL) {
    strappy_set_error(error_out, "WebView message records are missing.");
    return 0;
  }
  if (count > (((size_t)-1) / sizeof(*messages))) {
    strappy_set_error(error_out, "WebView message range is too large.");
    return 0;
  }

  messages = (strappy_webview_message *)calloc(count, sizeof(*messages));
  if (messages == NULL) {
    strappy_set_error(error_out, "Could not allocate WebView messages.");
    return 0;
  }

  for (index = 0U; index < count; index++) {
    strappy_session_webview_message_from_record(
      &list->records[start_index + index],
      &messages[index],
      database_display_names,
      database_display_name_count,
      tool_display_registry_json);
  }
  *messages_out = messages;
  *message_count_out = count;
  return 1;
}

static char *strappy_session_webview_messages_html_for_record_range(
  const strappy_session_message_record_list *list,
  size_t start_index,
  size_t end_index,
  const strappy_webview_database_display_name *database_display_names,
  size_t database_display_name_count,
  const char *tool_display_registry_json,
  const strappy_webview_render_context *render_context,
  char **error_out)
{
  strappy_webview_message *messages;
  char *messages_html;
  size_t count;

  messages = NULL;
  count = 0U;
  if (!strappy_session_webview_messages_for_record_range(
        list,
        start_index,
        end_index,
        database_display_names,
        database_display_name_count,
        tool_display_registry_json,
        &messages,
        &count,
        error_out)) {
    return NULL;
  }
  messages_html = (render_context != NULL) ?
    strappy_webview_messages_html_with_render_context(
      messages,
      count,
      strappy_webview_localized_labels(),
      render_context) :
    strappy_webview_messages_html(messages,
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

char *strappy_session_timeline_cursor_for_session(
  const char *db_path,
  long long session_id,
  char **error_out)
{
  strappy_response_timeline_cursor cursor;

  strappy_response_timeline_cursor_init(&cursor);
  if (!strappy_db_load_response_timeline_cursor(db_path,
                                                session_id,
                                                &cursor,
                                                error_out)) {
    return NULL;
  }
  return strappy_session_timeline_cursor_string(&cursor, error_out);
}

char *strappy_session_webview_messages_page_html_for_session(
  const char *db_path,
  long long session_id,
  const char *resource_dir,
  const char *error_text,
  const char *processing_status_json,
  strappy_webview_palette palette,
  size_t *message_count_out,
  char **timeline_cursor_out,
  char **error_out)
{
  strappy_session_database_display_context database_display;
  strappy_session_message_record_list list;
  const char *display_error;
  char *display_registry_error;
  char *display_registry_json;
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
  display_registry_error = NULL;
  display_registry_json = NULL;

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
      "{}",
      NULL,
      0U,
      display_error,
      processing_status_json,
      palette);
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
  display_registry_json =
    strappy_tools_display_registry_json(resource_dir, &display_registry_error);
  messages_html = strappy_session_webview_messages_html_for_record_range(
    &list,
    0U,
    list.count,
    database_display.names,
    database_display.count,
    (display_registry_json != NULL) ? display_registry_json : "{}",
    NULL,
    error_out);
  strappy_session_message_record_list_destroy(&list);
  if (messages_html == NULL) {
    if (timeline_cursor_out != NULL) {
      free(*timeline_cursor_out);
      *timeline_cursor_out = NULL;
    }
    strappy_free_string(display_registry_json);
    strappy_free_string(display_registry_error);
    strappy_session_database_display_context_destroy(&database_display);
    return NULL;
  }

  page_html = strappy_session_webview_messages_page_html_with_content(
    messages_html,
    (display_registry_json != NULL) ? display_registry_json : "{}",
    database_display.names,
    database_display.count,
    error_text,
    processing_status_json,
    palette);
  free(messages_html);
  strappy_free_string(display_registry_json);
  strappy_free_string(display_registry_error);
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

static char *strappy_session_webview_delta_js_with_render_context(
  const strappy_session_webview_render_context *context,
  long long session_id,
  const char *timeline_cursor,
  size_t *message_count_out,
  char **next_timeline_cursor_out,
  int reconcile,
  char **error_out)
{
  strappy_session_message_record_list list;
  strappy_response_timeline_cursor after_cursor;
  strappy_response_timeline_cursor next_cursor;
  strappy_webview_message *messages;
  char *messages_html;
  char *script;
  size_t message_count;

  if (message_count_out != NULL) {
    *message_count_out = 0U;
  }
  if (next_timeline_cursor_out != NULL) {
    *next_timeline_cursor_out = NULL;
  }
  if ((context == NULL) || (context->db_path == NULL) ||
      (context->webview == NULL)) {
    strappy_set_error(error_out, "WebView render context is not valid.");
    return NULL;
  }

  strappy_session_message_record_list_init(&list);
  strappy_response_timeline_cursor_init(&after_cursor);
  strappy_response_timeline_cursor_init(&next_cursor);
  if ((reconcile != 2 && !strappy_session_parse_timeline_cursor(timeline_cursor,
                                             session_id,
                                             &after_cursor,
                                             error_out)) ||
      !strappy_session_list_message_records_after(
        context->db_path,
        session_id,
        after_cursor.valid ? &after_cursor : NULL,
        &list,
        &next_cursor,
        error_out)) {
    strappy_session_message_record_list_destroy(&list);
    return NULL;
  }

  if (message_count_out != NULL) {
    *message_count_out = list.count;
  }
  if (next_timeline_cursor_out != NULL) {
    *next_timeline_cursor_out =
      strappy_session_timeline_cursor_string(&next_cursor, error_out);
    if (*next_timeline_cursor_out == NULL) {
      strappy_session_message_record_list_destroy(&list);
      return NULL;
    }
  }
  if (list.count == 0U && reconcile != 2) {
    strappy_session_message_record_list_destroy(&list);
    script = strappy_string_duplicate("");
    if (script == NULL) {
      if (next_timeline_cursor_out != NULL) {
        free(*next_timeline_cursor_out);
        *next_timeline_cursor_out = NULL;
      }
      strappy_set_error(error_out,
                        "Could not allocate empty WebView JavaScript.");
    }
    return script;
  }

  script = NULL;
  if (reconcile == 1) {
    messages = NULL;
    message_count = 0U;
    if (strappy_session_webview_messages_for_record_range(
          &list,
          0U,
          list.count,
          context->database_display.names,
          context->database_display.count,
          NULL,
          &messages,
          &message_count,
          error_out)) {
      script = strappy_webview_reconcile_messages_js_with_render_context(
        messages,
        message_count,
        strappy_webview_localized_labels(),
        context->webview);
    }
    free(messages);
  } else {
    messages_html = strappy_session_webview_messages_html_for_record_range(
      &list,
      0U,
      list.count,
      context->database_display.names,
      context->database_display.count,
      NULL,
      context->webview,
      error_out);
    if (messages_html != NULL) {
      script = (reconcile == 2) ?
        strappy_webview_sync_messages_js(messages_html) :
        strappy_webview_append_message_js(messages_html);
      free(messages_html);
    }
  }
  strappy_session_message_record_list_destroy(&list);
  if (script == NULL) {
    if (next_timeline_cursor_out != NULL) {
      free(*next_timeline_cursor_out);
      *next_timeline_cursor_out = NULL;
    }
    if ((error_out == NULL) || (*error_out == NULL)) {
      strappy_set_error(
        error_out,
        reconcile ? "Could not render WebView reconciliation JavaScript." :
          "Could not render WebView append JavaScript.");
    }
  }
  return script;
}

char *strappy_session_webview_append_messages_js_with_render_context(
  const strappy_session_webview_render_context *context,
  long long session_id,
  const char *timeline_cursor,
  size_t *appended_message_count_out,
  char **next_timeline_cursor_out,
  char **error_out)
{
  return strappy_session_webview_delta_js_with_render_context(
    context,
    session_id,
    timeline_cursor,
    appended_message_count_out,
    next_timeline_cursor_out,
    0,
    error_out);
}

char *strappy_session_webview_reconcile_messages_js_with_render_context(
  const strappy_session_webview_render_context *context,
  long long session_id,
  const char *timeline_cursor,
  size_t *reconciled_message_count_out,
  char **next_timeline_cursor_out,
  char **error_out)
{
  return strappy_session_webview_delta_js_with_render_context(
    context,
    session_id,
    timeline_cursor,
    reconciled_message_count_out,
    next_timeline_cursor_out,
    1,
    error_out);
}

static char *strappy_session_webview_delta_js_for_session(
  const char *db_path,
  long long session_id,
  const char *resource_dir,
  const char *timeline_cursor,
  size_t *message_count_out,
  char **next_timeline_cursor_out,
  int reconcile,
  char **error_out)
{
  strappy_session_webview_render_context *context;
  char *script;

  context = strappy_session_webview_render_context_create(db_path,
                                                          resource_dir,
                                                          error_out);
  if (context == NULL) {
    return NULL;
  }
  script = strappy_session_webview_delta_js_with_render_context(
    context,
    session_id,
    timeline_cursor,
    message_count_out,
    next_timeline_cursor_out,
    reconcile,
    error_out);
  strappy_session_webview_render_context_destroy(context);
  return script;
}

/* Initial load and recovery reconcile all committed rows. Normal scoped
 * invalidations use the request-range renderer below. */
char *strappy_session_webview_refresh_messages_js_for_session(
  const char *db_path, long long session_id, const char *resource_dir,
  char **error_out)
{
  return strappy_session_webview_delta_js_for_session(
    db_path, session_id, resource_dir, NULL, NULL, NULL, 2, error_out);
}

char *strappy_session_webview_refresh_from_request_js_for_session(
  const char *db_path, long long session_id, const char *resource_dir,
  long long minimum_request, char **error_out)
{
  strappy_session_webview_render_context *context;
  strappy_session_message_record_list list;
  char *html = NULL, *script = NULL;
  context = strappy_session_webview_render_context_create(db_path, resource_dir, error_out);
  if (!context) return NULL;
  strappy_session_message_record_list_init(&list);
  if (strappy_db_list_response_timeline_from_request(db_path, session_id,
      minimum_request, &list, error_out)) {
    html = strappy_session_webview_messages_html_for_record_range(&list, 0U,
      list.count, context->database_display.names, context->database_display.count,
      NULL, context->webview, error_out);
    if (html) script = strappy_webview_sync_messages_from_request_js(html, minimum_request);
  }
  free(html);
  strappy_session_message_record_list_destroy(&list);
  strappy_session_webview_render_context_destroy(context);
  return script;
}

char *strappy_session_webview_append_messages_js_for_session(
  const char *db_path,
  long long session_id,
  const char *resource_dir,
  const char *timeline_cursor,
  size_t *appended_message_count_out,
  char **next_timeline_cursor_out,
  char **error_out)
{
  return strappy_session_webview_delta_js_for_session(
    db_path,
    session_id,
    resource_dir,
    timeline_cursor,
    appended_message_count_out,
    next_timeline_cursor_out,
    0,
    error_out);
}

char *strappy_session_webview_reconcile_messages_js_for_session(
  const char *db_path,
  long long session_id,
  const char *resource_dir,
  const char *timeline_cursor,
  size_t *reconciled_message_count_out,
  char **next_timeline_cursor_out,
  char **error_out)
{
  return strappy_session_webview_delta_js_for_session(
    db_path,
    session_id,
    resource_dir,
    timeline_cursor,
    reconciled_message_count_out,
    next_timeline_cursor_out,
    1,
    error_out);
}

int strappy_session_webview_event_requires_message_update(
  const strappy_responses_event *event)
{
  if (event == NULL) {
    return 0;
  }
  if (event->type == STRAPPY_RESPONSES_EVENT_LEDGER_UPDATED) {
    return 1;
  }
  if (event->type != STRAPPY_RESPONSES_EVENT_LEDGER_CHANGED) {
    return 0;
  }
  return (event->status_kind == NULL) ||
    (strcmp(event->status_kind, "running") != 0);
}

char *strappy_session_webview_message_update_js_with_render_context(
  const strappy_session_webview_render_context *context,
  long long session_id,
  const char *message_key,
  char **error_out)
{
  strappy_session_message_record record;
  strappy_webview_message message;
  char *script;

  if ((context == NULL) || (context->db_path == NULL) ||
      (context->webview == NULL)) {
    strappy_set_error(error_out, "WebView render context is not valid.");
    return NULL;
  }
  strappy_session_message_record_init(&record);
  if (!strappy_session_load_message_record_by_key(context->db_path,
                                                  session_id,
                                                  message_key,
                                                  &record,
                                                  error_out)) {
    strappy_session_message_record_destroy(&record);
    return NULL;
  }
  strappy_session_webview_message_from_record(
    &record,
    &message,
    context->database_display.names,
    context->database_display.count,
    NULL);
  script = strappy_webview_message_update_js_with_render_context(
    &message,
    strappy_webview_localized_labels(),
    context->webview);
  strappy_session_message_record_destroy(&record);
  if ((script == NULL) && ((error_out == NULL) || (*error_out == NULL))) {
    strappy_set_error(error_out, "Could not render WebView update JavaScript.");
  }
  return script;
}

char *strappy_session_webview_message_update_js_for_key(
  const char *db_path,
  long long session_id,
  const char *resource_dir,
  const char *message_key,
  char **error_out)
{
  strappy_session_webview_render_context *context;
  char *script;

  context = strappy_session_webview_render_context_create(db_path,
                                                          resource_dir,
                                                          error_out);
  if (context == NULL) {
    return NULL;
  }
  script = strappy_session_webview_message_update_js_with_render_context(
    context,
    session_id,
    message_key,
    error_out);
  strappy_session_webview_render_context_destroy(context);
  return script;
}
