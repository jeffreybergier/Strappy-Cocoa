#include "strappy_webview.h"

#include "strappy_cocoa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct strappy_webview_buffer {
  char *data;
  size_t length;
  size_t capacity;
} strappy_webview_buffer;

struct strappy_webview_script_batch {
  strappy_webview_buffer buffer;
  int has_js;
  int failed;
  int finished;
};

static char *g_strappy_webview_font_dir = NULL;

typedef enum strappy_webview_label_index {
  STRAPPY_WEBVIEW_LABEL_AGENT = 0,
  STRAPPY_WEBVIEW_LABEL_YOU,
  STRAPPY_WEBVIEW_LABEL_HARNESS,
  STRAPPY_WEBVIEW_LABEL_DEVELOPER,
  STRAPPY_WEBVIEW_LABEL_THINKING,
  STRAPPY_WEBVIEW_LABEL_REQUEST_METADATA,
  STRAPPY_WEBVIEW_LABEL_TOOL,
  STRAPPY_WEBVIEW_LABEL_TOOL_CALL,
  STRAPPY_WEBVIEW_LABEL_TOOL_RESULT,
  STRAPPY_WEBVIEW_LABEL_RETRY,
  STRAPPY_WEBVIEW_LABEL_API_CALL,
  STRAPPY_WEBVIEW_LABEL_API_ERROR,
  STRAPPY_WEBVIEW_LABEL_RESPONSE_ITEM,
  STRAPPY_WEBVIEW_LABEL_REQUEST,
  STRAPPY_WEBVIEW_LABEL_RESPONSE,
  STRAPPY_WEBVIEW_LABEL_ROUND,
  STRAPPY_WEBVIEW_LABEL_ATTEMPT,
  STRAPPY_WEBVIEW_LABEL_COUNT
} strappy_webview_label_index;

static const char * const g_strappy_webview_label_keys[
  STRAPPY_WEBVIEW_LABEL_COUNT] = {
  "Agent",
  "You",
  "Harness",
  "Developer",
  "Thinking",
  "Request Metadata",
  "Tool",
  "Tool Call",
  "Tool Result",
  "Retry",
  "API Call",
  "API Error",
  "Response Item",
  "Request",
  "Response",
  "Round",
  "Attempt"
};

static char *g_strappy_webview_localized_label_values[
  STRAPPY_WEBVIEW_LABEL_COUNT];
static strappy_webview_labels g_strappy_webview_localized_labels;

static void strappy_webview_assign_localized_labels(
  strappy_webview_labels *labels,
  char * const *values)
{
  if ((labels == NULL) || (values == NULL)) {
    return;
  }

  labels->agent = values[STRAPPY_WEBVIEW_LABEL_AGENT];
  labels->you = values[STRAPPY_WEBVIEW_LABEL_YOU];
  labels->harness = values[STRAPPY_WEBVIEW_LABEL_HARNESS];
  labels->developer = values[STRAPPY_WEBVIEW_LABEL_DEVELOPER];
  labels->thinking = values[STRAPPY_WEBVIEW_LABEL_THINKING];
  labels->request_metadata = values[STRAPPY_WEBVIEW_LABEL_REQUEST_METADATA];
  labels->tool = values[STRAPPY_WEBVIEW_LABEL_TOOL];
  labels->tool_call = values[STRAPPY_WEBVIEW_LABEL_TOOL_CALL];
  labels->tool_result = values[STRAPPY_WEBVIEW_LABEL_TOOL_RESULT];
  labels->retry = values[STRAPPY_WEBVIEW_LABEL_RETRY];
  labels->api_call = values[STRAPPY_WEBVIEW_LABEL_API_CALL];
  labels->api_error = values[STRAPPY_WEBVIEW_LABEL_API_ERROR];
  labels->response_item = values[STRAPPY_WEBVIEW_LABEL_RESPONSE_ITEM];
  labels->request = values[STRAPPY_WEBVIEW_LABEL_REQUEST];
  labels->response = values[STRAPPY_WEBVIEW_LABEL_RESPONSE];
  labels->round = values[STRAPPY_WEBVIEW_LABEL_ROUND];
  labels->attempt = values[STRAPPY_WEBVIEW_LABEL_ATTEMPT];
}

int strappy_webview_configure_localized_labels(char **error_out)
{
  char *localized_values[STRAPPY_WEBVIEW_LABEL_COUNT];
  size_t index;

  memset(localized_values, 0, sizeof(localized_values));
  for (index = 0U; index < STRAPPY_WEBVIEW_LABEL_COUNT; index++) {
    localized_values[index] = strappy_cocoa_copy_localized_string(
      g_strappy_webview_label_keys[index],
      error_out);
    if (localized_values[index] == NULL) {
      size_t cleanup_index;

      for (cleanup_index = 0U; cleanup_index < index; cleanup_index++) {
        free(localized_values[cleanup_index]);
      }
      return 0;
    }
  }

  for (index = 0U; index < STRAPPY_WEBVIEW_LABEL_COUNT; index++) {
    free(g_strappy_webview_localized_label_values[index]);
    g_strappy_webview_localized_label_values[index] = localized_values[index];
  }
  strappy_webview_assign_localized_labels(
    &g_strappy_webview_localized_labels,
    g_strappy_webview_localized_label_values);
  return 1;
}

const strappy_webview_labels *strappy_webview_localized_labels(void)
{
  return &g_strappy_webview_localized_labels;
}

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

static int strappy_webview_url_unreserved(unsigned char value)
{
  if ((value >= 'A') && (value <= 'Z')) {
    return 1;
  }
  if ((value >= 'a') && (value <= 'z')) {
    return 1;
  }
  if ((value >= '0') && (value <= '9')) {
    return 1;
  }
  return (value == '-') || (value == '_') || (value == '.') || (value == '~');
}

static int strappy_webview_append_url_path_escaped(
  strappy_webview_buffer *buffer,
  const char *path)
{
  static const char hex[] = "0123456789ABCDEF";
  const unsigned char *cursor;
  char escaped[3];

  if (path == NULL) {
    return 1;
  }

  escaped[0] = '%';
  cursor = (const unsigned char *)path;
  while (*cursor != '\0') {
    if (strappy_webview_url_unreserved(*cursor) || (*cursor == '/')) {
      if (!strappy_webview_buffer_append_char(buffer, (char)*cursor)) {
        return 0;
      }
    } else {
      escaped[1] = hex[*cursor >> 4];
      escaped[2] = hex[*cursor & 15U];
      if (!strappy_webview_buffer_append(buffer, escaped, sizeof(escaped))) {
        return 0;
      }
    }
    cursor++;
  }

  return 1;
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

static int strappy_webview_append_inline_json(strappy_webview_buffer *buffer,
                                              const char *json)
{
  const unsigned char *cursor;

  cursor = (const unsigned char *)((json != NULL) ? json : "{}");
  while (*cursor != '\0') {
    if (*cursor == '<') {
      if (!strappy_webview_buffer_append_cstring(buffer, "\\u003c")) {
        return 0;
      }
    } else if (*cursor == '>') {
      if (!strappy_webview_buffer_append_cstring(buffer, "\\u003e")) {
        return 0;
      }
    } else if (*cursor == '&') {
      if (!strappy_webview_buffer_append_cstring(buffer, "\\u0026")) {
        return 0;
      }
    } else if (!strappy_webview_buffer_append_char(buffer, (char)*cursor)) {
      return 0;
    }
    cursor++;
  }
  return 1;
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

static int strappy_webview_is_json_space(char value)
{
  return (value == ' ') || (value == '\t') || (value == '\r') ||
         (value == '\n');
}

static int strappy_webview_json_bool(const char *json,
                                     const char *key,
                                     int default_value)
{
  char pattern[96];
  const char *cursor;
  int written;

  if ((json == NULL) || (json[0] == '\0') ||
      (key == NULL) || (key[0] == '\0')) {
    return default_value;
  }

  written = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  if ((written <= 0) || ((size_t)written >= sizeof(pattern))) {
    return default_value;
  }

  cursor = strstr(json, pattern);
  if (cursor == NULL) {
    return default_value;
  }
  cursor += (size_t)written;
  while (strappy_webview_is_json_space(*cursor)) {
    cursor++;
  }
  if (*cursor != ':') {
    return default_value;
  }
  cursor++;
  while (strappy_webview_is_json_space(*cursor)) {
    cursor++;
  }

  if (strncmp(cursor, "true", 4U) == 0) {
    return 1;
  }
  if (strncmp(cursor, "false", 5U) == 0) {
    return 0;
  }
  if (*cursor == '1') {
    return 1;
  }
  if (*cursor == '0') {
    return 0;
  }

  return default_value;
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

static const char *strappy_webview_developer_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) &&
      (labels->developer != NULL) &&
      (labels->developer[0] != '\0')) {
    return labels->developer;
  }
  return "Developer";
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

static const char *strappy_webview_tool_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) &&
      (labels->tool != NULL) &&
      (labels->tool[0] != '\0')) {
    return labels->tool;
  }
  return "Tool";
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

static const char *strappy_webview_api_call_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) && (labels->api_call != NULL) &&
      (labels->api_call[0] != '\0')) {
    return labels->api_call;
  }
  return "API Call";
}

static const char *strappy_webview_api_error_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) && (labels->api_error != NULL) &&
      (labels->api_error[0] != '\0')) {
    return labels->api_error;
  }
  return "API Error";
}

static const char *strappy_webview_response_item_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) && (labels->response_item != NULL) &&
      (labels->response_item[0] != '\0')) {
    return labels->response_item;
  }
  return "Response Item";
}

static const char *strappy_webview_request_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) && (labels->request != NULL) &&
      (labels->request[0] != '\0')) {
    return labels->request;
  }
  return "Request";
}

static const char *strappy_webview_response_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) && (labels->response != NULL) &&
      (labels->response[0] != '\0')) {
    return labels->response;
  }
  return "Response";
}

static const char *strappy_webview_round_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) && (labels->round != NULL) &&
      (labels->round[0] != '\0')) {
    return labels->round;
  }
  return "Round";
}

static const char *strappy_webview_attempt_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) && (labels->attempt != NULL) &&
      (labels->attempt[0] != '\0')) {
    return labels->attempt;
  }
  return "Attempt";
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

static int strappy_webview_is_developer_role(const char *role)
{
  return (role != NULL) && (strcmp(role, "developer") == 0);
}

static int strappy_webview_is_api_call_role(const char *role)
{
  return (role != NULL) && (strcmp(role, "api_call") == 0);
}

static int strappy_webview_is_api_error_role(const char *role)
{
  return (role != NULL) && (strcmp(role, "api_error") == 0);
}

static int strappy_webview_is_api_reasoning_role(const char *role)
{
  return (role != NULL) && (strcmp(role, "api_reasoning") == 0);
}

static int strappy_webview_is_api_function_call_role(const char *role)
{
  return (role != NULL) && (strcmp(role, "api_function_call") == 0);
}

static int strappy_webview_is_api_function_output_role(const char *role)
{
  return (role != NULL) && (strcmp(role, "api_function_output") == 0);
}

static int strappy_webview_is_api_item_role(const char *role)
{
  return (role != NULL) && (strcmp(role, "api_item") == 0);
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
  if (strappy_webview_is_developer_role(role)) {
    return strappy_webview_developer_label(labels);
  }
  if (strappy_webview_is_api_call_role(role)) {
    return strappy_webview_api_call_label(labels);
  }
  if (strappy_webview_is_api_error_role(role)) {
    return strappy_webview_api_error_label(labels);
  }
  if (strappy_webview_is_api_reasoning_role(role)) {
    return strappy_webview_thinking_label(labels);
  }
  if (strappy_webview_is_api_function_call_role(role)) {
    return strappy_webview_tool_call_label(labels);
  }
  if (strappy_webview_is_api_function_output_role(role)) {
    return strappy_webview_tool_result_label(labels);
  }
  if (strappy_webview_is_api_item_role(role)) {
    return strappy_webview_response_item_label(labels);
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

static int strappy_webview_format_usage_cost(double value,
                                             char *buffer,
                                             size_t buffer_size)
{
  size_t index;
  size_t length;
  int written;

  if ((buffer == NULL) || (buffer_size == 0U)) {
    return 0;
  }

  written = snprintf(buffer, buffer_size, "%.10f", value);
  if ((written < 0) || ((size_t)written >= buffer_size)) {
    buffer[0] = '\0';
    return 0;
  }

  length = (size_t)written;
  for (index = 0U; index < length; index++) {
    if (buffer[index] == ',') {
      buffer[index] = '.';
    }
  }
  while ((length > 0U) && (buffer[length - 1U] == '0')) {
    length--;
  }
  if ((length > 0U) && (buffer[length - 1U] == '.')) {
    length--;
  }
  buffer[length] = '\0';
  if (strcmp(buffer, "-0") == 0) {
    buffer[0] = '0';
    buffer[1] = '\0';
  }
  return 1;
}

static const char *strappy_webview_disclosure_icon_html(int collapsed)
{
  return collapsed ?
    "<i class=\"fa fa-solid fa-angle-right\" aria-hidden=\"true\">"
    "&#xF105;</i>" :
    "<i class=\"fa fa-solid fa-angle-down\" aria-hidden=\"true\">"
    "&#xF107;</i>";
}

static int strappy_webview_append_reasoning_html(
  strappy_webview_buffer *buffer,
  const char *reasoning,
  int render_when_empty,
  int collapsed,
  const strappy_webview_labels *labels)
{
  int collapse_reasoning;

  if (((reasoning == NULL) || (reasoning[0] == '\0')) && !render_when_empty) {
    return 1;
  }

  collapse_reasoning = ((reasoning != NULL) &&
                        (reasoning[0] != '\0') &&
                        collapsed);

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
                                            "><div class=\"reasoning-label disclosure-title\" "
                                            "onclick=\"return toggleReasoning(this)\">"
                                            "<a class=\"reasoning-toggle\" href=\"#\" "
                                            "aria-expanded=\"") ||
      !strappy_webview_buffer_append_cstring(
        buffer,
        collapse_reasoning ? "false" : "true") ||
      !strappy_webview_buffer_append_cstring(
        buffer,
        "\"><span class=\"reasoning-disclosure\">") ||
      !strappy_webview_buffer_append_cstring(buffer,
        strappy_webview_disclosure_icon_html(collapse_reasoning)) ||
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
  if ((!strappy_webview_is_assistant_role(role) &&
       !strappy_webview_is_api_call_role(role) &&
       !strappy_webview_is_api_error_role(role)) ||
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
           "\"><div class=\"request-metadata-title disclosure-title\" "
           "onclick=\"return toggleMetadata(this)\">"
           "<a class=\"request-metadata-toggle\" href=\"#\" "
           "aria-expanded=\"false\">"
           "<span class=\"metadata-disclosure\">") &&
         strappy_webview_buffer_append_cstring(
           buffer,
           strappy_webview_disclosure_icon_html(1)) &&
         strappy_webview_buffer_append_cstring(buffer, "</span></a>") &&
         strappy_webview_append_html_escaped(
           buffer,
           strappy_webview_request_metadata_label(labels)) &&
         strappy_webview_buffer_append_cstring(
           buffer,
           "</div>"
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
        "\"><div class=\"tool-rail-title disclosure-title\" "
        "onclick=\"return toggleToolColumn(this)\">"
        "<a class=\"tool-column-toggle\" href=\"#\" aria-expanded=\"") ||
      !strappy_webview_buffer_append_cstring(
        buffer,
        collapsed ? "false" : "true") ||
      !strappy_webview_buffer_append_cstring(
        buffer,
        "\"><span class=\"tool-column-disclosure\">") ||
      !strappy_webview_buffer_append_cstring(buffer,
        strappy_webview_disclosure_icon_html(collapsed)) ||
      !strappy_webview_buffer_append_cstring(
        buffer,
        "</span></a>Tool Calls<span class=\"tool-count\"></span></div>"
        "<div class=\"tool-cards\"></div></div>")) {
    return 0;
  }
  return 1;
}

static int strappy_webview_append_font_faces(strappy_webview_buffer *buffer)
{
  static const struct {
    const char *family;
    const char *file_name;
  } faces[] = {
    { "FA7S", "FA7-Solid-900.otf" },
    { "FA7R", "FA7-Regular-400.otf" },
    { "FA7B", "FA7-Brands-400.otf" }
  };
  size_t index;

  if (g_strappy_webview_font_dir == NULL) {
    return 1;
  }

  for (index = 0U; index < (sizeof(faces) / sizeof(faces[0])); index++) {
    if (!strappy_webview_buffer_append_cstring(buffer,
          "@font-face{font-family:'") ||
        !strappy_webview_buffer_append_cstring(buffer, faces[index].family) ||
        !strappy_webview_buffer_append_cstring(buffer,
          "';src:url('file://") ||
        !strappy_webview_append_url_path_escaped(buffer,
                                                 g_strappy_webview_font_dir) ||
        !strappy_webview_buffer_append_cstring(buffer, "/") ||
        !strappy_webview_buffer_append_cstring(buffer, faces[index].file_name) ||
        !strappy_webview_buffer_append_cstring(buffer,
          "') format('opentype');}")) {
      return 0;
    }
  }

  return strappy_webview_buffer_append_cstring(
    buffer,
    ".fa{font-family:'FA7S';font-style:normal;font-weight:normal;"
    "display:inline-block;line-height:1;vertical-align:-.08em;}"
    ".fa-solid{font-family:'FA7S';}"
    ".fa-regular{font-family:'FA7R';}"
    ".fa-brands{font-family:'FA7B';}");
}

static int strappy_webview_append_styles(strappy_webview_buffer *buffer)
{
  static const char * const chunks[] = {
    "html,body{margin:0;padding:0;background:#fff;color:#30363b;",
    "font:12px/1.38 -apple-system,'Helvetica Neue',Helvetica,Arial,sans-serif;",
    "letter-spacing:0;-webkit-text-size-adjust:none;}",
    ".page{padding:0;}",
    ".timeline-error{margin:80px auto 0;max-width:520px;padding:0 20px;",
    "box-sizing:border-box;color:#606970;text-align:center;line-height:1.3;}",
    ".layout{width:100%;}",
    ".chat-column{width:100%;box-sizing:border-box;}",
    ".processing-status{position:fixed;left:50%;right:auto;top:auto;bottom:6px;",
    "z-index:20;box-sizing:border-box;max-width:90%;",
    "-webkit-transform:translateX(-50%);transform:translateX(-50%);",
    "border:1px solid #959fa7;border-radius:17px;",
    "background:#b2bbc2;color:#30363b;padding:0 36px 0 12px;",
    "box-shadow:0 2px 8px rgba(0,0,0,.16);font-size:12px;",
    "font-weight:bold;line-height:32px;height:34px;}",
    ".processing-status-text{display:block;white-space:nowrap;",
    "overflow:hidden;text-overflow:ellipsis;}",
    ".processing-autoscroll{position:absolute;right:3px;top:3px;bottom:3px;",
    "width:26px;box-sizing:border-box;display:block;border-radius:13px;",
    "border:1px solid #959fa7;background:#dfe4e8;color:#4e5961;",
    "font-size:12px;line-height:24px;-webkit-appearance:none;appearance:none;",
    "padding:0;text-align:center;cursor:pointer;}",
    ".processing-autoscroll>.fa{position:absolute;left:0;top:50%;width:100%;",
    "height:12px;margin-top:-6px;font-size:12px;line-height:12px;",
    "text-align:center;vertical-align:0;}",
    ".processing-autoscroll-on{background:#a3adb5;border-color:#68727a;",
    "box-shadow:inset 0 1px 3px rgba(0,0,0,.2);color:#30363b;}",
    ".processing-status-retry_wait{border-color:#959fa7;background:#c1c8ce;",
    "color:#30363b;}",
    ".processing-status-retrying{border-color:#68727a;background:#a3adb5;",
    "color:#30363b;}",
    ".bubble,.reasoning,.tool-column,.request-metadata{box-shadow:none;}",
    ".tool-column{max-width:none;box-sizing:border-box;",
    "margin:0 -12px 10px;border:0;border-top:1px solid #959fa7;",
    "border-bottom:1px solid #959fa7;background:#c1c8ce;",
    "color:#30363b;padding:8px 12px;line-height:1.3;",
    "white-space:pre-wrap;word-wrap:break-word;}",
    ".tool-column-empty{display:none;}",
    "#messages{max-width:none;margin:0;}",
    "#tool-sources,.tool-source-bin,.row.tool_call,.row.tool{display:none;}",
    ".tool-cards{margin:0;}",
    ".tool-column-collapsed .tool-cards{display:none;}",
    ".tool-rail-title{font-size:12px;font-weight:bold;",
    "line-height:1.3;color:#30363b;margin:0 0 8px;}",
    ".tool-column-collapsed .tool-rail-title{margin-bottom:0;",
    "line-height:1.3;color:#606970;}",
    ".tool-column-toggle{color:#4e5961;text-decoration:none;}",
    ".tool-column-disclosure,.prompt-group-disclosure,.tool-disclosure,",
    ".reasoning-disclosure,.metadata-disclosure,.api-reasoning-disclosure{",
    "display:inline-block;box-sizing:border-box;width:12px;margin-right:4px;",
    "font-size:12px;line-height:1;vertical-align:baseline;text-align:center;}",
    ".api-exchange-disclosure{display:inline-block;box-sizing:border-box;",
    "width:16px;margin-right:4px;font-size:14px;line-height:1;",
    "vertical-align:baseline;text-align:center;}",
    ".disclosure-title{cursor:pointer;-webkit-tap-highlight-color:rgba(0,0,0,.08);}",
    ".processing-status-active .disclosure-title{cursor:default;",
    "-webkit-tap-highlight-color:transparent;}",
    ".processing-status-active .tool-column-toggle,",
    ".processing-status-active .prompt-group-toggle,",
    ".processing-status-active .reasoning-toggle,",
    ".processing-status-active .request-metadata-toggle,",
    ".processing-status-active .api-exchange-toggle,",
    ".processing-status-active .api-reasoning-toggle{display:none;}",
    ".processing-status-active .tool-disclosure{display:none;}",
    ".processing-status-active .tool-card-toggle{pointer-events:none;",
    "cursor:default;-webkit-tap-highlight-color:transparent;}",
    ".tool-count{display:none;font-weight:bold;}",
    ".tool-column-collapsed .tool-count{display:inline;}",
    ".streaming-active .tool-column-toggle,.streaming-active .reasoning-toggle{display:none;}",
    ".streaming-active .bubble,.streaming-active .reasoning-body{white-space:pre-wrap;}",
    "@-webkit-keyframes strappy-round-fade{from{opacity:0;}to{opacity:1;}}",
    ".row-inserting{-webkit-animation:strappy-round-fade .3s ease-out both;}",
    ".row{margin:0;padding:10px 12px 12px;clear:both;",
    "box-sizing:border-box;border-top:1px solid #959fa7;}",
    ".row:first-child{border-top:0;}",
    ".row:last-child{margin-bottom:0;}",
    ".role{font-size:12px;font-weight:bold;color:#606970;margin:0 0 4px;}",
    ".prompt-group-toggle{color:#4e5961;text-decoration:none;}",
    ".prompt-group-hidden{display:none;}",
    ".prompt-group-harness{box-sizing:border-box;margin-left:8px;",
    "padding-left:10px;border-left:2px solid #959fa7;}",
    ".prompt-group-collapsed-anchor .bubble,",
    ".prompt-group-collapsed-anchor .reasoning,",
    ".prompt-group-collapsed-anchor .tool-column,",
    ".prompt-group-collapsed-anchor .request-metadata,",
    ".prompt-group-collapsed-anchor .meta{display:none;}",
    ".bubble{display:block;max-width:none;box-sizing:border-box;",
    "border:0;border-top:1px solid #959fa7;border-bottom:1px solid #959fa7;",
    "background:#fff;padding:8px 12px;margin:0 -12px 10px;",
    "line-height:1.38;",
    "white-space:normal;word-wrap:break-word;}",
    ".assistant .role{display:block;}",
    ".assistant .bubble{background:#fff;",
    "border-top-color:#959fa7;border-bottom-color:#959fa7;",
    "font-size:16px;line-height:1.45;}",
    ".bubble-status{color:#606970;font-style:italic;}",
    ".tool_call .role,.tool .role{color:#606970;}",
    ".tool_call .bubble,.tool .bubble{max-width:100%;box-sizing:border-box;",
    "white-space:pre-wrap;}",
    ".tool_call .bubble,.tool .bubble{background:#dfe4e8;",
    "border-color:#959fa7;color:#30363b;}",
    ".api_call,.api_error{background:#b2bbc2;}",
    ".api_call .role,.api_error .role{color:#30363b;}",
    ".api_call .bubble,.api_error .bubble{font-size:12px;line-height:1.35;",
    "white-space:pre-wrap;color:#30363b;background:#dfe4e8;",
    "border-top-color:#959fa7;border-bottom-color:#959fa7;}",
    ".api_reasoning .role,.api_function_call .role,",
    ".api_function_output .role,.api_item .role,",
    ".api_server_tool .role{color:#30363b;}",
    ".api_reasoning .bubble,.api_item .bubble{background:#dfe4e8;",
    "color:#30363b;border-top-color:#959fa7;",
    "border-bottom-color:#959fa7;}",
    ".api_function_call .bubble,.api_function_output .bubble,",
    ".api_server_tool .bubble{",
    "background:#dfe4e8;color:#30363b;border-top-color:#959fa7;",
    "border-bottom-color:#959fa7;",
    "white-space:normal;font-size:12px;padding:4px 12px;}",
    ".api-tool-card .tool-card-body{font-family:inherit;",
    "font-size:12px;white-space:normal;}",
    ".api-tool-group-secondary>.role{display:none;}",
    ".api-tool-fallback{font-family:Menlo,Monaco,'Courier New',monospace;",
    "white-space:pre-wrap;word-wrap:break-word;}",
    ".tool-panel{line-height:1.3;",
    "white-space:normal;color:#30363b;}",
    ".tool-heading{font-weight:bold;margin:0 0 8px;color:#30363b;}",
    ".tool-subtle{color:#606970;font-size:12px;}",
    ".tool-value{white-space:pre-wrap;word-wrap:break-word;}",
    ".tool-pill{display:inline-block;border:1px solid #959fa7;",
    "background:#dfe4e8;padding:4px 6px;margin:0 4px 4px 0;",
    "color:#606970;}",
    ".tool-section{margin:8px 0 0;}",
    ".tool-kv{border-collapse:collapse;width:100%;}",
    ".tool-kv th,.tool-kv td{border-top:1px solid #959fa7;",
    "padding:4px 6px;text-align:left;vertical-align:top;}",
    ".tool-kv th{width:150px;color:#606970;font-weight:bold;",
    "background:#c1c8ce;}",
    ".tool-table-wrap{overflow:auto;margin:8px 0 0;",
    "border:1px solid #959fa7;background:#dfe4e8;}",
    ".tool-table{border-collapse:collapse;min-width:100%;}",
    ".tool-table th,.tool-table td{border:1px solid #959fa7;",
    "padding:4px 6px;text-align:left;vertical-align:top;}",
    ".tool-table th{background:#c1c8ce;color:#30363b;font-weight:bold;}",
    ".tool-toggle{display:inline-block;margin-top:8px;color:#4e5961;",
    "text-decoration:none;}",
    ".tool-raw{display:none;margin:8px 0 0;padding:8px;",
    "border:1px solid #959fa7;background:#dfe4e8;overflow:auto;",
    "white-space:pre-wrap;color:#30363b;}",
    ".tool-open .tool-raw{display:block;}",
    ".tool-error-icon{display:inline-block;margin-right:4px;line-height:1;",
    "vertical-align:middle;}",
    ".tool-error-icon .fa{vertical-align:0;}",
    ".tool-card{border-top:1px solid #959fa7;margin:0;",
    "line-height:1.3;word-wrap:break-word;}",
    ".tool-card:first-child{border-top:0;}",
    ".tool-card-toggle{display:block;box-sizing:border-box;width:100%;",
    "padding:4px 0;background:#dfe4e8;color:#30363b;",
    "text-decoration:none;font-weight:bold;",
    "white-space:nowrap;overflow:hidden;text-overflow:ellipsis;}",
    ".tool-card-summary{vertical-align:baseline;}",
    ".tool-card-body{display:none;max-height:320px;overflow:auto;",
    "border:1px solid #959fa7;background:#dfe4e8;padding:6px;",
    "margin:4px 0 8px;}",
    ".tool-card-open .tool-card-body{display:block;}",
    ".tool-input-title{font-weight:bold;",
    "color:#606970;margin:8px 0 4px;}",
    ".tool-input-title:first-child{margin-top:0;}",
    ".tool-field{margin:0 0 8px;}",
    ".tool-field:last-child{margin-bottom:0;}",
    ".tool-output{margin-top:8px;border-top:1px solid #959fa7;",
    "padding-top:8px;}",
    ".tool-output-toggle{color:#4e5961;text-decoration:none;}",
    ".tool-output-body{display:none;margin-top:8px;max-height:360px;overflow:auto;}",
    ".tool-output-open .tool-output-body{display:block;}",
    ".bubble p,.reasoning-body p{margin:0 0 8px;}",
    ".bubble h1,.bubble h2,.bubble h3,.bubble h4,.bubble h5,.bubble h6,",
    ".reasoning-body h1,.reasoning-body h2,.reasoning-body h3,",
    ".reasoning-body h4,.reasoning-body h5,.reasoning-body h6{",
    "font-size:inherit;line-height:1.3;margin:0 0 8px;font-weight:bold;}",
    ".bubble ul,.bubble ol,.reasoning-body ul,.reasoning-body ol{",
    "margin:0 0 8px 20px;padding:0;}",
    ".bubble li,.reasoning-body li{margin:0 0 4px;}",
    ".bubble blockquote,.reasoning-body blockquote{",
    "margin:0 0 8px;padding:0 0 0 12px;",
    "border-left:3px solid #959fa7;color:#606970;}",
    ".bubble pre,.reasoning-body pre{margin:0 0 8px;padding:8px;",
    "border:1px solid #959fa7;",
    "background:#dfe4e8;overflow:auto;white-space:pre-wrap;}",
    ".bubble code,.reasoning-body code{font-family:Menlo,Monaco,'Courier New',monospace;",
    "font-size:12px;line-height:1.3;",
    "background:#dfe4e8;",
    "padding:1px 3px;}",
    ".bubble pre code,.reasoning-body pre code{background:transparent;padding:0;}",
    ".bubble .table-wrap,.reasoning-body .table-wrap{",
    "margin:0 0 8px;overflow:auto;}",
    ".bubble table,.reasoning-body table{border-collapse:collapse;",
    "min-width:100%;font-size:inherit;line-height:1.3;}",
    ".bubble th,.bubble td,.reasoning-body th,.reasoning-body td{",
    "border:1px solid #959fa7;padding:4px 6px;text-align:left;}",
    ".bubble th,.reasoning-body th{background:#c1c8ce;font-weight:bold;}",
    ".bubble a,.reasoning-body a{color:#4e5961;text-decoration:none;}",
    ".bubble p:last-child,.bubble ul:last-child,.bubble ol:last-child,",
    ".bubble pre:last-child,.bubble blockquote:last-child,",
    ".bubble .table-wrap:last-child,",
    ".reasoning-body p:last-child,.reasoning-body ul:last-child,",
    ".reasoning-body ol:last-child,.reasoning-body pre:last-child,",
    ".reasoning-body blockquote:last-child,",
    ".reasoning-body .table-wrap:last-child{margin-bottom:0;}",
    ".reasoning{max-width:none;box-sizing:border-box;",
    "border:0;border-top:1px solid #959fa7;border-bottom:1px solid #959fa7;",
    "background:#dfe4e8;color:#30363b;padding:8px 12px;",
    "margin:0 -12px 10px;line-height:1.3;",
    "white-space:pre-wrap;word-wrap:break-word;}",
    ".reasoning-label{font-size:12px;font-weight:bold;",
    "line-height:1.3;color:#30363b;margin:0 0 8px;}",
    ".reasoning-collapsed .reasoning-label{margin-bottom:0;}",
    ".reasoning-toggle{color:#4e5961;text-decoration:none;}",
    ".reasoning-collapsed .reasoning-body{display:none;}",
    ".reasoning-body{white-space:normal;}",
    ".api-reasoning-toggle{color:#4e5961;text-decoration:none;}",
    ".api-reasoning-collapsed>.bubble{display:none;}",
    ".request-metadata{max-width:none;box-sizing:border-box;",
    "border:0;border-top:1px solid #959fa7;border-bottom:1px solid #959fa7;",
    "background:#dfe4e8;color:#30363b;padding:8px 12px;",
    "margin:10px -12px 0;line-height:1.3;",
    "white-space:pre-wrap;word-wrap:break-word;}",
    ".request-metadata-error,.request-metadata-warning{",
    "border-top-color:#959fa7;border-bottom-color:#959fa7;",
    "background:#dfe4e8;color:#30363b;}",
    ".request-metadata-error .request-metadata-title,",
    ".request-metadata-warning .request-metadata-title{color:#30363b;}",
    ".request-metadata-title{font-family:inherit;font-size:12px;",
    "line-height:1.3;font-weight:bold;color:#30363b;margin:0 0 8px;",
    "white-space:nowrap;overflow:hidden;text-overflow:ellipsis;}",
    ".request-metadata-toggle{color:#4e5961;text-decoration:none;}",
    ".request-metadata-collapsed .request-metadata-body{display:none;}",
    ".request-metadata-body{white-space:pre-wrap;}",
    ".user .role,.user .meta{text-align:left;}",
    ".user .role{background:#c1c8ce;color:#30363b;margin:0 -12px;",
    "padding:8px 12px;border-top:1px solid #959fa7;}",
    ".user .bubble{background:#fff;border-top:0;",
    "border-bottom:0;padding-top:4px;font-size:16px;line-height:1.45;}",
    ".harness .role,.harness .meta,.developer .role,.developer .meta{",
    "text-align:left;color:#30363b;}",
    ".harness .role,.developer .role{background:#c1c8ce;color:#30363b;",
    "margin:0 -12px;",
    "padding:8px 12px;border-top:1px solid #959fa7;}",
    ".harness .bubble,.developer .bubble{background:#fff;border-top:0;",
    "border-bottom:0;padding-top:4px;font-size:16px;line-height:1.45;}",
    ".meta{font-size:12px;color:#606970;margin-top:4px;}",
    ".state-pending .bubble{opacity:.72;}",
    ".state-pending .status,.state-cancelled .status{color:#606970;}",
    ".state-error .bubble{border-top-color:#959fa7;",
    "border-bottom-color:#959fa7;background:#dfe4e8;}",
    ".state-error .status{color:#30363b;font-weight:bold;}",
    ".status a{color:#4e5961;text-decoration:none;}",
    ".api-exchange-row{position:relative;border-top:0;",
    "background:#b2bbc2;color:#30363b;",
    "padding:0 10px;}",
    ".api-exchange-row:before{content:'';position:absolute;left:0;right:0;",
    "top:0;height:1px;background:#68727a;pointer-events:none;",
    "display:none;z-index:1;}",
    ".api-exchange-start{margin-top:0;border-top:0;}",
    ".api-exchange-start:before{display:block;}",
    ".api-exchange-end{margin-bottom:0;border-bottom:0;}",
    ".api-exchange-turn-header{margin:0 -10px;padding:7px 10px 6px;",
    "border-bottom:1px solid #959fa7;background:#a3adb5;",
    "color:#30363b;font-size:12px;font-weight:bold;line-height:1.3;}",
    ".api-exchange-toggle{color:#4e5961;text-decoration:none;}",
    ".api-exchange-turn-title{vertical-align:baseline;}",
    ".api-exchange-item,.api-exchange-metadata{background:#b2bbc2;}",
    ".api-exchange-item .role{margin:0 -10px;padding:5px 10px;}",
    ".api-exchange-item .bubble{margin:0 -10px;}",
    ".api-exchange-item .reasoning,.api-exchange-item .tool-column{",
    "margin-bottom:0;}",
    ".api-exchange-item.user .bubble,.api-exchange-item.harness .bubble,",
    ".api-exchange-item.developer .bubble{padding:3px 10px 5px;}",
    ".api-exchange-item.api_function_call .bubble,",
    ".api-exchange-item.api_function_output .bubble{padding-left:10px;",
    "padding-right:10px;}",
    ".api-exchange-metadata>.role{margin:0 -10px;",
    "padding:5px 10px;}",
    ".api-exchange-metadata>.bubble{margin:0 -10px;}",
    ".api-exchange-metadata>.request-metadata{margin:0 -10px;",
    "padding:8px 10px;border-bottom:0;}",
    ".api-exchange-metadata .request-metadata-title{margin-bottom:5px;}",
    ".api-exchange-metadata .request-metadata-collapsed ",
    ".request-metadata-title{margin-bottom:0;}",
    ".api-exchange-section-label{margin:0 -10px;padding:4px 10px;",
    "font-size:12px;line-height:1.2;font-weight:bold;color:#30363b;",
    "background:#c1c8ce;}",
    ".api-exchange-row,.api-exchange-row *{color:#30363b;}",
    ".api-exchange-row>.role{background:#c1c8ce;}",
    ".api-exchange-row.api_reasoning>.role{background:#dfe4e8;}",
    ".api-exchange-row>.bubble,.api-exchange-row>.reasoning,",
    ".api-exchange-row>.request-metadata{background:#dfe4e8;}",
    ".api-exchange-row>.tool-column{background:#c1c8ce;}",
    ".api-exchange-row .tool-card-toggle{background:#dfe4e8;}",
    ".api-exchange-row .tool-card-body,",
    ".api-exchange-row .tool-table-wrap,.api-exchange-row .tool-pill,",
    ".api-exchange-row .tool-raw{background:#dfe4e8;",
    "border-color:#959fa7;}",
    ".api-exchange-row>.role,.api-exchange-row>.bubble,",
    ".api-exchange-row>.reasoning,.api-exchange-row>.tool-column,",
    ".api-exchange-row>.request-metadata{border-top:0;border-bottom:0;}",
    ".api-exchange-row .meta,.api-exchange-row .status,",
    ".api-exchange-row .tool-subtle{color:#606970;}",
    ".api-exchange-row .tool-column-toggle,",
    ".api-exchange-row .prompt-group-toggle,",
    ".api-exchange-row .reasoning-toggle,",
    ".api-exchange-row .request-metadata-toggle,",
    ".api-exchange-row .api-exchange-toggle,",
    ".api-exchange-row .api-reasoning-toggle,",
    ".api-exchange-row .tool-toggle,",
    ".api-exchange-row .tool-output-toggle,",
    ".api-exchange-row .status a{color:#4e5961;}",
    ".api-exchange-metadata.api_error>.bubble,",
    ".api-exchange-row .request-metadata-error,",
    ".api-exchange-row .request-metadata-warning{background:#dfe4e8;",
    "color:#30363b;border-color:#959fa7;}",
    ".api-exchange-metadata.api_error>.role,",
    ".api-exchange-metadata.api_error>.bubble *,",
    ".api-exchange-row .request-metadata-error *,",
    ".api-exchange-row .request-metadata-warning *,",
    ".api-exchange-row.state-error>.status,",
    ".api-exchange-row.state-error>.status *{color:#30363b;}",
    ".api-exchange-row.state-error>.status{font-weight:bold;}",
    ".api-exchange-row.user>.bubble,",
    ".api-exchange-row.harness>.bubble,",
    ".api-exchange-row.developer>.bubble,",
    ".api-exchange-row.assistant>.bubble{background:#fff;",
    "font-size:16px;line-height:1.45;}",
    ".api-exchange-row.user>.bubble,.api-exchange-row.user>.bubble *,",
    ".api-exchange-row.harness>.bubble,.api-exchange-row.harness>.bubble *,",
    ".api-exchange-row.developer>.bubble,.api-exchange-row.developer>.bubble *,",
    ".api-exchange-row.assistant>.bubble,",
    ".api-exchange-row.assistant>.bubble *{color:#222;}",
    ".api-exchange-row.user>.bubble a,",
    ".api-exchange-row.harness>.bubble a,",
    ".api-exchange-row.developer>.bubble a,",
    ".api-exchange-row.assistant>.bubble a{color:#2468a8;}",
    ".api-exchange-collapsed-anchor>.role,",
    ".api-exchange-collapsed-anchor>.bubble,",
    ".api-exchange-collapsed-anchor>.reasoning,",
    ".api-exchange-collapsed-anchor>.tool-column,",
    ".api-exchange-collapsed-anchor>.request-metadata,",
    ".api-exchange-collapsed-anchor>.meta,",
    ".api-exchange-collapsed-anchor>.api-exchange-section-label{display:none;}",
    ".api-exchange-collapsed-conversation>.api-exchange-section-label{display:none;}",
    ".api-exchange-collapsed-row{display:none;}",
    "</style>",
    NULL
  };

  return strappy_webview_buffer_append_cstring(buffer, "<style>") &&
         strappy_webview_append_font_faces(buffer) &&
         strappy_webview_append_chunks(buffer, chunks);
}

static int strappy_webview_append_scripts(strappy_webview_buffer *buffer)
{
  static const char * const chunks[] = {
    "<script>",
    "function byId(i){return document.getElementById(i);}",
	    "function hasClass(e,n){return e&&(' '+e.className+' ').indexOf(' '+n+' ')>=0;}",
	    "function setClass(e,n,on){if(!e)return;if(on){if(!hasClass(e,n))e.className+=' '+n;}",
	    "else e.className=e.className.replace(new RegExp('\\\\s'+n,'g'),'');}",
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
#include "strappy_fontawesome_icons.inc"
    "function faIconCode(s,n){var k=String(n||'').toLowerCase();var m;",
    "if(k.indexOf('fa-')==0)k=k.substring(3);if(k.indexOf('0x')==0)k=k.substring(2);",
    "if(/^[0-9a-f]{3,6}$/i.test(k))return k.toUpperCase();",
    "s=String(s||'solid').toLowerCase();if(s!='solid'&&s!='regular'&&s!='brands')s='solid';",
    "m=faIconMap[s]||faIconMap.solid||{};return m[k]||'';}",
    "function faIconHTML(style,name,raw){var s=String(style||'solid').toLowerCase();var c,k,cls;",
    "if(s!='solid'&&s!='regular'&&s!='brands')s='solid';",
    "k=String(name||'').toLowerCase();if(k.indexOf('fa-')==0)k=k.substring(3);",
    "cls=/^[a-z0-9-]+$/.test(k)?' fa-'+k:'';c=faIconCode(s,name);",
    "if(c==='')return escHTML(raw);",
    "return '<i class=\"fa fa-'+s+cls+'\" aria-hidden=\"true\">&#x'+c+';</i>';}",
    "function disclosureIconHTML(collapsed){return faIconHTML('solid',",
    "collapsed?'angle-right':'angle-down',collapsed?'>':'v');}",
    "function toolErrorIconHTML(){return '<span class=\"tool-error-icon\" role=\"img\" ",
    "aria-label=\"Tool error\">'+faIconHTML('solid','triangle-exclamation','!')+'</span>';}",
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
    "s=s.replace(/\\[fa(?::(solid|regular|brands))?:([A-Za-z0-9_-]+|0x[0-9A-Fa-f]{3,6}|[0-9A-Fa-f]{3,6})\\]/g,",
    "function(m,st,n){return mdStash(a,faIconHTML(st,n,m));});",
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
    "n._strappyMarkdown=n.innerHTML;if(n._strappyMarkdownRendered==n._strappyMarkdown)return;",
    "n.innerHTML=mdToHTML(n._strappyMarkdown);n._strappyMarkdownRendered=n._strappyMarkdown;}",
    "var strappyStreamingMarkdownDirty=[];var strappyStreamingMarkdownNeedsFlush=0;",
    "function scheduleStreamingMarkdown(n){if(!n||n._strappyStreamingMarkdownDirty)return;",
    "n._strappyStreamingMarkdownDirty=1;strappyStreamingMarkdownDirty[strappyStreamingMarkdownDirty.length]=n;",
    "strappyStreamingMarkdownNeedsFlush=1;if(strappyBatchDepth===0)scheduleWebViewUpdate(strappyUpdateInterval);}",
    "function flushStreamingMarkdown(){var list=strappyStreamingMarkdownDirty;var i,n;",
    "strappyStreamingMarkdownDirty=[];strappyStreamingMarkdownNeedsFlush=0;",
    "for(i=0;i<list.length;i++){n=list[i];if(!n)continue;n._strappyStreamingMarkdownDirty=0;",
    "if(n.parentNode)renderMarkdownNode(n);}}",
    "function appendStreamingMarkdownNode(n,t){if(!n)return;if(typeof n._strappyMarkdown=='undefined')",
    "n._strappyMarkdown=n.innerHTML;n._strappyMarkdown+=t;n.innerHTML+=t;scheduleStreamingMarkdown(n);}",
    "function shouldRenderMarkdownBubble(n){return hasClass(n,'bubble')&&",
    "!hasClass(n,'bubble-status')&&ancestorHasClass(n,'assistant')&&",
    "!ancestorHasClass(n,'tool-column');}",
    "function shouldRenderMarkdownReasoning(n){return hasClass(n,'reasoning-body')&&",
    "ancestorHasClass(n,'assistant');}",
    "function renderMarkdown(root){root=root||document;var n=root.getElementsByTagName('*');",
    "for(var i=0;i<n.length;i++){if(shouldRenderMarkdownBubble(n[i])||",
    "shouldRenderMarkdownReasoning(n[i]))",
    "renderMarkdownNode(n[i]);}}",
    "function isObj(v){return v&&typeof v=='object'&&",
    "Object.prototype.toString.call(v)!='[object Array]';}",
    "function isArr(v){return Object.prototype.toString.call(v)=='[object Array]';}",
    "function jsonText(v){if(v===null||typeof v=='undefined')return '';",
    "if(typeof v=='string')return v;if(typeof v=='number'||typeof v=='boolean')",
    "return String(v);if(typeof JSON!='undefined'&&JSON.stringify)return JSON.stringify(v);",
    "return String(v);}",
    "var strappyProcessingStatus=null;var strappyProcessingStatusDirty=0;",
    "var strappyProcessingPromptGroupKey='';",
    "var strappyProcessingNextTick=0;",
    "function processingStatusObject(raw){var s=raw;if(!s)return null;",
    "if(typeof s=='string'){try{s=JSON.parse(s);}catch(e){return null;}}",
    "if(s&&s.processing_status)s=s.processing_status;return isObj(s)?s:null;}",
    "function processingPromptGroup(s){return jsonText(s&&(s.prompt_group_key||s.message_key));}",
    "function processingNodePromptGroup(n){var v;for(;n;n=n.parentNode){",
    "if(!n.getAttribute)continue;v=n.getAttribute('data-prompt-group-key')||'';",
    "if(v!=='')return v;}return '';}",
    "function setProcessingThinkingCollapsed(group,collapsed){var n,e,i;",
    "if(group==='')return;n=document.getElementsByTagName('*');for(i=0;i<n.length;i++){e=n[i];",
    "if(processingNodePromptGroup(e)!=group)continue;",
    "if(hasClass(e,'reasoning'))setReasoningCollapsed(e,collapsed);",
    "else if(hasClass(e,'api_reasoning'))setAPIReasoningCollapsed(e,collapsed);}}",
    "function processingInteractionsLocked(){var b=document.body;return !!(",
    "(strappyProcessingStatus&&strappyProcessingStatus.active)||",
    "hasClass(b,'processing-status-active'));}",
    "function syncProcessingInteractionState(active,group){var b=document.body;",
    "if(b)setClass(b,'processing-status-active',active?1:0);",
    "setProcessingThinkingCollapsed(group,active?0:1);}",
    "function processingNumber(v){var n=parseInt(v,10);return isNaN(n)?0:n;}",
    "function processingDuration(seconds){var s=processingNumber(seconds);var m,h;",
    "if(s<0)s=0;if(s<60)return s+'s';m=Math.floor(s/60);s=s%60;",
    "if(m<60)return m+'m '+(s<10?'0':'')+s+'s';h=Math.floor(m/60);",
    "m=m%60;return h+'h '+(m<10?'0':'')+m+'m';}",
    "function processingAttemptText(s){var a=processingNumber(s.retry_attempt);",
    "var max=processingNumber(s.retry_max_attempts);if(a>0&&max>0)return 'attempt '+a+' of '+max;",
    "if(a>0)return 'attempt '+a;return '';}",
    "function processingStatusText(s){var now=(new Date()).getTime();var started=processingNumber(s.started_ms);",
    "var elapsed=started>0?Math.floor((now-started)/1000):0;var kind=s.status_kind||'thinking';",
    "var attempt=processingAttemptText(s);var reason=s.retry_reason||'';var remaining;",
    "if(kind=='retry_wait'){remaining=processingNumber(s.retry_until_ms)>0?",
    "Math.ceil((processingNumber(s.retry_until_ms)-now)/1000):processingNumber(s.retry_after_seconds);",
    "if(remaining<0)remaining=0;return 'Retrying in '+processingDuration(remaining)",
    "+(reason?' - '+reason:'')+(attempt?' - '+attempt:'');}",
    "if(kind=='retrying')return 'Retrying'+(attempt?' - '+attempt:'')+' - '+processingDuration(elapsed)+' elapsed';",
    "if(kind=='tools')return 'Using tools - '+processingDuration(elapsed);",
    "return 'Thinking - '+processingDuration(elapsed);}",
    "var strappyAutoScrollEnabled=1;",
    "function updateAutoScrollButton(n){var b;if(!n)return;b=firstByClass(n,'processing-autoscroll');",
    "if(!b)return;b.className='processing-autoscroll'+(strappyAutoScrollEnabled?' processing-autoscroll-on':'');",
    "b.setAttribute('aria-pressed',strappyAutoScrollEnabled?'true':'false');",
    "b.title=strappyAutoScrollEnabled?'Disable autoscroll':'Enable autoscroll';",
    "b.setAttribute('aria-label',b.title);}",
    "function setAutoScrollEnabled(v){var n;strappyAutoScrollEnabled=v?1:0;",
    "n=byId('processing-status');if(n)updateAutoScrollButton(n);",
    "if(strappyAutoScrollEnabled)scrollBottomNow();}",
    "function toggleAutoScroll(){setAutoScrollEnabled(!strappyAutoScrollEnabled);return false;}",
    "function processingStatusTextNode(n){var t;if(!n)return null;t=firstByClass(n,'processing-status-text');",
    "if(!t){t=document.createElement('span');t.className='processing-status-text';",
    "if(n.firstChild)n.insertBefore(t,n.firstChild);else n.appendChild(t);}return t;}",
    "function processingAutoScrollButton(n){var b;if(!n)return null;b=firstByClass(n,'processing-autoscroll');",
    "if(!b){b=document.createElement('button');b.type='button';b.className='processing-autoscroll';",
    "b.onclick=function(){return toggleAutoScroll();};",
    "b.innerHTML=faIconHTML('solid','arrows-up-down','v');n.appendChild(b);}updateAutoScrollButton(n);return b;}",
    "function processingStatusNode(){var n=byId('processing-status');if(n)return n;",
    "n=document.createElement('div');n.id='processing-status';n.className='processing-status';",
    "n.setAttribute('role','status');n.setAttribute('aria-live','polite');",
    "processingStatusTextNode(n);processingAutoScrollButton(n);",
    "(document.body||document.documentElement).appendChild(n);return n;}",
    "function updateProcessingStatus(){var s=strappyProcessingStatus;var n,t;",
    "if(!s||!s.active){clearProcessingStatusNode();return;}n=processingStatusNode();",
    "n.className='processing-status processing-status-'+(s.status_kind||'thinking');",
    "t=processingStatusTextNode(n);setNodeText(t,processingStatusText(s));processingAutoScrollButton(n);",
    "syncProcessingInteractionState(1,strappyProcessingPromptGroupKey);}",
    "function setProcessingStatus(raw){var s=processingStatusObject(raw);var group;",
    "if(!s||!s.active){clearProcessingStatus();return;}strappyProcessingStatus=s;",
    "group=processingPromptGroup(s);if(group==='')group=strappyProcessingPromptGroupKey;",
    "if(strappyProcessingPromptGroupKey!==''&&group!==strappyProcessingPromptGroupKey){",
    "setProcessingThinkingCollapsed(strappyProcessingPromptGroupKey,1);",
    "settleAPIExchangesForPrompt(strappyProcessingPromptGroupKey);}",
    "setAPIExchangesCollapsedForPrompt(group,0);",
    "strappyProcessingPromptGroupKey=group;syncProcessingInteractionState(1,group);",
    "decorateAPIExchanges(document);decorateAPIToolGroups(document);",
    "strappyProcessingStatusDirty=1;strappyProcessingNextTick=0;",
    "if(strappyAutoScrollEnabled)strappyUpdateShouldScroll=1;scheduleWebViewUpdate(0);}",
    "function clearProcessingStatus(){var group=strappyProcessingPromptGroupKey;",
    "settleAPIExchangesForPrompt(group);",
    "strappyProcessingStatus=null;syncProcessingInteractionState(0,group);strappyProcessingStatusDirty=1;",
    "decorateAPIExchanges(document);decorateAPIToolGroups(document);",
    "if(strappyAutoScrollEnabled)strappyUpdateShouldScroll=1;",
    "strappyProcessingNextTick=0;scheduleWebViewUpdate(0);}",
    "function clearProcessingStatusNode(){var n=byId('processing-status');",
    "if(n&&n.parentNode)n.parentNode.removeChild(n);",
    "syncProcessingInteractionState(0,strappyProcessingPromptGroupKey);",
    "strappyProcessingPromptGroupKey='';}",
    "function initProcessingStatusFromRenderState(){var n=document.getElementsByTagName('*');var i,raw,s;",
    "for(i=n.length-1;i>=0;i--){raw=n[i].getAttribute?n[i].getAttribute('data-render-state'):'';",
    "s=processingStatusObject(raw);if(s&&s.active){setProcessingStatus(s);return;}}}",
    "function addMetaLine(lines,label,value){var t=jsonText(value);",
    "if(t!=='')lines[lines.length]=label+': '+t;}",
    "function addNestedMetaLine(lines,label,root,parent,key){",
    "if(isObj(root)&&isObj(root[parent]))addMetaLine(lines,label,root[parent][key]);}",
    "function metadataLower(v){return jsonText(v).toLowerCase();}",
    "function metadataHasAny(t,a){for(var i=0;i<a.length;i++){if(t.indexOf(a[i])>=0)return 1;}return 0;}",
    "function metadataValueIsError(v){var t=metadataLower(v);if(t==='')return 0;",
    "if(t=='error'||t=='content_filter'||t=='content-filter'||t=='content filter'||",
    "t=='sensitive'||t=='safety'||t=='safety_filter'||t=='safety-filter'||",
    "t=='safety filter'||t=='moderation'||t=='moderated'||t=='blocked'||",
    "t=='filtered'||t=='refusal'||t=='refused')return 1;",
    "return metadataHasAny(t,['content_filter','content-filter','content filter',",
    "'sensitive','safety','moderation','policy','blocked','prohibited','forbidden',",
    "'filtered','refusal','refused']);}",
    "function metadataValueIsWarning(v){var t=metadataLower(v);if(t==='')return 0;",
    "return metadataHasAny(t,['length','max_tokens','max-tokens','max tokens',",
    "'token_limit','token-limit','token limit','context_length','context-length',",
    "'context length']);}",
    "function metadataFinishStatus(root){var gen=metadataGen(root);var rs=metadataLower(isObj(root)?root.status:'');var s=metadataLower(isObj(root)?root.finish_status:'');",
    "if(isObj(root)&&root.error)return 'error';if(rs=='failed'||rs=='cancelled')return 'error';if(rs=='incomplete')return 'warning';",
    "if(s=='error'||s=='warning'||s=='ok')return s;",
    "if(isObj(root)&&(metadataValueIsError(root.finish_reason)||metadataValueIsError(root.native_finish_reason)))return 'error';",
    "if(isObj(gen)&&(metadataValueIsError(gen.finish_reason)||metadataValueIsError(gen.native_finish_reason)))return 'error';",
    "if(isObj(root)&&(metadataValueIsWarning(root.finish_reason)||metadataValueIsWarning(root.native_finish_reason)))return 'warning';",
    "if(isObj(gen)&&(metadataValueIsWarning(gen.finish_reason)||metadataValueIsWarning(gen.native_finish_reason)))return 'warning';",
    "return 'ok';}",
    "function formatMetadata(root){var lines=[];var usage;var gen;",
    "if(!isObj(root))return jsonText(root);",
    "addMetaLine(lines,'Response ID',firstMetadataValue(root.id,root.response_id));",
    "addMetaLine(lines,'Model',root.model);",
    "addMetaLine(lines,'Created',firstMetadataValue(root.created_at,root.created));",
    "addMetaLine(lines,'Response status',root.status);",
    "if(isObj(root.incomplete_details))addMetaLine(lines,'Incomplete reason',root.incomplete_details.reason);",
    "if(isObj(root.error)){addMetaLine(lines,'Error code',root.error.code);addMetaLine(lines,'Error',root.error.message);}",
    "addMetaLine(lines,'Finish reason',root.finish_reason);",
    "addMetaLine(lines,'Native finish reason',root.native_finish_reason);",
    "addMetaLine(lines,'Finish status',metadataFinishStatus(root));",
    "addMetaLine(lines,'Service tier',root.service_tier);",
    "addMetaLine(lines,'System fingerprint',root.system_fingerprint);",
    "addMetaLine(lines,'HTTP status',root.http_status);",
    "usage=root.usage;if(isObj(usage)){",
    "addMetaLine(lines,'Cost',usage.cost);",
    "addMetaLine(lines,'Input tokens',usage.input_tokens);",
    "addMetaLine(lines,'Output tokens',usage.output_tokens);",
    "addMetaLine(lines,'Prompt tokens',usage.prompt_tokens);",
    "addMetaLine(lines,'Completion tokens',usage.completion_tokens);",
    "addMetaLine(lines,'Total tokens',usage.total_tokens);",
    "addNestedMetaLine(lines,'Cached prompt tokens',usage,'prompt_tokens_details','cached_tokens');",
    "addNestedMetaLine(lines,'Cached input tokens',usage,'input_tokens_details','cached_tokens');",
    "addNestedMetaLine(lines,'Cache write tokens',usage,'prompt_tokens_details','cache_write_tokens');",
    "addNestedMetaLine(lines,'Prompt audio tokens',usage,'prompt_tokens_details','audio_tokens');",
    "addNestedMetaLine(lines,'Reasoning tokens',usage,'completion_tokens_details','reasoning_tokens');",
    "addNestedMetaLine(lines,'Output reasoning tokens',usage,'output_tokens_details','reasoning_tokens');",
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
    "addMetaLine(lines,'Prompt tokens',gen.tokens_prompt);",
    "addMetaLine(lines,'Completion tokens',gen.tokens_completion);",
    "addMetaLine(lines,'Reasoning tokens',gen.tokens_reasoning);",
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
    "function firstMetadataValue(){for(var i=0;i<arguments.length;i++){",
    "if(jsonText(arguments[i])!=='')return arguments[i];}return '';}",
    "function parseMetadata(raw){if(typeof JSON!='undefined'&&JSON.parse)",
    "return JSON.parse(raw);return eval('('+raw+')');}",
    "function renderMetadata(root){root=root||document;var n=root.getElementsByTagName('*');",
    "for(var i=0;i<n.length;i++){if(!hasClass(n[i],'request-metadata'))continue;",
    "var raw=n[i].getAttribute('data-metadata');var body=firstByClass(n[i],'request-metadata-body');",
    "var parsed;",
    "if(!raw||!body)continue;try{parsed=parseMetadata(raw);setClass(n[i],'request-metadata-error',metadataFinishStatus(parsed)=='error');",
    "setClass(n[i],'request-metadata-warning',metadataFinishStatus(parsed)=='warning');setNodeText(body,formatMetadata(parsed));}",
    "catch(e){setNodeText(body,raw);}}}",
    "function toggleMetadata(a){if(processingInteractionsLocked())return false;",
    "var p=a;while(p&&!hasClass(p,'request-metadata'))p=p.parentNode;",
    "if(!p)return false;var d=firstByClass(p,'metadata-disclosure');var t=firstByClass(p,'request-metadata-toggle');",
    "if(hasClass(p,'request-metadata-collapsed')){",
    "p.className=p.className.replace(/\\srequest-metadata-collapsed/g,'');if(d)d.innerHTML=disclosureIconHTML(0);",
    "if(t)t.setAttribute('aria-expanded','true');}",
    "else{p.className+=' request-metadata-collapsed';if(d)d.innerHTML=disclosureIconHTML(1);",
    "if(t)t.setAttribute('aria-expanded','false');}return false;}",
    "var strappyAPIExchangeCollapsed={};",
    "function apiExchangeId(row){return row&&row.getAttribute?row.getAttribute('data-api-call-id')||'':'';}",
    "function apiExchangeDirection(row){return row&&row.getAttribute?row.getAttribute('data-direction')||'':'';}",
    "function apiExchangeKind(row){return row&&row.getAttribute?row.getAttribute('data-kind')||'':'';}",
    "function apiExchangeCumulativeUsageCost(rows){var i,value;for(i=0;i<rows.length;i++){",
    "value=rows[i].getAttribute?rows[i].getAttribute('data-cumulative-usage-cost')||'':'';if(value!=='')return value;}return '';}",
    "function formatCumulativeUsageCost(value){return '$'+(value!==''?value:'-');}",
    "function rowIsAPIExchangeMetadata(row){return apiExchangeKind(row)=='response_api_call'||hasClass(row,'api_call')||hasClass(row,'api_error');}",
    "function rowIsAPIExchangeItem(row){var d=apiExchangeDirection(row);return d=='request'||d=='response';}",
    "function rowIsAPIExchangeAnswer(row){return hasClass(row,'assistant')&&apiExchangeDirection(row)=='response';}",
    "function rowIsAPIExchangeConversation(row){return hasClass(row,'user')||hasClass(row,'harness')||",
    "hasClass(row,'developer')||hasClass(row,'assistant');}",
    "function rowIsAPIExchangeError(row){return hasClass(row,'api_error')||hasClass(row,'state-error');}",
    "function apiExchangeRowsHaveError(rows){var i;for(i=0;i<rows.length;i++){if(rowIsAPIExchangeError(rows[i]))return 1;}return 0;}",
    "function apiExchangeCollapsed(id,promptKey,hasError){if(promptKey!==''&&promptKey===strappyProcessingPromptGroupKey&&processingInteractionsLocked())return 0;",
    "return typeof strappyAPIExchangeCollapsed[id]=='undefined'?(hasError?0:1):(strappyAPIExchangeCollapsed[id]?1:0);}",
    "function setAPIExchangesCollapsedForPrompt(group,collapsed){var rows=messageRows();var seen={};var i,id;",
    "if(group==='')return;for(i=0;i<rows.length;i++){if(promptGroupKey(rows[i])!==group)continue;",
    "id=apiExchangeId(rows[i]);if(id===''||seen['$'+id])continue;seen['$'+id]=1;",
    "strappyAPIExchangeCollapsed[id]=collapsed?1:0;}}",
    "function settleAPIExchangesForPrompt(group){var rows=messageRows();var exchanges={};var keys=[];var i,id,key;",
    "if(group==='')return;for(i=0;i<rows.length;i++){if(promptGroupKey(rows[i])!==group)continue;id=apiExchangeId(rows[i]);",
    "if(id==='')continue;key='$'+id;if(!exchanges[key]){exchanges[key]={id:id,hasError:0};keys[keys.length]=key;}",
    "if(rowIsAPIExchangeError(rows[i]))exchanges[key].hasError=1;}",
    "for(i=0;i<keys.length;i++){key=keys[i];strappyAPIExchangeCollapsed[exchanges[key].id]=exchanges[key].hasError?0:1;}}",
    "function removeAPIExchangeDecoration(row){var names=['api-exchange-row','api-exchange-start','api-exchange-end',",
    "'api-exchange-turn-anchor','api-exchange-metadata','api-exchange-item','api-exchange-request','api-exchange-response',",
    "'api-exchange-answer','api-exchange-section-start','api-exchange-collapsed-anchor','api-exchange-collapsed-row',",
    "'api-exchange-collapsed-conversation'];",
    "var i,n;for(i=0;i<names.length;i++)setRowClass(row,names[i],0);",
    "n=firstByClass(row,'api-exchange-section-label');if(n&&n.parentNode)n.parentNode.removeChild(n);}",
    "function removeAPIExchangeTurnHeader(row){var n=firstByClass(row,'api-exchange-turn-header');",
    "if(n&&n.parentNode)n.parentNode.removeChild(n);}",
    "function ensureAPIExchangeTurnHeader(row,id,collapsed,cumulativeUsageCost){var h,a,d,title,titleText,roundNumber,attemptNumber,roundLabel,attemptLabel;",
    "if(!row)return;roundNumber=row.getAttribute('data-round-number')||'1';attemptNumber=row.getAttribute('data-attempt-number')||'1';",
    "roundLabel=row.getAttribute('data-round-label')||'Round';attemptLabel=row.getAttribute('data-attempt-label')||'Attempt';",
    "h=document.createElement('div');h.className='api-exchange-turn-header disclosure-title';a=document.createElement('a');",
    "a.className='api-exchange-toggle';a.href='#';a.setAttribute('data-api-call-id',id);",
    "a.setAttribute('aria-expanded',collapsed?'false':'true');",
    "d=document.createElement('span');d.className='api-exchange-disclosure';d.innerHTML=disclosureIconHTML(collapsed);",
    "a.appendChild(d);h.appendChild(a);title=document.createElement('span');title.className='api-exchange-turn-title';",
    "titleText=roundLabel+' '+roundNumber;if(parseInt(attemptNumber,10)>1)",
    "titleText+=' \\u00b7 '+attemptLabel+' '+attemptNumber;titleText+=' \\u00b7 '+formatCumulativeUsageCost(cumulativeUsageCost);",
    "setNodeText(title,titleText);h.appendChild(title);",
    "h.onclick=function(){return toggleAPIExchange(a);};",
    "row.insertBefore(h,row.firstChild);}",
    "function ensureAPIExchangeSectionLabel(row){var label=row.getAttribute?row.getAttribute('data-direction-label')||'':'';var n;",
    "if(label==='')return;n=document.createElement('div');n.className='api-exchange-section-label';setNodeText(n,label);",
    "row.insertBefore(n,row.firstChild);}",
    "function decorateAPIExchanges(root){var rows=messageRows();var groups={};var keys=[];var i,j,row,id,key,g,anchor,collapsed,last,d,previous;",
    "var answer,conversation,hasError;",
    "for(i=0;i<rows.length;i++){removeAPIExchangeDecoration(rows[i]);removeAPIExchangeTurnHeader(rows[i]);",
    "id=apiExchangeId(rows[i]);if(id===''||(!rowIsAPIExchangeMetadata(rows[i])&&!rowIsAPIExchangeItem(rows[i])))continue;",
    "key='$'+id;if(!groups[key]){groups[key]={id:id,promptKey:promptGroupKey(rows[i]),rows:[]};keys[keys.length]=key;}",
    "groups[key].rows[groups[key].rows.length]=rows[i];}",
    "for(i=0;i<keys.length;i++){g=groups[keys[i]];anchor=g.rows[0];hasError=apiExchangeRowsHaveError(g.rows);",
    "collapsed=apiExchangeCollapsed(g.id,g.promptKey,hasError);previous='';last=g.rows[g.rows.length-1];",
    "if(collapsed){last=anchor;for(j=0;j<g.rows.length;j++){if(rowIsAPIExchangeConversation(g.rows[j]))last=g.rows[j];}}",
    "for(j=0;j<g.rows.length;j++){row=g.rows[j];d=apiExchangeDirection(row);answer=rowIsAPIExchangeAnswer(row);",
    "conversation=rowIsAPIExchangeConversation(row);",
    "setRowClass(row,'api-exchange-row',1);",
    "setRowClass(row,'api-exchange-start',j===0);setRowClass(row,'api-exchange-end',row===last);",
    "setRowClass(row,'api-exchange-turn-anchor',row===anchor);setRowClass(row,'api-exchange-metadata',rowIsAPIExchangeMetadata(row));",
    "setRowClass(row,'api-exchange-item',rowIsAPIExchangeItem(row));",
    "setRowClass(row,'api-exchange-request',d=='request');setRowClass(row,'api-exchange-response',d=='response');",
    "setRowClass(row,'api-exchange-answer',answer);",
    "setRowClass(row,'api-exchange-collapsed-conversation',collapsed&&conversation);",
    "setRowClass(row,'api-exchange-collapsed-anchor',collapsed&&row===anchor&&!conversation);",
    "setRowClass(row,'api-exchange-collapsed-row',collapsed&&row!==anchor&&!conversation);",
    "if(d!==''&&d!==previous){setRowClass(row,'api-exchange-section-start',1);ensureAPIExchangeSectionLabel(row);previous=d;}}",
    "ensureAPIExchangeTurnHeader(anchor,g.id,collapsed,apiExchangeCumulativeUsageCost(g.rows));}}",
    "function toggleAPIExchange(a){if(processingInteractionsLocked())return false;",
    "var id=a&&a.getAttribute?a.getAttribute('data-api-call-id'):'';var current;if(id==='')return false;",
    "current=a.getAttribute('aria-expanded')=='false'?1:0;strappyAPIExchangeCollapsed[id]=current?0:1;",
    "decorateAPIExchanges(document);decorateAPIToolGroups(document);decoratePromptGroups(document);return false;}",
    "var strappyPromptGroupCollapsed={};",
    "function promptGroupKey(row){return row&&row.getAttribute?row.getAttribute('data-prompt-group-key')||'':'';}",
    "function rowActor(row){return row&&row.getAttribute?row.getAttribute('data-actor')||'':'';}",
    "function rowIsHarness(row){return rowActor(row)=='harness'||hasClass(row,'harness');}",
    "function rowIsActiveHarness(row){return rowIsHarness(row)&&(hasClass(row,'streaming-active')||",
    "hasClass(row,'state-pending')||hasClass(row,'state-error')||hasClass(row,'state-cancelled'));}",
    "function setRowClass(row,name,on){if(!row)return;if(on){if(!hasClass(row,name))row.className+=' '+name;}",
    "else row.className=row.className.replace(new RegExp('\\\\s'+name,'g'),'');}",
    "function removePromptGroupToggle(row){var role=firstByClass(row,'role');var a;if(!role)return;",
    "a=firstByClass(role,'prompt-group-toggle');if(!a)return;role.onclick=null;",
    "setClass(role,'disclosure-title',0);if(a.parentNode)a.parentNode.removeChild(a);}",
    "function promptGroupAnchor(rows){var i;for(i=0;i<rows.length;i++){if(rowIsHarness(rows[i]))return rows[i];}",
    "return null;}",
    "function promptGroupHarnessCount(rows){var i,n=0;for(i=0;i<rows.length;i++)if(rowIsHarness(rows[i]))n++;return n;}",
    "function promptGroupHasActiveHarness(rows){var i;for(i=0;i<rows.length;i++){if(rowIsActiveHarness(rows[i]))return 1;}return 0;}",
    "function promptGroupDefaultCollapsed(rows){return promptGroupHarnessCount(rows)>1&&!promptGroupHasActiveHarness(rows);}",
    "function promptGroupCollapsed(key,rows){return typeof strappyPromptGroupCollapsed[key]=='undefined'?",
    "(promptGroupDefaultCollapsed(rows)?1:0):(strappyPromptGroupCollapsed[key]?1:0);}",
    "function ensurePromptGroupToggle(row,key,collapsed){var role,a,d;if(!row)return;",
    "role=firstByClass(row,'role');if(!role)return;a=firstByClass(role,'prompt-group-toggle');",
    "if(!a){a=document.createElement('a');a.className='prompt-group-toggle';a.href='#';",
    "d=document.createElement('span');",
    "d.className='prompt-group-disclosure';a.appendChild(d);role.insertBefore(a,role.firstChild);}",
    "a.setAttribute('data-prompt-group-key',key);a.setAttribute('aria-expanded',collapsed?'false':'true');",
    "setClass(role,'disclosure-title',1);role.onclick=function(){return togglePromptGroup(a);};",
    "d=firstByClass(a,'prompt-group-disclosure');",
    "if(d)d.innerHTML=disclosureIconHTML(collapsed);}",
    "function decoratePromptGroups(root){var rows=messageRows();var groups={};var keys=[];var i,k,row;",
    "for(i=0;i<rows.length;i++){row=rows[i];k=promptGroupKey(row);",
    "removePromptGroupToggle(row);",
    "setRowClass(row,'prompt-group-hidden',0);setRowClass(row,'prompt-group-collapsed-anchor',0);setRowClass(row,'prompt-group-harness',",
    "k!==''&&rowIsHarness(row));if(k==='')continue;",
    "if(!groups[k]){groups[k]=[];keys[keys.length]=k;}groups[k][groups[k].length]=row;}",
    "for(i=0;i<keys.length;i++){k=keys[i];var g=groups[k];var collapsed=promptGroupCollapsed(k,g);var anchor=promptGroupAnchor(g);",
    "if(anchor&&promptGroupHarnessCount(g)>1)ensurePromptGroupToggle(anchor,k,collapsed);",
    "setRowClass(anchor,'prompt-group-collapsed-anchor',collapsed&&rowIsHarness(anchor));",
    "for(var j=0;j<g.length;j++)setRowClass(g[j],'prompt-group-hidden',collapsed&&rowIsHarness(g[j])&&g[j]!==anchor);}}",
    "function togglePromptGroup(a){if(processingInteractionsLocked())return false;",
    "var key=a&&a.getAttribute?a.getAttribute('data-prompt-group-key'):'';",
    "var rows=messageRows();var group=[];for(var i=0;i<rows.length;i++){if(promptGroupKey(rows[i])==key)group[group.length]=rows[i];}",
    "if(key==='')return false;strappyPromptGroupCollapsed[key]=promptGroupCollapsed(key,group)?0:1;",
    "decoratePromptGroups(document);return false;}",
    "function setMessagePromptGroup(id,key,actor){var r=byId(id);if(!r)return;",
    "if(key&&r.setAttribute)r.setAttribute('data-prompt-group-key',key);",
    "if(actor&&r.setAttribute)r.setAttribute('data-actor',actor);renderMessageDecorations(document);}",
    "function toggleReasoning(a){if(processingInteractionsLocked())return false;",
    "var p=a;while(p&&!hasClass(p,'reasoning'))p=p.parentNode;",
    "if(!p)return false;setReasoningCollapsed(p,hasClass(p,'reasoning-collapsed')?0:1);return false;}",
    "function setReasoningCollapsed(box,collapsed){var d=firstByClass(box,'reasoning-disclosure');",
    "var a=firstByClass(box,'reasoning-toggle');",
    "if(collapsed){if(!hasClass(box,'reasoning-collapsed'))box.className+=' reasoning-collapsed';",
    "if(d)d.innerHTML=disclosureIconHTML(1);if(a)a.setAttribute('aria-expanded','false');}",
    "else{box.className=box.className.replace(/\\sreasoning-collapsed/g,'');",
    "if(d)d.innerHTML=disclosureIconHTML(0);if(a)a.setAttribute('aria-expanded','true');}}",
    "function setMessageReasoningCollapsed(id,collapsed){var r=byId(id);var box,body;",
    "if(!r)return;box=firstByClass(r,'reasoning');body=firstByClass(r,'reasoning-body');",
    "if(!box||!body||nodeText(body)==='')return;setReasoningCollapsed(box,collapsed);}",
    "function setAPIReasoningCollapsed(row,collapsed){var d=firstByClass(row,'api-reasoning-disclosure');",
    "var a=firstByClass(row,'api-reasoning-toggle');if(collapsed){setRowClass(row,'api-reasoning-collapsed',1);",
    "if(d)d.innerHTML=disclosureIconHTML(1);if(a)a.setAttribute('aria-expanded','false');}",
    "else{setRowClass(row,'api-reasoning-collapsed',0);if(d)d.innerHTML=disclosureIconHTML(0);",
    "if(a)a.setAttribute('aria-expanded','true');}}",
    "function toggleAPIReasoning(a){if(processingInteractionsLocked())return false;",
    "var row=a;while(row&&!hasClass(row,'api_reasoning'))row=row.parentNode;",
    "if(!row)return false;setAPIReasoningCollapsed(row,hasClass(row,'api-reasoning-collapsed')?0:1);return false;}",
    "function setToolColumnCollapsed(box,collapsed){var d=firstByClass(box,'tool-column-disclosure');",
    "var a=firstByClass(box,'tool-column-toggle');",
    "if(collapsed){if(!hasClass(box,'tool-column-collapsed'))box.className+=' tool-column-collapsed';",
    "if(d)d.innerHTML=disclosureIconHTML(1);if(a)a.setAttribute('aria-expanded','false');}",
    "else{box.className=box.className.replace(/\\stool-column-collapsed/g,'');",
    "if(d)d.innerHTML=disclosureIconHTML(0);if(a)a.setAttribute('aria-expanded','true');}}",
    "function toggleToolColumn(a){if(processingInteractionsLocked())return false;",
    "var p=a;while(p&&!hasClass(p,'tool-column'))p=p.parentNode;",
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
    "function databaseListHTML(o,raw){var direct=isArr(o);var dbs=direct?o:(isArr(o.databases)?o.databases:[]);var rest;var h;",
    "h=['<div><span class=\"tool-pill\">'+dbs.length+' databases</span>'];",
    "if(!direct&&o.availability_state)h[h.length]='<span class=\"tool-pill\">'+escHTML(jsonText(o.availability_state))+'</span>';",
    "h[h.length]='</div>';if(dbs.length)h[h.length]=toolObjectArrayTable(dbs);",
    "else h[h.length]='<span class=\"tool-subtle\">No databases</span>';",
    "rest=direct?{}:toolObjectWithout(o,{databases:1});if(toolKeys(rest).length)h[h.length]=toolObjectTable(rest);",
    "return toolPanel('Database List','',h.join(''),raw,'');}",
    "function queryHTML(o,raw){var cols=isArr(o.columns)?o.columns:[];var rows=isArr(o.rows)?o.rows:[];",
    "var rest,col,label;var h=['<div>'];if(o.database_id)h[h.length]='<span class=\"tool-pill\">'+escHTML(jsonText(o.database_id))+'</span>';",
    "h[h.length]='<span class=\"tool-pill\">'+rows.length+' rows shown</span>';",
    "if(o.rows_truncated||o.truncated)h[h.length]='<span class=\"tool-pill\">truncated</span>';",
    "if(o.ok===false)h[h.length]='<span class=\"tool-pill\">error</span>';h[h.length]='</div>';",
    "h[h.length]='<div class=\"tool-table-wrap\"><table class=\"tool-table\"><thead><tr>';",
    "for(var c=0;c<cols.length;c++){col=cols[c];label=typeof col=='string'?col:(isObj(col)&&col.name?col.name:'c'+c);h[h.length]='<th>'+escHTML(jsonText(label))+'</th>';}",
    "h[h.length]='</tr></thead><tbody>';for(var r=0;r<rows.length;r++){var row=rows[r]||[];",
    "h[h.length]='<tr>';for(c=0;c<cols.length;c++)h[h.length]='<td>'+toolJSONHTML(row[c])+'</td>';h[h.length]='</tr>';}",
    "h[h.length]='</tbody></table></div>';rest=toolObjectWithout(o,{columns:1,rows:1,rows_truncated:1,database_id:1,ok:1,truncated:1});",
    "if(toolKeys(rest).length)h[h.length]=toolObjectTable(rest);",
    "return toolPanel('Database Query','',h.join(''),raw,o.ok===false?'tool-error':'');}",
    "function toolResultPanel(o,raw,name){if((name=='database_list_info'&&isArr(o))||(isObj(o)&&isArr(o.databases)))return databaseListHTML(o,raw);",
    "if(isObj(o)&&isArr(o.columns)&&isArr(o.rows))return queryHTML(o,raw);",
    "if(isObj(o)&&o.error)return toolPanel('Tool Error',jsonText(o.tool_name||name||''),",
    "'<div class=\"tool-value\">'+escHTML(jsonText(o.error))+'</div>'+toolObjectTable(o),raw,'tool-error');",
    "return toolPanel('Tool Result',jsonText(name||''),toolJSONHTML(o),raw,'');}",
    "function toggleToolCard(a){if(processingInteractionsLocked())return false;",
    "var p=a;while(p&&!hasClass(p,'tool-card'))p=p.parentNode;",
    "if(!p)return false;var d=firstByClass(a,'tool-disclosure');",
    "if(hasClass(p,'tool-card-open')){p.className=p.className.replace(/\\stool-card-open/g,'');",
    "if(d)d.innerHTML=disclosureIconHTML(1);if(a.setAttribute)a.setAttribute('aria-expanded','false');}",
    "else{p.className+=' tool-card-open';if(d)d.innerHTML=disclosureIconHTML(0);",
    "if(a.setAttribute)a.setAttribute('aria-expanded','true');}return false;}",
    "function toolDisplaySpec(name){var registry=(typeof strappyToolDisplayRegistry!='undefined')?strappyToolDisplayRegistry:null;",
    "var spec=isObj(registry)?registry[jsonText(name)]:null;return isObj(spec)?spec:null;}",
    "function toolPathValue(root,path){var value=root;var i,key;if(!isArr(path)||path.length===0)return null;",
    "for(i=0;i<path.length;i++){key=jsonText(path[i]);if((!isObj(value)&&!isArr(value))||!toolOwn(value,key))return null;value=value[key];}return value;}",
    "function toolPromotedValue(name,args,dbNames,hintNames,item){var spec=toolDisplaySpec(name);var parsed,key,value,transform,raw,parts,i;",
    "if(!spec)return '';if(isArr(spec.promoted_path))value=toolPathValue(item,spec.promoted_path);",
    "else{parsed=toolArgsValue(args);if(!isObj(parsed))return '';key=jsonText(spec.promoted_argument);",
    "if(key===''||!toolOwn(parsed,key))return '';value=parsed[key];}if(value===null||typeof value=='undefined')return '';",
    "transform=jsonText(spec.transform||'text');if(transform=='comma_separated'){if(!isArr(value))return '';",
    "parts=[];for(i=0;i<value.length;i++)parts[parts.length]=jsonText(value[i]);return parts.join(', ');}raw=jsonText(value);",
    "if(raw==='')return '';if(transform=='database_filename')return dbNames&&dbNames[raw]?jsonText(dbNames[raw]):raw;",
    "if(transform=='database_hint_filename')return hintNames&&hintNames[raw]?jsonText(hintNames[raw]):'#'+raw;",
    "if(transform=='identifier')return '#'+raw;return raw;}",
    "function toolDisplayTitle(name,args,dbNames,hintNames,item){var spec=toolDisplaySpec(name);",
    "var title=jsonText((spec&&spec.label)||name||'Tool');var promoted=toolPromotedValue(name,args,dbNames,hintNames,item);",
    "return title+(promoted!==''?' \\u00b7 '+promoted:'');}",
    "function decorateToolArgs(name,args,dbNames,hintNames){var parsed=toolArgsValue(args);var spec=toolDisplaySpec(name);",
    "var key,raw,transform,filename,out,k;if(!isObj(parsed)||!spec)return args;key=jsonText(spec.promoted_argument);",
    "if(key===''||!toolOwn(parsed,key))return args;raw=jsonText(parsed[key]);transform=jsonText(spec.transform||'text');",
    "if(transform=='database_filename'&&dbNames&&dbNames[raw])filename=jsonText(dbNames[raw]);",
    "else if(transform=='database_hint_filename'&&hintNames&&hintNames[raw])filename=jsonText(hintNames[raw]);",
    "if(!filename)return args;out={filename:filename};for(k in parsed){if(toolOwn(parsed,k))out[k]=parsed[k];}return out;}",
    "function toolSectionHTML(label,body){return '<div class=\"tool-field\"><div class=\"tool-input-title\">'",
    "+escHTML(label)+'</div>'+(body||'')+'</div>';}",
    "function toolInputHTML(name,args,dbNames,hintNames){var shown=decorateToolArgs(name,args,dbNames,hintNames);",
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
    "function toolCardSummary(card,index){return (index+1)+': '+toolDisplayTitle(card.name,card.args,card.dbNames,card.hintNames);}",
    "function setToolCardSummary(n,text,error){if(n)n.innerHTML=",
    "(error?toolErrorIconHTML():'')+escHTML(text);}",
    "function toolCardHTML(card,index){var name=jsonText(card.name||'Tool');",
    "var cls='tool-card'+(card.error?' tool-error':'');var h='<div class=\"'+cls+'\">'",
    "+'<a class=\"tool-card-toggle disclosure-title\" href=\"#\" aria-expanded=\"false\" onclick=\"return toggleToolCard(this)\">'",
    "+'<span class=\"tool-disclosure\">'+disclosureIconHTML(1)+'</span>'",
    "+(card.error?toolErrorIconHTML():'')+'<span class=\"tool-card-summary\">'",
    "+escHTML(toolCardSummary(card,index))+'</span></a><div class=\"tool-card-body\">';",
    "if(!card.outputOnly)h+=toolInputHTML(name,card.args||'',card.dbNames,card.hintNames);",
    "h+=toolOutputHTML(card.output,name,card.error);return h+'</div></div>';}",
    "function toolCallCardData(call){var fn=isObj(call)?call['function']:null;",
    "return {name:isObj(fn)?jsonText(fn.name):'unknown',id:isObj(call)?jsonText(call.id):'',",
    "args:(isObj(fn)&&typeof fn.arguments!='undefined')?fn.arguments:'',output:null,error:false};}",
    "function appendCardsFromEvents(cards,events,dbNames,hintNames){var map={};var order=[];var i,e,c,id;",
    "for(i=0;i<events.length;i++){e=events[i];id=jsonText(e.tool_call_id||('event-'+i));",
    "if(!map[id]){map[id]={name:jsonText(e.tool_name||'Tool'),id:id,args:e.arguments_json||'',output:null,error:false};",
    "map[id].dbNames=dbNames;map[id].hintNames=hintNames;map[id]._index=order.length;order[order.length]=map[id];}",
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
    "function collectDatabaseHintNamesFromObject(hintNames,dbNames,o){var hints=isObj(o)&&isArr(o.database_hints)?o.database_hints:null;",
    "var i,h,id,name,db;if(!isObj(o))return;id=jsonText(o.id);name=jsonText(o.filename);db=jsonText(o.database_id);",
    "if(name===''&&db!==''&&dbNames&&dbNames[db])name=jsonText(dbNames[db]);if(id!==''&&name!=='')hintNames[id]=name;",
    "if(!hints)return;for(i=0;i<hints.length;i++){h=hints[i]||{};id=jsonText(h.id);name=jsonText(h.filename);db=jsonText(h.database_id);",
    "if(name===''&&db!==''&&dbNames&&dbNames[db])name=jsonText(dbNames[db]);if(id!==''&&name!=='')hintNames[id]=name;}}",
    "function collectDatabaseHintNamesFromRaw(hintNames,dbNames,raw){var events=parseToolEvents(raw);var i,o;",
    "if(events){for(i=0;i<events.length;i++){o=parseJSONSafe(events[i].result_json||'');collectDatabaseHintNamesFromObject(hintNames,dbNames,o);}return;}",
    "o=parseJSONSafe(raw);collectDatabaseHintNamesFromObject(hintNames,dbNames,o);}",
    "function collectDatabaseHintNames(rows,dbNames){var hintNames={};var i;for(i=0;i<rows.length;i++)",
    "collectDatabaseHintNamesFromRaw(hintNames,dbNames,toolRowRaw(rows[i]));return hintNames;}",
    "function isAPIToolCallRow(row){return hasClass(row,'row')&&hasClass(row,'api_function_call');}",
    "function isAPIToolOutputRow(row){return hasClass(row,'row')&&hasClass(row,'api_function_output');}",
    "function apiToolRows(){var m=byId('messages');var out=[];var n,i;if(!m)return out;",
    "n=m.getElementsByTagName('*');for(i=0;i<n.length;i++){if(isAPIToolCallRow(n[i])||isAPIToolOutputRow(n[i]))out[out.length]=n[i];}return out;}",
    "function apiToolAttr(row,name){return row&&row.getAttribute?row.getAttribute('data-'+name)||'':'';}",
    "function apiToolLabel(row){var label=apiToolAttr(row,'tool-label');return label!==''?label:'Tool';}",
    "function apiToolRaw(row,name){var raw=apiToolAttr(row,name);var body,fallback;if(raw!=='')return raw;",
    "body=firstByClass(row,'tool-card-body');fallback=body?firstByClass(body,'api-tool-fallback'):null;return fallback?nodeText(fallback):'';}",
    "function isAPIServerToolRow(row){var kind,spec;if(!hasClass(row,'row')||!hasClass(row,'api_item'))return false;",
    "kind=apiToolAttr(row,'kind');spec=toolDisplaySpec(kind);return !!(spec&&spec.response_item);}",
    "function apiServerToolRows(){var m=byId('messages');var out=[];var n,i;if(!m)return out;",
    "n=m.getElementsByTagName('*');for(i=0;i<n.length;i++){if(isAPIServerToolRow(n[i]))out[out.length]=n[i];}return out;}",
    "function responseItemObject(row){var item={};var kind=apiToolAttr(row,'kind');var raw,action,value;",
    "if(kind!=='')item.type=kind;raw=apiToolAttr(row,'response-item-action-json');if(raw!==''){action=parseJSONSafe(raw);if(action!==null)item.action=action;}",
    "value=apiToolAttr(row,'response-item-url');if(value!=='')item.url=value;value=apiToolAttr(row,'response-item-title');if(value!=='')item.title=value;",
    "value=apiToolAttr(row,'response-item-status');if(value!=='')item.status=value;value=apiToolAttr(row,'response-item-http-status');if(value!=='')item.httpStatus=value;return item;}",
    "function responseItemDisplayObject(item){var out={};var k;if(isObj(item.action)){for(k in item.action){if(toolOwn(item.action,k))out[k]=item.action[k];}}",
    "if(typeof item.url!='undefined')out.url=item.url;if(typeof item.title!='undefined')out.title=item.title;",
    "if(typeof item.status!='undefined')out.status=item.status;if(typeof item.httpStatus!='undefined')out.httpStatus=item.httpStatus;return out;}",
    "function responseItemHasError(row,item){var status=jsonText(item.status);return hasClass(row,'state-error')||status=='failed'||status=='cancelled';}",
    "function ensureServerToolCardBubble(bubble){if(hasClass(bubble,'api-tool-card'))return;",
    "bubble.className+=' api-tool-card tool-card';bubble.innerHTML='<a class=\"tool-card-toggle disclosure-title\" href=\"#\" aria-expanded=\"false\" onclick=\"return toggleToolCard(this)\">'",
    "+'<span class=\"tool-disclosure\">'+disclosureIconHTML(1)+'</span><span class=\"tool-card-summary\"></span></a><div class=\"tool-card-body\"></div>';}",
    "function renderAPIServerToolRows(){var rows=apiServerToolRows();var i,row,kind,spec,item,title,error,bubble,body,summary,role,shown;",
    "for(i=0;i<rows.length;i++){row=rows[i];kind=apiToolAttr(row,'kind');spec=toolDisplaySpec(kind);item=responseItemObject(row);",
    "title=toolDisplayTitle(kind,'',null,null,item);error=responseItemHasError(row,item);bubble=firstByClass(row,'bubble');if(!bubble)continue;",
    "ensureServerToolCardBubble(bubble);setClass(row,'api_server_tool',1);role=firstByClass(row,'role');if(role)role.style.display='none';",
    "body=firstByClass(bubble,'tool-card-body');summary=firstByClass(bubble,'tool-card-summary');shown=responseItemDisplayObject(item);",
    "setToolCardSummary(summary,apiToolLabel(row)+': '+title,error);if(body)body.innerHTML=toolPanel(error?'Tool Error':'Tool Activity',",
    "jsonText((spec&&spec.label)||kind),toolJSONHTML(shown),'',error?'tool-error':'');",
    "if(error&&!hasClass(bubble,'tool-error'))bubble.className+=' tool-error';if(!error)bubble.className=bubble.className.replace(/\\stool-error/g,'');}}",
    "function renderAPIToolRows(){var rows=apiToolRows();var calls={};var dbNames={};var hintNames={};",
    "var i,row,id,name,raw,args,title,body,summary,error,bubble;",
    "for(i=0;i<rows.length;i++){row=rows[i];id=apiToolAttr(row,'tool-call-id');",
    "if(isAPIToolCallRow(row)&&id!=='')calls[id]={name:apiToolAttr(row,'tool-name'),args:apiToolRaw(row,'arguments-json')};",
    "if(isAPIToolOutputRow(row))collectDatabaseNamesFromRaw(dbNames,apiToolRaw(row,'result-json'));}",
    "for(i=0;i<rows.length;i++){row=rows[i];if(isAPIToolOutputRow(row))",
    "collectDatabaseHintNamesFromRaw(hintNames,dbNames,apiToolRaw(row,'result-json'));}",
    "for(i=0;i<rows.length;i++){row=rows[i];id=apiToolAttr(row,'tool-call-id');name=apiToolAttr(row,'tool-name');",
    "if(name===''&&id!==''&&calls[id])name=calls[id].name;if(name==='')name=isAPIToolCallRow(row)?'Tool Call':'Tool Result';",
    "body=firstByClass(row,'tool-card-body');summary=firstByClass(row,'tool-card-summary');if(!body)continue;",
    "bubble=firstByClass(row,'bubble');args=isAPIToolCallRow(row)?apiToolRaw(row,'arguments-json'):",
    "((id!==''&&calls[id])?calls[id].args:'');title=toolDisplayTitle(name,args,dbNames,hintNames);",
    "if(isAPIToolCallRow(row)){raw=apiToolRaw(row,'arguments-json');setToolCardSummary(summary,apiToolLabel(row)+': '+title,0);",
    "body.innerHTML=toolPanel('Tool Call',name,toolInputHTML(name,raw,dbNames,hintNames),raw,'');",
    "if(bubble)bubble.className=bubble.className.replace(/\\stool-error/g,'');}",
    "else{raw=apiToolRaw(row,'result-json');error=toolOutputHasError(raw);body.innerHTML=toolOutputHTML(raw,name,error);",
    "setToolCardSummary(summary,apiToolLabel(row)+': '+title,error);",
    "if(bubble){if(error&&!hasClass(bubble,'tool-error'))bubble.className+=' tool-error';",
    "if(!error)bubble.className=bubble.className.replace(/\\stool-error/g,'');}}}}",
    "function apiToolGroupKey(row){var id=apiExchangeId(row);var kind=isAPIToolCallRow(row)?'calls':'outputs';",
    "return id!==''?'api-call-'+id+'-'+kind:(promptGroupKey(row)||'api-tools')+'-'+kind;}",
    "function removeAPIToolGroupDecoration(row){var role;setRowClass(row,'api-tool-group-row',0);",
    "setRowClass(row,'api-tool-group-anchor',0);setRowClass(row,'api-tool-group-secondary',0);",
    "role=firstByClass(row,'role');if(!role)return;role.style.display='';",
    "role.onclick=null;setClass(role,'disclosure-title',0);}",
    "function decorateAPIToolGroups(root){var rows=apiToolRows();var groups={};var keys=[];var i,j,row,key,g;",
    "for(i=0;i<rows.length;i++){row=rows[i];removeAPIToolGroupDecoration(row);key=apiToolGroupKey(row);",
    "if(key==='')continue;if(!groups[key]){groups[key]={rows:[]};keys[keys.length]=key;}",
    "groups[key].rows[groups[key].rows.length]=row;}",
    "for(i=0;i<keys.length;i++){key=keys[i];g=groups[key];",
    "for(j=0;j<g.rows.length;j++){row=g.rows[j];setRowClass(row,'api-tool-group-row',1);",
    "setRowClass(row,'api-tool-group-anchor',j===0);setRowClass(row,'api-tool-group-secondary',j>0);}}}",
    "function appendCardsFromToolCall(cards,pending,raw,dbNames,hintNames){var calls=toolCallsPayload(raw);var i,c;",
    "if(isArr(calls)){for(i=0;i<calls.length;i++){c=toolCallCardData(calls[i]);c.dbNames=dbNames;c.hintNames=hintNames;",
    "cards[cards.length]=c;pending[pending.length]=c;}}",
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
    "function rowByMessageKeyAny(key){var rows=messageRows();var p=byId('tool-sources')||byId('tools');var n,i;",
    "if(!key)return null;if(p){n=p.childNodes;for(i=0;i<n.length;i++){if(hasClass(n[i],'row'))rows[rows.length]=n[i];}}",
    "for(i=0;i<rows.length;i++){if(rowMessageKey(rows[i])==key)return rows[i];}return null;}",
    "function rowIdByMessageKey(key){var r=rowByMessageKeyAny(key);return r?rowId(r):'';}",
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
    "if(c)c.innerHTML=count>0?' - '+(lastError?toolErrorIconHTML():'')+",
    "escHTML(lastSummary!==''?lastSummary:jsonText(count)):'';",
    "if(lastError){if(!hasClass(box,'tool-column-error'))box.className+=' tool-column-error';}",
    "else box.className=box.className.replace(/\\stool-column-error/g,'');}",
    "function ensureAssistantToolBox(row){var box=firstByClass(row,'tool-column');var bubble;if(box)return box;",
    "box=document.createElement('div');box.className='tool-column tool-column-empty';",
    "box.innerHTML='<div class=\"tool-rail-title disclosure-title\" onclick=\"return toggleToolColumn(this)\"><a class=\"tool-column-toggle\" href=\"#\" aria-expanded=\"true\"><span class=\"tool-column-disclosure\">'+disclosureIconHTML(0)+'</span></a>Tool Calls<span class=\"tool-count\"></span></div><div class=\"tool-cards\"></div>';",
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
    "function rebuildToolCards(){var rows=toolSourceRows();var grouped={};var pending={};var targets=[];",
    "var dbNames,hintNames;var i,row,raw,events,c,target;clearToolBoxes();dbNames=collectDatabaseNames(rows);",
    "hintNames=collectDatabaseHintNames(rows,dbNames);for(i=0;i<rows.length;i++){row=rows[i];raw=toolRowRaw(row);if(raw==='')continue;",
    "target=ensureToolRowTarget(row);if(target==='')continue;if(!grouped[target]){grouped[target]=[];pending[target]=[];targets[targets.length]=target;}",
    "events=parseToolEvents(raw);if(events){appendCardsFromEvents(grouped[target],events,dbNames,hintNames);continue;}",
    "if(hasClass(row,'tool_call'))appendCardsFromToolCall(grouped[target],pending[target],raw,dbNames,hintNames);",
    "else{c=pending[target].length?pending[target].shift():null;if(c){c.output=raw;c.error=toolOutputHasError(raw);}",
    "else{grouped[target][grouped[target].length]={name:'Tool Result',id:'',args:'',output:raw,outputOnly:true,error:toolOutputHasError(raw)};}}}",
    "for(i=0;i<targets.length;i++)renderToolCardsForTarget(targets[i],grouped[targets[i]]);scrollToolRailBottom();}",
    "function renderToolNode(row){toolRowRaw(row);rebuildToolCards();}",
    "function renderTools(root){renderAPIToolRows();renderAPIServerToolRows();moveToolRows(root);rebuildToolCards();}",
    "function renderMessageDecorations(root){renderMarkdown(root);renderMetadata(root);renderTools(root);decorateAPIExchanges(root);decorateAPIToolGroups(root);decoratePromptGroups(root);",
    "if(processingInteractionsLocked())syncProcessingInteractionState(1,strappyProcessingPromptGroupKey);}",
    "var strappyBatchDepth=0;var strappyNeedsRender=0;var strappyBatchShouldScroll=0;",
    "var strappyUpdateInterval=300;var strappyStatusInterval=1000;",
    "var strappyUpdateTimer=null;var strappyUpdateDue=0;var strappyUpdateFlushing=0;",
    "var strappyUpdateShouldScroll=0;",
    "function scrollHeight(){var b=document.body||{};var e=document.documentElement||{};",
    "return Math.max(b.scrollHeight||0,e.scrollHeight||0,b.offsetHeight||0,e.offsetHeight||0);}",
    "function scrollBottomNow(){var b=document.body||{};var e=document.documentElement||{};",
    "var h=scrollHeight();",
    "if(e)e.scrollTop=h;if(b)b.scrollTop=h;window.scrollTo(0,h);}",
    "function strappyTextQueuesHaveEntries(){var k;for(k in strappyTextQueues)return 1;return 0;}",
    "function scheduleStatusTick(){var now,delay;if(!strappyProcessingStatus||!strappyProcessingStatus.active)return;",
    "now=(new Date()).getTime();if(strappyProcessingNextTick<=now)strappyProcessingNextTick=now+strappyStatusInterval;",
    "delay=strappyProcessingNextTick-now;if(delay<0)delay=0;scheduleWebViewUpdate(delay);}",
    "function scheduleWebViewUpdate(delay){var now,due;if(strappyUpdateFlushing)return;",
    "if(typeof delay!='number')delay=strappyUpdateInterval;if(delay<0)delay=0;now=(new Date()).getTime();due=now+delay;",
    "if(strappyUpdateTimer&&due>=strappyUpdateDue)return;if(strappyUpdateTimer)clearTimeout(strappyUpdateTimer);",
    "strappyUpdateDue=due;strappyUpdateTimer=setTimeout(flushWebViewUpdates,delay);}",
    "function flushWebViewUpdates(){var now,stick;if(strappyUpdateTimer){clearTimeout(strappyUpdateTimer);",
    "strappyUpdateTimer=null;}strappyUpdateDue=0;strappyUpdateFlushing=1;now=(new Date()).getTime();",
    "stick=strappyAutoScrollEnabled?1:0;strappyUpdateShouldScroll=0;",
    "if(strappyTextQueuesHaveEntries())flushTextQueues();",
    "if(strappyStreamingMarkdownNeedsFlush)flushStreamingMarkdown();",
    "if(strappyProcessingStatusDirty||(strappyProcessingStatus&&strappyProcessingStatus.active&&now>=strappyProcessingNextTick)){",
    "strappyProcessingStatusDirty=0;updateProcessingStatus();strappyProcessingNextTick=now+strappyStatusInterval;}",
    "else if(strappyProcessingStatus&&strappyProcessingStatus.active)strappyProcessingNextTick=now+strappyStatusInterval;",
    "if(stick)scrollBottomNow();strappyUpdateFlushing=0;",
    "if(strappyTextQueuesHaveEntries()||strappyStreamingMarkdownNeedsFlush||strappyProcessingStatusDirty)",
    "scheduleWebViewUpdate(strappyUpdateInterval);else scheduleStatusTick();}",
    "function beginMessageBatch(){if(strappyBatchDepth===0)strappyBatchShouldScroll=strappyAutoScrollEnabled?1:0;strappyBatchDepth++;}",
    "function endMessageBatch(){if(strappyBatchDepth>0)strappyBatchDepth--;",
    "if(strappyBatchDepth===0){if(strappyTextQueuesHaveEntries())flushTextQueues();",
    "if(strappyStreamingMarkdownNeedsFlush)flushStreamingMarkdown();",
    "if(strappyNeedsRender){strappyNeedsRender=0;renderMessageDecorations(document);}",
    "if(strappyBatchShouldScroll)scrollBottomNow();strappyBatchShouldScroll=0;}}",
    "function renderAfterMutation(root){if(strappyBatchDepth>0){strappyNeedsRender=1;return;}",
    "renderMessageDecorations(root);}",
    "function clearTimelineError(){var e=byId('timeline-error');if(e)e.style.display='none';}",
    "function nodesFromHTML(html){var d=document.createElement('div');d.innerHTML=html;return d;}",
    "function clearInsertedRowAnimation(n){if(!n)return;",
    "setClass(n,'row-inserting',0);if(n.removeEventListener)",
    "n.removeEventListener('webkitAnimationEnd',finishInsertedRowAnimation,false);}",
    "function finishInsertedRowAnimation(e){var n=e&&e.currentTarget?e.currentTarget:null;",
    "if(!n||(e.animationName&&e.animationName!='strappy-round-fade'))return;",
    "clearInsertedRowAnimation(n);}",
    "function scheduleInsertedRowCleanup(n){setTimeout(function(){clearInsertedRowAnimation(n);},400);}",
    "function prepareInsertedRow(n){if(!n||!hasClass(n,'row'))return;",
    "if(n.addEventListener)n.addEventListener('webkitAnimationEnd',finishInsertedRowAnimation,false);",
    "setClass(n,'row-inserting',1);scheduleInsertedRowCleanup(n);}",
    "function scrollBottom(){if(!strappyAutoScrollEnabled)return;if(strappyBatchDepth>0){strappyBatchShouldScroll=1;return;}",
    "strappyUpdateShouldScroll=1;scheduleWebViewUpdate(0);}",
    "function scrollToolRailBottom(){}",
    "function updateToolTargets(oldId,newId){var rows,i;if(!oldId||!newId||oldId==newId)return;",
    "rows=toolSourceRows().concat(messageRows());for(i=0;i<rows.length;i++){if(toolTarget(rows[i])==oldId)setToolTarget(rows[i],newId);}}",
    "var strappyTextQueues={};",
    "function queueTextAppend(id,t,kind){var k=kind+'|'+id;if(!strappyTextQueues[k])",
    "strappyTextQueues[k]={id:id,kind:kind,text:''};strappyTextQueues[k].text+=escHTML(t);",
    "if(strappyAutoScrollEnabled)strappyUpdateShouldScroll=1;",
    "if(strappyBatchDepth===0)scheduleWebViewUpdate(strappyUpdateInterval);}",
    "function flushTextQueues(){var k,q,r,n;",
    "for(k in strappyTextQueues){q=strappyTextQueues[k];r=byId(q.id);if(!r)continue;",
    "n=(q.kind=='reasoning')?firstByClass(r,'reasoning-body'):firstByClass(r,'bubble');",
    "if(!n)continue;if(q.kind=='reasoning'){if(hasClass(r,'streaming-active')){",
    "appendStreamingMarkdownNode(n,q.text);continue;}if(typeof n._strappyMarkdown=='undefined')n._strappyMarkdown=n.innerHTML;",
    "n._strappyMarkdown+=q.text;",
    "if(shouldRenderMarkdownReasoning(n))renderMarkdownNode(n);",
    "else n.innerHTML=n._strappyMarkdown;continue;}",
    "if(hasClass(r,'streaming-active')){appendStreamingMarkdownNode(n,q.text);continue;}",
    "if(typeof n._strappyMarkdown=='undefined')n._strappyMarkdown=n.innerHTML;",
    "n._strappyMarkdown+=q.text;",
    "if(shouldRenderMarkdownBubble(n))renderMarkdownNode(n);",
    "else n.innerHTML=n._strappyMarkdown;}strappyTextQueues={};}",
    "function appendMessage(html){clearTimelineError();var m=byId('messages');var previous,n;if(!m)return;previous=m.lastChild;",
    "if(m.insertAdjacentHTML){m.insertAdjacentHTML('beforeend',html);}",
    "else{var d=nodesFromHTML(html);while(d.firstChild)m.appendChild(d.firstChild);}",
    "n=previous?previous.nextSibling:m.firstChild;while(n){prepareInsertedRow(n);n=n.nextSibling;}",
    "renderAfterMutation(m);scrollBottom();}",
    "function rawMarkdown(n){if(!n)return '';return (typeof n._strappyMarkdown!='undefined')?n._strappyMarkdown:(n.innerHTML||'');}",
    "function setRawMarkdown(n,raw){if(!n)return;n._strappyMarkdown=raw||'';n._strappyMarkdownRendered=null;n.innerHTML=n._strappyMarkdown;}",
    "function preserveLongerMarkdown(oldNode,newNode){var oldRaw,newRaw;if(!oldNode||!newNode)return;",
    "oldRaw=rawMarkdown(oldNode);newRaw=rawMarkdown(newNode);",
    "if(oldRaw.length>newRaw.length&&(newRaw===''||oldRaw.indexOf(newRaw)>=0))setRawMarkdown(newNode,oldRaw);}",
    "function preserveLiveMessageText(oldRow,newRow){if(!oldRow||!newRow)return;",
    "preserveLongerMarkdown(firstByClass(oldRow,'bubble'),firstByClass(newRow,'bubble'));",
    "preserveLongerMarkdown(firstByClass(oldRow,'reasoning-body'),firstByClass(newRow,'reasoning-body'));}",
    "function replaceMessage(id,html){clearTimelineError();var old=byId(id);var oldId,target,wasAssistant,next,newId;",
    "flushTextQueues();",
    "if(!old){appendMessage(html);return;}var d=nodesFromHTML(html);",
    "oldId=rowId(old);target=toolTarget(old);wasAssistant=isAssistantRow(old);",
    "if(d.firstChild){next=d.firstChild;if(target!==''&&isToolRow(next))setToolTarget(next,target);",
    "if(isAssistantRow(old)&&isAssistantRow(next))preserveLiveMessageText(old,next);",
    "old.parentNode.replaceChild(next,old);newId=rowId(next);if(wasAssistant)updateToolTargets(oldId,newId);}",
    "renderAfterMutation(document);scrollBottom();}",
    "function insertMessageBefore(id,html){clearTimelineError();var before=byId(id);var m=byId('messages');var n;",
    "if(!m){return;}if(!before){appendMessage(html);return;}var d=nodesFromHTML(html);",
    "while(d.firstChild){n=d.firstChild;prepareInsertedRow(n);m.insertBefore(n,before);}",
    "renderAfterMutation(m);scrollBottom();}",
    "function setMessageState(id,status,state){var r=byId(id);var s;if(!r)return;",
    "r.className=r.className.replace(/\\sstate-[^\\s]+/g,'');if(state)r.className+=' state-'+state;",
    "s=firstByClass(r,'status');if(status){if(!s){s=document.createElement('div');",
    "s.className='meta status';r.appendChild(s);}s.innerHTML=status;}else if(s&&s.parentNode)s.parentNode.removeChild(s);scrollBottom();}",
    "function setMessageThinking(id,status){var r=byId(id);var b;if(!r)return;",
    "b=firstByClass(r,'bubble');if(b&&hasClass(b,'bubble-status'))b.innerHTML=status||'';}",
    "function appendMessageText(id,t){var r=byId(id);var b;if(!r)return;",
    "b=firstByClass(r,'bubble');if(b)b.style.display='block';",
    "if(b&&hasClass(b,'bubble-status')){b.className=b.className.replace(/\\sbubble-status/g,'');",
    "b._strappyMarkdown='';b.innerHTML='';}",
    "if(!b)return;",
    "queueTextAppend(id,t,'content');}",
    "function appendMessageTextByMessageKey(key,t){var id=rowIdByMessageKey(key);",
    "if(id==='')return false;appendMessageText(id,t);return true;}",
    "function moveMessageTextToReasoning(id){var r=byId(id);var b,box,body,raw;",
    "flushTextQueues();if(!r)return;b=firstByClass(r,'bubble');box=firstByClass(r,'reasoning');",
    "body=firstByClass(r,'reasoning-body');if(!b||!box||!body)return;",
    "raw=(typeof b._strappyMarkdown!='undefined')?b._strappyMarkdown:escHTML(nodeText(b));",
    "if(raw==='')return;if(typeof body._strappyMarkdown=='undefined')body._strappyMarkdown=body.innerHTML;",
    "if(body._strappyMarkdown!==''&&raw.charAt(0)!='\\n')raw='\\n'+raw;",
    "if(hasClass(r,'streaming-active'))appendStreamingMarkdownNode(body,raw);",
    "else{body._strappyMarkdown+=raw;if(shouldRenderMarkdownReasoning(body))renderMarkdownNode(body);",
    "else body.innerHTML=body._strappyMarkdown;}",
    "b._strappyMarkdown='';b.className=b.className.replace(/\\sbubble-status/g,'');",
    "b.innerHTML='';b.style.display='none';",
    "box.style.display='block';",
    "if(!hasClass(r,'streaming-active'))r.className+=' streaming-active';",
    "setReasoningCollapsed(box,0);setMessageToolColumnCollapsed(id,1);scrollBottom();}",
    "function moveMessageTextToReasoningByMessageKey(key){var id=rowIdByMessageKey(key);",
    "if(id==='')return false;moveMessageTextToReasoning(id);return true;}",
    "function appendReasoningText(id,t){var r=byId(id);if(!r)return;",
    "var box=firstByClass(r,'reasoning');var body=firstByClass(r,'reasoning-body');",
    "if(box)box.style.display='block';if(!body)return;",
    "if(box)setReasoningCollapsed(box,0);",
    "queueTextAppend(id,t,'reasoning');}",
    "function appendReasoningTextByMessageKey(key,t){var id=rowIdByMessageKey(key);",
    "if(id==='')return false;appendReasoningText(id,t);return true;}",
    "function appendToolEventText(id,t){var r=byId(id);var b;if(!r)return;",
    "if(!isToolRow(r))r.style.display='block';b=firstByClass(r,'bubble');if(!b)return;",
    "if(typeof b._strappyRawText=='undefined')b._strappyRawText=nodeText(b);",
    "b._strappyRawText+=t;renderAfterMutation(document);scrollBottom();}",
    "function removeMessage(id){flushTextQueues();var r=byId(id);",
    "if(r&&r.parentNode)r.parentNode.removeChild(r);renderAfterMutation(document);}",
    "setTimeout(initProcessingStatusFromRenderState,0);",
    "</script>",
    NULL
  };

  return strappy_webview_append_chunks(buffer, chunks);
}

void strappy_webview_free(char *value)
{
  free(value);
}

strappy_webview_script_batch *strappy_webview_script_batch_create(void)
{
  strappy_webview_script_batch *batch;

  batch = (strappy_webview_script_batch *)malloc(sizeof(*batch));
  if (batch == NULL) {
    return NULL;
  }

  strappy_webview_buffer_init(&batch->buffer);
  batch->has_js = 0;
  batch->failed = 0;
  batch->finished = 0;

  if (!strappy_webview_buffer_append_cstring(&batch->buffer,
                                             "beginMessageBatch();try{")) {
    strappy_webview_script_batch_destroy(batch);
    return NULL;
  }

  return batch;
}

void strappy_webview_script_batch_destroy(strappy_webview_script_batch *batch)
{
  if (batch == NULL) {
    return;
  }

  strappy_webview_buffer_destroy(&batch->buffer);
  free(batch);
}

int strappy_webview_script_batch_append_js(
  strappy_webview_script_batch *batch,
  const char *java_script)
{
  if ((batch == NULL) || batch->failed || batch->finished) {
    return 0;
  }
  if ((java_script == NULL) || (java_script[0] == '\0')) {
    return 1;
  }

  if (!strappy_webview_buffer_append_cstring(&batch->buffer, java_script)) {
    batch->failed = 1;
    return 0;
  }

  batch->has_js = 1;
  return 1;
}

int strappy_webview_script_batch_has_js(
  const strappy_webview_script_batch *batch)
{
  return ((batch != NULL) &&
          batch->has_js &&
          !batch->failed &&
          !batch->finished) ? 1 : 0;
}

char *strappy_webview_script_batch_finish_js(
  strappy_webview_script_batch *batch)
{
  if (!strappy_webview_script_batch_has_js(batch)) {
    return NULL;
  }

  if (!strappy_webview_buffer_append_cstring(&batch->buffer,
                                             "}finally{endMessageBatch();}")) {
    batch->failed = 1;
    return NULL;
  }

  batch->finished = 1;
  return strappy_webview_buffer_finish(&batch->buffer);
}

void strappy_webview_set_font_dir(const char *abs_dir)
{
  size_t length;
  char *copy;

  free(g_strappy_webview_font_dir);
  g_strappy_webview_font_dir = NULL;

  if ((abs_dir == NULL) || (abs_dir[0] == '\0')) {
    return;
  }

  length = strlen(abs_dir);
  copy = (char *)malloc(length + 1U);
  if (copy == NULL) {
    return;
  }

  memcpy(copy, abs_dir, length + 1U);
  g_strappy_webview_font_dir = copy;
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
  const char *render_state_json;
  const char *tool_name;
  const char *direction;
  const char *direction_label;
  const char *status_to_render;
  char *owned_status_html;
  char http_status_text[64];
  char api_call_id_text[64];
  char round_number_text[64];
  char attempt_number_text[64];
  char cumulative_usage_cost_text[64];
  int has_state;
  int render_created_at;
  int render_streaming;
  int reasoning_render_when_empty;
  int reasoning_collapsed;
  int tool_column_collapsed;
  int render_bubble_status;
  int hide_empty_answer_bubble;
  int suppress_status_meta;
  int render_api_tool_card;
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
  render_state_json = (message != NULL) ?
    strappy_webview_string_or_empty(message->render_state_json) : "";
  tool_name = (message != NULL) ?
    strappy_webview_string_or_empty(message->tool_name) : "";
  direction = (message != NULL) ?
    strappy_webview_string_or_empty(message->direction) : "";
  direction_label = "";
  if (strcmp(direction, "request") == 0) {
    direction_label = strappy_webview_request_label(labels);
  } else if (strcmp(direction, "response") == 0) {
    direction_label = strappy_webview_response_label(labels);
  }
  api_call_id_text[0] = '\0';
  round_number_text[0] = '\0';
  attempt_number_text[0] = '\0';
  cumulative_usage_cost_text[0] = '\0';
  if ((message != NULL) && (message->api_call_id > 0LL)) {
    snprintf(api_call_id_text,
             sizeof(api_call_id_text),
             "%lld",
             message->api_call_id);
    if (message->round_number > 0L) {
      snprintf(round_number_text,
               sizeof(round_number_text),
               "%ld",
               message->round_number);
    }
    if (message->attempt_number > 0L) {
      snprintf(attempt_number_text,
               sizeof(attempt_number_text),
               "%ld",
               message->attempt_number);
    }
    if (message->has_cumulative_usage_cost &&
        !strappy_webview_format_usage_cost(message->cumulative_usage_cost,
                                          cumulative_usage_cost_text,
                                          sizeof(cumulative_usage_cost_text))) {
      return NULL;
    }
  }
  status_to_render = strappy_webview_string_or_empty(status_html);
  owned_status_html = NULL;
  has_state = (state != NULL) && (state[0] != '\0');
  render_streaming = strappy_webview_json_bool(render_state_json,
                                              "streaming",
                                              0);
  reasoning_render_when_empty =
    strappy_webview_json_bool(render_state_json,
                              "reasoning_render_when_empty",
                              render_streaming ? 1 : 0);
  reasoning_collapsed =
    strappy_webview_json_bool(render_state_json,
                              "reasoning_collapsed",
                              ((reasoning[0] != '\0') &&
                               !reasoning_render_when_empty) ? 1 : 0);
  tool_column_collapsed =
    strappy_webview_json_bool(render_state_json,
                              "tool_column_collapsed",
                              1);
  render_bubble_status = 0;
  hide_empty_answer_bubble =
    render_streaming &&
    strappy_webview_is_assistant_role(role) &&
    (text[0] == '\0');
  suppress_status_meta =
    render_streaming &&
    (state != NULL) &&
    (strcmp(state, "pending") == 0);
  render_api_tool_card =
    strappy_webview_is_api_function_call_role(role) ||
    strappy_webview_is_api_function_output_role(role);
  render_created_at =
    (created_at[0] != '\0') &&
    ((message == NULL) || (message->api_call_id <= 0LL)) &&
    !render_streaming &&
    !strappy_webview_is_user_role(role) &&
    !strappy_webview_is_harness_role(role) &&
    !strappy_webview_is_developer_role(role);

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
  if (!has_state && (message != NULL) && message->is_error) {
    state = "error";
    has_state = 1;
  }

  strappy_webview_buffer_init(&buffer);
  ok = strappy_webview_buffer_append_cstring(&buffer, "<div id=\"") &&
       strappy_webview_append_element_id(&buffer, message) &&
       strappy_webview_buffer_append_cstring(&buffer, "\" class=\"row ") &&
       strappy_webview_append_html_escaped(&buffer, role);

  if (ok && render_streaming) {
    ok = strappy_webview_buffer_append_cstring(&buffer, " streaming-active");
  }

  if (ok && strappy_webview_is_api_reasoning_role(role)) {
    ok = strappy_webview_buffer_append_cstring(
      &buffer,
      " api-reasoning-collapsed");
  }

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
       strappy_webview_append_data_attribute(&buffer,
                                             "api-call-id",
                                             api_call_id_text) &&
       strappy_webview_append_data_attribute(&buffer,
                                             "round-number",
                                             round_number_text) &&
       strappy_webview_append_data_attribute(&buffer,
                                             "round-label",
                                             (api_call_id_text[0] != '\0') ?
                                               strappy_webview_round_label(labels) :
                                               NULL) &&
       strappy_webview_append_data_attribute(&buffer,
                                             "attempt-number",
                                             attempt_number_text) &&
       strappy_webview_append_data_attribute(&buffer,
                                             "attempt-label",
                                             (api_call_id_text[0] != '\0') ?
                                               strappy_webview_attempt_label(labels) :
                                               NULL) &&
       strappy_webview_append_data_attribute(
         &buffer,
         "cumulative-usage-cost",
         cumulative_usage_cost_text) &&
       strappy_webview_append_data_attribute(&buffer,
                                             "direction",
                                             direction) &&
       strappy_webview_append_data_attribute(&buffer,
                                             "direction-label",
                                             direction_label) &&
       strappy_webview_append_data_attribute(
         &buffer,
         "tool-label",
         (render_api_tool_card || strappy_webview_is_api_item_role(role)) ?
           strappy_webview_tool_label(labels) : NULL) &&
       strappy_webview_append_data_attribute(&buffer,
                                             "tool-call-id",
                                             (message != NULL) ?
                                               message->tool_call_id : NULL) &&
       strappy_webview_append_data_attribute(&buffer,
                                             "tool-name",
                                             (message != NULL) ?
                                               message->tool_name : NULL) &&
       strappy_webview_append_data_attribute(&buffer,
                                             "arguments-json",
                                             (message != NULL) ?
                                               message->arguments_json : NULL) &&
       strappy_webview_append_data_attribute(&buffer,
                                             "result-json",
                                             (message != NULL) ?
                                               message->result_json : NULL) &&
       strappy_webview_append_data_attribute(
         &buffer,
         "response-item-action-json",
         (message != NULL) ? message->response_item_action_json : NULL) &&
       strappy_webview_append_data_attribute(
         &buffer,
         "response-item-url",
         (message != NULL) ? message->response_item_url : NULL) &&
       strappy_webview_append_data_attribute(
         &buffer,
         "response-item-title",
         (message != NULL) ? message->response_item_title : NULL) &&
       strappy_webview_append_data_attribute(
         &buffer,
         "response-item-status",
         (message != NULL) ? message->response_item_status : NULL) &&
       strappy_webview_append_data_attribute(
         &buffer,
         "response-item-http-status",
         (message != NULL) ? message->response_item_http_status : NULL) &&
       strappy_webview_append_data_attribute(&buffer,
                                             "render-state",
                                             (message != NULL) ?
                                               message->render_state_json : NULL);
  if (ok && render_streaming && strappy_webview_is_assistant_role(role)) {
    ok = strappy_webview_append_data_attribute(
           &buffer,
           "thinking-label",
           strappy_webview_thinking_label(labels));
  }
  if (ok && !render_api_tool_card) {
    ok = strappy_webview_buffer_append_cstring(&buffer, "><div class=\"role");
    if (ok && strappy_webview_is_api_reasoning_role(role)) {
      ok = strappy_webview_buffer_append_cstring(
        &buffer,
        " disclosure-title\" "
        "onclick=\"return toggleAPIReasoning(this)\">"
        "<a class=\"api-reasoning-toggle\" href=\"#\" "
        "aria-expanded=\"false\">"
        "<span class=\"api-reasoning-disclosure\">") &&
        strappy_webview_buffer_append_cstring(
          &buffer,
          strappy_webview_disclosure_icon_html(1)) &&
        strappy_webview_buffer_append_cstring(&buffer, "</span></a>");
    } else if (ok) {
      ok = strappy_webview_buffer_append_cstring(&buffer, "\">");
    }
    ok = ok &&
         strappy_webview_append_html_escaped(
           &buffer,
           strappy_webview_role_label(role, labels)) &&
         strappy_webview_buffer_append_cstring(&buffer, "</div>");
  } else if (ok) {
    ok = strappy_webview_buffer_append_cstring(&buffer, ">");
  }

  if (ok && strappy_webview_is_assistant_role(role)) {
    ok = strappy_webview_append_reasoning_html(&buffer,
                                               reasoning,
                                               reasoning_render_when_empty,
                                               reasoning_collapsed,
                                               labels);
    if (ok) {
      ok = strappy_webview_append_tool_column_html(&buffer,
                                                  tool_column_collapsed);
    }
  }

  ok = ok &&
       strappy_webview_buffer_append_cstring(&buffer, "<div class=\"bubble");
  if (ok && render_api_tool_card) {
    ok = strappy_webview_buffer_append_cstring(
      &buffer,
      " api-tool-card tool-card");
  }
  if (ok && render_bubble_status) {
    ok = strappy_webview_buffer_append_cstring(&buffer, " bubble-status");
  }
  ok = ok && strappy_webview_buffer_append_cstring(&buffer, "\"");
  if (ok && hide_empty_answer_bubble) {
    ok = strappy_webview_buffer_append_cstring(&buffer,
                                               " style=\"display:none;\"");
  }
  ok = ok && strappy_webview_buffer_append_cstring(&buffer, ">");
  if (ok && render_bubble_status) {
    if (status_to_render[0] != '\0') {
      ok = strappy_webview_buffer_append_cstring(&buffer, status_to_render);
    } else {
      ok = strappy_webview_append_html_escaped(
             &buffer,
             strappy_webview_thinking_label(labels));
    }
  } else if (render_api_tool_card) {
    ok = strappy_webview_buffer_append_cstring(
           &buffer,
           "<a class=\"tool-card-toggle disclosure-title\" href=\"#\" "
           "aria-expanded=\"false\" "
           "onclick=\"return toggleToolCard(this)\">"
           "<span class=\"tool-disclosure\">") &&
         strappy_webview_buffer_append_cstring(
           &buffer,
           strappy_webview_disclosure_icon_html(1)) &&
         strappy_webview_buffer_append_cstring(
           &buffer,
           "</span>"
           "<span class=\"tool-card-summary\">") &&
         strappy_webview_append_html_escaped(
           &buffer,
           strappy_webview_tool_label(labels)) &&
         strappy_webview_buffer_append_cstring(&buffer, ": ") &&
         strappy_webview_append_html_escaped(
           &buffer,
           (tool_name[0] != '\0') ? tool_name :
             strappy_webview_role_label(role, labels)) &&
         strappy_webview_buffer_append_cstring(
           &buffer,
           "</span></a><div class=\"tool-card-body\">"
           "<div class=\"api-tool-fallback\">") &&
         strappy_webview_append_html_escaped(&buffer, text) &&
         strappy_webview_buffer_append_cstring(&buffer, "</div></div>");
  } else {
    ok = ok && strappy_webview_append_html_escaped(&buffer, text);
  }
  ok = ok &&
       strappy_webview_buffer_append_cstring(&buffer, "</div>") &&
       strappy_webview_append_metadata_html(&buffer, role, metadata_json, labels);

  if (ok && (status_to_render[0] != '\0') &&
      !render_bubble_status && !suppress_status_meta) {
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

char *strappy_webview_messages_html(
  const strappy_webview_message *messages,
  size_t count,
  const strappy_webview_labels *labels)
{
  strappy_webview_buffer buffer;
  char *message_html;
  size_t index;

  if ((messages == NULL) && (count > 0U)) {
    return NULL;
  }

  strappy_webview_buffer_init(&buffer);
  for (index = 0U; index < count; index++) {
    message_html = strappy_webview_message_html(&messages[index],
                                                labels,
                                                NULL,
                                                NULL);
    if (message_html == NULL) {
      strappy_webview_buffer_destroy(&buffer);
      return NULL;
    }
    if (!strappy_webview_buffer_append_cstring(&buffer, message_html)) {
      free(message_html);
      strappy_webview_buffer_destroy(&buffer);
      return NULL;
    }
    free(message_html);
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
  static const char *streaming_render_state =
    "{\"streaming\":true,\"reasoning_render_when_empty\":true,"
    "\"reasoning_collapsed\":false,\"tool_column_collapsed\":true}";
  strappy_webview_message message;

  if ((element_id == NULL) || (element_id[0] == '\0')) {
    element_id = "streaming-assistant";
  }

  memset(&message, 0, sizeof(message));
  message.element_id = element_id;
  message.role = "assistant";
  message.actor = actor;
  message.prompt_group_key = prompt_group_key;
  message.text = strappy_webview_string_or_empty(text);
  message.reasoning = strappy_webview_string_or_empty(reasoning);
  message.render_state_json = streaming_render_state;
  return strappy_webview_message_html(&message, labels, state, status_html);
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

char *strappy_webview_messages_page_html(
  const char *messages_html,
  const char *tool_display_registry_json,
  const char *error_text)
{
  strappy_webview_buffer buffer;
  int ok;

  if (messages_html == NULL) {
    messages_html = "";
  }
  strappy_webview_buffer_init(&buffer);
  ok = strappy_webview_buffer_append_cstring(
         &buffer,
         "<!doctype html><html><head><meta charset=\"utf-8\">"
         "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">") &&
       strappy_webview_append_styles(&buffer) &&
       strappy_webview_buffer_append_cstring(
         &buffer,
         "<script>var strappyToolDisplayRegistry=") &&
       strappy_webview_append_inline_json(
         &buffer,
         ((tool_display_registry_json != NULL) &&
          (tool_display_registry_json[0] != '\0')) ?
           tool_display_registry_json : "{}") &&
       strappy_webview_buffer_append_cstring(&buffer, ";</script>") &&
       strappy_webview_append_scripts(&buffer) &&
       strappy_webview_buffer_append_cstring(
         &buffer,
         "</head><body><div class=\"page\">");

  if (ok && (messages_html[0] == '\0') &&
      (error_text != NULL) && (error_text[0] != '\0')) {
    ok = strappy_webview_buffer_append_cstring(
           &buffer,
           "<div id=\"timeline-error\" class=\"timeline-error\">") &&
         strappy_webview_append_html_escaped(&buffer, error_text) &&
         strappy_webview_buffer_append_cstring(&buffer, "</div>");
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

static char *strappy_webview_message_element_id(
  const strappy_webview_message *message)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_append_element_id(&buffer, message)) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_message_update_js(
  const strappy_webview_message *message,
  const strappy_webview_labels *labels)
{
  char *element_id;
  char *message_html;
  char *js;

  message_html = strappy_webview_message_html(message, labels, NULL, NULL);
  if (message_html == NULL) {
    return NULL;
  }

  element_id = strappy_webview_message_element_id(message);
  if (element_id == NULL) {
    free(message_html);
    return NULL;
  }

  js = strappy_webview_replace_message_js(element_id, message_html);
  free(element_id);
  free(message_html);
  return js;
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

char *strappy_webview_append_message_text_by_key_js(const char *message_key,
                                                    const char *delta)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(&buffer,
                                             "appendMessageTextByMessageKey(") ||
      !strappy_webview_append_js_string(&buffer, message_key) ||
      !strappy_webview_buffer_append_cstring(&buffer, ",") ||
      !strappy_webview_append_js_string(&buffer, delta) ||
      !strappy_webview_buffer_append_cstring(&buffer, ");")) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_append_reasoning_text_by_key_js(const char *message_key,
                                                      const char *delta)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(
        &buffer,
        "appendReasoningTextByMessageKey(") ||
      !strappy_webview_append_js_string(&buffer, message_key) ||
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

char *strappy_webview_move_message_text_to_reasoning_by_key_js(
  const char *message_key)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(
        &buffer,
        "moveMessageTextToReasoningByMessageKey(") ||
      !strappy_webview_append_js_string(&buffer, message_key) ||
      !strappy_webview_buffer_append_cstring(&buffer, ");")) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_set_processing_status_js(const char *status_json)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(&buffer, "setProcessingStatus(") ||
      !strappy_webview_append_js_string(&buffer, status_json) ||
      !strappy_webview_buffer_append_cstring(&buffer, ");")) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_clear_processing_status_js(void)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(&buffer,
                                             "clearProcessingStatus();")) {
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
