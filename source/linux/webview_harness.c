#include "strappy_webview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int harness_expect_contains(const char *text, const char *needle)
{
  if ((text != NULL) && (needle != NULL) && (strstr(text, needle) != NULL)) {
    return 1;
  }

  fprintf(stderr, "Expected generated webview output to contain: %s\n",
          (needle != NULL) ? needle : "(null)");
  return 0;
}

static int harness_expect_not_contains(const char *text, const char *needle)
{
  if ((text == NULL) || (needle == NULL) || (strstr(text, needle) == NULL)) {
    return 1;
  }

  fprintf(stderr, "Expected generated webview output not to contain: %s\n",
          needle);
  return 0;
}

static int harness_check_page_scripts(void)
{
  strappy_webview_message message;
  char *message_html;
  char *page_html;
  int ok;

  memset(&message, 0, sizeof(message));
  message.element_id = "assistant-1";
  message.role = "assistant";
  message.actor = "user";
  message.prompt_group_key = "prompt-group-page";
  message.text = "Done.";

  message_html = strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (message_html == NULL) {
    fprintf(stderr, "Could not generate assistant message HTML.\n");
    return 0;
  }

  page_html = strappy_webview_messages_page_html(message_html,
                                                "Empty",
                                                1,
                                                0,
                                                "Load More");
  strappy_webview_free(message_html);
  if (page_html == NULL) {
    fprintf(stderr, "Could not generate messages page HTML.\n");
    return 0;
  }

  ok = harness_expect_contains(page_html, "@font-face") &&
       harness_expect_contains(page_html,
                               "font:13px -apple-system,Helvetica,Arial,sans-serif;") &&
       harness_expect_contains(page_html,
                               ".tool-panel{font:12px -apple-system,Helvetica,Arial,sans-serif;") &&
       harness_expect_contains(page_html, "function faIconHTML") &&
       harness_expect_contains(page_html, "\"heart\":'F004'") &&
       harness_expect_contains(page_html, "[fa(?::(solid|regular|brands))?") &&
       harness_expect_contains(page_html, "function toolJSONHTML") &&
       harness_expect_contains(page_html, "function toolObjectArrayTable") &&
       harness_expect_contains(page_html, "function toolOutputHasError") &&
       harness_expect_contains(page_html, "c.error=toolOutputHasError(raw)") &&
       harness_expect_contains(page_html, "function toolUsesDatabaseId") &&
       harness_expect_contains(page_html, "database_context_read") &&
       harness_expect_contains(page_html, "function beginMessageBatch") &&
       harness_expect_contains(page_html, "function scrollBottomNow") &&
       harness_expect_contains(page_html, "function shouldAutoScroll") &&
       harness_expect_contains(page_html, ".page{padding:18px 10px;}") &&
       harness_expect_contains(page_html, ".processing-status{position:fixed") &&
       harness_expect_contains(page_html,
                               ".bubble,.reasoning,.tool-column,.request-metadata{") &&
       harness_expect_contains(page_html,
                               "box-shadow:0 2px 9px rgba(0,0,0,.12);") &&
       harness_expect_contains(page_html, "function setProcessingStatus") &&
       harness_expect_contains(page_html, "function clearProcessingStatus") &&
       harness_expect_contains(page_html, "function initProcessingStatusFromRenderState") &&
       harness_expect_contains(page_html, "processing_status") &&
       harness_expect_contains(page_html, "function shouldRenderMarkdownBubble") &&
       harness_expect_contains(page_html, "function shouldRenderMarkdownReasoning") &&
       harness_expect_contains(page_html, "ancestorHasClass(n,'assistant')") &&
       harness_expect_contains(page_html, ".reasoning-body{white-space:normal;}") &&
       harness_expect_contains(page_html,
                               "shouldRenderMarkdownBubble(n[i])||"
                               "shouldRenderMarkdownReasoning(n[i])") &&
       harness_expect_contains(page_html, "_strappyMarkdownRendered") &&
       harness_expect_contains(page_html, "function renderStreamingTextNode") &&
       harness_expect_contains(page_html,
                               "q.kind=='reasoning'){if(typeof n._strappyMarkdown") &&
       harness_expect_contains(page_html, "queueTextAppend(id,t,'content')") &&
       harness_expect_contains(page_html, "appendMessageTextByMessageKey") &&
       harness_expect_contains(page_html, "appendReasoningTextByMessageKey") &&
       harness_expect_contains(page_html, "function setToolBoxCount") &&
       harness_expect_contains(page_html, "setToolBoxCount(box,cards.length,last") &&
       harness_expect_contains(page_html, "toolCardSummary(last,cards.length-1)") &&
       harness_expect_contains(page_html,
                               "tool-column-collapsed .tool-rail-title") &&
       harness_expect_contains(page_html, "Raw JSON preview") &&
       harness_expect_contains(page_html, "tool-column tool-column-empty") &&
       harness_expect_contains(page_html, "tool-column-toggle") &&
       harness_expect_contains(page_html, "tool-column-collapsed .tool-count") &&
       harness_expect_contains(page_html, "streaming-active .tool-column-toggle") &&
       harness_expect_contains(page_html, "tool-column-error") &&
       harness_expect_contains(page_html, "function decoratePromptGroups") &&
       harness_expect_contains(page_html, "function togglePromptGroup") &&
       harness_expect_contains(page_html, "function setMessagePromptGroup") &&
       harness_expect_contains(page_html, "function promptGroupDefaultCollapsed") &&
       harness_expect_contains(page_html, "function promptGroupCollapsed") &&
       harness_expect_contains(page_html, "function rowIsActiveHarness") &&
       harness_expect_contains(page_html,
                               "function promptGroupAnchor(rows){var i;for(i=0;i<rows.length;i++){if(rowIsHarness(rows[i]))return rows[i];}") &&
       harness_expect_contains(page_html,
                               "setRowClass(anchor,'prompt-group-collapsed-anchor'") &&
       harness_expect_contains(page_html,
                               "prompt-group-collapsed-anchor .bubble") &&
       harness_expect_contains(page_html, "collapsed&&rowIsHarness(g[j])&&g[j]!==anchor") &&
       harness_expect_contains(page_html, "prompt-group-harness") &&
       harness_expect_contains(page_html, "data-prompt-group-key=\"prompt-group-page\"") &&
       harness_expect_contains(page_html, "reasoning-collapsed .reasoning-label");

  strappy_webview_free(page_html);
  return ok;
}

static int harness_check_fontawesome_rendering(void)
{
  strappy_webview_message message;
  char *message_html;
  char *page_html;
  int ok;

  strappy_webview_set_font_dir("/tmp/Strappy Fonts");

  memset(&message, 0, sizeof(message));
  message.element_id = "assistant-icon";
  message.role = "assistant";
  message.text =
    "Ready [fa:heart] [fa:regular:face-smile] [fa:brands:github] "
    "[fa:alarm-clock] [fa:solid:F004].";

  message_html = strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (message_html == NULL) {
    fprintf(stderr, "Could not generate Font Awesome message HTML.\n");
    return 0;
  }

  page_html = strappy_webview_messages_page_html(message_html,
                                                "Empty",
                                                1,
                                                0,
                                                "Load More");
  strappy_webview_free(message_html);
  if (page_html == NULL) {
    fprintf(stderr, "Could not generate Font Awesome page HTML.\n");
    return 0;
  }

  ok = harness_expect_contains(page_html, "file:///tmp/Strappy%20Fonts/FA7-Solid-900.otf") &&
       harness_expect_contains(page_html, "file:///tmp/Strappy%20Fonts/FA7-Regular-400.otf") &&
       harness_expect_contains(page_html, "file:///tmp/Strappy%20Fonts/FA7-Brands-400.otf") &&
       harness_expect_contains(page_html, ".fa-regular{font-family:'FA7R';}") &&
       harness_expect_contains(page_html, "\"alarm-clock\":'F34E'") &&
       harness_expect_contains(page_html, "\"github\":'F09B'") &&
       harness_expect_contains(page_html, "Ready [fa:heart]") &&
       harness_expect_contains(page_html, "faIconHTML(st,n,m)");

  strappy_webview_free(page_html);
  strappy_webview_set_font_dir(NULL);
  return ok;
}

static int harness_check_tool_column_state(void)
{
  strappy_webview_message message;
  char *final_html;
  char *streaming_html;
  char *reloaded_streaming_html;
  char *reloaded_content_html;
  int ok;

  memset(&message, 0, sizeof(message));
  message.element_id = "assistant-final";
  message.role = "assistant";
  message.actor = "user";
  message.prompt_group_key = "prompt-group-final";
  message.text = "Done.";

  final_html = strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (final_html == NULL) {
    fprintf(stderr, "Could not generate final assistant message HTML.\n");
    return 0;
  }

  streaming_html = strappy_webview_streaming_assistant_message_html(
    "assistant-streaming",
    "",
    "",
    "pending",
    "Thinking",
    "user",
    "prompt-group-test",
    NULL);
  if (streaming_html == NULL) {
    fprintf(stderr, "Could not generate streaming assistant message HTML.\n");
    strappy_webview_free(final_html);
    return 0;
  }

  message.element_id = "assistant-reloaded-streaming";
  message.text = "";
  message.reasoning = "Thinking through it";
  message.render_state_json =
    "{\"streaming\":true,\"reasoning_render_when_empty\":true,"
    "\"reasoning_collapsed\":false,\"tool_column_collapsed\":true}";
  reloaded_streaming_html =
    strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (reloaded_streaming_html == NULL) {
    fprintf(stderr, "Could not generate reloaded streaming assistant HTML.\n");
    strappy_webview_free(streaming_html);
    strappy_webview_free(final_html);
    return 0;
  }

  message.element_id = "assistant-reloaded-content";
  message.text = "Answer text";
  message.reasoning = "Thinking through it";
  message.render_state_json =
    "{\"streaming\":true,\"reasoning_render_when_empty\":true,"
    "\"reasoning_collapsed\":false,\"tool_column_collapsed\":true,"
    "\"content_started\":true}";
  reloaded_content_html =
    strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (reloaded_content_html == NULL) {
    fprintf(stderr, "Could not generate reloaded content assistant HTML.\n");
    strappy_webview_free(reloaded_streaming_html);
    strappy_webview_free(streaming_html);
    strappy_webview_free(final_html);
    return 0;
  }

  ok = harness_expect_contains(final_html,
                               "tool-column tool-column-empty tool-column-collapsed") &&
       harness_expect_contains(final_html,
                               "tool-column-disclosure\">&#9658;") &&
       harness_expect_contains(streaming_html,
                               "row assistant streaming-active state-pending") &&
       harness_expect_contains(streaming_html,
                               "tool-column tool-column-empty tool-column-collapsed") &&
       harness_expect_contains(streaming_html,
                               "data-prompt-group-key=\"prompt-group-test\"") &&
       harness_expect_contains(streaming_html,
                               "class=\"bubble\" style=\"display:none;\"") &&
       harness_expect_contains(streaming_html,
                               "tool-column-disclosure\">&#9658;") &&
       harness_expect_not_contains(streaming_html,
                                   "tool-column-disclosure\">&#9660;") &&
       harness_expect_not_contains(streaming_html,
                                   "bubble bubble-status") &&
       harness_expect_contains(reloaded_streaming_html,
                               "row assistant streaming-active") &&
       harness_expect_contains(reloaded_streaming_html,
                               "data-render-state=\"{&quot;streaming&quot;:true") &&
       harness_expect_contains(reloaded_streaming_html,
                               "Thinking through it") &&
       harness_expect_contains(reloaded_streaming_html,
                               "class=\"bubble\" style=\"display:none;\"") &&
       harness_expect_not_contains(reloaded_streaming_html,
                                   "bubble bubble-status") &&
       harness_expect_not_contains(reloaded_streaming_html,
                                   "reasoning-collapsed") &&
       harness_expect_contains(reloaded_content_html,
                               "row assistant streaming-active") &&
       harness_expect_not_contains(reloaded_content_html,
                                   "reasoning-collapsed") &&
       harness_expect_contains(reloaded_content_html,
                               "Answer text") &&
       harness_expect_contains(reloaded_content_html,
                               "Thinking through it") &&
       harness_expect_not_contains(reloaded_content_html,
                                   "bubble bubble-status");

  strappy_webview_free(reloaded_content_html);
  strappy_webview_free(reloaded_streaming_html);
  strappy_webview_free(streaming_html);
  strappy_webview_free(final_html);
  return ok;
}

static int harness_check_tool_event_text(void)
{
  char *event_text;
  char *script;
  int ok;

  event_text = strappy_webview_tool_event_text(
    "result",
    "call-1",
    "memory_user_fact_read",
    "{\"query\":\"Alice\"}",
    "{\"records\":[{\"kind\":\"person\",\"value\":{\"name\":\"Alice\"}}]}");
  if (event_text == NULL) {
    fprintf(stderr, "Could not generate tool event text.\n");
    return 0;
  }

  script = strappy_webview_append_tool_event_text_js("streaming-tools",
                                                     event_text);
  if (script == NULL) {
    fprintf(stderr, "Could not generate tool event script.\n");
    strappy_webview_free(event_text);
    return 0;
  }

  ok = harness_expect_contains(event_text, "\"tool_name\":\"memory_user_fact_read\"") &&
       harness_expect_contains(event_text, "\\\"records\\\"") &&
       harness_expect_contains(script, "appendToolEventText");

  strappy_webview_free(script);
  strappy_webview_free(event_text);
  return ok;
}

static int harness_check_processing_status_scripts(void)
{
  char *set_script;
  char *clear_script;
  int ok;

  set_script = strappy_webview_set_processing_status_js(
    "{\"active\":true,\"status_kind\":\"retry_wait\","
    "\"retry_after_seconds\":7,\"retry_attempt\":2,"
    "\"retry_max_attempts\":3}");
  if (set_script == NULL) {
    fprintf(stderr, "Could not generate processing status JS.\n");
    return 0;
  }

  clear_script = strappy_webview_clear_processing_status_js();
  if (clear_script == NULL) {
    fprintf(stderr, "Could not generate processing status clear JS.\n");
    strappy_webview_free(set_script);
    return 0;
  }

  ok = harness_expect_contains(set_script, "setProcessingStatus('{\"active\":true") &&
       harness_expect_contains(set_script, "\"status_kind\":\"retry_wait\"") &&
       harness_expect_contains(clear_script, "clearProcessingStatus();");

  strappy_webview_free(clear_script);
  strappy_webview_free(set_script);
  return ok;
}

static int harness_check_tool_activity_target(void)
{
  char *html;
  int ok;

  html = strappy_webview_tool_activity_message_html(
    "streaming-harness-tools",
    "",
    "pending",
    "Running tools...",
    "harness",
    "prompt-group-harness",
    "streaming-harness-assistant",
    NULL);
  if (html == NULL) {
    fprintf(stderr, "Could not generate tool activity HTML.\n");
    return 0;
  }

  ok = harness_expect_contains(html, "class=\"row tool_call tool_activity") &&
       harness_expect_contains(html, "data-actor=\"harness\"") &&
       harness_expect_contains(html,
                               "data-tool-target=\"streaming-harness-assistant\"") &&
       harness_expect_contains(html, "style=\"display:none\"");

  strappy_webview_free(html);
  return ok;
}

static int harness_check_harness_prompt_group_collapse(void)
{
  strappy_webview_message message;
  char *user_html;
  char *assistant_html;
  char *harness_prompt_html;
  char *harness_assistant_html;
  char *messages_html;
  char *page_html;
  char *streaming_harness_html;
  size_t messages_length;
  int ok;

  memset(&message, 0, sizeof(message));
  message.element_id = "collapse-user";
  message.role = "user";
  message.actor = "user";
  message.prompt_group_key = "prompt-group-collapse";
  message.text = "What should I remember?";
  user_html = strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (user_html == NULL) {
    fprintf(stderr, "Could not generate prompt group user HTML.\n");
    return 0;
  }

  memset(&message, 0, sizeof(message));
  message.element_id = "collapse-assistant";
  message.role = "assistant";
  message.actor = "user";
  message.prompt_group_key = "prompt-group-collapse";
  message.text = "Done.";
  assistant_html = strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (assistant_html == NULL) {
    fprintf(stderr, "Could not generate prompt group assistant HTML.\n");
    strappy_webview_free(user_html);
    return 0;
  }

  memset(&message, 0, sizeof(message));
  message.element_id = "collapse-harness-prompt";
  message.role = "harness";
  message.actor = "harness";
  message.prompt_group_key = "prompt-group-collapse";
  message.text = "Learning Summary";
  harness_prompt_html =
    strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (harness_prompt_html == NULL) {
    fprintf(stderr, "Could not generate prompt group harness prompt HTML.\n");
    strappy_webview_free(assistant_html);
    strappy_webview_free(user_html);
    return 0;
  }

  memset(&message, 0, sizeof(message));
  message.element_id = "collapse-harness-assistant";
  message.role = "assistant";
  message.actor = "harness";
  message.prompt_group_key = "prompt-group-collapse";
  message.text = "Learning Summary Complete";
  harness_assistant_html =
    strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (harness_assistant_html == NULL) {
    fprintf(stderr, "Could not generate prompt group harness assistant HTML.\n");
    strappy_webview_free(harness_prompt_html);
    strappy_webview_free(assistant_html);
    strappy_webview_free(user_html);
    return 0;
  }

  messages_length = strlen(user_html) +
                    strlen(assistant_html) +
                    strlen(harness_prompt_html) +
                    strlen(harness_assistant_html);
  messages_html = (char *)malloc(messages_length + 1U);
  if (messages_html == NULL) {
    fprintf(stderr, "Could not allocate prompt group collapse fixture HTML.\n");
    strappy_webview_free(harness_assistant_html);
    strappy_webview_free(harness_prompt_html);
    strappy_webview_free(assistant_html);
    strappy_webview_free(user_html);
    return 0;
  }
  snprintf(messages_html,
           messages_length + 1U,
           "%s%s%s%s",
           user_html,
           assistant_html,
           harness_prompt_html,
           harness_assistant_html);

  page_html = strappy_webview_messages_page_html(messages_html,
                                                "Empty",
                                                1,
                                                0,
                                                "Load More");
  free(messages_html);
  if (page_html == NULL) {
    fprintf(stderr, "Could not generate prompt group collapse page HTML.\n");
    strappy_webview_free(harness_assistant_html);
    strappy_webview_free(harness_prompt_html);
    strappy_webview_free(assistant_html);
    strappy_webview_free(user_html);
    return 0;
  }

  streaming_harness_html = strappy_webview_streaming_assistant_message_html(
    "collapse-harness-streaming",
    "",
    "",
    "pending",
    "Thinking",
    "harness",
    "prompt-group-collapse",
    NULL);
  if (streaming_harness_html == NULL) {
    fprintf(stderr, "Could not generate streaming harness group HTML.\n");
    strappy_webview_free(harness_assistant_html);
    strappy_webview_free(harness_prompt_html);
    strappy_webview_free(assistant_html);
    strappy_webview_free(user_html);
    strappy_webview_free(page_html);
    return 0;
  }

  ok = harness_expect_contains(user_html,
                               "data-prompt-group-key=\"prompt-group-collapse\"") &&
       harness_expect_contains(assistant_html,
                               "data-actor=\"user\"") &&
       harness_expect_contains(harness_prompt_html,
                               "class=\"row harness\"") &&
       harness_expect_contains(harness_prompt_html,
                               "data-actor=\"harness\"") &&
       harness_expect_contains(harness_assistant_html,
                               "class=\"row assistant\"") &&
       harness_expect_contains(harness_assistant_html,
                               "data-actor=\"harness\"") &&
       harness_expect_not_contains(harness_assistant_html,
                                   "streaming-active") &&
       harness_expect_contains(page_html,
                               "id=\"collapse-harness-prompt\"") &&
       harness_expect_contains(page_html,
                               "id=\"collapse-harness-assistant\"") &&
       harness_expect_contains(page_html,
                               "renderMessageDecorations(document);") &&
       harness_expect_contains(streaming_harness_html,
                               "row assistant streaming-active state-pending") &&
       harness_expect_contains(streaming_harness_html,
                               "data-actor=\"harness\"");

  strappy_webview_free(streaming_harness_html);
  strappy_webview_free(page_html);
  strappy_webview_free(harness_assistant_html);
  strappy_webview_free(harness_prompt_html);
  strappy_webview_free(assistant_html);
  strappy_webview_free(user_html);
  return ok;
}

static int harness_check_harness_message(void)
{
  strappy_webview_message message;
  char *message_html;
  int ok;

  memset(&message, 0, sizeof(message));
  message.element_id = "harness-1";
  message.role = "harness";
  message.text = "Learning Summary";

  message_html = strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (message_html == NULL) {
    fprintf(stderr, "Could not generate harness message HTML.\n");
    return 0;
  }

  ok = harness_expect_contains(message_html, "class=\"row harness\"") &&
       harness_expect_contains(message_html, "<div class=\"role\">Harness</div>") &&
       harness_expect_contains(message_html, "Learning Summary");

  strappy_webview_free(message_html);
  return ok;
}

static int harness_check_harness_assistant_metadata(void)
{
  strappy_webview_message message;
  char *message_html;
  int ok;

  memset(&message, 0, sizeof(message));
  message.element_id = "harness-assistant-1";
  message.role = "assistant";
  message.actor = "harness";
  message.text = "Learning Summary Complete";
  message.reasoning = "Harness thinking";
  message.metadata_json =
    "{\"usage\":{\"cost\":\"0.001\",\"prompt_tokens\":1,\"completion_tokens\":2}}";

  message_html = strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (message_html == NULL) {
    fprintf(stderr, "Could not generate harness assistant HTML.\n");
    return 0;
  }

  ok = harness_expect_contains(message_html, "data-actor=\"harness\"") &&
       harness_expect_contains(message_html, "request-metadata") &&
       harness_expect_contains(message_html, "Harness thinking") &&
       harness_expect_contains(message_html, "Learning Summary Complete");

  strappy_webview_free(message_html);
  return ok;
}

static int harness_check_error_message_state(void)
{
  strappy_webview_message message;
  char *message_html;
  int ok;

  memset(&message, 0, sizeof(message));
  message.element_id = "assistant-error-1";
  message.role = "assistant";
  message.text = "OpenRouter request failed: Timeout was reached";
  message.metadata_json =
    "{\"finish_reason\":\"error\",\"http_status\":0,"
    "\"error\":\"OpenRouter request failed: Timeout was reached\"}";
  message.is_error = 1;

  message_html = strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (message_html == NULL) {
    fprintf(stderr, "Could not generate error assistant HTML.\n");
    return 0;
  }

  ok = harness_expect_contains(message_html,
                               "class=\"row assistant state-error\"") &&
       harness_expect_not_contains(message_html, "streaming-active") &&
       harness_expect_contains(message_html, "request-metadata");

  strappy_webview_free(message_html);
  return ok;
}

int main(void)
{
  strappy_webview_set_font_dir("/tmp/Strappy Fonts");
  if (!harness_check_page_scripts()) {
    return 1;
  }
  if (!harness_check_fontawesome_rendering()) {
    return 1;
  }
  if (!harness_check_tool_column_state()) {
    return 1;
  }
  if (!harness_check_tool_event_text()) {
    return 1;
  }
  if (!harness_check_processing_status_scripts()) {
    return 1;
  }
  if (!harness_check_tool_activity_target()) {
    return 1;
  }
  if (!harness_check_harness_prompt_group_collapse()) {
    return 1;
  }
  if (!harness_check_harness_message()) {
    return 1;
  }
  if (!harness_check_harness_assistant_metadata()) {
    return 1;
  }
  if (!harness_check_error_message_state()) {
    return 1;
  }

  return 0;
}
