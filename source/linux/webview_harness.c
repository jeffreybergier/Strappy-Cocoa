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
       harness_expect_contains(page_html, "Raw JSON preview") &&
       harness_expect_contains(page_html, "tool-column tool-column-empty");

  strappy_webview_free(page_html);
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

int main(void)
{
  if (!harness_check_page_scripts()) {
    return 1;
  }
  if (!harness_check_tool_event_text()) {
    return 1;
  }

  return 0;
}
