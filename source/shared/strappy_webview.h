#ifndef STRAPPY_WEBVIEW_H
#define STRAPPY_WEBVIEW_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct strappy_webview_labels {
  const char *agent;
  const char *you;
  const char *thinking;
  const char *request_metadata;
  const char *retry;
} strappy_webview_labels;

typedef struct strappy_webview_message {
  long long message_id;
  long http_status;
  const char *element_id;
  const char *role;
  const char *text;
  const char *reasoning;
  const char *metadata_json;
  const char *created_at;
} strappy_webview_message;

void strappy_webview_free(char *value);

char *strappy_webview_status_html(const char *text,
                                  int retry,
                                  const strappy_webview_labels *labels);
char *strappy_webview_message_html(const strappy_webview_message *message,
                                   const strappy_webview_labels *labels,
                                   const char *state,
                                   const char *status_html);
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
  const strappy_webview_labels *labels);
char *strappy_webview_message_html_with_reasoning(
  const strappy_webview_message *message,
  const char *reasoning,
  const strappy_webview_labels *labels);
char *strappy_webview_messages_page_html(const char *messages_html,
                                         const char *empty_text,
                                         int has_messages,
                                         int has_more,
                                         const char *load_more_label);

char *strappy_webview_append_message_js(const char *message_html);
char *strappy_webview_replace_message_js(const char *element_id,
                                         const char *message_html);
char *strappy_webview_set_message_state_js(const char *element_id,
                                           const char *status_html,
                                           const char *state);
char *strappy_webview_append_message_text_js(const char *element_id,
                                             const char *delta);
char *strappy_webview_append_reasoning_text_js(const char *element_id,
                                               const char *delta);
char *strappy_webview_remove_message_js(const char *element_id);
char *strappy_webview_prepend_messages_js(const char *messages_html,
                                          int has_more);

#ifdef __cplusplus
}
#endif

#endif
