#include "strappy_client.h"
#include "strappy_core.h"
#include "strappy_provider.h"
#include "strappy_responses.h"
#include "strappy_session.h"

#include <emscripten/emscripten.h>
#include <stdlib.h>
#include <string.h>

#define STRAPPY_WEB_RESOURCE_DIR "/Resources"

static char *strappy_web_conversation_error = NULL;
static char *strappy_web_conversation_timeline_cursor = NULL;
static strappy_session_webview_render_context
  *strappy_web_conversation_render_context = NULL;
static long long strappy_web_conversation_session_id = 0LL;
static int strappy_web_conversation_active = 0;
static int strappy_web_conversation_cancelled = 0;

void strappy_client_web_cancel_active_request(void);
const char *strappy_web_database_path(void);

EM_JS(int, strappy_web_conversation_key_length, (), {
  if (typeof Module.strappyCopyCredential !== "function") {
    return 0;
  }
  const key = Module.strappyCopyCredential();
  if (typeof key !== "string" || key.length === 0) {
    return 0;
  }
  return new TextEncoder().encode(key).length;
});

EM_JS(int, strappy_web_conversation_copy_key,
      (char *target, size_t capacity), {
  if (typeof Module.strappyCopyCredential !== "function") {
    return 0;
  }
  const key = Module.strappyCopyCredential();
  if (typeof key !== "string") {
    return 0;
  }
  const bytes = new TextEncoder().encode(key);
  if (!target || capacity <= bytes.length) {
    return 0;
  }
  HEAPU8.set(bytes, target);
  HEAPU8[target + bytes.length] = 0;
  return 1;
});

EM_JS(void, strappy_web_conversation_post_event,
      (const char *kind, const char *payload), {
  self.postMessage({
    type: "conversation-event",
    kind: UTF8ToString(kind),
    payload: payload ? UTF8ToString(payload) : "",
  });
});

static void strappy_web_conversation_clear_error(void)
{
  free(strappy_web_conversation_error);
  strappy_web_conversation_error = NULL;
}

static void strappy_web_conversation_set_error(const char *message)
{
  strappy_web_conversation_clear_error();
  strappy_web_conversation_error = strappy_string_duplicate(
    ((message != NULL) && (message[0] != '\0')) ? message :
      "The conversation operation failed.");
}

static void strappy_web_conversation_replace_cursor(char *next_cursor)
{
  if (next_cursor == NULL) {
    return;
  }
  free(strappy_web_conversation_timeline_cursor);
  strappy_web_conversation_timeline_cursor = next_cursor;
}

static int strappy_web_conversation_credentials(
  const char *provider_id,
  const char *provider_account_id,
  int force_refresh,
  char **bearer_token_out,
  char **upstream_account_id_out,
  void *user_data,
  char **error_out)
{
  char *key;
  int key_length;

  (void)force_refresh;
  (void)user_data;
  if (bearer_token_out != NULL) {
    *bearer_token_out = NULL;
  }
  if (upstream_account_id_out != NULL) {
    *upstream_account_id_out = NULL;
  }
  if ((provider_id == NULL) ||
      (strcmp(provider_id, STRAPPY_PROVIDER_OPENROUTER) != 0) ||
      (provider_account_id == NULL) || (provider_account_id[0] == '\0') ||
      (bearer_token_out == NULL)) {
    strappy_set_error(error_out,
                      "The browser credential request is not supported.");
    return 0;
  }
  key_length = strappy_web_conversation_key_length();
  if ((key_length <= 0) || (key_length > 65536)) {
    strappy_set_error(error_out, "Enter an OpenRouter API key first.");
    return 0;
  }
  key = (char *)malloc((size_t)key_length + 1U);
  if (key == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate the browser credential copy.");
    return 0;
  }
  if (!strappy_web_conversation_copy_key(
        key, (size_t)key_length + 1U) || (key[0] == '\0')) {
    free(key);
    strappy_set_error(error_out, "Enter an OpenRouter API key first.");
    return 0;
  }
  *bearer_token_out = key;
  return 1;
}

static void strappy_web_conversation_emit_script(char *script)
{
  if ((script != NULL) && (script[0] != '\0')) {
    strappy_web_conversation_post_event("timeline-script", script);
  }
  free(script);
}

static void strappy_web_conversation_reload_after_render_error(
  const char *message)
{
  strappy_web_conversation_set_error(message);
  strappy_web_conversation_post_event(
    "timeline-reload", strappy_web_conversation_error);
}

static int strappy_web_conversation_handle_event(
  const strappy_responses_event *event,
  void *user_data)
{
  char *error;
  char *next_cursor;
  char *script;
  size_t message_count;

  (void)user_data;
  if (event == NULL) {
    return 1;
  }
  if (event->type == STRAPPY_RESPONSES_EVENT_CANCELLATION_POLL) {
    return strappy_web_conversation_cancelled ? 0 : 1;
  }
  if (event->type == STRAPPY_RESPONSES_EVENT_SESSION_UPDATED) {
    strappy_web_conversation_post_event("session-updated", "");
    return 1;
  }
  if (event->type == STRAPPY_RESPONSES_EVENT_PROCESSING_STATUS) {
    script = strappy_session_webview_set_processing_status_js(
      event->status_json);
    strappy_web_conversation_emit_script(script);
    strappy_web_conversation_post_event(
      "processing-status",
      (event->status_kind != NULL) ? event->status_kind : "");
    return 1;
  }
  if ((event->type != STRAPPY_RESPONSES_EVENT_LEDGER_CHANGED) &&
      (event->type != STRAPPY_RESPONSES_EVENT_LEDGER_UPDATED)) {
    return 1;
  }

  error = NULL;
  if (event->type == STRAPPY_RESPONSES_EVENT_LEDGER_CHANGED) {
    next_cursor = NULL;
    message_count = 0U;
    script = strappy_session_webview_append_messages_js_with_render_context(
      strappy_web_conversation_render_context,
      strappy_web_conversation_session_id,
      strappy_web_conversation_timeline_cursor,
      &message_count,
      &next_cursor,
      &error);
    if (script == NULL) {
      strappy_web_conversation_reload_after_render_error(error);
      free(error);
      free(next_cursor);
      return 1;
    }
    strappy_web_conversation_replace_cursor(next_cursor);
    strappy_web_conversation_emit_script(script);
  }

  if (strappy_session_webview_event_requires_message_update(event) &&
      (event->message_key != NULL) && (event->message_key[0] != '\0')) {
    free(error);
    error = NULL;
    script = strappy_session_webview_message_update_js_with_render_context(
      strappy_web_conversation_render_context,
      strappy_web_conversation_session_id,
      event->message_key,
      &error);
    if (script == NULL) {
      strappy_web_conversation_reload_after_render_error(error);
      free(error);
      return 1;
    }
    strappy_web_conversation_emit_script(script);
  }
  free(error);
  if (event->is_terminal) {
    strappy_web_conversation_post_event("terminal", "");
  }
  return 1;
}

EMSCRIPTEN_KEEPALIVE
int strappy_web_conversation_initialize(long long session_id)
{
  char *error;

  strappy_web_conversation_clear_error();
  if (session_id <= 0LL) {
    strappy_web_conversation_set_error("A browser session is not available.");
    return 0;
  }
  free(strappy_web_conversation_timeline_cursor);
  strappy_web_conversation_timeline_cursor = NULL;
  strappy_session_webview_render_context_destroy(
    strappy_web_conversation_render_context);
  strappy_web_conversation_render_context = NULL;
  error = NULL;
  strappy_web_conversation_timeline_cursor =
    strappy_session_timeline_cursor_for_session(strappy_web_database_path(),
                                                session_id,
                                                &error);
  if (strappy_web_conversation_timeline_cursor == NULL) {
    strappy_web_conversation_set_error(error);
    free(error);
    return 0;
  }
  strappy_web_conversation_render_context =
    strappy_session_webview_render_context_create(strappy_web_database_path(),
                                                  STRAPPY_WEB_RESOURCE_DIR,
                                                  &error);
  if (strappy_web_conversation_render_context == NULL) {
    strappy_web_conversation_set_error(error);
    free(error);
    free(strappy_web_conversation_timeline_cursor);
    strappy_web_conversation_timeline_cursor = NULL;
    return 0;
  }
  free(error);
  strappy_web_conversation_session_id = session_id;
  strappy_responses_set_provider_credentials_callback(
    strappy_web_conversation_credentials, NULL);
  return 1;
}

EMSCRIPTEN_KEEPALIVE
char *strappy_web_conversation_load_timeline(void)
{
  char *error;
  char *next_cursor;
  char *page;
  size_t message_count;

  strappy_web_conversation_clear_error();
  if (strappy_web_conversation_session_id <= 0LL) {
    strappy_web_conversation_set_error("A browser session is not available.");
    return NULL;
  }
  error = NULL;
  next_cursor = NULL;
  message_count = 0U;
  page = strappy_session_webview_messages_page_html_for_session(
    strappy_web_database_path(),
    strappy_web_conversation_session_id,
    STRAPPY_WEB_RESOURCE_DIR,
    NULL,
    NULL,
    STRAPPY_WEBVIEW_PALETTE_NEUTRAL,
    &message_count,
    &next_cursor,
    &error);
  if (page == NULL) {
    strappy_web_conversation_set_error(error);
    free(error);
    free(next_cursor);
    return NULL;
  }
  free(error);
  strappy_web_conversation_replace_cursor(next_cursor);
  return page;
}

EMSCRIPTEN_KEEPALIVE
int strappy_web_conversation_submit_prompt(const char *prompt)
{
  strappy_session_record record;
  char *error;
  int ok;

  strappy_web_conversation_clear_error();
  if (strappy_web_conversation_active) {
    strappy_web_conversation_set_error("A prompt is already being processed.");
    return 0;
  }
  if ((prompt == NULL) || (prompt[0] == '\0')) {
    strappy_web_conversation_set_error("Enter a prompt first.");
    return 0;
  }
  if ((strappy_web_conversation_session_id <= 0LL) ||
      (strappy_web_conversation_render_context == NULL)) {
    strappy_web_conversation_set_error("The browser session is not initialized.");
    return 0;
  }

  error = NULL;
  strappy_web_conversation_active = 1;
  strappy_web_conversation_cancelled = 0;
  strappy_session_record_init(&record);
  ok = strappy_session_send_prompt_with_events_and_load(
    prompt,
    NULL,
    NULL,
    STRAPPY_WEB_RESOURCE_DIR,
    strappy_web_database_path(),
    strappy_web_conversation_session_id,
    strappy_web_conversation_handle_event,
    NULL,
    &record,
    &error);
  strappy_session_record_destroy(&record);
  strappy_web_conversation_active = 0;
  if (!ok) {
    strappy_web_conversation_set_error(error);
    free(error);
    return 0;
  }
  free(error);
  return 1;
}

EMSCRIPTEN_KEEPALIVE
void strappy_web_conversation_cancel(void)
{
  strappy_web_conversation_cancelled = 1;
  strappy_client_web_cancel_active_request();
}

EMSCRIPTEN_KEEPALIVE
int strappy_web_conversation_is_active(void)
{
  return strappy_web_conversation_active;
}

EMSCRIPTEN_KEEPALIVE
const char *strappy_web_conversation_last_error(void)
{
  return (strappy_web_conversation_error != NULL) ?
    strappy_web_conversation_error : "";
}

EMSCRIPTEN_KEEPALIVE
void strappy_web_string_free(char *value)
{
  free(value);
}
