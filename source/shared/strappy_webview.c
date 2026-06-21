#include "strappy_webview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct strappy_webview_buffer {
  char *data;
  size_t length;
  size_t capacity;
} strappy_webview_buffer;

static void strappy_webview_buffer_init(strappy_webview_buffer *buffer)
{
  if (buffer == NULL) {
    return;
  }

  buffer->data = NULL;
  buffer->length = 0U;
  buffer->capacity = 0U;
}

static void strappy_webview_buffer_destroy(strappy_webview_buffer *buffer)
{
  if (buffer == NULL) {
    return;
  }

  free(buffer->data);
  strappy_webview_buffer_init(buffer);
}

static int strappy_webview_buffer_reserve(strappy_webview_buffer *buffer,
                                          size_t extra)
{
  char *next_data;
  size_t needed;
  size_t next_capacity;

  if (buffer == NULL) {
    return 0;
  }
  if (buffer->length > (((size_t)-1) - extra - 1U)) {
    return 0;
  }

  needed = buffer->length + extra + 1U;
  if (needed <= buffer->capacity) {
    return 1;
  }

  next_capacity = (buffer->capacity > 0U) ? buffer->capacity : 256U;
  while (next_capacity < needed) {
    if (next_capacity > (((size_t)-1) / 2U)) {
      next_capacity = needed;
      break;
    }
    next_capacity *= 2U;
  }

  next_data = (char *)realloc(buffer->data, next_capacity);
  if (next_data == NULL) {
    return 0;
  }

  buffer->data = next_data;
  buffer->capacity = next_capacity;
  return 1;
}

static int strappy_webview_buffer_append(strappy_webview_buffer *buffer,
                                         const char *data,
                                         size_t length)
{
  if ((buffer == NULL) || ((data == NULL) && (length > 0U))) {
    return 0;
  }
  if (!strappy_webview_buffer_reserve(buffer, length)) {
    return 0;
  }

  if (length > 0U) {
    memcpy(buffer->data + buffer->length, data, length);
  }
  buffer->length += length;
  buffer->data[buffer->length] = '\0';
  return 1;
}

static int strappy_webview_buffer_append_cstring(strappy_webview_buffer *buffer,
                                                 const char *data)
{
  if (data == NULL) {
    return 1;
  }

  return strappy_webview_buffer_append(buffer, data, strlen(data));
}

static int strappy_webview_buffer_append_char(strappy_webview_buffer *buffer,
                                              char character)
{
  return strappy_webview_buffer_append(buffer, &character, 1U);
}

static char *strappy_webview_buffer_finish(strappy_webview_buffer *buffer)
{
  char *result;

  if (buffer == NULL) {
    return NULL;
  }

  if (buffer->data == NULL) {
    if (!strappy_webview_buffer_append_cstring(buffer, "")) {
      return NULL;
    }
  }

  result = buffer->data;
  buffer->data = NULL;
  buffer->length = 0U;
  buffer->capacity = 0U;
  return result;
}

static int strappy_webview_append_html_escaped(strappy_webview_buffer *buffer,
                                               const char *text)
{
  const unsigned char *cursor;

  if (text == NULL) {
    return 1;
  }

  cursor = (const unsigned char *)text;
  while (*cursor != '\0') {
    if (*cursor == '&') {
      if (!strappy_webview_buffer_append_cstring(buffer, "&amp;")) {
        return 0;
      }
    } else if (*cursor == '<') {
      if (!strappy_webview_buffer_append_cstring(buffer, "&lt;")) {
        return 0;
      }
    } else if (*cursor == '>') {
      if (!strappy_webview_buffer_append_cstring(buffer, "&gt;")) {
        return 0;
      }
    } else if (*cursor == '"') {
      if (!strappy_webview_buffer_append_cstring(buffer, "&quot;")) {
        return 0;
      }
    } else if (!strappy_webview_buffer_append_char(buffer, (char)*cursor)) {
      return 0;
    }
    cursor++;
  }

  return 1;
}

static int strappy_webview_append_js_string(strappy_webview_buffer *buffer,
                                            const char *text)
{
  const unsigned char *cursor;
  char escaped[8];

  if (!strappy_webview_buffer_append_char(buffer, '\'')) {
    return 0;
  }

  if (text != NULL) {
    cursor = (const unsigned char *)text;
    while (*cursor != '\0') {
      if (*cursor == '\\') {
        if (!strappy_webview_buffer_append_cstring(buffer, "\\\\")) {
          return 0;
        }
      } else if (*cursor == '\'') {
        if (!strappy_webview_buffer_append_cstring(buffer, "\\'")) {
          return 0;
        }
      } else if (*cursor == '\n') {
        if (!strappy_webview_buffer_append_cstring(buffer, "\\n")) {
          return 0;
        }
      } else if (*cursor == '\r') {
        if (!strappy_webview_buffer_append_cstring(buffer, "\\r")) {
          return 0;
        }
      } else if (*cursor == '\t') {
        if (!strappy_webview_buffer_append_cstring(buffer, "\\t")) {
          return 0;
        }
      } else if (*cursor < 32U) {
        snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned int)*cursor);
        if (!strappy_webview_buffer_append_cstring(buffer, escaped)) {
          return 0;
        }
      } else if (!strappy_webview_buffer_append_char(buffer, (char)*cursor)) {
        return 0;
      }
      cursor++;
    }
  }

  return strappy_webview_buffer_append_char(buffer, '\'');
}

static int strappy_webview_append_json_string(strappy_webview_buffer *buffer,
                                              const char *text)
{
  const unsigned char *cursor;
  char escaped[8];

  if (!strappy_webview_buffer_append_char(buffer, '"')) {
    return 0;
  }

  if (text != NULL) {
    cursor = (const unsigned char *)text;
    while (*cursor != '\0') {
      if (*cursor == '"') {
        if (!strappy_webview_buffer_append_cstring(buffer, "\\\"")) {
          return 0;
        }
      } else if (*cursor == '\\') {
        if (!strappy_webview_buffer_append_cstring(buffer, "\\\\")) {
          return 0;
        }
      } else if (*cursor == '\b') {
        if (!strappy_webview_buffer_append_cstring(buffer, "\\b")) {
          return 0;
        }
      } else if (*cursor == '\f') {
        if (!strappy_webview_buffer_append_cstring(buffer, "\\f")) {
          return 0;
        }
      } else if (*cursor == '\n') {
        if (!strappy_webview_buffer_append_cstring(buffer, "\\n")) {
          return 0;
        }
      } else if (*cursor == '\r') {
        if (!strappy_webview_buffer_append_cstring(buffer, "\\r")) {
          return 0;
        }
      } else if (*cursor == '\t') {
        if (!strappy_webview_buffer_append_cstring(buffer, "\\t")) {
          return 0;
        }
      } else if (*cursor < 32U) {
        snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned int)*cursor);
        if (!strappy_webview_buffer_append_cstring(buffer, escaped)) {
          return 0;
        }
      } else if (!strappy_webview_buffer_append_char(buffer, (char)*cursor)) {
        return 0;
      }
      cursor++;
    }
  }

  return strappy_webview_buffer_append_char(buffer, '"');
}

static int strappy_webview_append_chunks(strappy_webview_buffer *buffer,
                                         const char * const *chunks)
{
  size_t index;

  if ((buffer == NULL) || (chunks == NULL)) {
    return 0;
  }

  for (index = 0U; chunks[index] != NULL; index++) {
    if (!strappy_webview_buffer_append_cstring(buffer, chunks[index])) {
      return 0;
    }
  }

  return 1;
}

static const char *strappy_webview_string_or_empty(const char *value)
{
  return (value != NULL) ? value : "";
}

static const char *strappy_webview_agent_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) && (labels->agent != NULL) && (labels->agent[0] != '\0')) {
    return labels->agent;
  }
  return "Agent";
}

static const char *strappy_webview_you_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) && (labels->you != NULL) && (labels->you[0] != '\0')) {
    return labels->you;
  }
  return "You";
}

static const char *strappy_webview_harness_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) &&
      (labels->harness != NULL) &&
      (labels->harness[0] != '\0')) {
    return labels->harness;
  }
  return "Harness";
}

static const char *strappy_webview_thinking_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) &&
      (labels->thinking != NULL) &&
      (labels->thinking[0] != '\0')) {
    return labels->thinking;
  }
  return "Thinking";
}

static const char *strappy_webview_request_metadata_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) &&
      (labels->request_metadata != NULL) &&
      (labels->request_metadata[0] != '\0')) {
    return labels->request_metadata;
  }
  return "Request Metadata";
}

static const char *strappy_webview_retry_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) && (labels->retry != NULL) && (labels->retry[0] != '\0')) {
    return labels->retry;
  }
  return "Retry";
}

static const char *strappy_webview_tool_call_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) &&
      (labels->tool_call != NULL) &&
      (labels->tool_call[0] != '\0')) {
    return labels->tool_call;
  }
  return "Tool Call";
}

static const char *strappy_webview_tool_result_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) &&
      (labels->tool_result != NULL) &&
      (labels->tool_result[0] != '\0')) {
    return labels->tool_result;
  }
  return "Tool Result";
}

static int strappy_webview_is_assistant_role(const char *role)
{
  return (role != NULL) && (strcmp(role, "assistant") == 0);
}

static int strappy_webview_is_user_role(const char *role)
{
  return (role != NULL) && (strcmp(role, "user") == 0);
}

static int strappy_webview_is_tool_call_role(const char *role)
{
  return (role != NULL) && (strcmp(role, "tool_call") == 0);
}

static int strappy_webview_is_tool_result_role(const char *role)
{
  return (role != NULL) && (strcmp(role, "tool") == 0);
}

static int strappy_webview_is_harness_role(const char *role)
{
  return (role != NULL) && (strcmp(role, "harness") == 0);
}

static const char *strappy_webview_role_label(
  const char *role,
  const strappy_webview_labels *labels)
{
  if (strappy_webview_is_assistant_role(role)) {
    return strappy_webview_agent_label(labels);
  }
  if (strappy_webview_is_tool_call_role(role)) {
    return strappy_webview_tool_call_label(labels);
  }
  if (strappy_webview_is_tool_result_role(role)) {
    return strappy_webview_tool_result_label(labels);
  }
  if (strappy_webview_is_harness_role(role)) {
    return strappy_webview_harness_label(labels);
  }
  return strappy_webview_you_label(labels);
}

static int strappy_webview_append_element_id(strappy_webview_buffer *buffer,
                                             const strappy_webview_message *message)
{
  char identifier[64];

  if ((message != NULL) &&
      (message->element_id != NULL) &&
      (message->element_id[0] != '\0')) {
    return strappy_webview_append_html_escaped(buffer, message->element_id);
  }

  if ((message != NULL) && (message->message_id > 0LL)) {
    snprintf(identifier, sizeof(identifier), "saved-%lld", message->message_id);
  } else {
    snprintf(identifier, sizeof(identifier), "saved-0");
  }

  return strappy_webview_append_html_escaped(buffer, identifier);
}

static int strappy_webview_append_data_attribute(strappy_webview_buffer *buffer,
                                                 const char *name,
                                                 const char *value)
{
  if ((value == NULL) || (value[0] == '\0')) {
    return 1;
  }

  return strappy_webview_buffer_append_cstring(buffer, " data-") &&
         strappy_webview_buffer_append_cstring(buffer, name) &&
         strappy_webview_buffer_append_cstring(buffer, "=\"") &&
         strappy_webview_append_html_escaped(buffer, value) &&
         strappy_webview_buffer_append_cstring(buffer, "\"");
}

static int strappy_webview_append_reasoning_html(
  strappy_webview_buffer *buffer,
  const char *reasoning,
  int render_when_empty,
  const strappy_webview_labels *labels)
{
  int collapse_reasoning;

  if (((reasoning == NULL) || (reasoning[0] == '\0')) && !render_when_empty) {
    return 1;
  }

  collapse_reasoning = ((reasoning != NULL) &&
                        (reasoning[0] != '\0') &&
                        !render_when_empty);

  if (!strappy_webview_buffer_append_cstring(buffer, "<div class=\"reasoning")) {
    return 0;
  }
  if (collapse_reasoning) {
    if (!strappy_webview_buffer_append_cstring(buffer, " reasoning-collapsed")) {
      return 0;
    }
  }
  if (!strappy_webview_buffer_append_cstring(buffer, "\"")) {
    return 0;
  }
  if ((reasoning == NULL) || (reasoning[0] == '\0')) {
    if (!strappy_webview_buffer_append_cstring(buffer, " style=\"display:none\"")) {
      return 0;
    }
  }
  if (!strappy_webview_buffer_append_cstring(buffer,
                                            "><div class=\"reasoning-label\">"
                                            "<a class=\"reasoning-toggle\" href=\"#\" "
                                            "onclick=\"return toggleReasoning(this)\">"
                                            "<span class=\"reasoning-disclosure\">") ||
      !strappy_webview_buffer_append_cstring(buffer,
                                            collapse_reasoning ?
                                            "&#9658;" : "&#9660;") ||
      !strappy_webview_buffer_append_cstring(buffer,
                                            "</span></a>") ||
      !strappy_webview_append_html_escaped(buffer,
                                           strappy_webview_thinking_label(labels))) {
    return 0;
  }
  if (!strappy_webview_buffer_append_cstring(buffer,
                                            "</div><div class=\"reasoning-body\">") ||
      !strappy_webview_append_html_escaped(buffer, reasoning) ||
      !strappy_webview_buffer_append_cstring(buffer, "</div></div>")) {
    return 0;
  }

  return 1;
}

static int strappy_webview_append_metadata_html(
  strappy_webview_buffer *buffer,
  const char *role,
  const char *metadata_json,
  const strappy_webview_labels *labels)
{
  if (!strappy_webview_is_assistant_role(role) ||
      (metadata_json == NULL) ||
      (metadata_json[0] == '\0')) {
    return 1;
  }

  return strappy_webview_buffer_append_cstring(
           buffer,
           "<div class=\"request-metadata request-metadata-collapsed\" "
           "data-metadata=\"") &&
         strappy_webview_append_html_escaped(buffer, metadata_json) &&
         strappy_webview_buffer_append_cstring(
           buffer,
           "\"><div class=\"request-metadata-title\">"
           "<a class=\"request-metadata-toggle\" href=\"#\" "
           "onclick=\"return toggleMetadata(this)\">"
           "<span class=\"metadata-disclosure\">&#9658;</span></a>") &&
         strappy_webview_append_html_escaped(
           buffer,
           strappy_webview_request_metadata_label(labels)) &&
         strappy_webview_buffer_append_cstring(
           buffer,
           "<span class=\"request-metadata-summary\"></span></div>"
           "<div class=\"request-metadata-body\"></div></div>");
}

static int strappy_webview_append_tool_column_html(
  strappy_webview_buffer *buffer,
  int collapsed)
{
  if (!strappy_webview_buffer_append_cstring(
        buffer,
        "<div class=\"tool-column tool-column-empty")) {
    return 0;
  }
  if (collapsed &&
      !strappy_webview_buffer_append_cstring(buffer,
                                            " tool-column-collapsed")) {
    return 0;
  }
  if (!strappy_webview_buffer_append_cstring(
        buffer,
        "\"><div class=\"tool-rail-title\">"
        "<a class=\"tool-column-toggle\" href=\"#\" "
        "onclick=\"return toggleToolColumn(this)\">"
        "<span class=\"tool-column-disclosure\">") ||
      !strappy_webview_buffer_append_cstring(buffer,
                                            collapsed ? "&#9658;" : "&#9660;") ||
      !strappy_webview_buffer_append_cstring(
        buffer,
        "</span></a>Tool Calls<span class=\"tool-count\"></span></div>"
        "<div class=\"tool-cards\"></div></div>")) {
    return 0;
  }
  return 1;
}

static int strappy_webview_append_styles(strappy_webview_buffer *buffer)
{
  static const char * const chunks[] = {
    "<style>",
    "html,body{margin:0;padding:0;background:#f4f4f4;color:#222;",
    "font:13px Helvetica,Arial,sans-serif;}",
    ".page{padding:18px 14px;}",
    ".empty{margin:90px auto 0;max-width:520px;color:#777;",
    "text-align:center;line-height:1.45;}",
    ".layout{width:100%;}",
    ".chat-column{width:100%;box-sizing:border-box;}",
    ".tool-column{max-width:100%;box-sizing:border-box;margin:0 0 7px;",
    "border:1px solid #cbd7e2;background:#f7fbff;color:#4d6478;",
    "padding:10px 12px;line-height:1.4;white-space:pre-wrap;word-wrap:break-word;}",
    ".tool-column-error{border-color:#d99;background:#fff0f6;color:#7a253f;}",
    ".tool-column-empty{display:none;}",
    "#messages{max-width:none;margin:0;}",
    "#tool-sources,.tool-source-bin{display:none;}",
    ".tool-cards{margin:0;}",
    ".tool-column-collapsed .tool-cards{display:none;}",
    ".tool-rail-title{font-size:11px;font-weight:bold;text-transform:uppercase;",
    "line-height:14px;color:#4d6478;margin:0 0 5px;}",
    ".tool-column-collapsed .tool-rail-title{margin-bottom:0;font-size:13px;",
    "line-height:18px;color:#2f4150;text-transform:none;}",
    ".tool-column-toggle{color:#2468a8;text-decoration:none;margin-right:4px;}",
    ".tool-column-disclosure{display:inline-block;width:12px;",
    "font:10px Monaco,Consolas,monospace;}",
    ".tool-count{display:none;font-weight:bold;text-transform:none;}",
    ".tool-column-collapsed .tool-count{display:inline;}",
    ".streaming-active .tool-column-toggle,.streaming-active .reasoning-toggle{display:none;}",
    ".load-more{display:block;margin:0 auto 14px;padding:7px 10px;",
    "text-align:center;color:#2468a8;text-decoration:none;}",
    ".row{margin:0 0 16px;clear:both;}",
    ".row:last-child{margin-bottom:0;}",
    ".role{font-size:11px;font-weight:bold;color:#666;",
    "text-transform:uppercase;margin:0 0 5px 2px;}",
    ".prompt-group-toggle{color:#2468a8;text-decoration:none;margin-right:4px;}",
    ".prompt-group-disclosure{display:inline-block;width:12px;",
    "font:10px Monaco,Consolas,monospace;}",
    ".prompt-group-hidden{display:none;}",
    ".prompt-group-harness{box-sizing:border-box;margin-left:18px;",
    "padding-left:10px;border-left:2px solid #d7caeb;}",
    ".bubble{display:block;max-width:100%;box-sizing:border-box;",
    "border:1px solid #d8d8d8;",
    "background:#fff;padding:12px 14px;line-height:1.45;",
    "white-space:normal;word-wrap:break-word;}",
    ".assistant .bubble{background:#fcfcfc;}",
    ".bubble-status{color:#777;font-style:italic;}",
    ".tool_call .role,.tool .role{color:#4d6478;}",
    ".tool_call .bubble,.tool .bubble{max-width:100%;box-sizing:border-box;",
    "font:12px Menlo,Consolas,Monaco,monospace;white-space:pre-wrap;}",
    ".tool_call .bubble{background:#fffdf2;border-color:#ded6a8;}",
    ".tool .bubble{background:#f7fbff;border-color:#cbd7e2;}",
    ".tool-panel{font:12px Helvetica,Arial,sans-serif;line-height:1.35;",
    "white-space:normal;color:#24313d;}",
    ".tool-heading{font-weight:bold;margin:0 0 6px;color:#2f4150;}",
    ".tool-subtle{color:#687887;font-size:11px;}",
    ".tool-value{white-space:pre-wrap;word-wrap:break-word;}",
    ".tool-pill{display:inline-block;border:1px solid #cbd7e2;background:#fff;",
    "padding:1px 5px;margin:0 4px 4px 0;color:#4d6478;}",
    ".tool-section{margin:7px 0 0;}",
    ".tool-kv{border-collapse:collapse;width:100%;font:12px Menlo,Consolas,Monaco,monospace;}",
    ".tool-kv th,.tool-kv td{border-top:1px solid #e3e7ea;",
    "padding:4px 6px;text-align:left;vertical-align:top;}",
    ".tool-kv th{width:150px;color:#52616f;font-weight:bold;background:#fafafa;}",
    ".tool-table-wrap{overflow:auto;margin:7px 0 0;border:1px solid #d8e0e7;background:#fff;}",
    ".tool-table{border-collapse:collapse;min-width:100%;",
    "font:12px Menlo,Consolas,Monaco,monospace;}",
    ".tool-table th,.tool-table td{border:1px solid #e0e6eb;",
    "padding:4px 6px;text-align:left;vertical-align:top;}",
    ".tool-table th{background:#f3f6f8;color:#40515f;font-weight:bold;}",
    ".tool-toggle{display:inline-block;margin-top:7px;color:#2468a8;",
    "text-decoration:none;font-size:11px;}",
    ".tool-raw{display:none;margin:7px 0 0;padding:8px;border:1px solid #d8d8d8;",
    "background:#f8f8f8;overflow:auto;white-space:pre-wrap;",
    "font:11px Menlo,Consolas,Monaco,monospace;color:#333;}",
    ".tool-open .tool-raw{display:block;}",
    ".tool-error{border-color:#d99;background:#fff7f7;color:#7a2525;}",
    ".tool-card{border-top:1px solid #dbe5ed;margin:0;",
    "line-height:1.35;word-wrap:break-word;}",
    ".tool-card:first-child{border-top:0;}",
    ".tool-card-toggle{display:block;padding:3px 0;color:#2f4150;",
    "text-decoration:none;font-weight:bold;}",
    ".tool-card-summary{vertical-align:middle;}",
    ".tool-card-body{display:none;max-height:320px;overflow:auto;",
    "border:1px solid #e3e7ea;background:#fff;padding:8px;margin:3px 0 6px;}",
    ".tool-card-open .tool-card-body{display:block;}",
    ".tool-input-title{font-size:11px;font-weight:bold;text-transform:uppercase;",
    "color:#687887;margin:7px 0 4px;}",
    ".tool-input-title:first-child{margin-top:0;}",
    ".tool-field{margin:0 0 9px;}",
    ".tool-field:last-child{margin-bottom:0;}",
    ".tool-output{margin-top:8px;border-top:1px solid #e3e7ea;padding-top:6px;}",
    ".tool-output-toggle{color:#2468a8;text-decoration:none;font-size:11px;}",
    ".tool-disclosure{display:inline-block;width:12px;font:10px Monaco,Consolas,monospace;}",
    ".tool-output-body{display:none;margin-top:6px;max-height:360px;overflow:auto;}",
    ".tool-output-open .tool-output-body{display:block;}",
    ".bubble p,.reasoning-body p{margin:0 0 9px;}",
    ".bubble h1,.bubble h2,.bubble h3,.bubble h4,.bubble h5,.bubble h6,",
    ".reasoning-body h1,.reasoning-body h2,.reasoning-body h3,",
    ".reasoning-body h4,.reasoning-body h5,.reasoning-body h6{",
    "font-size:14px;line-height:1.3;margin:0 0 8px;font-weight:bold;}",
    ".bubble ul,.bubble ol,.reasoning-body ul,.reasoning-body ol{",
    "margin:0 0 9px 20px;padding:0;}",
    ".bubble li,.reasoning-body li{margin:0 0 3px;}",
    ".bubble blockquote,.reasoning-body blockquote{",
    "margin:0 0 9px;padding:0 0 0 10px;",
    "border-left:3px solid #d8d8d8;color:#555;}",
    ".bubble pre,.reasoning-body pre{margin:0 0 9px;padding:8px;",
    "border:1px solid #ddd;",
    "background:#f2f2f2;overflow:auto;white-space:pre-wrap;}",
    ".bubble code,.reasoning-body code{font:12px Monaco,Consolas,monospace;",
    "background:#f2f2f2;",
    "padding:1px 3px;}",
    ".bubble pre code,.reasoning-body pre code{background:transparent;padding:0;}",
    ".bubble .table-wrap,.reasoning-body .table-wrap{",
    "margin:0 0 9px;overflow:auto;}",
    ".bubble table,.reasoning-body table{border-collapse:collapse;",
    "min-width:100%;font-size:12px;line-height:1.35;}",
    ".bubble th,.bubble td,.reasoning-body th,.reasoning-body td{",
    "border:1px solid #d8d8d8;padding:5px 7px;text-align:left;}",
    ".bubble th,.reasoning-body th{background:#f2f2f2;font-weight:bold;}",
    ".bubble a,.reasoning-body a{color:#2468a8;text-decoration:none;}",
    ".bubble p:last-child,.bubble ul:last-child,.bubble ol:last-child,",
    ".bubble pre:last-child,.bubble blockquote:last-child,",
    ".bubble .table-wrap:last-child,",
    ".reasoning-body p:last-child,.reasoning-body ul:last-child,",
    ".reasoning-body ol:last-child,.reasoning-body pre:last-child,",
    ".reasoning-body blockquote:last-child,",
    ".reasoning-body .table-wrap:last-child{margin-bottom:0;}",
    ".reasoning{max-width:100%;box-sizing:border-box;",
    "border:1px solid #ddd;background:#fffdf2;",
    "color:#4f4a36;padding:10px 12px;margin:0 0 7px;line-height:1.4;",
    "white-space:pre-wrap;word-wrap:break-word;}",
    ".reasoning-label{font-size:11px;font-weight:bold;text-transform:uppercase;",
    "line-height:14px;color:#7a7046;margin:0 0 5px;}",
    ".reasoning-collapsed .reasoning-label{margin-bottom:0;}",
    ".reasoning-toggle{color:#2468a8;text-decoration:none;margin-right:4px;}",
    ".reasoning-disclosure{display:inline-block;width:12px;font:10px Monaco,Consolas,monospace;}",
    ".reasoning-collapsed .reasoning-body{display:none;}",
    ".reasoning-body{white-space:pre-wrap;}",
    ".request-metadata{max-width:100%;box-sizing:border-box;",
    "border:1px solid #cbd7e2;background:#f7fbff;color:#2e3f4f;",
    "padding:9px 11px;margin:7px 0 0;line-height:1.35;",
    "white-space:pre-wrap;word-wrap:break-word;}",
    ".request-metadata-title{font-size:11px;font-weight:bold;",
    "text-transform:uppercase;color:#4d6478;margin:0 0 5px;}",
    ".request-metadata-toggle{color:#2468a8;text-decoration:none;margin-right:4px;}",
    ".metadata-disclosure{display:inline-block;width:12px;font:10px Monaco,Consolas,monospace;}",
    ".request-metadata-summary{display:none;font-weight:normal;text-transform:none;",
    "color:#687887;margin-left:7px;}",
    ".request-metadata-collapsed .request-metadata-summary{display:inline;}",
    ".request-metadata-collapsed .request-metadata-body{display:none;}",
    ".request-metadata-body{font:11px Menlo,Consolas,Monaco,monospace;",
    "white-space:pre-wrap;}",
    ".user .role,.user .meta{text-align:left;}",
    ".user .bubble{margin-left:0;background:#eef5ff;border-color:#c8d8ef;}",
    ".harness .role,.harness .meta{text-align:left;color:#6a597c;}",
    ".harness .bubble{margin-left:0;background:#f6f1ff;border-color:#d7caeb;}",
    ".meta{font-size:11px;color:#777;margin-top:6px;}",
    ".state-pending .bubble{opacity:.72;}",
    ".state-pending .status{color:#777;}",
    ".state-error .bubble{border-color:#d99;background:#fff7f7;}",
    ".state-error .status{color:#a22;}",
    ".status a{color:#2468a8;text-decoration:none;}",
    "</style>",
    NULL
  };

  return strappy_webview_append_chunks(buffer, chunks);
}

static int strappy_webview_append_scripts(strappy_webview_buffer *buffer)
{
  static const char * const chunks[] = {
    "<script>",
    "function byId(i){return document.getElementById(i);}",
    "function hasClass(e,n){return e&&(' '+e.className+' ').indexOf(' '+n+' ')>=0;}",
    "function firstByClass(root,name){var n=root.getElementsByTagName('*');",
    "for(var i=0;i<n.length;i++){if(hasClass(n[i],name))return n[i];}return null;}",
    "function ancestorHasClass(e,n){for(e=e&&e.parentNode;e;e=e.parentNode){",
    "if(hasClass(e,n))return true;}return false;}",
    "function nodeText(n){if(!n)return '';if(typeof n.textContent!='undefined')",
    "return n.textContent;return n.innerText||'';}",
    "function setNodeText(n,t){if(!n)return;if(typeof n.textContent!='undefined')",
    "n.textContent=t;else n.innerText=t;}",
    "function escHTML(t){return String(t).replace(/&/g,'&amp;')",
    ".replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/\"/g,'&quot;');}",
    "function mdSafeHref(h){var v=(h||'').replace(/^\\s+|\\s+$/g,'').toLowerCase();",
    "return v.indexOf('http://')==0||v.indexOf('https://')==0||",
    "v.indexOf('mailto:')==0;}",
    "function mdStash(a,s){var k='\\x1f'+a.length+'\\x1f';a[a.length]=s;return k;}",
    "function mdRestore(s,a){var p;do{p=s;s=s.replace(/\\x1f(\\d+)\\x1f/g,",
    "function(m,i){return a[parseInt(i,10)]||'';});}while(s!=p&&s.indexOf('\\x1f')>=0);return s;}",
    "function mdInline(s){var a=[];s=s.replace(/`([^`\\n]+)`/g,",
    "function(m,c){return mdStash(a,'<code>'+c+'</code>');});",
    "s=s.replace(/\\[([^\\]\\n]+)\\]\\(([^)\\s]+)\\)/g,function(m,l,h){",
    "if(!mdSafeHref(h))return l;return mdStash(a,'<a href=\"'+h+'\">'+mdInline(l)+'</a>');});",
    "s=s.replace(/(\\*\\*|__)([^\\n]+?)\\1/g,'<strong>$2</strong>');",
    "s=s.replace(/~~([^\\n]+?)~~/g,'<s>$1</s>');",
    "s=s.replace(/(^|[^*])\\*([^*\\n]+)\\*/g,'$1<em>$2</em>');",
    "s=s.replace(/(^|[^_])_([^_\\n]+)_/g,'$1<em>$2</em>');",
    "return mdRestore(s,a);}",
    "function mdLangClass(line){var l=line.replace(/^\\s*```\\s*/,'')",
    ".replace(/\\s+$/,'');return /^[A-Za-z0-9_-]+$/.test(l)?' class=\"language-'+l+'\"':'';}",
    "function mdParagraph(lines){return '<p>'+mdInline(lines.join('\\n'))",
    ".replace(/\\n/g,'<br />')+'</p>';}",
    "function mdList(lines,start,ordered){var out=[];var i=start;var m;var r;",
    "if(ordered)r=/^\\s*\\d+\\.\\s+(.+)$/;else r=/^\\s*[-*+]\\s+(.+)$/;",
    "for(;i<lines.length;i++){",
    "m=r.exec(lines[i]);if(!m)break;out[out.length]='<li>'+mdInline(m[1])+'</li>';}",
    "return {html:'<'+(ordered?'ol':'ul')+'>'+out.join('')+'</'+(ordered?'ol':'ul')+'>',next:i};}",
    "function mdQuote(lines,start){var out=[];var i=start;var m;",
    "for(;i<lines.length;i++){m=/^\\s*>\\s?(.*)$/.exec(lines[i]);if(!m)break;",
    "out[out.length]=mdInline(m[1]);}return {html:'<blockquote>'+out.join('<br />')+'</blockquote>',next:i};}",
    "function mdTrim(s){return String(s).replace(/^\\s+|\\s+$/g,'');}",
    "function mdEscaped(s,i){var n=0;i--;while(i>=0&&s.charAt(i)=='\\\\'){n++;i--;}return n%2==1;}",
    "function mdHasPipe(s){for(var i=0;i<s.length;i++){",
    "if(s.charAt(i)=='|'&&!mdEscaped(s,i))return true;}return false;}",
    "function mdSplitTableRow(line){var s=mdTrim(line);var c=[];var p='';var i,ch;",
    "if(s.charAt(0)=='|'&&!mdEscaped(s,0))s=s.substring(1);",
    "if(s.length&&s.charAt(s.length-1)=='|'&&!mdEscaped(s,s.length-1))",
    "s=s.substring(0,s.length-1);",
    "for(i=0;i<s.length;i++){ch=s.charAt(i);if(ch=='|'&&!mdEscaped(s,i)){",
    "c[c.length]=mdTrim(p).replace(/\\\\\\|/g,'|');p='';}else p+=ch;}",
    "c[c.length]=mdTrim(p).replace(/\\\\\\|/g,'|');return c;}",
    "function mdTableAligns(line){var c=mdSplitTableRow(line);var a=[];var i,t;",
    "if(!mdHasPipe(line))return null;for(i=0;i<c.length;i++){",
    "t=c[i].replace(/\\s/g,'');if(!/^:?-{3,}:?$/.test(t))return null;",
    "if(t.charAt(0)==':'&&t.charAt(t.length-1)==':')a[i]='center';",
    "else if(t.charAt(t.length-1)==':')a[i]='right';else a[i]='';}return a;}",
    "function mdTableCell(tag,text,align){var a=align?",
    "' style=\"text-align:'+align+'\"':'';return '<'+tag+a+'>'+mdInline(text)+'</'+tag+'>';}",
    "function mdTable(lines,start){var h=mdSplitTableRow(lines[start]);",
    "var a=mdTableAligns(lines[start+1]);var cols=Math.max(h.length,a.length);",
    "var out=['<div class=\"table-wrap\"><table><thead><tr>'];var i,j,c;",
    "for(j=0;j<cols;j++)out[out.length]=mdTableCell('th',h[j]||'',a[j]);",
    "out[out.length]='</tr></thead><tbody>';",
    "for(i=start+2;i<lines.length&&!/^\\s*$/.test(lines[i])&&mdHasPipe(lines[i]);i++){",
    "c=mdSplitTableRow(lines[i]);out[out.length]='<tr>';",
    "for(j=0;j<cols;j++)out[out.length]=mdTableCell('td',c[j]||'',a[j]);",
    "out[out.length]='</tr>';}out[out.length]='</tbody></table></div>';",
    "return {html:out.join(''),next:i};}",
    "function mdToHTML(raw){var text=(raw||'').replace(/\\r\\n/g,'\\n').replace(/\\r/g,'\\n');",
    "var lines=text.split('\\n');var out=[];var para=[];var i,line,m,r,code;",
    "function flush(){if(para.length){out[out.length]=mdParagraph(para);para=[];}}",
    "for(i=0;i<lines.length;i++){line=lines[i];if(/^\\s*$/.test(line)){flush();continue;}",
    "if(/^\\s*```/.test(line)){flush();code=[];var cls=mdLangClass(line);",
    "for(i=i+1;i<lines.length&&!/^\\s*```\\s*$/.test(lines[i]);i++)code[code.length]=lines[i];",
    "out[out.length]='<pre><code'+cls+'>'+code.join('\\n')+'</code></pre>';continue;}",
    "m=/^(#{1,6})\\s+(.+)$/.exec(line);if(m){flush();",
    "out[out.length]='<h'+m[1].length+'>'+mdInline(m[2])+'</h'+m[1].length+'>';continue;}",
    "if((i+1)<lines.length&&mdHasPipe(line)&&mdTableAligns(lines[i+1])){",
    "flush();r=mdTable(lines,i);out[out.length]=r.html;i=r.next-1;continue;}",
    "if(/^\\s*[-*+]\\s+/.test(line)){flush();r=mdList(lines,i,0);out[out.length]=r.html;i=r.next-1;continue;}",
    "if(/^\\s*\\d+\\.\\s+/.test(line)){flush();r=mdList(lines,i,1);out[out.length]=r.html;i=r.next-1;continue;}",
    "if(/^\\s*>/.test(line)){flush();r=mdQuote(lines,i);out[out.length]=r.html;i=r.next-1;continue;}",
    "para[para.length]=line;}flush();return out.join('');}",
    "function renderMarkdownNode(n){if(!n)return;if(typeof n._strappyMarkdown=='undefined')",
    "n._strappyMarkdown=n.innerHTML;n.innerHTML=mdToHTML(n._strappyMarkdown);}",
    "function shouldRenderMarkdownBubble(n){return hasClass(n,'bubble')&&",
    "!hasClass(n,'bubble-status')&&ancestorHasClass(n,'assistant')&&",
    "!ancestorHasClass(n,'tool-column');}",
    "function renderMarkdown(root){root=root||document;var n=root.getElementsByTagName('*');",
    "for(var i=0;i<n.length;i++){if(shouldRenderMarkdownBubble(n[i]))",
    "renderMarkdownNode(n[i]);}}",
    "function isObj(v){return v&&typeof v=='object'&&",
    "Object.prototype.toString.call(v)!='[object Array]';}",
    "function isArr(v){return Object.prototype.toString.call(v)=='[object Array]';}",
    "function jsonText(v){if(v===null||typeof v=='undefined')return '';",
    "if(typeof v=='string')return v;if(typeof v=='number'||typeof v=='boolean')",
    "return String(v);if(typeof JSON!='undefined'&&JSON.stringify)return JSON.stringify(v);",
    "return String(v);}",
    "function addMetaLine(lines,label,value){var t=jsonText(value);",
    "if(t!=='')lines[lines.length]=label+': '+t;}",
    "function addNestedMetaLine(lines,label,root,parent,key){",
    "if(isObj(root)&&isObj(root[parent]))addMetaLine(lines,label,root[parent][key]);}",
    "function formatMetadata(root){var lines=[];var usage;var gen;",
    "if(!isObj(root))return jsonText(root);",
    "addMetaLine(lines,'Response ID',root.response_id);",
    "addMetaLine(lines,'Model',root.model);",
    "addMetaLine(lines,'Created',root.created);",
    "addMetaLine(lines,'Finish reason',root.finish_reason);",
    "addMetaLine(lines,'Native finish reason',root.native_finish_reason);",
    "addMetaLine(lines,'Service tier',root.service_tier);",
    "addMetaLine(lines,'System fingerprint',root.system_fingerprint);",
    "addMetaLine(lines,'HTTP status',root.http_status);",
    "usage=root.usage;if(isObj(usage)){",
    "addMetaLine(lines,'Cost',usage.cost);",
    "addMetaLine(lines,'Prompt tokens',usage.prompt_tokens);",
    "addMetaLine(lines,'Completion tokens',usage.completion_tokens);",
    "addMetaLine(lines,'Total tokens',usage.total_tokens);",
    "addNestedMetaLine(lines,'Cached prompt tokens',usage,'prompt_tokens_details','cached_tokens');",
    "addNestedMetaLine(lines,'Cache write tokens',usage,'prompt_tokens_details','cache_write_tokens');",
    "addNestedMetaLine(lines,'Prompt audio tokens',usage,'prompt_tokens_details','audio_tokens');",
    "addNestedMetaLine(lines,'Reasoning tokens',usage,'completion_tokens_details','reasoning_tokens');",
    "addNestedMetaLine(lines,'Completion audio tokens',usage,'completion_tokens_details','audio_tokens');",
    "addNestedMetaLine(lines,'Accepted prediction tokens',usage,'completion_tokens_details','accepted_prediction_tokens');",
    "addNestedMetaLine(lines,'Rejected prediction tokens',usage,'completion_tokens_details','rejected_prediction_tokens');",
    "addNestedMetaLine(lines,'Upstream inference cost',usage,'cost_details','upstream_inference_cost');}",
    "gen=root.generation;if(isObj(gen)){if(isObj(gen.data))gen=gen.data;",
    "addMetaLine(lines,'Provider',gen.provider_name);",
    "addMetaLine(lines,'Router',gen.router);",
    "addMetaLine(lines,'OpenRouter request ID',gen.request_id);",
    "addMetaLine(lines,'Upstream ID',gen.upstream_id);",
    "addMetaLine(lines,'Streamed',gen.streamed);",
    "addMetaLine(lines,'Data region',gen.data_region);",
    "addMetaLine(lines,'Latency (ms)',gen.latency);",
    "addMetaLine(lines,'Generation time (ms)',gen.generation_time);",
    "addMetaLine(lines,'Moderation latency (ms)',gen.moderation_latency);",
    "addMetaLine(lines,'Native prompt tokens',gen.native_tokens_prompt);",
    "addMetaLine(lines,'Native completion tokens',gen.native_tokens_completion);",
    "addMetaLine(lines,'Native reasoning tokens',gen.native_tokens_reasoning);",
    "addMetaLine(lines,'Native cached tokens',gen.native_tokens_cached);",
    "addMetaLine(lines,'Total cost',gen.total_cost);",
    "addMetaLine(lines,'Usage',gen.usage);",
    "addMetaLine(lines,'Upstream inference cost',gen.upstream_inference_cost);",
    "addMetaLine(lines,'Generation finish reason',gen.finish_reason);",
    "addMetaLine(lines,'Generation native finish reason',gen.native_finish_reason);",
    "addMetaLine(lines,'Web search engine',gen.web_search_engine);",
    "addMetaLine(lines,'Search results',gen.num_search_results);}",
    "if(lines.length)return lines.join('\\n');return jsonText(root);}",
    "function metadataGen(root){var gen=isObj(root)?root.generation:null;",
    "if(isObj(gen)&&isObj(gen.data))gen=gen.data;return gen;}",
    "function metadataSummaryValue(v){var t=jsonText(v);return t!==''?t:'-';}",
    "function metadataSummaryCost(v){var t=jsonText(v);if(t==='')return '$-';",
    "return t.charAt(0)=='$'?t:'$'+t;}",
    "function formatMetadataSummary(root){var usage=isObj(root)?root.usage:null;",
    "var gen=metadataGen(root);var cost='';var input='';var output='';",
    "if(isObj(usage)){cost=usage.cost;input=usage.prompt_tokens;output=usage.completion_tokens;}",
    "if(metadataSummaryValue(cost)=='-'&&isObj(gen))cost=gen.total_cost||gen.upstream_inference_cost;",
    "if(metadataSummaryValue(input)=='-'&&isObj(gen))input=gen.native_tokens_prompt;",
    "if(metadataSummaryValue(output)=='-'&&isObj(gen))output=gen.native_tokens_completion;",
    "return metadataSummaryCost(cost)+' \\u2191 '+metadataSummaryValue(input)+' \\u2193 '+metadataSummaryValue(output);}",
    "function parseMetadata(raw){if(typeof JSON!='undefined'&&JSON.parse)",
    "return JSON.parse(raw);return eval('('+raw+')');}",
    "function renderMetadata(root){root=root||document;var n=root.getElementsByTagName('*');",
    "for(var i=0;i<n.length;i++){if(!hasClass(n[i],'request-metadata'))continue;",
    "var raw=n[i].getAttribute('data-metadata');var body=firstByClass(n[i],'request-metadata-body');",
    "var summary=firstByClass(n[i],'request-metadata-summary');var parsed;",
    "if(!raw||!body)continue;try{parsed=parseMetadata(raw);setNodeText(body,formatMetadata(parsed));",
    "if(summary)setNodeText(summary,formatMetadataSummary(parsed));}",
    "catch(e){setNodeText(body,raw);if(summary)setNodeText(summary,'');}}}",
    "function toggleMetadata(a){var p=a;while(p&&!hasClass(p,'request-metadata'))p=p.parentNode;",
    "if(!p)return false;var d=firstByClass(a,'metadata-disclosure');",
    "if(hasClass(p,'request-metadata-collapsed')){",
    "p.className=p.className.replace(/\\srequest-metadata-collapsed/g,'');if(d)d.innerHTML='&#9660;';}",
    "else{p.className+=' request-metadata-collapsed';if(d)d.innerHTML='&#9658;';}return false;}",
    "var strappyPromptGroupCollapsed={};",
    "function promptGroupKey(row){return row&&row.getAttribute?row.getAttribute('data-prompt-group-key')||'':'';}",
    "function rowActor(row){return row&&row.getAttribute?row.getAttribute('data-actor')||'':'';}",
    "function rowIsHarness(row){return rowActor(row)=='harness'||hasClass(row,'harness');}",
    "function setRowClass(row,name,on){if(!row)return;if(on){if(!hasClass(row,name))row.className+=' '+name;}",
    "else row.className=row.className.replace(new RegExp('\\\\s'+name,'g'),'');}",
    "function removePromptGroupToggle(row){var role=firstByClass(row,'role');var a;if(!role)return;",
    "a=firstByClass(role,'prompt-group-toggle');if(a&&a.parentNode)a.parentNode.removeChild(a);}",
    "function promptGroupAnchor(rows){var i;for(i=0;i<rows.length;i++){if(rowIsHarness(rows[i]))return rows[i];}",
    "return null;}",
    "function promptGroupHarnessCount(rows){var i,n=0;for(i=0;i<rows.length;i++)if(rowIsHarness(rows[i]))n++;return n;}",
    "function ensurePromptGroupToggle(row,key,collapsed){var role,a,d;if(!row)return;",
    "role=firstByClass(row,'role');if(!role)return;a=firstByClass(role,'prompt-group-toggle');",
    "if(!a){a=document.createElement('a');a.className='prompt-group-toggle';a.href='#';",
    "a.onclick=function(){return togglePromptGroup(this);};d=document.createElement('span');",
    "d.className='prompt-group-disclosure';a.appendChild(d);role.insertBefore(a,role.firstChild);}",
    "a.setAttribute('data-prompt-group-key',key);d=firstByClass(a,'prompt-group-disclosure');",
    "if(d)d.innerHTML=collapsed?'&#9658;':'&#9660;';}",
    "function decoratePromptGroups(root){var rows=messageRows();var groups={};var keys=[];var i,k,row;",
    "for(i=0;i<rows.length;i++){row=rows[i];k=promptGroupKey(row);",
    "removePromptGroupToggle(row);",
    "setRowClass(row,'prompt-group-hidden',0);setRowClass(row,'prompt-group-harness',",
    "k!==''&&rowIsHarness(row));if(k==='')continue;",
    "if(!groups[k]){groups[k]=[];keys[keys.length]=k;}groups[k][groups[k].length]=row;}",
    "for(i=0;i<keys.length;i++){k=keys[i];var g=groups[k];var anchor=promptGroupAnchor(g);",
    "var collapsed=strappyPromptGroupCollapsed[k]?1:0;if(anchor&&promptGroupHarnessCount(g)>1)ensurePromptGroupToggle(anchor,k,collapsed);",
    "for(var j=0;j<g.length;j++)setRowClass(g[j],'prompt-group-hidden',collapsed&&rowIsHarness(g[j])&&g[j]!==anchor);}}",
    "function togglePromptGroup(a){var key=a&&a.getAttribute?a.getAttribute('data-prompt-group-key'):'';",
    "if(key==='')return false;strappyPromptGroupCollapsed[key]=strappyPromptGroupCollapsed[key]?0:1;",
    "decoratePromptGroups(document);return false;}",
    "function setMessagePromptGroup(id,key,actor){var r=byId(id);if(!r)return;",
    "if(key&&r.setAttribute)r.setAttribute('data-prompt-group-key',key);",
    "if(actor&&r.setAttribute)r.setAttribute('data-actor',actor);renderMessageDecorations(document);}",
    "function toggleReasoning(a){var p=a;while(p&&!hasClass(p,'reasoning'))p=p.parentNode;",
    "if(!p)return false;setReasoningCollapsed(p,hasClass(p,'reasoning-collapsed')?0:1);return false;}",
    "function setReasoningCollapsed(box,collapsed){var d=firstByClass(box,'reasoning-disclosure');",
    "if(collapsed){if(!hasClass(box,'reasoning-collapsed'))box.className+=' reasoning-collapsed';",
    "if(d)d.innerHTML='&#9658;';}else{box.className=box.className.replace(/\\sreasoning-collapsed/g,'');",
    "if(d)d.innerHTML='&#9660;';}}",
    "function setMessageReasoningCollapsed(id,collapsed){var r=byId(id);var box,body;",
    "if(!r)return;box=firstByClass(r,'reasoning');body=firstByClass(r,'reasoning-body');",
    "if(!box||!body||nodeText(body)==='')return;setReasoningCollapsed(box,collapsed);}",
    "function setToolColumnCollapsed(box,collapsed){var d=firstByClass(box,'tool-column-disclosure');",
    "if(collapsed){if(!hasClass(box,'tool-column-collapsed'))box.className+=' tool-column-collapsed';",
    "if(d)d.innerHTML='&#9658;';}else{box.className=box.className.replace(/\\stool-column-collapsed/g,'');",
    "if(d)d.innerHTML='&#9660;';}}",
    "function toggleToolColumn(a){var p=a;while(p&&!hasClass(p,'tool-column'))p=p.parentNode;",
    "if(!p)return false;setToolColumnCollapsed(p,hasClass(p,'tool-column-collapsed')?0:1);return false;}",
    "function setMessageToolColumnCollapsed(id,collapsed){var r=byId(id);var box;",
    "if(!r)return;box=firstByClass(r,'tool-column');if(!box)return;",
    "setToolColumnCollapsed(box,collapsed);}",
    "function parseJSONSafe(raw){try{if(typeof JSON!='undefined'&&JSON.parse)",
    "return JSON.parse(raw);return eval('('+raw+')');}catch(e){return null;}}",
    "function shortText(v,n){var t=jsonText(v).replace(/\\s+/g,' ');",
    "if(t.length>n)t=t.substring(0,n-1)+'...';return t;}",
    "function toolRawPreview(raw){var t=jsonText(raw);var n=12000;if(t.length>n)",
    "return t.substring(0,n)+'\\n... raw JSON preview truncated; '+(t.length-n)+' characters hidden';return t;}",
    "function toolRaw(raw){return '<a class=\"tool-toggle\" href=\"#\" onclick=\"return toggleToolRaw(this)\">Raw JSON preview</a>'",
    "+'<pre class=\"tool-raw\">'+escHTML(toolRawPreview(raw))+'</pre>';}",
    "function toggleToolRaw(a){var p=a;while(p&&!hasClass(p,'tool-panel'))p=p.parentNode;",
    "if(!p)return false;if(hasClass(p,'tool-open'))",
    "p.className=p.className.replace(/\\stool-open/g,'');else p.className+=' tool-open';return false;}",
    "function toolOwn(o,k){return !Object.prototype.hasOwnProperty||Object.prototype.hasOwnProperty.call(o,k);}",
    "function toolSpecialValue(v){var m=[];if(!isObj(v))return null;",
    "if(v.type=='blob')return '<span class=\"tool-subtle\">blob, '+escHTML(jsonText(v.size_bytes))+' bytes</span>';",
    "if(v.type=='text'&&typeof v.value!='undefined'){if(v.truncated)m[m.length]='truncated';",
    "if(typeof v.size_bytes!='undefined')m[m.length]=jsonText(v.size_bytes)+' bytes';",
    "if(v.contains_nul_bytes)m[m.length]='contains NUL bytes';",
    "return '<span class=\"tool-value\">'+escHTML(jsonText(v.value))+'</span>'",
    "+(m.length?' <span class=\"tool-subtle\">'+escHTML(m.join(', '))+'</span>':'');}",
    "return null;}",
    "function toolScalarValue(v){var s=toolSpecialValue(v);if(s!==null)return s;",
    "if(v===null||typeof v=='undefined')return '<span class=\"tool-subtle\">null</span>';",
    "return '<span class=\"tool-value\">'+escHTML(jsonText(v))+'</span>';}",
    "function toolKeys(obj){var keys=[];var k;for(k in obj){if(toolOwn(obj,k))keys[keys.length]=k;}return keys;}",
    "function toolAddColumn(cols,seen,key){var marker='$'+key;if(typeof seen[marker]=='undefined'){seen[marker]=1;cols[cols.length]=key;}}",
    "function toolAllRowsAreObjects(arr){var i;for(i=0;i<arr.length;i++){",
    "if(!isObj(arr[i])||toolSpecialValue(arr[i])!==null)return 0;}return arr.length>0;}",
    "function toolObjectColumns(arr){var cols=[];var seen={};var i,k,o;",
    "for(i=0;i<arr.length;i++){o=arr[i];for(k in o){if(toolOwn(o,k))toolAddColumn(cols,seen,k);}}return cols;}",
    "function toolArrayMaxColumns(arr){var max=0;var i;if(arr.length===0)return 0;",
    "for(i=0;i<arr.length;i++){if(!isArr(arr[i]))return -1;if(arr[i].length>max)max=arr[i].length;}return max;}",
    "function toolJSONHTML(v){var s=toolSpecialValue(v);if(s!==null)return s;",
    "if(isObj(v))return toolObjectTable(v);if(isArr(v))return toolArrayTable(v);return toolScalarValue(v);}",
    "function toolObjectTable(obj){var keys=toolKeys(obj);var h;var i,k;if(keys.length===0)",
    "return '<span class=\"tool-subtle\">empty object</span>';",
    "h=['<div class=\"tool-table-wrap\"><table class=\"tool-table\"><thead><tr>'",
    "+'<th>Field</th><th>Value</th></tr></thead><tbody>'];",
    "for(i=0;i<keys.length;i++){k=keys[i];h[h.length]='<tr><td>'+escHTML(k)+'</td><td>'+toolJSONHTML(obj[k])+'</td></tr>';}",
    "h[h.length]='</tbody></table></div>';return h.join('');}",
    "function toolObjectArrayTable(arr){var cols=toolObjectColumns(arr);var h;var i,c,o,k;",
    "if(cols.length===0)return '<span class=\"tool-subtle\">empty object rows</span>';",
    "h=['<div class=\"tool-table-wrap\"><table class=\"tool-table\"><thead><tr>'];",
    "for(c=0;c<cols.length;c++)h[h.length]='<th>'+escHTML(cols[c])+'</th>';",
    "h[h.length]='</tr></thead><tbody>';for(i=0;i<arr.length;i++){o=arr[i];h[h.length]='<tr>';",
    "for(c=0;c<cols.length;c++){k=cols[c];h[h.length]='<td>'+",
    "(typeof o[k]=='undefined'?'<span class=\"tool-subtle\">-</span>':toolJSONHTML(o[k]))+'</td>';}",
    "h[h.length]='</tr>';}h[h.length]='</tbody></table></div>';return h.join('');}",
    "function toolArrayRowsTable(arr,count){var h=['<div class=\"tool-table-wrap\"><table class=\"tool-table\"><thead><tr>'];",
    "var i,c,row;for(c=0;c<count;c++)h[h.length]='<th>'+c+'</th>';",
    "h[h.length]='</tr></thead><tbody>';for(i=0;i<arr.length;i++){row=arr[i];h[h.length]='<tr>';",
    "for(c=0;c<count;c++)h[h.length]='<td>'+",
    "(typeof row[c]=='undefined'?'<span class=\"tool-subtle\">-</span>':toolJSONHTML(row[c]))+'</td>';",
    "h[h.length]='</tr>';}h[h.length]='</tbody></table></div>';return h.join('');}",
    "function toolIndexValueTable(arr){var h=['<div class=\"tool-table-wrap\"><table class=\"tool-table\"><thead><tr>'",
    "+'<th>Index</th><th>Value</th></tr></thead><tbody>'];var i;",
    "for(i=0;i<arr.length;i++)h[h.length]='<tr><td>'+i+'</td><td>'+toolJSONHTML(arr[i])+'</td></tr>';",
    "h[h.length]='</tbody></table></div>';return h.join('');}",
    "function toolArrayTable(arr){var cols;if(arr.length===0)return '<span class=\"tool-subtle\">empty array</span>';",
    "if(toolAllRowsAreObjects(arr))return toolObjectArrayTable(arr);cols=toolArrayMaxColumns(arr);",
    "if(cols>=0)return toolArrayRowsTable(arr,cols);return toolIndexValueTable(arr);}",
    "function toolKV(obj){return toolObjectTable(obj);}",
    "function toolPanel(title,meta,body,raw,cls){return '<div class=\"tool-panel '+(cls||'')+'\">'",
    "+'<div class=\"tool-heading\">'+escHTML(title)+(meta?' <span class=\"tool-subtle\">'+escHTML(meta)+'</span>':'')+'</div>'",
    "+(body||'')+(raw?toolRaw(raw):'')+'</div>';}",
    "function toolCallsPayload(raw){var p=raw.replace(/^\\s*Tool call input:\\s*/,'');",
    "return parseJSONSafe(p);}",
    "function toolArgsValue(args){var parsed=args;var p;if(typeof args=='string'){",
    "p=parseJSONSafe(args);if(p!==null)parsed=p;}return parsed;}",
    "function toolArgsHTML(args){var parsed=toolArgsValue(args);return toolJSONHTML(parsed);}",
    "function parseToolEvents(raw){var lines=raw.split(/\\n/);var out=[];var i,o;",
    "for(i=0;i<lines.length;i++){if(!/^\\s*\\{/.test(lines[i]))continue;",
    "o=parseJSONSafe(lines[i]);if(isObj(o)&&o.event)out[out.length]=o;}return out.length?out:null;}",
    "function toolObjectWithout(o,skip){var out={};var k;for(k in o){if(toolOwn(o,k)&&!toolOwn(skip,k))out[k]=o[k];}return out;}",
    "function databaseListHTML(o,raw){var dbs=isArr(o.databases)?o.databases:[];var rest;var h;",
    "h=['<div><span class=\"tool-pill\">'+dbs.length+' databases</span>'];",
    "if(o.availability_state)h[h.length]='<span class=\"tool-pill\">'+escHTML(jsonText(o.availability_state))+'</span>';",
    "h[h.length]='</div>';if(dbs.length)h[h.length]=toolObjectArrayTable(dbs);",
    "else h[h.length]='<span class=\"tool-subtle\">No databases</span>';",
    "rest=toolObjectWithout(o,{databases:1});if(toolKeys(rest).length)h[h.length]=toolObjectTable(rest);",
    "return toolPanel('Database List','',h.join(''),raw,'');}",
    "function queryHTML(o,raw){var cols=isArr(o.columns)?o.columns:[];var rows=isArr(o.rows)?o.rows:[];",
    "var rest;var h=['<div><span class=\"tool-pill\">'+escHTML(jsonText(o.database_id))+'</span>',",
    "'<span class=\"tool-pill\">'+rows.length+' rows shown</span>'];",
    "if(o.truncated)h[h.length]='<span class=\"tool-pill\">truncated</span>';",
    "if(o.ok===false)h[h.length]='<span class=\"tool-pill\">error</span>';h[h.length]='</div>';",
    "h[h.length]='<div class=\"tool-table-wrap\"><table class=\"tool-table\"><thead><tr>';",
    "for(var c=0;c<cols.length;c++)h[h.length]='<th>'+escHTML(jsonText(cols[c].name||('c'+c)))+'</th>';",
    "h[h.length]='</tr></thead><tbody>';for(var r=0;r<rows.length;r++){var row=rows[r]||[];",
    "h[h.length]='<tr>';for(c=0;c<cols.length;c++)h[h.length]='<td>'+toolJSONHTML(row[c])+'</td>';h[h.length]='</tr>';}",
    "h[h.length]='</tbody></table></div>';rest=toolObjectWithout(o,{columns:1,rows:1});",
    "if(toolKeys(rest).length)h[h.length]=toolObjectTable(rest);",
    "return toolPanel('Database Query','',h.join(''),raw,o.ok===false?'tool-error':'');}",
    "function toolResultPanel(o,raw,name){if(isObj(o)&&isArr(o.databases))return databaseListHTML(o,raw);",
    "if(isObj(o)&&isArr(o.columns)&&isArr(o.rows))return queryHTML(o,raw);",
    "if(isObj(o)&&o.error)return toolPanel('Tool Error',jsonText(o.tool_name||name||''),",
    "'<div class=\"tool-value\">'+escHTML(jsonText(o.error))+'</div>'+toolObjectTable(o),raw,'tool-error');",
    "return toolPanel('Tool Result',jsonText(name||''),toolJSONHTML(o),raw,'');}",
    "function toggleToolCard(a){var p=a;while(p&&!hasClass(p,'tool-card'))p=p.parentNode;",
    "if(!p)return false;var d=firstByClass(a,'tool-disclosure');",
    "if(hasClass(p,'tool-card-open')){p.className=p.className.replace(/\\stool-card-open/g,'');",
    "if(d)d.innerHTML='&#9658;';}else{p.className+=' tool-card-open';if(d)d.innerHTML='&#9660;';}return false;}",
    "function toolUsesDatabaseId(name){return name=='database_query'||name=='database_context_read';}",
    "function decorateToolArgs(name,args,dbNames){var parsed=args;var p;var db;var out;var k;",
    "if(typeof args=='string'){p=parseJSONSafe(args);if(p!==null)parsed=p;}",
    "if(toolUsesDatabaseId(name)&&isObj(parsed)){db=jsonText(parsed.database_id);",
    "if(dbNames&&dbNames[db]){out={filename:dbNames[db]};",
    "for(k in parsed){if(toolOwn(parsed,k))out[k]=parsed[k];}return out;}}return args;}",
    "function toolSectionHTML(label,body){return '<div class=\"tool-field\"><div class=\"tool-input-title\">'",
    "+escHTML(label)+'</div>'+(body||'')+'</div>';}",
    "function toolInputHTML(name,args,dbNames){var shown=decorateToolArgs(name,args,dbNames);",
    "var t=jsonText(shown);return toolSectionHTML('Input',",
    "t!==''?toolArgsHTML(shown):'<span class=\"tool-subtle\">No input</span>');}",
    "function toolOutputBody(raw,name,error){var o;if(!raw)return '<span class=\"tool-subtle\">Output pending</span>';",
    "o=parseJSONSafe(raw);if(o===null)return toolPanel(error?'Tool Error':'Tool Result',jsonText(name||''),",
    "'<div>'+escHTML(shortText(raw,600))+'</div>',raw,error?'tool-error':'');",
    "return toolResultPanel(o,raw,name);}",
    "function toolOutputHasError(raw){var o;if(!raw)return 0;o=parseJSONSafe(raw);",
    "return isObj(o)&&((o.error&&jsonText(o.error)!=='')||o.ok===false);}",
    "function toolOutputHTML(raw,name,error){return toolSectionHTML('Output',",
    "toolOutputBody(raw,name,error));}",
    "function toolFilenameForCard(card){var name=jsonText(card.name||'');var parsed=toolArgsValue(card.args||'');",
    "var db='';var f='';if(toolUsesDatabaseId(name)&&isObj(parsed)){f=jsonText(parsed.filename);",
    "db=jsonText(parsed.database_id);if(f===''&&card.dbNames&&db!==''&&card.dbNames[db])f=jsonText(card.dbNames[db]);",
    "if(f===''&&db!=='')f=db;}return f;}",
    "function toolCardSummary(card,index){var name=jsonText(card.name||'Tool');var file=toolFilenameForCard(card);",
    "return (index+1)+': '+name+(file!==''?' - '+file:'');}",
    "function toolCardHTML(card,index){var name=jsonText(card.name||'Tool');",
    "var cls='tool-card'+(card.error?' tool-error':'');var h='<div class=\"'+cls+'\">'",
    "+'<a class=\"tool-card-toggle\" href=\"#\" onclick=\"return toggleToolCard(this)\">'",
    "+'<span class=\"tool-disclosure\">&#9658;</span><span class=\"tool-card-summary\">'",
    "+escHTML(toolCardSummary(card,index))+'</span></a><div class=\"tool-card-body\">';",
    "if(!card.outputOnly)h+=toolInputHTML(name,card.args||'',card.dbNames);",
    "h+=toolOutputHTML(card.output,name,card.error);return h+'</div></div>';}",
    "function toolCallCardData(call){var fn=isObj(call)?call['function']:null;",
    "return {name:isObj(fn)?jsonText(fn.name):'unknown',id:isObj(call)?jsonText(call.id):'',",
    "args:(isObj(fn)&&typeof fn.arguments!='undefined')?fn.arguments:'',output:null,error:false};}",
    "function appendCardsFromEvents(cards,events,dbNames){var map={};var order=[];var i,e,c,id;",
    "for(i=0;i<events.length;i++){e=events[i];id=jsonText(e.tool_call_id||('event-'+i));",
    "if(!map[id]){map[id]={name:jsonText(e.tool_name||'Tool'),id:id,args:e.arguments_json||'',output:null,error:false};",
    "map[id].dbNames=dbNames;map[id]._index=order.length;order[order.length]=map[id];}",
    "c=map[id];if(e.tool_name)c.name=jsonText(e.tool_name);",
    "if(e.event=='call'){c.args=e.arguments_json||c.args;}else{c.output=e.result_json||'';",
    "c.error=(e.event=='error')||toolOutputHasError(c.output);}}",
    "for(i=0;i<order.length;i++)cards[cards.length]=order[i];}",
    "function collectDatabaseNamesFromObject(dbNames,o){var dbs=isObj(o)&&isArr(o.databases)?o.databases:null;var i,d,id,name;",
    "if(isObj(o)){id=jsonText(o.database_id);name=jsonText(o.filename);if(id!==''&&name!=='')dbNames[id]=name;}",
    "if(!dbs)return;for(i=0;i<dbs.length;i++){d=dbs[i]||{};id=jsonText(d.database_id);name=jsonText(d.filename);",
    "if(id!==''&&name!=='')dbNames[id]=name;}}",
    "function collectDatabaseNamesFromRaw(dbNames,raw){var events=parseToolEvents(raw);var i,o;",
    "if(events){for(i=0;i<events.length;i++){o=parseJSONSafe(events[i].result_json||'');collectDatabaseNamesFromObject(dbNames,o);}return;}",
    "o=parseJSONSafe(raw);collectDatabaseNamesFromObject(dbNames,o);}",
    "function collectDatabaseNames(rows){var dbNames={};var i;for(i=0;i<rows.length;i++)collectDatabaseNamesFromRaw(dbNames,toolRowRaw(rows[i]));return dbNames;}",
    "function appendCardsFromToolCall(cards,pending,raw,dbNames){var calls=toolCallsPayload(raw);var i,c;",
    "if(isArr(calls)){for(i=0;i<calls.length;i++){c=toolCallCardData(calls[i]);c.dbNames=dbNames;cards[cards.length]=c;pending[pending.length]=c;}}",
    "else{c={name:'Tool Call',id:'',args:raw,output:null,error:false};cards[cards.length]=c;pending[pending.length]=c;}}",
    "function isToolRow(row){return hasClass(row,'row')&&(hasClass(row,'tool_call')||hasClass(row,'tool'));}",
    "function isAssistantRow(row){return hasClass(row,'row')&&hasClass(row,'assistant');}",
    "function toolSources(){return byId('tool-sources')||byId('tools');}",
    "function rowId(row){return row&&row.id?row.id:'';}",
    "function messageRows(){var m=byId('messages');var out=[];var n,i;if(!m)return out;n=m.childNodes;",
    "for(i=0;i<n.length;i++){if(hasClass(n[i],'row'))out[out.length]=n[i];}return out;}",
    "function assistantRows(){var m=byId('messages');var out=[];var n,i;if(!m)return out;",
    "n=m.getElementsByTagName('*');for(i=0;i<n.length;i++){if(isAssistantRow(n[i]))out[out.length]=n[i];}return out;}",
    "function rowMessageKey(row){return row&&row.getAttribute?row.getAttribute('data-message-key')||'':'';}",
    "function rowTargetMessageKey(row){return row&&row.getAttribute?row.getAttribute('data-target-message-key')||'':'';}",
    "function assistantByMessageKey(key){var rows=assistantRows();var i;if(!key)return null;",
    "for(i=0;i<rows.length;i++){if(rowMessageKey(rows[i])==key)return rows[i];}return null;}",
    "function explicitToolTarget(row){var key=rowTargetMessageKey(row);var target;if(key==='')return '';",
    "target=assistantByMessageKey(key);return target?rowId(target):'';}",
    "function setToolTarget(row,target){if(row&&target&&row.setAttribute)row.setAttribute('data-tool-target',target);}",
    "function toolTarget(row){return row&&row.getAttribute?row.getAttribute('data-tool-target')||'':'';}",
    "function assignToolTargetsFromMessageOrder(){var rows=messageRows();var pending=[];var last='';var i,j,row,target;",
    "for(i=0;i<rows.length;i++){row=rows[i];if(isToolRow(row)){target=explicitToolTarget(row);",
    "if(target!==''){setToolTarget(row,target);continue;}if(toolTarget(row)==='')pending[pending.length]=row;continue;}",
    "if(isAssistantRow(row)){target=rowId(row);if(target!==''){last=target;for(j=0;j<pending.length;j++)setToolTarget(pending[j],target);pending=[];}}}",
    "if(last!==''){for(j=0;j<pending.length;j++)setToolTarget(pending[j],last);}}",
    "function defaultToolTarget(){var rows=assistantRows();return rows.length?rowId(rows[rows.length-1]):'';}",
    "function ensureToolRowTarget(row){var target=explicitToolTarget(row);if(target!==''){setToolTarget(row,target);return target;}",
    "target=toolTarget(row);if(target===''){target=defaultToolTarget();setToolTarget(row,target);}return target;}",
    "function setToolBoxEmpty(box,empty){if(!box)return;if(empty){if(!hasClass(box,'tool-column-empty'))box.className+=' tool-column-empty';}",
    "else box.className=box.className.replace(/\\stool-column-empty/g,'');}",
    "function setToolBoxCount(box,count,lastSummary,lastError){var c=firstByClass(box,'tool-count');",
    "if(c)setNodeText(c,count>0?' - '+(lastSummary!==''?lastSummary:jsonText(count)):'');",
    "if(lastError){if(!hasClass(box,'tool-column-error'))box.className+=' tool-column-error';}",
    "else box.className=box.className.replace(/\\stool-column-error/g,'');}",
    "function ensureAssistantToolBox(row){var box=firstByClass(row,'tool-column');var bubble;if(box)return box;",
    "box=document.createElement('div');box.className='tool-column tool-column-empty';",
    "box.innerHTML='<div class=\"tool-rail-title\"><a class=\"tool-column-toggle\" href=\"#\" onclick=\"return toggleToolColumn(this)\"><span class=\"tool-column-disclosure\">&#9660;</span></a>Tool Calls<span class=\"tool-count\"></span></div><div class=\"tool-cards\"></div>';",
    "bubble=firstByClass(row,'bubble');if(bubble)row.insertBefore(box,bubble);else row.appendChild(box);return box;}",
    "function clearToolBoxes(){var rows=assistantRows();var i,box,cards;for(i=0;i<rows.length;i++){",
    "box=ensureAssistantToolBox(rows[i]);cards=firstByClass(box,'tool-cards');if(cards)cards.innerHTML='';setToolBoxCount(box,0,'',0);setToolBoxEmpty(box,1);}}",
    "function renderToolCardsForTarget(target,cards){var row=byId(target);var box,slot,h=[],i,last;if(!row||!isAssistantRow(row))return;",
    "box=ensureAssistantToolBox(row);slot=firstByClass(box,'tool-cards');if(!slot)return;",
    "for(i=0;i<cards.length;i++)h[h.length]=toolCardHTML(cards[i],i);slot.innerHTML=h.join('');",
    "last=cards.length?cards[cards.length-1]:null;setToolBoxCount(box,cards.length,last?toolCardSummary(last,cards.length-1):'',last?last.error:0);setToolBoxEmpty(box,cards.length===0);}",
    "function toolRowSort(row){var m=/^saved-(\\d+)$/.exec((row&&row.id)||'');return m?parseInt(m[1],10):900000000;}",
    "function insertToolSource(row){var p=toolSources();var v,n,i;if(!p||row.parentNode==p)return;",
    "v=toolRowSort(row);n=p.childNodes;for(i=0;i<n.length;i++){if(isToolRow(n[i])&&toolRowSort(n[i])>v){p.insertBefore(row,n[i]);return;}}p.appendChild(row);}",
    "function moveToolRows(root){var p=toolSources();var n,rows=[],i;if(!p)return;assignToolTargetsFromMessageOrder();root=root||document;",
    "n=root.getElementsByTagName('*');for(i=0;i<n.length;i++){if(isToolRow(n[i]))rows[rows.length]=n[i];}",
    "for(i=0;i<rows.length;i++)insertToolSource(rows[i]);}",
    "function toolSourceRows(){var p=toolSources();var out=[];var n,i;if(!p)return out;n=p.childNodes;",
    "for(i=0;i<n.length;i++){if(isToolRow(n[i]))out[out.length]=n[i];}return out;}",
    "function toolRowRaw(row){var b=firstByClass(row,'bubble');var raw;if(!b)return '';",
    "raw=(typeof b._strappyRawText!='undefined')?b._strappyRawText:nodeText(b);b._strappyRawText=raw;return raw;}",
    "function rebuildToolCards(){var rows=toolSourceRows();var grouped={};var pending={};var targets=[];var dbNames;var i,row,raw,events,c,target;",
    "clearToolBoxes();dbNames=collectDatabaseNames(rows);for(i=0;i<rows.length;i++){row=rows[i];raw=toolRowRaw(row);if(raw==='')continue;",
    "target=ensureToolRowTarget(row);if(target==='')continue;if(!grouped[target]){grouped[target]=[];pending[target]=[];targets[targets.length]=target;}",
    "events=parseToolEvents(raw);if(events){appendCardsFromEvents(grouped[target],events,dbNames);continue;}",
    "if(hasClass(row,'tool_call'))appendCardsFromToolCall(grouped[target],pending[target],raw,dbNames);",
    "else{c=pending[target].length?pending[target].shift():null;if(c){c.output=raw;c.error=toolOutputHasError(raw);}",
    "else{grouped[target][grouped[target].length]={name:'Tool Result',id:'',args:'',output:raw,outputOnly:true,error:toolOutputHasError(raw)};}}}",
    "for(i=0;i<targets.length;i++)renderToolCardsForTarget(targets[i],grouped[targets[i]]);scrollToolRailBottom();}",
    "function renderToolNode(row){toolRowRaw(row);rebuildToolCards();}",
    "function renderTools(root){moveToolRows(root);rebuildToolCards();}",
    "function renderMessageDecorations(root){renderMarkdown(root);renderMetadata(root);renderTools(root);decoratePromptGroups(root);}",
    "var strappyBatchDepth=0;var strappyNeedsRender=0;",
    "function beginMessageBatch(){strappyBatchDepth++;}",
    "function endMessageBatch(){if(strappyBatchDepth>0)strappyBatchDepth--;",
    "if(strappyBatchDepth===0&&strappyNeedsRender){strappyNeedsRender=0;",
    "renderMessageDecorations(document);scrollBottom();}}",
    "function renderAfterMutation(root){if(strappyBatchDepth>0){strappyNeedsRender=1;return;}",
    "renderMessageDecorations(root);}",
    "function clearEmpty(){var e=byId('empty');if(e)e.style.display='none';}",
    "function nodesFromHTML(html){var d=document.createElement('div');d.innerHTML=html;return d;}",
    "function scrollBottomNow(){var b=document.body||{};var e=document.documentElement||{};",
    "var h=Math.max(b.scrollHeight||0,e.scrollHeight||0,b.offsetHeight||0,e.offsetHeight||0);",
    "if(e)e.scrollTop=h;if(b)b.scrollTop=h;window.scrollTo(0,h);}",
    "function scrollBottom(){scrollBottomNow();setTimeout(scrollBottomNow,0);",
    "setTimeout(scrollBottomNow,80);if(window.requestAnimationFrame)",
    "window.requestAnimationFrame(scrollBottomNow);}",
    "function scrollToolRailBottom(){}",
    "function updateToolTargets(oldId,newId){var rows,i;if(!oldId||!newId||oldId==newId)return;",
    "rows=toolSourceRows().concat(messageRows());for(i=0;i<rows.length;i++){if(toolTarget(rows[i])==oldId)setToolTarget(rows[i],newId);}}",
    "var strappyTextQueues={};var strappyTextTimer=null;",
    "function queueTextAppend(id,t,kind){var k=kind+'|'+id;if(!strappyTextQueues[k])",
    "strappyTextQueues[k]={id:id,kind:kind,text:''};strappyTextQueues[k].text+=escHTML(t);",
    "if(!strappyTextTimer)strappyTextTimer=setTimeout(flushTextQueues,60);}",
    "function flushTextQueues(){var k,q,r,n;strappyTextTimer=null;",
    "for(k in strappyTextQueues){q=strappyTextQueues[k];r=byId(q.id);if(!r)continue;",
    "n=(q.kind=='reasoning')?firstByClass(r,'reasoning-body'):firstByClass(r,'bubble');",
    "if(!n)continue;if(q.kind=='reasoning'){if(typeof n._strappyPlain=='undefined')n._strappyPlain=n.innerHTML;",
    "n._strappyPlain+=q.text;if(n.insertAdjacentHTML)n.insertAdjacentHTML('beforeend',q.text);else n.innerHTML=n._strappyPlain;continue;}",
    "if(typeof n._strappyMarkdown=='undefined')n._strappyMarkdown=n.innerHTML;",
    "n._strappyMarkdown+=q.text;if(shouldRenderMarkdownBubble(n))renderMarkdownNode(n);",
    "else n.innerHTML=n._strappyMarkdown;}strappyTextQueues={};scrollBottom();}",
    "function appendMessage(html){clearEmpty();var m=byId('messages');if(!m)return;",
    "if(m.insertAdjacentHTML){m.insertAdjacentHTML('beforeend',html);}",
    "else{var d=nodesFromHTML(html);while(d.firstChild)m.appendChild(d.firstChild);}",
    "renderAfterMutation(m);scrollBottom();}",
    "function replaceMessage(id,html){clearEmpty();var old=byId(id);var oldId,target,wasAssistant,next,newId;",
    "flushTextQueues();",
    "if(!old){appendMessage(html);return;}var d=nodesFromHTML(html);",
    "oldId=rowId(old);target=toolTarget(old);wasAssistant=isAssistantRow(old);",
    "if(d.firstChild){next=d.firstChild;if(target!==''&&isToolRow(next))setToolTarget(next,target);",
    "old.parentNode.replaceChild(next,old);newId=rowId(next);if(wasAssistant)updateToolTargets(oldId,newId);}",
    "renderAfterMutation(document);scrollBottom();}",
    "function insertMessageBefore(id,html){clearEmpty();var before=byId(id);var m=byId('messages');",
    "if(!m){return;}if(!before){appendMessage(html);return;}var d=nodesFromHTML(html);",
    "while(d.firstChild)m.insertBefore(d.firstChild,before);renderAfterMutation(m);scrollBottom();}",
    "function prependMessages(html,hasMore){var m=byId('messages');if(!m)return;",
    "var d=nodesFromHTML(html);while(d.lastChild)m.insertBefore(d.lastChild,m.firstChild);",
    "var l=byId('load-more');if(l&&!hasMore)l.parentNode.removeChild(l);",
    "renderAfterMutation(m);}",
    "function setMessageState(id,status,state){var r=byId(id);var s;if(!r)return;",
    "r.className=r.className.replace(/\\sstate-[^\\s]+/g,'');if(state)r.className+=' state-'+state;",
    "s=firstByClass(r,'status');if(status){if(!s){s=document.createElement('div');",
    "s.className='meta status';r.appendChild(s);}s.innerHTML=status;}else if(s&&s.parentNode)s.parentNode.removeChild(s);scrollBottom();}",
    "function setMessageThinking(id,status){var r=byId(id);var b;if(!r)return;",
    "b=firstByClass(r,'bubble');if(b&&hasClass(b,'bubble-status'))b.innerHTML=status||'';}",
    "function appendMessageText(id,t){var r=byId(id);var b;if(!r)return;",
    "flushTextQueues();b=firstByClass(r,'bubble');if(b)b.style.display='block';",
    "if(b&&hasClass(b,'bubble-status')){b.className=b.className.replace(/\\sbubble-status/g,'');",
    "b._strappyMarkdown='';b.innerHTML='';}",
    "r.className=r.className.replace(/\\sstreaming-active/g,'');",
    "setMessageReasoningCollapsed(id,1);setMessageToolColumnCollapsed(id,1);if(!b)return;",
    "if(typeof b._strappyMarkdown=='undefined')b._strappyMarkdown=b.innerHTML;",
    "b._strappyMarkdown+=escHTML(t);if(shouldRenderMarkdownBubble(b))renderMarkdownNode(b);",
    "else b.innerHTML=b._strappyMarkdown;scrollBottom();}",
    "function moveMessageTextToReasoning(id){var r=byId(id);var b,box,body,raw;",
    "flushTextQueues();if(!r)return;b=firstByClass(r,'bubble');box=firstByClass(r,'reasoning');",
    "body=firstByClass(r,'reasoning-body');if(!b||!box||!body)return;",
    "raw=(typeof b._strappyMarkdown!='undefined')?b._strappyMarkdown:escHTML(nodeText(b));",
    "if(raw==='')return;if(typeof body._strappyPlain=='undefined')body._strappyPlain=body.innerHTML;",
    "if(body._strappyPlain!==''&&raw.charAt(0)!='\\n')body._strappyPlain+='\\n';",
    "body._strappyPlain+=raw;body.innerHTML=body._strappyPlain;",
    "b._strappyMarkdown='';b.className=b.className.replace(/\\sbubble-status/g,'')+' bubble-status';",
    "b.innerHTML=escHTML((r.getAttribute?r.getAttribute('data-thinking-label'):'')||'Thinking');",
    "b.style.display='block';",
    "box.style.display='block';",
    "if(!hasClass(r,'streaming-active'))r.className+=' streaming-active';",
    "setReasoningCollapsed(box,0);setMessageToolColumnCollapsed(id,1);scrollBottom();}",
    "function appendReasoningText(id,t){var r=byId(id);if(!r)return;",
    "var box=firstByClass(r,'reasoning');var body=firstByClass(r,'reasoning-body');",
    "if(box)box.style.display='block';if(!body)return;",
    "if(box)setReasoningCollapsed(box,0);",
    "queueTextAppend(id,t,'reasoning');}",
    "function appendToolEventText(id,t){var r=byId(id);var b;if(!r)return;",
    "r.style.display='block';b=firstByClass(r,'bubble');if(!b)return;",
    "if(typeof b._strappyRawText=='undefined')b._strappyRawText=nodeText(b);",
    "b._strappyRawText+=t;renderAfterMutation(document);scrollBottom();}",
    "function removeMessage(id){flushTextQueues();var r=byId(id);",
    "if(r&&r.parentNode)r.parentNode.removeChild(r);renderAfterMutation(document);}",
    "</script>",
    NULL
  };

  return strappy_webview_append_chunks(buffer, chunks);
}

void strappy_webview_free(char *value)
{
  free(value);
}

char *strappy_webview_status_html(const char *text,
                                  int retry,
                                  const strappy_webview_labels *labels)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_append_html_escaped(&buffer, text)) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }

  if (retry) {
    if (!strappy_webview_buffer_append_cstring(
          &buffer,
          " &middot; <a href=\"strappy-action://retry\">") ||
        !strappy_webview_append_html_escaped(&buffer,
                                             strappy_webview_retry_label(labels)) ||
        !strappy_webview_buffer_append_cstring(&buffer, "</a>")) {
      strappy_webview_buffer_destroy(&buffer);
      return NULL;
    }
  }

  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_message_html(const strappy_webview_message *message,
                                   const strappy_webview_labels *labels,
                                   const char *state,
                                   const char *status_html)
{
  strappy_webview_buffer buffer;
  const char *role;
  const char *text;
  const char *reasoning;
  const char *created_at;
  const char *metadata_json;
  const char *status_to_render;
  char *owned_status_html;
  char http_status_text[64];
  int has_state;
  int render_created_at;
  int ok;

  role = ((message != NULL) && (message->role != NULL) &&
          (message->role[0] != '\0')) ? message->role : "assistant";
  text = (message != NULL) ? strappy_webview_string_or_empty(message->text) : "";
  reasoning = (message != NULL) ?
    strappy_webview_string_or_empty(message->reasoning) : "";
  created_at = (message != NULL) ?
    strappy_webview_string_or_empty(message->created_at) : "";
  metadata_json = (message != NULL) ?
    strappy_webview_string_or_empty(message->metadata_json) : "";
  status_to_render = strappy_webview_string_or_empty(status_html);
  owned_status_html = NULL;
  has_state = (state != NULL) && (state[0] != '\0');
  render_created_at =
    (created_at[0] != '\0') &&
    !strappy_webview_is_user_role(role) &&
    !strappy_webview_is_harness_role(role);

  if (!has_state && (status_to_render[0] == '\0') &&
      (message != NULL) && (message->http_status >= 400L)) {
    snprintf(http_status_text, sizeof(http_status_text), "HTTP %ld",
             message->http_status);
    owned_status_html = strappy_webview_status_html(http_status_text, 0, labels);
    if (owned_status_html == NULL) {
      return NULL;
    }
    status_to_render = owned_status_html;
    state = "error";
    has_state = 1;
  }

  strappy_webview_buffer_init(&buffer);
  ok = strappy_webview_buffer_append_cstring(&buffer, "<div id=\"") &&
       strappy_webview_append_element_id(&buffer, message) &&
       strappy_webview_buffer_append_cstring(&buffer, "\" class=\"row ") &&
       strappy_webview_append_html_escaped(&buffer, role);

  if (ok && has_state) {
    ok = strappy_webview_buffer_append_cstring(&buffer, " state-") &&
         strappy_webview_append_html_escaped(&buffer, state);
  }

  ok = ok &&
       strappy_webview_buffer_append_cstring(&buffer, "\"") &&
       strappy_webview_append_data_attribute(&buffer,
                                             "kind",
                                             (message != NULL) ?
                                               message->kind : NULL) &&
       strappy_webview_append_data_attribute(&buffer,
                                             "actor",
                                             (message != NULL) ?
                                               message->actor : NULL) &&
       strappy_webview_append_data_attribute(&buffer,
                                             "prompt-group-key",
                                             (message != NULL) ?
                                               message->prompt_group_key : NULL) &&
       strappy_webview_append_data_attribute(&buffer,
                                             "message-key",
                                             (message != NULL) ?
                                               message->message_key : NULL) &&
       strappy_webview_append_data_attribute(&buffer,
                                             "target-message-key",
                                             (message != NULL) ?
                                               message->target_message_key : NULL) &&
       strappy_webview_buffer_append_cstring(&buffer, "><div class=\"role\">") &&
       strappy_webview_append_html_escaped(&buffer,
                                           strappy_webview_role_label(role, labels)) &&
       strappy_webview_buffer_append_cstring(&buffer, "</div>");

  if (ok && strappy_webview_is_assistant_role(role)) {
    ok = strappy_webview_append_reasoning_html(&buffer, reasoning, 0, labels);
    if (ok) {
      ok = strappy_webview_append_tool_column_html(&buffer, 1);
    }
  }

  ok = ok &&
       strappy_webview_buffer_append_cstring(&buffer, "<div class=\"bubble\">") &&
       strappy_webview_append_html_escaped(&buffer, text) &&
       strappy_webview_buffer_append_cstring(&buffer, "</div>") &&
       strappy_webview_append_metadata_html(&buffer, role, metadata_json, labels);

  if (ok && (status_to_render[0] != '\0')) {
    ok = strappy_webview_buffer_append_cstring(
           &buffer,
           "<div class=\"meta status\">") &&
         strappy_webview_buffer_append_cstring(&buffer, status_to_render) &&
         strappy_webview_buffer_append_cstring(&buffer, "</div>");
  } else if (ok && render_created_at) {
    ok = strappy_webview_buffer_append_cstring(&buffer, "<div class=\"meta\">") &&
         strappy_webview_append_html_escaped(&buffer, created_at) &&
         strappy_webview_buffer_append_cstring(&buffer, "</div>");
  }

  ok = ok && strappy_webview_buffer_append_cstring(&buffer, "</div>");

  free(owned_status_html);
  if (!ok) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_pending_message_html(
  const char *prompt,
  const char *element_id,
  const char *state,
  const char *status_html,
  const strappy_webview_labels *labels)
{
  strappy_webview_message message;

  memset(&message, 0, sizeof(message));
  message.element_id = element_id;
  message.role = "user";
  message.text = strappy_webview_string_or_empty(prompt);
  return strappy_webview_message_html(&message, labels, state, status_html);
}

char *strappy_webview_streaming_assistant_message_html(
  const char *element_id,
  const char *text,
  const char *reasoning,
  const char *state,
  const char *status_html,
  const char *actor,
  const char *prompt_group_key,
  const strappy_webview_labels *labels)
{
  strappy_webview_buffer buffer;
  int has_state;
  int status_is_pending;
  int render_bubble_status;
  int ok;

  if ((element_id == NULL) || (element_id[0] == '\0')) {
    element_id = "streaming-assistant";
  }
  has_state = (state != NULL) && (state[0] != '\0');
  status_is_pending = (state != NULL) && (strcmp(state, "pending") == 0);
  render_bubble_status =
    ((text == NULL) || (text[0] == '\0')) &&
    (status_html != NULL) &&
    (status_html[0] != '\0') &&
    status_is_pending;

  strappy_webview_buffer_init(&buffer);
  ok = strappy_webview_buffer_append_cstring(&buffer, "<div id=\"") &&
       strappy_webview_append_html_escaped(&buffer, element_id) &&
       strappy_webview_buffer_append_cstring(&buffer,
                                             "\" class=\"row assistant streaming-active");
  if (ok && has_state) {
    ok = strappy_webview_buffer_append_cstring(&buffer, " state-") &&
         strappy_webview_append_html_escaped(&buffer, state);
  }
  ok = ok &&
       strappy_webview_buffer_append_cstring(&buffer, "\"") &&
       strappy_webview_append_data_attribute(&buffer, "actor", actor) &&
       strappy_webview_append_data_attribute(&buffer,
                                             "prompt-group-key",
                                             prompt_group_key) &&
       strappy_webview_buffer_append_cstring(
         &buffer,
         " data-thinking-label=\"") &&
       strappy_webview_append_html_escaped(
         &buffer,
         strappy_webview_thinking_label(labels)) &&
       strappy_webview_buffer_append_cstring(&buffer, "\"><div class=\"role\">") &&
       strappy_webview_append_html_escaped(&buffer,
                                           strappy_webview_agent_label(labels)) &&
       strappy_webview_buffer_append_cstring(&buffer, "</div>") &&
       strappy_webview_append_reasoning_html(&buffer, reasoning, 1, labels) &&
       strappy_webview_append_tool_column_html(&buffer, 1) &&
       strappy_webview_buffer_append_cstring(&buffer, "<div class=\"bubble");
  if (ok && render_bubble_status) {
    ok = strappy_webview_buffer_append_cstring(&buffer, " bubble-status");
  }
  ok = ok &&
       strappy_webview_buffer_append_cstring(&buffer, "\">");
  if (ok && render_bubble_status) {
    ok = strappy_webview_buffer_append_cstring(&buffer, status_html);
  } else {
    ok = strappy_webview_append_html_escaped(&buffer, text);
  }
  ok = ok && strappy_webview_buffer_append_cstring(&buffer, "</div>");

  if (ok && !status_is_pending &&
      (status_html != NULL) && (status_html[0] != '\0')) {
    ok = strappy_webview_buffer_append_cstring(
           &buffer,
           "<div class=\"meta status\">") &&
         strappy_webview_buffer_append_cstring(&buffer, status_html) &&
         strappy_webview_buffer_append_cstring(&buffer, "</div>");
  }

  ok = ok && strappy_webview_buffer_append_cstring(&buffer, "</div>");
  if (!ok) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_tool_activity_message_html(
  const char *element_id,
  const char *text,
  const char *state,
  const char *status_html,
  const char *actor,
  const char *prompt_group_key,
  const char *target_element_id,
  const strappy_webview_labels *labels)
{
  strappy_webview_buffer buffer;
  int has_state;
  int has_text;
  int ok;

  if ((element_id == NULL) || (element_id[0] == '\0')) {
    element_id = "streaming-tools";
  }
  has_state = (state != NULL) && (state[0] != '\0');
  has_text = (text != NULL) && (text[0] != '\0');

  strappy_webview_buffer_init(&buffer);
  ok = strappy_webview_buffer_append_cstring(&buffer, "<div id=\"") &&
       strappy_webview_append_html_escaped(&buffer, element_id) &&
       strappy_webview_buffer_append_cstring(
         &buffer,
         "\" class=\"row tool_call tool_activity");
  if (ok && has_state) {
    ok = strappy_webview_buffer_append_cstring(&buffer, " state-") &&
         strappy_webview_append_html_escaped(&buffer, state);
  }
  ok = ok &&
       strappy_webview_buffer_append_cstring(&buffer, "\"") &&
       strappy_webview_append_data_attribute(&buffer, "actor", actor) &&
       strappy_webview_append_data_attribute(&buffer,
                                             "prompt-group-key",
                                             prompt_group_key) &&
       strappy_webview_append_data_attribute(&buffer,
                                             "tool-target",
                                             target_element_id);
  if (ok && !has_text) {
    ok = strappy_webview_buffer_append_cstring(&buffer, " style=\"display:none\"");
  }
  ok = ok &&
       strappy_webview_buffer_append_cstring(&buffer, "><div class=\"role\">") &&
       strappy_webview_append_html_escaped(
         &buffer,
         strappy_webview_tool_call_label(labels)) &&
       strappy_webview_buffer_append_cstring(&buffer, "</div><div class=\"bubble\">") &&
       strappy_webview_append_html_escaped(&buffer, text) &&
       strappy_webview_buffer_append_cstring(&buffer, "</div>");

  if (ok && (status_html != NULL) && (status_html[0] != '\0')) {
    ok = strappy_webview_buffer_append_cstring(
           &buffer,
           "<div class=\"meta status\">") &&
         strappy_webview_buffer_append_cstring(&buffer, status_html) &&
         strappy_webview_buffer_append_cstring(&buffer, "</div>");
  }

  ok = ok && strappy_webview_buffer_append_cstring(&buffer, "</div>");
  if (!ok) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_set_message_prompt_group_js(
  const char *element_id,
  const char *prompt_group_key,
  const char *actor)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(&buffer, "setMessagePromptGroup(") ||
      !strappy_webview_append_js_string(&buffer, element_id) ||
      !strappy_webview_buffer_append_cstring(&buffer, ",") ||
      !strappy_webview_append_js_string(&buffer, prompt_group_key) ||
      !strappy_webview_buffer_append_cstring(&buffer, ",") ||
      !strappy_webview_append_js_string(&buffer, actor) ||
      !strappy_webview_buffer_append_cstring(&buffer, ");")) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_message_html_with_reasoning(
  const strappy_webview_message *message,
  const char *reasoning,
  const strappy_webview_labels *labels)
{
  strappy_webview_message copy;

  if ((reasoning == NULL) || (reasoning[0] == '\0') ||
      (message == NULL) ||
      !strappy_webview_is_assistant_role(message->role)) {
    return strappy_webview_message_html(message, labels, NULL, NULL);
  }

  copy = *message;
  copy.reasoning = reasoning;
  return strappy_webview_message_html(&copy, labels, NULL, NULL);
}

char *strappy_webview_messages_page_html(const char *messages_html,
                                         const char *empty_text,
                                         int has_messages,
                                         int has_more,
                                         const char *load_more_label)
{
  strappy_webview_buffer buffer;
  int ok;

  strappy_webview_buffer_init(&buffer);
  ok = strappy_webview_buffer_append_cstring(
         &buffer,
         "<!doctype html><html><head><meta charset=\"utf-8\">") &&
       strappy_webview_append_styles(&buffer) &&
       strappy_webview_append_scripts(&buffer) &&
       strappy_webview_buffer_append_cstring(
         &buffer,
         "</head><body><div class=\"page\">");

  if (ok && has_more) {
    ok = strappy_webview_buffer_append_cstring(
           &buffer,
           "<a id=\"load-more\" class=\"load-more\" "
           "href=\"strappy-action://load-more\">") &&
         strappy_webview_append_html_escaped(&buffer, load_more_label) &&
         strappy_webview_buffer_append_cstring(&buffer, "</a>");
  }

  if (ok && !has_messages) {
    ok = strappy_webview_buffer_append_cstring(
           &buffer,
           "<div id=\"empty\" class=\"empty\">") &&
         strappy_webview_append_html_escaped(&buffer, empty_text) &&
         strappy_webview_buffer_append_cstring(&buffer, "</div>");
  } else if (ok) {
    ok = strappy_webview_buffer_append_cstring(
      &buffer,
      "<div id=\"empty\" class=\"empty\" style=\"display:none\"></div>");
  }

  ok = ok &&
       strappy_webview_buffer_append_cstring(
         &buffer,
         "<div id=\"strappy-layout\" class=\"layout\">"
         "<div class=\"chat-column\"><div id=\"messages\">") &&
       strappy_webview_buffer_append_cstring(&buffer, messages_html) &&
       strappy_webview_buffer_append_cstring(
         &buffer,
         "</div></div><div id=\"tool-sources\" class=\"tool-source-bin\"></div>"
         "</div></div><script>renderMessageDecorations(document);"
         "</script></body></html>");

  if (!ok) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_append_message_js(const char *message_html)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(&buffer, "appendMessage(") ||
      !strappy_webview_append_js_string(&buffer, message_html) ||
      !strappy_webview_buffer_append_cstring(&buffer, ");")) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_replace_message_js(const char *element_id,
                                         const char *message_html)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(&buffer, "replaceMessage(") ||
      !strappy_webview_append_js_string(&buffer, element_id) ||
      !strappy_webview_buffer_append_cstring(&buffer, ",") ||
      !strappy_webview_append_js_string(&buffer, message_html) ||
      !strappy_webview_buffer_append_cstring(&buffer, ");")) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_insert_message_before_js(const char *before_element_id,
                                               const char *message_html)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(&buffer, "insertMessageBefore(") ||
      !strappy_webview_append_js_string(&buffer, before_element_id) ||
      !strappy_webview_buffer_append_cstring(&buffer, ",") ||
      !strappy_webview_append_js_string(&buffer, message_html) ||
      !strappy_webview_buffer_append_cstring(&buffer, ");")) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_set_message_state_js(const char *element_id,
                                           const char *status_html,
                                           const char *state)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(&buffer, "setMessageState(") ||
      !strappy_webview_append_js_string(&buffer, element_id) ||
      !strappy_webview_buffer_append_cstring(&buffer, ",") ||
      !strappy_webview_append_js_string(&buffer, status_html) ||
      !strappy_webview_buffer_append_cstring(&buffer, ",") ||
      !strappy_webview_append_js_string(&buffer, state) ||
      !strappy_webview_buffer_append_cstring(&buffer, ");")) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_set_message_thinking_js(const char *element_id,
                                              const char *status_html)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(&buffer, "setMessageThinking(") ||
      !strappy_webview_append_js_string(&buffer, element_id) ||
      !strappy_webview_buffer_append_cstring(&buffer, ",") ||
      !strappy_webview_append_js_string(&buffer, status_html) ||
      !strappy_webview_buffer_append_cstring(&buffer, ");")) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_append_message_text_js(const char *element_id,
                                             const char *delta)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(&buffer, "appendMessageText(") ||
      !strappy_webview_append_js_string(&buffer, element_id) ||
      !strappy_webview_buffer_append_cstring(&buffer, ",") ||
      !strappy_webview_append_js_string(&buffer, delta) ||
      !strappy_webview_buffer_append_cstring(&buffer, ");")) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_append_reasoning_text_js(const char *element_id,
                                               const char *delta)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(&buffer, "appendReasoningText(") ||
      !strappy_webview_append_js_string(&buffer, element_id) ||
      !strappy_webview_buffer_append_cstring(&buffer, ",") ||
      !strappy_webview_append_js_string(&buffer, delta) ||
      !strappy_webview_buffer_append_cstring(&buffer, ");")) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_move_message_text_to_reasoning_js(const char *element_id)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(
        &buffer,
        "moveMessageTextToReasoning(") ||
      !strappy_webview_append_js_string(&buffer, element_id) ||
      !strappy_webview_buffer_append_cstring(&buffer, ");")) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_tool_event_text(const char *event_type,
                                      const char *tool_call_id,
                                      const char *tool_name,
                                      const char *arguments_json,
                                      const char *result_json)
{
  strappy_webview_buffer buffer;
  int ok;

  strappy_webview_buffer_init(&buffer);
  ok = strappy_webview_buffer_append_cstring(&buffer, "{\"event\":") &&
       strappy_webview_append_json_string(&buffer, event_type) &&
       strappy_webview_buffer_append_cstring(&buffer, ",\"tool_call_id\":") &&
       strappy_webview_append_json_string(&buffer, tool_call_id) &&
       strappy_webview_buffer_append_cstring(&buffer, ",\"tool_name\":") &&
       strappy_webview_append_json_string(&buffer, tool_name) &&
       strappy_webview_buffer_append_cstring(&buffer, ",\"arguments_json\":") &&
       strappy_webview_append_json_string(&buffer, arguments_json) &&
       strappy_webview_buffer_append_cstring(&buffer, ",\"result_json\":") &&
       strappy_webview_append_json_string(&buffer, result_json) &&
       strappy_webview_buffer_append_cstring(&buffer, "}\n");
  if (!ok) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }

  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_append_tool_event_text_js(const char *element_id,
                                                const char *event_text)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(&buffer, "appendToolEventText(") ||
      !strappy_webview_append_js_string(&buffer, element_id) ||
      !strappy_webview_buffer_append_cstring(&buffer, ",") ||
      !strappy_webview_append_js_string(&buffer, event_text) ||
      !strappy_webview_buffer_append_cstring(&buffer, ");")) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_remove_message_js(const char *element_id)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(&buffer, "removeMessage(") ||
      !strappy_webview_append_js_string(&buffer, element_id) ||
      !strappy_webview_buffer_append_cstring(&buffer, ");")) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_prepend_messages_js(const char *messages_html,
                                          int has_more)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(&buffer, "prependMessages(") ||
      !strappy_webview_append_js_string(&buffer, messages_html) ||
      !strappy_webview_buffer_append_cstring(&buffer, has_more ? ",true);" : ",false);")) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}
