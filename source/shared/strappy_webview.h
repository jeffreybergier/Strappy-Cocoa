#ifndef STRAPPY_WEBVIEW_H
#define STRAPPY_WEBVIEW_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STRAPPY_WEBVIEW_PROCESSING_WAITING_LABEL_COUNT 16U

typedef struct strappy_webview_labels {
  const char *agent;
  const char *you;
  const char *harness;
  const char *developer;
  const char *thinking;
  const char *processing_tools;
  const char *processing_waiting[
    STRAPPY_WEBVIEW_PROCESSING_WAITING_LABEL_COUNT];
  const char *processing_autoscroll_on;
  const char *processing_autoscroll_off;
  const char *response_metadata;
  const char *waiting_for_response;
  const char *no_http_response;
  const char *tool;
  const char *tool_call;
  const char *tool_result;
  const char *retry;
  const char *response_status;
  const char *api_error;
  const char *response_item;
  const char *request;
  const char *response;
  const char *round;
  const char *attempt;
  const char *included_in_future_context;
  const char *answer_quality;
  const char *passed;
  const char *failed;
  const char *not_applicable;
  const char *check;
  const char *answer_provided;
  const char *no_unicode_emoji;
  const char *source_link_included;
  const char *database_context_checked;
  const char *session_named;
  const char *fontawesome_shortcode_confirmed;
  const char *no_web_search_or_fetch_used;
  const char *unicode_emoji_found;
  const char *linked_source_reference_not_found;
  const char *check_did_not_apply;
  const char *required_tool_not_called;
} strappy_webview_labels;

typedef struct strappy_webview_database_display_name {
  const char *database_id;
  const char *filename;
} strappy_webview_database_display_name;

typedef struct strappy_webview_message {
  long long message_id;
  long long round_id;
  long long api_call_id;
  long round_number;
  long attempt_number;
  long http_status;
  const char *attempt_state;
  double cumulative_usage_cost;
  int has_cumulative_usage_cost;
  const char *element_id;
  const char *role;
  const char *kind;
  const char *actor;
  const char *prompt_group_key;
  const char *message_key;
  const char *target_message_key;
  const char *direction;
  const char *tool_call_id;
  const char *tool_name;
  const char *arguments_json;
  const char *result_json;
  const char *response_item_action_json;
  const char *response_item_url;
  const char *response_item_title;
  const char *response_item_status;
  const char *response_item_http_status;
  const char *request_method;
  const char *request_endpoint;
  const char *text;
  const char *reasoning;
  const char *metadata_json;
  const char *render_state_json;
  const char *created_at;
  int can_include_in_context;
  int include_in_context;
  int is_error;
  const strappy_webview_database_display_name *database_display_names;
  size_t database_display_name_count;
} strappy_webview_message;

typedef struct strappy_webview_script_batch strappy_webview_script_batch;

int strappy_webview_configure_localized_labels(char **error_out);
const strappy_webview_labels *strappy_webview_localized_labels(void);
void strappy_webview_set_font_dir(const char *abs_dir);
void strappy_webview_free(char *value);

strappy_webview_script_batch *strappy_webview_script_batch_create(void);
void strappy_webview_script_batch_destroy(
  strappy_webview_script_batch *batch);
int strappy_webview_script_batch_append_js(
  strappy_webview_script_batch *batch,
  const char *java_script);
int strappy_webview_script_batch_has_js(
  const strappy_webview_script_batch *batch);
char *strappy_webview_script_batch_finish_js(
  strappy_webview_script_batch *batch);

char *strappy_webview_status_html(const char *text,
                                  int retry,
                                  const strappy_webview_labels *labels);
char *strappy_webview_message_html(const strappy_webview_message *message,
                                   const strappy_webview_labels *labels,
                                   const char *state,
                                   const char *status_html);
char *strappy_webview_messages_html(
  const strappy_webview_message *messages,
  size_t count,
  const strappy_webview_labels *labels);
char *strappy_webview_message_update_js(
  const strappy_webview_message *message,
  const strappy_webview_labels *labels);
char *strappy_webview_pending_message_html(
  const char *prompt,
  const char *element_id,
  const char *state,
  const char *status_html,
  const strappy_webview_labels *labels);
char *strappy_webview_streaming_assistant_message_html(
  const char *element_id,
  const char *text,
  const char *reasoning,
  const char *state,
  const char *status_html,
  const char *actor,
  const char *prompt_group_key,
  const strappy_webview_labels *labels);
char *strappy_webview_tool_activity_message_html(
  const char *element_id,
  const char *text,
  const char *state,
  const char *status_html,
  const char *actor,
  const char *prompt_group_key,
  const char *target_element_id,
  const strappy_webview_labels *labels);
char *strappy_webview_set_message_prompt_group_js(
  const char *element_id,
  const char *prompt_group_key,
  const char *actor);
char *strappy_webview_message_html_with_reasoning(
  const strappy_webview_message *message,
  const char *reasoning,
  const strappy_webview_labels *labels);
char *strappy_webview_messages_page_html(
  const char *messages_html,
  const char *tool_display_registry_json,
  const strappy_webview_database_display_name *database_display_names,
  size_t database_display_name_count,
  const char *error_text,
  const char *processing_status_json);

char *strappy_webview_append_message_js(const char *message_html);
char *strappy_webview_replace_message_js(const char *element_id,
                                         const char *message_html);
char *strappy_webview_insert_message_before_js(const char *before_element_id,
                                               const char *message_html);
char *strappy_webview_set_message_state_js(const char *element_id,
                                           const char *status_html,
                                           const char *state);
char *strappy_webview_set_message_thinking_js(const char *element_id,
                                              const char *status_html);
char *strappy_webview_append_message_text_js(const char *element_id,
                                             const char *delta);
char *strappy_webview_append_reasoning_text_js(const char *element_id,
                                               const char *delta);
char *strappy_webview_append_message_text_by_key_js(const char *message_key,
                                                    const char *delta);
char *strappy_webview_append_reasoning_text_by_key_js(const char *message_key,
                                                      const char *delta);
char *strappy_webview_move_message_text_to_reasoning_js(const char *element_id);
char *strappy_webview_move_message_text_to_reasoning_by_key_js(
  const char *message_key);
char *strappy_webview_set_processing_status_js(const char *status_json);
char *strappy_webview_set_round_context_inclusion_js(
  long long round_id,
  int include_in_context,
  int animated);
char *strappy_webview_clear_processing_status_js(void);
char *strappy_webview_tool_event_text(const char *event_type,
                                      const char *tool_call_id,
                                      const char *tool_name,
                                      const char *arguments_json,
                                      const char *result_json);
char *strappy_webview_append_tool_event_text_js(const char *element_id,
                                                const char *event_text);
char *strappy_webview_remove_message_js(const char *element_id);

#ifdef __cplusplus
}
#endif

#endif
