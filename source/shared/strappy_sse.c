#include "strappy_sse.h"

#include "strappy_core.h"

#include <cJSON.h>
#include <stdlib.h>
#include <string.h>

#define STRAPPY_SSE_MAX_EVENT_BYTES (16U * 1024U * 1024U)
#define STRAPPY_SSE_MAX_STREAM_BYTES (64U * 1024U * 1024U)
#define STRAPPY_SSE_MAX_OUTPUT_ITEMS 4096U

static int strappy_sse_replace_missing_citations(const char *text,
                                                 char **normalized_out,
                                                 char **error_out)
{
  static const char marker_start[] = "\356\210\200cite\356\210\202";
  static const char marker_end[] = "\356\210\201";
  static const char replacement[] = "**`[Citation Missing]`**";
  const char *cursor;
  const char *start;
  const char *end;
  char *normalized;
  char *output;
  size_t normalized_length;

  *normalized_out = NULL;
  cursor = text;
  normalized_length = 0U;
  while ((start = strstr(cursor, marker_start)) != NULL) {
    size_t prefix_length;

    end = strstr(start + sizeof(marker_start) - 1U, marker_end);
    if (end == NULL) {
      break;
    }
    prefix_length = (size_t)(start - cursor);
    if ((prefix_length > ((size_t)-1) - normalized_length) ||
        ((sizeof(replacement) - 1U) >
         ((size_t)-1) - normalized_length - prefix_length)) {
      strappy_set_error(error_out,
                        "ChatGPT citation fallback text is too large.");
      return 0;
    }
    normalized_length += prefix_length + sizeof(replacement) - 1U;
    cursor = end + sizeof(marker_end) - 1U;
  }
  if (cursor == text) {
    return 1;
  }
  if (strlen(cursor) >= ((size_t)-1) - normalized_length) {
    strappy_set_error(error_out,
                      "ChatGPT citation fallback text is too large.");
    return 0;
  }
  normalized_length += strlen(cursor);
  normalized = (char *)malloc(normalized_length + 1U);
  if (normalized == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate ChatGPT citation fallback text.");
    return 0;
  }

  cursor = text;
  output = normalized;
  while ((start = strstr(cursor, marker_start)) != NULL) {
    size_t prefix_length;

    end = strstr(start + sizeof(marker_start) - 1U, marker_end);
    if (end == NULL) {
      break;
    }
    prefix_length = (size_t)(start - cursor);
    memcpy(output, cursor, prefix_length);
    output += prefix_length;
    memcpy(output, replacement, sizeof(replacement) - 1U);
    output += sizeof(replacement) - 1U;
    cursor = end + sizeof(marker_end) - 1U;
  }
  memcpy(output, cursor, strlen(cursor) + 1U);
  *normalized_out = normalized;
  return 1;
}

static int strappy_sse_normalize_missing_citations(cJSON *item,
                                                   char **error_out)
{
  cJSON *content;
  cJSON *part;

  if ((item == NULL) || !cJSON_IsObject(item)) {
    return 1;
  }
  content = cJSON_GetObjectItemCaseSensitive(item, "content");
  if ((content == NULL) || !cJSON_IsArray(content)) {
    return 1;
  }
  for (part = content->child; part != NULL; part = part->next) {
    cJSON *annotations;
    cJSON *text;
    cJSON *replacement;
    char *normalized;

    if (!cJSON_IsObject(part)) {
      continue;
    }
    annotations = cJSON_GetObjectItemCaseSensitive(part, "annotations");
    if (cJSON_IsArray(annotations) && (cJSON_GetArraySize(annotations) > 0)) {
      continue;
    }
    text = cJSON_GetObjectItemCaseSensitive(part, "text");
    if (!cJSON_IsString(text) || (text->valuestring == NULL)) {
      continue;
    }
    normalized = NULL;
    if (!strappy_sse_replace_missing_citations(text->valuestring,
                                               &normalized,
                                               error_out)) {
      return 0;
    }
    if (normalized == NULL) {
      continue;
    }
    replacement = cJSON_CreateString(normalized);
    free(normalized);
    if ((replacement == NULL) ||
        !cJSON_ReplaceItemInObjectCaseSensitive(part,
                                                "text",
                                                replacement)) {
      cJSON_Delete(replacement);
      strappy_set_error(error_out,
                        "Could not store ChatGPT citation fallback text.");
      return 0;
    }
  }
  return 1;
}

static int strappy_sse_normalize_output(cJSON *output, char **error_out)
{
  cJSON *item;

  if ((output == NULL) || !cJSON_IsArray(output)) {
    return 1;
  }
  for (item = output->child; item != NULL; item = item->next) {
    if (!strappy_sse_normalize_missing_citations(item, error_out)) {
      return 0;
    }
  }
  return 1;
}

static int strappy_sse_store_completed_output(strappy_sse_parser *parser,
                                               cJSON *event,
                                               char **error_out)
{
  cJSON *item;
  cJSON *copy;
  cJSON *output;

  item = cJSON_GetObjectItemCaseSensitive(event, "item");
  if (!cJSON_IsObject(item)) {
    strappy_set_error(error_out,
                      "ChatGPT completed output event has no item object.");
    return 0;
  }
  if (!strappy_sse_normalize_missing_citations(item, error_out)) {
    return 0;
  }
  if (parser->completed_output_count >= STRAPPY_SSE_MAX_OUTPUT_ITEMS) {
    strappy_set_error(error_out, "ChatGPT response has too many output items.");
    return 0;
  }
  output = (cJSON *)parser->completed_output;
  if (output == NULL) {
    output = cJSON_CreateArray();
    if (output == NULL) {
      strappy_set_error(error_out,
                        "Could not allocate ChatGPT completed output.");
      return 0;
    }
    parser->completed_output = output;
  }
  copy = cJSON_Duplicate(item, 1);
  if ((copy == NULL) || !cJSON_AddItemToArray(output, copy)) {
    cJSON_Delete(copy);
    strappy_set_error(error_out,
                      "Could not retain ChatGPT completed output item.");
    return 0;
  }
  parser->completed_output_count++;
  return 1;
}

static int strappy_sse_append(strappy_sse_parser *parser,
                              char byte,
                              char **error_out)
{
  size_t capacity;
  char *next;

  if (parser->event_length >= STRAPPY_SSE_MAX_EVENT_BYTES) {
    strappy_set_error(error_out, "ChatGPT SSE event is too large.");
    return 0;
  }
  if ((parser->event_length + 1U) >= parser->event_capacity) {
    capacity = (parser->event_capacity == 0U) ? 1024U :
      parser->event_capacity * 2U;
    if ((capacity < parser->event_capacity) ||
        (capacity > (STRAPPY_SSE_MAX_EVENT_BYTES + 1U))) {
      capacity = STRAPPY_SSE_MAX_EVENT_BYTES + 1U;
    }
    next = (char *)realloc(parser->event_buffer, capacity);
    if (next == NULL) {
      strappy_set_error(error_out,
                        "Could not allocate ChatGPT SSE event buffer.");
      return 0;
    }
    parser->event_buffer = next;
    parser->event_capacity = capacity;
  }
  parser->event_buffer[parser->event_length++] = byte;
  parser->event_buffer[parser->event_length] = '\0';
  return 1;
}

static int strappy_sse_append_data(char **data,
                                   size_t *length,
                                   const char *value,
                                   size_t value_length,
                                   char **error_out)
{
  size_t separator_length;
  char *next;

  separator_length = (*length > 0U) ? 1U : 0U;
  if ((*length > (((size_t)-1) - value_length - separator_length - 1U)) ||
      ((*length + value_length + separator_length) >
       STRAPPY_SSE_MAX_EVENT_BYTES)) {
    strappy_set_error(error_out, "ChatGPT SSE data is too large.");
    return 0;
  }
  next = (char *)realloc(*data,
                         *length + separator_length + value_length + 1U);
  if (next == NULL) {
    strappy_set_error(error_out, "Could not allocate ChatGPT SSE data.");
    return 0;
  }
  *data = next;
  if (separator_length > 0U) {
    (*data)[(*length)++] = '\n';
  }
  memcpy(*data + *length, value, value_length);
  *length += value_length;
  (*data)[*length] = '\0';
  return 1;
}

static int strappy_sse_store_terminal(strappy_sse_parser *parser,
                                      cJSON *event,
                                      const char *type,
                                      char **error_out)
{
  cJSON *response;
  cJSON *response_output;
  cJSON *completed_output;
  cJSON *completed_output_copy;
  char *json;

  response = cJSON_GetObjectItemCaseSensitive(event, "response");
  if ((strcmp(type, "response.completed") == 0) ||
      (strcmp(type, "response.done") == 0) ||
      (strcmp(type, "response.incomplete") == 0) ||
      (strcmp(type, "response.failed") == 0)) {
    if (!cJSON_IsObject(response)) {
      strappy_set_error(error_out,
                        "ChatGPT terminal SSE event has no response object.");
      return 0;
    }
    response_output = cJSON_GetObjectItemCaseSensitive(response, "output");
    if (!strappy_sse_normalize_output(response_output, error_out)) {
      return 0;
    }
    completed_output = (cJSON *)parser->completed_output;
    if ((completed_output != NULL) &&
        ((!cJSON_IsArray(response_output)) ||
         (cJSON_GetArraySize(response_output) == 0))) {
      completed_output_copy = cJSON_Duplicate(completed_output, 1);
      if (completed_output_copy == NULL) {
        strappy_set_error(error_out,
                          "Could not merge ChatGPT completed output.");
        return 0;
      }
      if (response_output != NULL) {
        cJSON_ReplaceItemInObjectCaseSensitive(response,
                                               "output",
                                               completed_output_copy);
      } else {
        cJSON_AddItemToObject(response, "output", completed_output_copy);
      }
    }
    json = cJSON_PrintUnformatted(response);
  } else {
    json = cJSON_PrintUnformatted(event);
  }
  if (json == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate ChatGPT terminal response.");
    return 0;
  }
  free(parser->terminal_json);
  parser->terminal_json = json;
  parser->terminal_received = 1;
  return 1;
}

static int strappy_sse_process_event(strappy_sse_parser *parser,
                                     char **error_out)
{
  char *cursor;
  char *line_end;
  char *data;
  size_t data_length;
  cJSON *event;
  cJSON *type_value;
  const char *type;
  int ok;

  if (parser->event_length == 0U) {
    return 1;
  }
  data = NULL;
  data_length = 0U;
  cursor = parser->event_buffer;
  ok = 1;
  while (ok && (*cursor != '\0')) {
    const char *value;
    size_t value_length;

    line_end = strchr(cursor, '\n');
    if (line_end == NULL) {
      line_end = cursor + strlen(cursor);
    }
    if (((size_t)(line_end - cursor) >= 5U) &&
        (memcmp(cursor, "data:", 5U) == 0)) {
      value = cursor + 5U;
      if ((value < line_end) && (*value == ' ')) {
        value++;
      }
      value_length = (size_t)(line_end - value);
      ok = strappy_sse_append_data(&data,
                                   &data_length,
                                   value,
                                   value_length,
                                   error_out);
    }
    cursor = (*line_end == '\0') ? line_end : line_end + 1;
  }
  parser->event_length = 0U;
  if (parser->event_buffer != NULL) {
    parser->event_buffer[0] = '\0';
  }
  if (!ok) {
    free(data);
    return 0;
  }
  if ((data == NULL) || (data[0] == '\0') ||
      (strcmp(data, "[DONE]") == 0)) {
    free(data);
    return 1;
  }
  event = cJSON_Parse(data);
  free(data);
  if (!cJSON_IsObject(event)) {
    cJSON_Delete(event);
    strappy_set_error(error_out, "ChatGPT SSE data is not valid JSON.");
    return 0;
  }
  parser->parsed_event_count++;
  type_value = cJSON_GetObjectItemCaseSensitive(event, "type");
  type = (cJSON_IsString(type_value) && (type_value->valuestring != NULL)) ?
    type_value->valuestring : NULL;
  if ((type != NULL) &&
      (strcmp(type, "response.output_item.done") == 0)) {
    ok = strappy_sse_store_completed_output(parser, event, error_out);
  } else if ((type != NULL) &&
      ((strcmp(type, "response.completed") == 0) ||
       (strcmp(type, "response.done") == 0) ||
       (strcmp(type, "response.incomplete") == 0) ||
       (strcmp(type, "response.failed") == 0) ||
       (strcmp(type, "error") == 0))) {
    ok = strappy_sse_store_terminal(parser, event, type, error_out);
  }
  cJSON_Delete(event);
  return ok;
}

void strappy_sse_parser_init(strappy_sse_parser *parser)
{
  if (parser == NULL) {
    return;
  }
  memset(parser, 0, sizeof(*parser));
}

void strappy_sse_parser_destroy(strappy_sse_parser *parser)
{
  if (parser == NULL) {
    return;
  }
  free(parser->event_buffer);
  cJSON_Delete((cJSON *)parser->completed_output);
  free(parser->terminal_json);
  strappy_sse_parser_init(parser);
}

int strappy_sse_parser_feed(strappy_sse_parser *parser,
                            const char *bytes,
                            size_t length,
                            char **error_out)
{
  size_t index;

  if ((parser == NULL) || ((bytes == NULL) && (length > 0U))) {
    strappy_set_error(error_out, "ChatGPT SSE parser input is invalid.");
    return 0;
  }
  if (parser->terminal_received) {
    return 1;
  }
  if ((parser->stream_bytes > (((size_t)-1) - length)) ||
      ((parser->stream_bytes + length) > STRAPPY_SSE_MAX_STREAM_BYTES)) {
    strappy_set_error(error_out, "ChatGPT SSE stream is too large.");
    return 0;
  }
  parser->stream_bytes += length;
  for (index = 0U; index < length; index++) {
    if (bytes[index] == '\r') {
      continue;
    }
    if ((bytes[index] == '\n') && (parser->event_length > 0U) &&
        (parser->event_buffer[parser->event_length - 1U] == '\n')) {
      parser->event_length--;
      parser->event_buffer[parser->event_length] = '\0';
      if (!strappy_sse_process_event(parser, error_out)) {
        return 0;
      }
      if (parser->terminal_received) {
        return 1;
      }
      continue;
    }
    if (!strappy_sse_append(parser, bytes[index], error_out)) {
      return 0;
    }
  }
  return 1;
}

int strappy_sse_parser_finish(strappy_sse_parser *parser,
                              char **error_out)
{
  if (parser == NULL) {
    strappy_set_error(error_out, "ChatGPT SSE parser is missing.");
    return 0;
  }
  if (!parser->terminal_received && (parser->event_length > 0U) &&
      !strappy_sse_process_event(parser, error_out)) {
    return 0;
  }
  if (!parser->terminal_received || (parser->terminal_json == NULL)) {
    strappy_set_error(error_out,
                      "ChatGPT SSE stream ended before a terminal response.");
    return 0;
  }
  return 1;
}

int strappy_sse_parser_is_terminal(const strappy_sse_parser *parser)
{
  return ((parser != NULL) && parser->terminal_received) ? 1 : 0;
}

int strappy_sse_parser_has_events(const strappy_sse_parser *parser)
{
  return ((parser != NULL) && (parser->parsed_event_count > 0U)) ? 1 : 0;
}

char *strappy_sse_parser_take_terminal_json(strappy_sse_parser *parser)
{
  char *json;

  if (parser == NULL) {
    return NULL;
  }
  json = parser->terminal_json;
  parser->terminal_json = NULL;
  return json;
}
