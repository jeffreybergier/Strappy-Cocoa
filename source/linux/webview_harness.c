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

  ok = harness_expect_contains(page_html, "function toolJSONHTML") &&
       harness_expect_contains(page_html, "function toolObjectArrayTable") &&
       harness_expect_contains(page_html, "function toolOutputHasError") &&
       harness_expect_contains(page_html, "c.error=toolOutputHasError(raw)") &&
       harness_expect_contains(page_html, "function toolUsesDatabaseId") &&
       harness_expect_contains(page_html, "database_context_read") &&
       harness_expect_contains(page_html, "function beginMessageBatch") &&
       harness_expect_contains(page_html, "function scrollBottomNow") &&
       harness_expect_contains(page_html, "function shouldRenderMarkdownBubble") &&
       harness_expect_contains(page_html, "ancestorHasClass(n,'assistant')") &&
       harness_expect_contains(page_html, "setMessageToolColumnCollapsed(id,1)") &&
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
       harness_expect_contains(page_html, "reasoning-collapsed .reasoning-label");

  strappy_webview_free(page_html);
  return ok;
}

static int harness_check_tool_column_state(void)
{
  strappy_webview_message message;
  char *final_html;
  char *streaming_html;
  int ok;

  memset(&message, 0, sizeof(message));
  message.element_id = "assistant-final";
  message.role = "assistant";
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
    NULL);
  if (streaming_html == NULL) {
    fprintf(stderr, "Could not generate streaming assistant message HTML.\n");
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
                               "tool-column-disclosure\">&#9658;") &&
       harness_expect_not_contains(streaming_html,
                                   "tool-column-disclosure\">&#9660;");

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
    "helper_user_info_read",
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

  ok = harness_expect_contains(event_text, "\"tool_name\":\"helper_user_info_read\"") &&
       harness_expect_contains(event_text, "\\\"records\\\"") &&
       harness_expect_contains(script, "appendToolEventText");

  strappy_webview_free(script);
  strappy_webview_free(event_text);
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
  message.metadata_json =
    "{\"usage\":{\"cost\":\"0.001\",\"prompt_tokens\":1,\"completion_tokens\":2}}";

  message_html = strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (message_html == NULL) {
    fprintf(stderr, "Could not generate harness assistant HTML.\n");
    return 0;
  }

  ok = harness_expect_contains(message_html, "data-actor=\"harness\"") &&
       harness_expect_contains(message_html, "request-metadata") &&
       harness_expect_contains(message_html, "Learning Summary Complete");

  strappy_webview_free(message_html);
  return ok;
}

int main(void)
{
  if (!harness_check_page_scripts()) {
    return 1;
  }
  if (!harness_check_tool_column_state()) {
    return 1;
  }
  if (!harness_check_tool_event_text()) {
    return 1;
  }
  if (!harness_check_harness_message()) {
    return 1;
  }
  if (!harness_check_harness_assistant_metadata()) {
    return 1;
  }

  return 0;
}
