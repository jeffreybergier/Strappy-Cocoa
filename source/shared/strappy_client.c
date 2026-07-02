#include "strappy_client.h"

#include "strappy_core.h"
#include "strappy_tools.h"

#include <curl/curl.h>
#include <cJSON.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>

#define STRAPPY_CLIENT_GENERATION_METADATA_TIMEOUT_SECONDS 60L
#define STRAPPY_CLIENT_NON_STREAMING_TIMEOUT_SECONDS 300L
#define STRAPPY_CLIENT_STREAM_LOW_SPEED_TIME_SECONDS 60L

typedef struct strappy_http_buffer {
  char *data;
  size_t length;
} strappy_http_buffer;

static int strappy_curl_initialized = 0;
static char *strappy_cainfo_path = NULL;

static void strappy_http_buffer_init(strappy_http_buffer *buffer)
{
  if (buffer == NULL) {
    return;
  }

  buffer->data = NULL;
  buffer->length = 0U;
}

static void strappy_http_buffer_destroy(strappy_http_buffer *buffer)
{
  if (buffer == NULL) {
    return;
  }

  free(buffer->data);
  strappy_http_buffer_init(buffer);
}

static void strappy_http_buffer_clear(strappy_http_buffer *buffer)
{
  if (buffer == NULL) {
    return;
  }

  if (buffer->data != NULL) {
    buffer->data[0] = '\0';
  }
  buffer->length = 0U;
}

static int strappy_http_buffer_append(strappy_http_buffer *buffer,
                                      const char *data,
                                      size_t length)
{
  char *next_data;

  if ((buffer == NULL) || ((data == NULL) && (length > 0U))) {
    return 0;
  }

  if (buffer->length > (((size_t)-1) - length - 1U)) {
    return 0;
  }

  next_data = (char *)realloc(buffer->data, buffer->length + length + 1U);
  if (next_data == NULL) {
    return 0;
  }

  buffer->data = next_data;
  if (length > 0U) {
    memcpy(buffer->data + buffer->length, data, length);
  }
  buffer->length += length;
  buffer->data[buffer->length] = '\0';
  return 1;
}

static int strappy_http_buffer_append_cstring(strappy_http_buffer *buffer,
                                              const char *data)
{
  if (data == NULL) {
    return 1;
  }

  return strappy_http_buffer_append(buffer, data, strlen(data));
}

int strappy_client_set_cainfo(const char *path, char **error_out)
{
  char *copy;

  if ((path == NULL) || (path[0] == '\0')) {
    strappy_set_error(error_out, "CA certificate path is not configured.");
    return 0;
  }

  copy = strappy_string_duplicate(path);
  if (copy == NULL) {
    strappy_set_error(error_out, "Could not allocate CA certificate path.");
    return 0;
  }

  free(strappy_cainfo_path);
  strappy_cainfo_path = copy;
  return 1;
}

static int strappy_client_ensure_curl_initialized(char **error_out)
{
  CURLcode code;

  if (strappy_curl_initialized) {
    return 1;
  }

  code = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (code != CURLE_OK) {
    strappy_set_formatted_error(error_out,
                             "Could not initialize curl: %s",
                             curl_easy_strerror(code));
    return 0;
  }

  strappy_curl_initialized = 1;
  return 1;
}

static size_t strappy_client_write_callback(void *contents,
                                       size_t size,
                                       size_t nmemb,
                                       void *userp)
{
  strappy_http_buffer *buffer;
  size_t real_size;

  if ((contents == NULL) || (userp == NULL)) {
    return 0U;
  }

  if ((size != 0U) && (nmemb > (((size_t)-1) / size))) {
    return 0U;
  }

  real_size = size * nmemb;
  buffer = (strappy_http_buffer *)userp;

  if (!strappy_http_buffer_append(buffer, (const char *)contents, real_size)) {
    return 0U;
  }

  return real_size;
}

static int strappy_client_string_starts_with(const char *value,
                                             const char *prefix)
{
  size_t prefix_length;

  if ((value == NULL) || (prefix == NULL)) {
    return 0;
  }

  prefix_length = strlen(prefix);
  return (strncmp(value, prefix, prefix_length) == 0) ? 1 : 0;
}

static size_t strappy_client_header_callback(char *contents,
                                             size_t size,
                                             size_t nmemb,
                                             void *userp)
{
  strappy_chat_result *result;
  const char *header_name = "X-Generation-Id:";
  size_t real_size;
  size_t header_name_length;
  size_t start;
  size_t end;
  char *copy;

  if ((contents == NULL) || (userp == NULL)) {
    return 0U;
  }

  if ((size != 0U) && (nmemb > (((size_t)-1) / size))) {
    return 0U;
  }

  real_size = size * nmemb;
  header_name_length = strlen(header_name);
  if ((real_size <= header_name_length) ||
      (strncasecmp(contents, header_name, header_name_length) != 0)) {
    return real_size;
  }

  start = header_name_length;
  while ((start < real_size) &&
         isspace((unsigned char)contents[start])) {
    start++;
  }

  end = real_size;
  while ((end > start) &&
         isspace((unsigned char)contents[end - 1U])) {
    end--;
  }

  if (end <= start) {
    return real_size;
  }

  copy = strappy_string_duplicate_length(contents + start, end - start);
  if (copy == NULL) {
    return 0U;
  }

  result = (strappy_chat_result *)userp;
  free(result->response_id);
  result->response_id = copy;
  return real_size;
}

static int strappy_client_add_header(struct curl_slist **headers,
                            const char *header,
                            char **error_out)
{
  struct curl_slist *next_headers;

  next_headers = curl_slist_append(*headers, header);
  if (next_headers == NULL) {
    strappy_set_error(error_out, "Could not allocate OpenRouter HTTP headers.");
    return 0;
  }

  *headers = next_headers;
  return 1;
}

static char *strappy_join_strings(const char *first, const char *second)
{
  size_t first_length;
  size_t second_length;
  char *joined;

  if ((first == NULL) || (second == NULL)) {
    return NULL;
  }

  first_length = strlen(first);
  second_length = strlen(second);
  if (first_length > (((size_t)-1) - second_length - 1U)) {
    return NULL;
  }

  joined = (char *)malloc(first_length + second_length + 1U);
  if (joined == NULL) {
    return NULL;
  }

  memcpy(joined, first, first_length);
  memcpy(joined + first_length, second, second_length);
  joined[first_length + second_length] = '\0';

  return joined;
}

static int strappy_has_suffix(const char *value, const char *suffix)
{
  size_t value_length;
  size_t suffix_length;

  if ((value == NULL) || (suffix == NULL)) {
    return 0;
  }

  value_length = strlen(value);
  suffix_length = strlen(suffix);
  if (suffix_length > value_length) {
    return 0;
  }

  return strcmp(value + value_length - suffix_length, suffix) == 0;
}

static char *strappy_client_build_chat_url(const char *endpoint)
{
  const char *path = "/chat/completions";
  size_t endpoint_length;
  char *base_without_slash;
  char *url;

  if (endpoint == NULL) {
    return NULL;
  }

  if (strappy_has_suffix(endpoint, path)) {
    return strappy_string_duplicate(endpoint);
  }

  endpoint_length = strlen(endpoint);
  while ((endpoint_length > 0U) && (endpoint[endpoint_length - 1U] == '/')) {
    endpoint_length--;
  }

  base_without_slash = strappy_string_duplicate_length(endpoint, endpoint_length);
  if (base_without_slash == NULL) {
    return NULL;
  }

  url = strappy_join_strings(base_without_slash, path);
  free(base_without_slash);
  return url;
}

static char *strappy_client_build_openrouter_user_models_url(
  const char *endpoint)
{
  const char *api_marker = "/api/v1";
  const char *models_path = "/models/user";
  const char *default_path = "/api/v1/models/user";
  const char *marker;
  size_t endpoint_length;
  size_t base_length;
  char *base;
  char *url;

  if (endpoint == NULL) {
    return NULL;
  }

  marker = strstr(endpoint, api_marker);
  if (marker != NULL) {
    base_length = (size_t)(marker - endpoint) + strlen(api_marker);
    base = strappy_string_duplicate_length(endpoint, base_length);
    if (base == NULL) {
      return NULL;
    }
    url = strappy_join_strings(base, models_path);
    free(base);
    return url;
  }

  endpoint_length = strlen(endpoint);
  while ((endpoint_length > 0U) && (endpoint[endpoint_length - 1U] == '/')) {
    endpoint_length--;
  }

  base = strappy_string_duplicate_length(endpoint, endpoint_length);
  if (base == NULL) {
    return NULL;
  }

  url = strappy_join_strings(base, default_path);
  free(base);
  return url;
}

static int strappy_client_should_request_reasoning(const strappy_config *config)
{
  if ((config == NULL) || (config->api_endpoint == NULL)) {
    return 0;
  }

  return (strstr(config->api_endpoint, "openrouter.ai") != NULL) ? 1 : 0;
}

static int strappy_client_is_openrouter_endpoint(const strappy_config *config)
{
  if ((config == NULL) || (config->api_endpoint == NULL)) {
    return 0;
  }

  return (strstr(config->api_endpoint, "openrouter.ai") != NULL) ? 1 : 0;
}

static int strappy_client_should_request_stream_usage(const strappy_config *config)
{
  if ((config == NULL) || (config->api_endpoint == NULL)) {
    return 0;
  }

  return (strstr(config->api_endpoint, "api.openai.com") != NULL) ? 1 : 0;
}

static int strappy_client_replace_result_string(char **target,
                                                const char *value)
{
  char *copy;

  if (target == NULL) {
    return 0;
  }

  if (value == NULL) {
    return 1;
  }

  copy = strappy_string_duplicate(value);
  if (copy == NULL) {
    return 0;
  }

  free(*target);
  *target = copy;
  return 1;
}

static char *strappy_client_json_value_text(cJSON *value)
{
  if (value == NULL || cJSON_IsNull(value)) {
    return NULL;
  }

  if (cJSON_IsString(value) && (value->valuestring != NULL)) {
    return strappy_string_duplicate(value->valuestring);
  }

  if (cJSON_IsBool(value) || cJSON_IsNumber(value)) {
    return cJSON_PrintUnformatted(value);
  }

  return NULL;
}

static int strappy_client_metadata_append_text(strappy_http_buffer *buffer,
                                               const char *text)
{
  return strappy_http_buffer_append_cstring(buffer, text);
}

static int strappy_client_metadata_append_line(strappy_http_buffer *buffer,
                                               const char *label,
                                               const char *value)
{
  if ((buffer == NULL) || (label == NULL) || (value == NULL)) {
    return 1;
  }

  if (value[0] == '\0') {
    return 1;
  }

  if ((buffer->length > 0U) &&
      !strappy_client_metadata_append_text(buffer, "\n")) {
    return 0;
  }

  return strappy_client_metadata_append_text(buffer, label) &&
         strappy_client_metadata_append_text(buffer, ": ") &&
         strappy_client_metadata_append_text(buffer, value);
}

static int strappy_client_metadata_append_item(strappy_http_buffer *buffer,
                                               const char *label,
                                               cJSON *object,
                                               const char *key)
{
  cJSON *item;
  char *value;
  int ok;

  if ((object == NULL) || (key == NULL)) {
    return 1;
  }

  item = cJSON_GetObjectItem(object, key);
  value = strappy_client_json_value_text(item);
  if (value == NULL) {
    return 1;
  }

  ok = strappy_client_metadata_append_line(buffer, label, value);
  free(value);
  return ok;
}

static int strappy_client_metadata_append_nested_item(
  strappy_http_buffer *buffer,
  const char *label,
  cJSON *object,
  const char *parent_key,
  const char *key)
{
  cJSON *parent;

  if ((object == NULL) || (parent_key == NULL)) {
    return 1;
  }

  parent = cJSON_GetObjectItem(object, parent_key);
  if (!cJSON_IsObject(parent)) {
    return 1;
  }

  return strappy_client_metadata_append_item(buffer, label, parent, key);
}

static int strappy_client_reasoning_append_text(strappy_http_buffer *buffer,
                                                const char *text)
{
  if ((buffer == NULL) || (text == NULL) || (text[0] == '\0')) {
    return 1;
  }

  if ((buffer->length > 0U) &&
      !strappy_http_buffer_append_cstring(buffer, "\n")) {
    return 0;
  }

  return strappy_http_buffer_append_cstring(buffer, text);
}

static int strappy_client_reasoning_append_delta(strappy_http_buffer *buffer,
                                                 const char *text)
{
  if ((buffer == NULL) || (text == NULL) || (text[0] == '\0')) {
    return 1;
  }

  return strappy_http_buffer_append_cstring(buffer, text);
}

static int strappy_client_reasoning_append_details_text(
  strappy_http_buffer *buffer,
  cJSON *details)
{
  int count;
  int index;

  if (!cJSON_IsArray(details)) {
    return 1;
  }

  count = cJSON_GetArraySize(details);
  for (index = 0; index < count; index++) {
    cJSON *item;
    cJSON *text;
    cJSON *summary;

    item = cJSON_GetArrayItem(details, index);
    if (!cJSON_IsObject(item)) {
      continue;
    }

    text = cJSON_GetObjectItem(item, "text");
    if (cJSON_IsString(text) && (text->valuestring != NULL)) {
      if (!strappy_client_reasoning_append_text(buffer, text->valuestring)) {
        return 0;
      }
      continue;
    }

    summary = cJSON_GetObjectItem(item, "summary");
    if (cJSON_IsString(summary) && (summary->valuestring != NULL) &&
        !strappy_client_reasoning_append_text(buffer, summary->valuestring)) {
      return 0;
    }
  }

  return 1;
}

static const char *strappy_client_reasoning_detail_text(cJSON *detail)
{
  cJSON *text;
  cJSON *summary;

  if (!cJSON_IsObject(detail)) {
    return NULL;
  }

  text = cJSON_GetObjectItem(detail, "text");
  if (cJSON_IsString(text) && (text->valuestring != NULL)) {
    return text->valuestring;
  }

  summary = cJSON_GetObjectItem(detail, "summary");
  if (cJSON_IsString(summary) && (summary->valuestring != NULL)) {
    return summary->valuestring;
  }

  return NULL;
}

static char *strappy_client_reasoning_text_from_message(cJSON *message)
{
  strappy_http_buffer buffer;
  cJSON *reasoning;
  cJSON *reasoning_content;
  cJSON *reasoning_details;
  char *copy;
  int ok;

  if (!cJSON_IsObject(message)) {
    return NULL;
  }

  strappy_http_buffer_init(&buffer);
  ok = 1;

  reasoning = cJSON_GetObjectItem(message, "reasoning");
  if (cJSON_IsString(reasoning) && (reasoning->valuestring != NULL)) {
    ok = strappy_client_reasoning_append_text(&buffer, reasoning->valuestring);
  }

  reasoning_content = cJSON_GetObjectItem(message, "reasoning_content");
  if (ok && cJSON_IsString(reasoning_content) &&
      (reasoning_content->valuestring != NULL)) {
    if (!cJSON_IsString(reasoning) ||
        (reasoning->valuestring == NULL) ||
        (strcmp(reasoning->valuestring, reasoning_content->valuestring) != 0)) {
      ok = strappy_client_reasoning_append_text(&buffer,
                                                reasoning_content->valuestring);
    }
  }

  reasoning_details = cJSON_GetObjectItem(message, "reasoning_details");
  if (ok) {
    ok = strappy_client_reasoning_append_details_text(&buffer,
                                                      reasoning_details);
  }

  if (!ok || (buffer.data == NULL) || (buffer.length == 0U)) {
    strappy_http_buffer_destroy(&buffer);
    return NULL;
  }

  copy = strappy_string_duplicate(buffer.data);
  strappy_http_buffer_destroy(&buffer);
  return copy;
}

static int strappy_client_capture_json_string(char **target, cJSON *object, const char *key)
{
  cJSON *item;

  if ((target == NULL) || (object == NULL) || (key == NULL)) {
    return 1;
  }

  item = cJSON_GetObjectItem(object, key);
  if (cJSON_IsString(item) && (item->valuestring != NULL)) {
    return strappy_client_replace_result_string(target, item->valuestring);
  }

  return 1;
}

static int strappy_client_capture_response_id(cJSON *root,
                                              strappy_chat_result *result)
{
  cJSON *item;

  if ((root == NULL) || (result == NULL)) {
    return 1;
  }

  item = cJSON_GetObjectItem(root, "id");
  if (!cJSON_IsString(item) || (item->valuestring == NULL)) {
    return 1;
  }

  if ((result->response_id != NULL) &&
      strappy_client_string_starts_with(result->response_id, "gen-") &&
      !strappy_client_string_starts_with(item->valuestring, "gen-")) {
    return 1;
  }

  return strappy_client_replace_result_string(&result->response_id,
                                              item->valuestring);
}

static int strappy_client_capture_json_value(char **target, cJSON *object, const char *key)
{
  cJSON *item;
  char *value;
  int ok;

  if ((target == NULL) || (object == NULL) || (key == NULL)) {
    return 1;
  }

  item = cJSON_GetObjectItem(object, key);
  value = strappy_client_json_value_text(item);
  if (value == NULL) {
    return 1;
  }

  ok = strappy_client_replace_result_string(target, value);
  free(value);
  return ok;
}

static int strappy_client_capture_response_metadata(cJSON *root,
                                                    cJSON *choice,
                                                    strappy_chat_result *result)
{
  cJSON *usage;
  char *usage_json;

  if ((root == NULL) || (result == NULL)) {
    return 1;
  }

  if (!strappy_client_capture_response_id(root, result) ||
      !strappy_client_capture_json_value(&result->created, root, "created") ||
      !strappy_client_capture_json_string(&result->service_tier, root, "service_tier") ||
      !strappy_client_capture_json_string(&result->system_fingerprint,
                                          root,
                                          "system_fingerprint") ||
      !strappy_client_capture_json_string(&result->native_finish_reason,
                                          root,
                                          "native_finish_reason")) {
    return 0;
  }

  if (choice != NULL &&
      !strappy_client_capture_json_string(&result->native_finish_reason,
                                          choice,
                                          "native_finish_reason")) {
    return 0;
  }

  usage = cJSON_GetObjectItem(root, "usage");
  if (cJSON_IsObject(usage)) {
    usage_json = cJSON_PrintUnformatted(usage);
    if (usage_json != NULL) {
      free(result->usage_json);
      result->usage_json = usage_json;
    }
  }

  return 1;
}

static int strappy_client_capture_message_json(cJSON *message,
                                               strappy_chat_result *result)
{
  char *message_json;
  char *reasoning_text;

  if ((result == NULL) || !cJSON_IsObject(message)) {
    return 1;
  }

  message_json = cJSON_PrintUnformatted(message);
  if (message_json == NULL) {
    return 0;
  }

  free(result->message_json);
  result->message_json = message_json;

  reasoning_text = strappy_client_reasoning_text_from_message(message);
  if (reasoning_text != NULL) {
    free(result->reasoning_text);
    result->reasoning_text = reasoning_text;
  }

  return 1;
}

static int strappy_client_json_add_string_if_present(cJSON *object,
                                                    const char *key,
                                                    const char *value)
{
  if ((object == NULL) || (key == NULL) || (value == NULL) ||
      (value[0] == '\0')) {
    return 1;
  }

  return (cJSON_AddStringToObject(object, key, value) != NULL) ? 1 : 0;
}

static int strappy_client_json_add_number(cJSON *object,
                                          const char *key,
                                          long value)
{
  if ((object == NULL) || (key == NULL)) {
    return 1;
  }

  return (cJSON_AddNumberToObject(object, key, (double)value) != NULL) ? 1 : 0;
}

static int strappy_client_json_add_parsed_if_present(cJSON *object,
                                                     const char *key,
                                                     const char *json)
{
  cJSON *value;

  if ((object == NULL) || (key == NULL) || (json == NULL) ||
      (json[0] == '\0')) {
    return 1;
  }

  value = cJSON_Parse(json);
  if (value == NULL) {
    return 1;
  }

  if (!cJSON_AddItemToObject(object, key, value)) {
    cJSON_Delete(value);
    return 0;
  }

  return 1;
}

static int strappy_client_build_metadata_json(strappy_chat_result *result)
{
  cJSON *root;
  char *json;
  int ok;

  if (result == NULL) {
    return 1;
  }

  root = cJSON_CreateObject();
  if (root == NULL) {
    return 0;
  }

  ok = strappy_client_json_add_string_if_present(root,
                                                 "response_id",
                                                 result->response_id) &&
       strappy_client_json_add_string_if_present(root, "model", result->model) &&
       strappy_client_json_add_string_if_present(root,
                                                 "created",
                                                 result->created) &&
       strappy_client_json_add_string_if_present(root,
                                                 "finish_reason",
                                                 result->finish_reason) &&
       strappy_client_json_add_string_if_present(root,
                                                 "native_finish_reason",
                                                 result->native_finish_reason) &&
       strappy_client_json_add_string_if_present(root,
                                                 "service_tier",
                                                 result->service_tier) &&
       strappy_client_json_add_string_if_present(root,
                                                 "system_fingerprint",
                                                 result->system_fingerprint) &&
       strappy_client_json_add_number(root, "http_status", result->http_status) &&
       strappy_client_json_add_parsed_if_present(root,
                                                 "usage",
                                                 result->usage_json) &&
       strappy_client_json_add_parsed_if_present(
         root,
         "generation",
         result->generation_metadata_json);

  if (!ok) {
    cJSON_Delete(root);
    return 0;
  }

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL) {
    return 0;
  }

  free(result->metadata_json);
  result->metadata_json = json;
  return 1;
}

static int strappy_client_build_metadata_text(strappy_chat_result *result)
{
  strappy_http_buffer buffer;
  cJSON *usage_root;
  cJSON *generation_root;
  cJSON *generation_data;
  char *copy;
  int ok;

  if (result == NULL) {
    return 1;
  }

  strappy_http_buffer_init(&buffer);
  ok = strappy_client_metadata_append_line(&buffer,
                                           "Response ID",
                                           result->response_id) &&
       strappy_client_metadata_append_line(&buffer, "Model", result->model) &&
       strappy_client_metadata_append_line(&buffer, "Created", result->created) &&
       strappy_client_metadata_append_line(&buffer,
                                           "Finish reason",
                                           result->finish_reason) &&
       strappy_client_metadata_append_line(&buffer,
                                           "Native finish reason",
                                           result->native_finish_reason) &&
       strappy_client_metadata_append_line(&buffer,
                                           "Service tier",
                                           result->service_tier) &&
       strappy_client_metadata_append_line(&buffer,
                                           "System fingerprint",
                                           result->system_fingerprint);

  usage_root = NULL;
  if (ok && (result->usage_json != NULL) && (result->usage_json[0] != '\0')) {
    usage_root = cJSON_Parse(result->usage_json);
    if (usage_root != NULL) {
      ok = strappy_client_metadata_append_item(&buffer,
                                               "Cost",
                                               usage_root,
                                               "cost") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Prompt tokens",
                                               usage_root,
                                               "prompt_tokens") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Completion tokens",
                                               usage_root,
                                               "completion_tokens") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Total tokens",
                                               usage_root,
                                               "total_tokens") &&
           strappy_client_metadata_append_nested_item(
             &buffer,
             "Cached prompt tokens",
             usage_root,
             "prompt_tokens_details",
             "cached_tokens") &&
           strappy_client_metadata_append_nested_item(
             &buffer,
             "Cache write tokens",
             usage_root,
             "prompt_tokens_details",
             "cache_write_tokens") &&
           strappy_client_metadata_append_nested_item(
             &buffer,
             "Prompt audio tokens",
             usage_root,
             "prompt_tokens_details",
             "audio_tokens") &&
           strappy_client_metadata_append_nested_item(
             &buffer,
             "Reasoning tokens",
             usage_root,
             "completion_tokens_details",
             "reasoning_tokens") &&
           strappy_client_metadata_append_nested_item(
             &buffer,
             "Completion audio tokens",
             usage_root,
             "completion_tokens_details",
             "audio_tokens") &&
           strappy_client_metadata_append_nested_item(
             &buffer,
             "Accepted prediction tokens",
             usage_root,
             "completion_tokens_details",
             "accepted_prediction_tokens") &&
           strappy_client_metadata_append_nested_item(
             &buffer,
             "Rejected prediction tokens",
             usage_root,
             "completion_tokens_details",
             "rejected_prediction_tokens") &&
           strappy_client_metadata_append_nested_item(
             &buffer,
             "Upstream inference cost",
             usage_root,
             "cost_details",
             "upstream_inference_cost");
      cJSON_Delete(usage_root);
    }
  }

  generation_root = NULL;
  if (ok &&
      (result->generation_metadata_json != NULL) &&
      (result->generation_metadata_json[0] != '\0')) {
    generation_root = cJSON_Parse(result->generation_metadata_json);
    if (generation_root != NULL) {
      generation_data = cJSON_GetObjectItem(generation_root, "data");
      if (!cJSON_IsObject(generation_data)) {
        generation_data = generation_root;
      }

      ok = strappy_client_metadata_append_item(&buffer,
                                               "Provider",
                                               generation_data,
                                               "provider_name") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Router",
                                               generation_data,
                                               "router") &&
           strappy_client_metadata_append_item(&buffer,
                                               "OpenRouter request ID",
                                               generation_data,
                                               "request_id") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Upstream ID",
                                               generation_data,
                                               "upstream_id") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Streamed",
                                               generation_data,
                                               "streamed") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Data region",
                                               generation_data,
                                               "data_region") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Latency (ms)",
                                               generation_data,
                                               "latency") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Generation time (ms)",
                                               generation_data,
                                               "generation_time") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Moderation latency (ms)",
                                               generation_data,
                                               "moderation_latency") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Prompt tokens",
                                               generation_data,
                                               "tokens_prompt") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Completion tokens",
                                               generation_data,
                                               "tokens_completion") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Reasoning tokens",
                                               generation_data,
                                               "tokens_reasoning") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Native prompt tokens",
                                               generation_data,
                                               "native_tokens_prompt") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Native completion tokens",
                                               generation_data,
                                               "native_tokens_completion") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Native reasoning tokens",
                                               generation_data,
                                               "native_tokens_reasoning") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Native cached tokens",
                                               generation_data,
                                               "native_tokens_cached") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Total cost",
                                               generation_data,
                                               "total_cost") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Usage",
                                               generation_data,
                                               "usage") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Upstream inference cost",
                                               generation_data,
                                               "upstream_inference_cost") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Generation finish reason",
                                               generation_data,
                                               "finish_reason") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Generation native finish reason",
                                               generation_data,
                                               "native_finish_reason") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Web search engine",
                                               generation_data,
                                               "web_search_engine") &&
           strappy_client_metadata_append_item(&buffer,
                                               "Search results",
                                               generation_data,
                                               "num_search_results");
      cJSON_Delete(generation_root);
    }
  }

  if (!ok) {
    strappy_http_buffer_destroy(&buffer);
    return 0;
  }

  free(result->metadata_text);
  result->metadata_text = NULL;
  if ((buffer.data != NULL) && (buffer.length > 0U)) {
    copy = strappy_string_duplicate(buffer.data);
    if (copy == NULL) {
      strappy_http_buffer_destroy(&buffer);
      return 0;
    }
    result->metadata_text = copy;
  }

  strappy_http_buffer_destroy(&buffer);
  return 1;
}

static int strappy_client_append_json_string(strappy_http_buffer *buffer,
                                             const char *value)
{
  const unsigned char *cursor;
  char escaped[8];

  if (!strappy_http_buffer_append_cstring(buffer, "\"")) {
    return 0;
  }

  if (value != NULL) {
    cursor = (const unsigned char *)value;
    while (*cursor != '\0') {
      if (*cursor == '"') {
        if (!strappy_http_buffer_append_cstring(buffer, "\\\"")) {
          return 0;
        }
      } else if (*cursor == '\\') {
        if (!strappy_http_buffer_append_cstring(buffer, "\\\\")) {
          return 0;
        }
      } else if (*cursor == '\b') {
        if (!strappy_http_buffer_append_cstring(buffer, "\\b")) {
          return 0;
        }
      } else if (*cursor == '\f') {
        if (!strappy_http_buffer_append_cstring(buffer, "\\f")) {
          return 0;
        }
      } else if (*cursor == '\n') {
        if (!strappy_http_buffer_append_cstring(buffer, "\\n")) {
          return 0;
        }
      } else if (*cursor == '\r') {
        if (!strappy_http_buffer_append_cstring(buffer, "\\r")) {
          return 0;
        }
      } else if (*cursor == '\t') {
        if (!strappy_http_buffer_append_cstring(buffer, "\\t")) {
          return 0;
        }
      } else if (*cursor < 32U) {
        snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned int)*cursor);
        if (!strappy_http_buffer_append_cstring(buffer, escaped)) {
          return 0;
        }
      } else if (!strappy_http_buffer_append(buffer, (const char *)cursor, 1U)) {
        return 0;
      }
      cursor++;
    }
  }

  return strappy_http_buffer_append_cstring(buffer, "\"");
}

static int strappy_client_is_json_space(char character)
{
  return (character == ' ') ||
         (character == '\t') ||
         (character == '\n') ||
         (character == '\r');
}

static int strappy_client_json_object_bounds(const char *json,
                                             const char **start_out,
                                             size_t *length_out)
{
  const char *start;
  const char *end;

  if ((json == NULL) || (start_out == NULL) || (length_out == NULL)) {
    return 0;
  }

  start = json;
  while ((*start != '\0') && strappy_client_is_json_space(*start)) {
    start++;
  }

  end = start + strlen(start);
  while ((end > start) && strappy_client_is_json_space(*(end - 1))) {
    end--;
  }

  if ((end <= start) || (*start != '{') || (*(end - 1) != '}')) {
    return 0;
  }

  *start_out = start;
  *length_out = (size_t)(end - start);
  return 1;
}

static int strappy_client_append_basic_message_json(
  strappy_http_buffer *buffer,
  const strappy_chat_message *message)
{
  if ((message == NULL) ||
      (message->role == NULL) ||
      (message->role[0] == '\0') ||
      (message->content == NULL)) {
    return 0;
  }

  return strappy_http_buffer_append_cstring(buffer, "{\"role\":") &&
         strappy_client_append_json_string(buffer, message->role) &&
         strappy_http_buffer_append_cstring(buffer, ",\"content\":") &&
         strappy_client_append_json_string(buffer, message->content) &&
         strappy_http_buffer_append_cstring(buffer, "}");
}

static const char *strappy_client_tool_call_name(cJSON *tool_call)
{
  cJSON *function;
  cJSON *name;

  if (!cJSON_IsObject(tool_call)) {
    return NULL;
  }

  function = cJSON_GetObjectItem(tool_call, "function");
  if (!cJSON_IsObject(function)) {
    return NULL;
  }

  name = cJSON_GetObjectItem(function, "name");
  if (!cJSON_IsString(name) || (name->valuestring == NULL) ||
      (name->valuestring[0] == '\0')) {
    return NULL;
  }

  return name->valuestring;
}

static char *strappy_client_tool_calls_display_text(cJSON *tool_calls)
{
  strappy_http_buffer buffer;
  const char *name;
  int count;
  int index;
  int visible_count;
  char *text;

  if (!cJSON_IsArray(tool_calls)) {
    return NULL;
  }

  count = cJSON_GetArraySize(tool_calls);
  if (count <= 0) {
    return NULL;
  }

  strappy_http_buffer_init(&buffer);
  visible_count = 0;
  for (index = 0; index < count; index++) {
    name = strappy_client_tool_call_name(cJSON_GetArrayItem(tool_calls, index));
    if (name == NULL) {
      name = "unknown";
    }

    if (visible_count == 0) {
      if (!strappy_http_buffer_append_cstring(&buffer,
                                              "Tool call requested: ")) {
        strappy_http_buffer_destroy(&buffer);
        return NULL;
      }
    } else if (!strappy_http_buffer_append_cstring(&buffer, "\nTool call requested: ")) {
      strappy_http_buffer_destroy(&buffer);
      return NULL;
    }

    if (!strappy_http_buffer_append_cstring(&buffer, name)) {
      strappy_http_buffer_destroy(&buffer);
      return NULL;
    }

    visible_count++;
  }

  if ((buffer.data == NULL) || (buffer.length == 0U)) {
    strappy_http_buffer_destroy(&buffer);
    return NULL;
  }

  text = buffer.data;
  buffer.data = NULL;
  buffer.length = 0U;
  strappy_http_buffer_destroy(&buffer);
  return text;
}

static char *strappy_client_text_with_tool_calls_display(const char *content,
                                                         cJSON *tool_calls)
{
  char *tool_text;
  char *prefix;
  char *combined;

  tool_text = strappy_client_tool_calls_display_text(tool_calls);
  if (tool_text == NULL) {
    return NULL;
  }

  if ((content == NULL) || (content[0] == '\0')) {
    return tool_text;
  }

  prefix = strappy_join_strings(content, "\n\n");
  if (prefix == NULL) {
    free(tool_text);
    return NULL;
  }

  combined = strappy_join_strings(prefix, tool_text);
  free(prefix);
  free(tool_text);
  return combined;
}

static char *strappy_client_build_messages_request_json(
                                     const strappy_config *config,
                                     const strappy_chat_message *chat_messages,
                                     size_t chat_message_count,
                                     int should_stream,
                                     char **error_out)
{
  strappy_http_buffer buffer;
  char *json;
  char *tools_json;
  size_t index;
  int ok;

  if ((config == NULL) ||
      (config->api_model == NULL) ||
      (config->api_model[0] == '\0') ||
      ((chat_messages == NULL) && (chat_message_count > 0U))) {
    strappy_set_error(error_out, "OpenRouter request is incomplete.");
    return NULL;
  }

  if ((config->guidance_resource_dir != NULL) &&
      (config->guidance_resource_dir[0] != '\0')) {
    if ((config->tool_allowlist != NULL) &&
        (config->tool_allowlist_count > 0U)) {
      tools_json =
        strappy_tools_request_json_filtered(config->guidance_resource_dir,
                                            config->tool_allowlist,
                                            config->tool_allowlist_count,
                                            error_out);
    } else {
      tools_json = strappy_tools_request_json(config->guidance_resource_dir,
                                              error_out);
    }
  } else {
    tools_json = strappy_string_duplicate("");
    if (tools_json == NULL) {
      strappy_set_error(error_out, "Could not allocate empty tool schema list.");
    }
  }
  if (tools_json == NULL) {
    return NULL;
  }

  strappy_http_buffer_init(&buffer);
  ok = strappy_http_buffer_append_cstring(&buffer, "{\"model\":") &&
       strappy_client_append_json_string(&buffer, config->api_model) &&
       strappy_http_buffer_append_cstring(&buffer, ",\"stream\":") &&
       strappy_http_buffer_append_cstring(&buffer,
                                          should_stream ? "true" : "false");

  if (ok && strappy_client_should_request_reasoning(config)) {
    ok = strappy_http_buffer_append_cstring(
      &buffer,
      ",\"reasoning\":{\"enabled\":true,\"exclude\":false},"
      "\"include_reasoning\":true");
  }

  if (ok && should_stream && strappy_client_should_request_stream_usage(config)) {
    ok = strappy_http_buffer_append_cstring(
      &buffer,
      ",\"stream_options\":{\"include_usage\":true}");
  }

  if (ok && (tools_json[0] != '\0')) {
    ok = strappy_http_buffer_append_cstring(&buffer, ",\"tools\":") &&
         strappy_http_buffer_append_cstring(&buffer, tools_json);
  }

  ok = ok && strappy_http_buffer_append_cstring(&buffer, ",\"messages\":[");

  for (index = 0U; ok && (index < chat_message_count); index++) {
    const char *raw_message;
    size_t raw_message_length;

    if (index > 0U) {
      ok = strappy_http_buffer_append_cstring(&buffer, ",");
    }
    if (!ok) {
      break;
    }

    /* Stored JSON columns are opaque on read; splice saved message objects
       without round-tripping them through cJSON. */
    raw_message = NULL;
    raw_message_length = 0U;
    if (strappy_client_json_object_bounds(chat_messages[index].message_json,
                                          &raw_message,
                                          &raw_message_length)) {
      ok = strappy_http_buffer_append(&buffer,
                                      raw_message,
                                      raw_message_length);
    } else {
      ok = strappy_client_append_basic_message_json(&buffer,
                                                   &chat_messages[index]);
    }
  }

  ok = ok && strappy_http_buffer_append_cstring(&buffer, "]}");
  free(tools_json);
  if (!ok) {
    strappy_http_buffer_destroy(&buffer);
    strappy_set_error(error_out, "Could not build OpenRouter JSON request.");
    return NULL;
  }

  json = buffer.data;
  buffer.data = NULL;
  buffer.length = 0U;
  strappy_http_buffer_destroy(&buffer);
  return json;
}

static char *strappy_client_extract_api_error(cJSON *root)
{
  cJSON *error;
  cJSON *message;

  if (root == NULL) {
    return NULL;
  }

  error = cJSON_GetObjectItem(root, "error");
  if (cJSON_IsString(error) && (error->valuestring != NULL)) {
    return strappy_string_duplicate(error->valuestring);
  }

  if (!cJSON_IsObject(error)) {
    return NULL;
  }

  message = cJSON_GetObjectItem(error, "message");
  if (cJSON_IsString(message) && (message->valuestring != NULL)) {
    return strappy_string_duplicate(message->valuestring);
  }

  return NULL;
}

static char *strappy_client_extract_assistant_content(cJSON *content)
{
  cJSON *item;
  char *combined;
  int count;
  int index;

  if (cJSON_IsString(content) && (content->valuestring != NULL)) {
    return strappy_string_duplicate(content->valuestring);
  }

  if (!cJSON_IsArray(content)) {
    return NULL;
  }

  combined = strappy_string_duplicate("");
  if (combined == NULL) {
    return NULL;
  }

  count = cJSON_GetArraySize(content);
  for (index = 0; index < count; index++) {
    cJSON *text;
    char *next_combined;

    item = cJSON_GetArrayItem(content, index);
    if (!cJSON_IsObject(item)) {
      continue;
    }

    text = cJSON_GetObjectItem(item, "text");
    if (!cJSON_IsString(text) || (text->valuestring == NULL)) {
      continue;
    }

    next_combined = strappy_join_strings(combined, text->valuestring);
    free(combined);
    combined = next_combined;
    if (combined == NULL) {
      return NULL;
    }
  }

  if (combined[0] == '\0') {
    free(combined);
    return NULL;
  }

  return combined;
}

static int strappy_client_parse_response(const char *json,
                                          long http_status,
                                          strappy_chat_result *result,
                                          char **error_out)
{
  cJSON *root;
  cJSON *choices;
  cJSON *first_choice;
  cJSON *message;
  cJSON *content;
  cJSON *tool_calls;
  cJSON *finish_reason;
  cJSON *model;
  char *api_error;
  char *response_text;
  char *raw_json;
  char *finish_reason_text;
  char *model_text;
  char *tool_display_text;
  int has_tool_calls;

  if ((json == NULL) || (json[0] == '\0')) {
    strappy_set_error(error_out, "OpenRouter returned an empty response.");
    return 0;
  }

  root = cJSON_Parse(json);
  if (root == NULL) {
    strappy_set_error(error_out, "OpenRouter returned invalid JSON.");
    return 0;
  }

  api_error = strappy_client_extract_api_error(root);
  if ((http_status < 200L) || (http_status >= 300L)) {
    if (api_error != NULL) {
      strappy_set_formatted_error(error_out,
                               "OpenRouter request failed with HTTP %ld: %s",
                               http_status,
                               api_error);
      free(api_error);
    } else {
      strappy_set_formatted_error(error_out,
                               "OpenRouter request failed with HTTP %ld.",
                               http_status);
    }
    cJSON_Delete(root);
    return 0;
  }
  free(api_error);

  choices = cJSON_GetObjectItem(root, "choices");
  first_choice = cJSON_GetArrayItem(choices, 0);
  message = cJSON_GetObjectItem(first_choice, "message");
  content = cJSON_GetObjectItem(message, "content");
  tool_calls = cJSON_GetObjectItem(message, "tool_calls");
  has_tool_calls =
    (cJSON_IsArray(tool_calls) && (cJSON_GetArraySize(tool_calls) > 0)) ? 1 : 0;

  response_text = strappy_client_extract_assistant_content(content);
  if (has_tool_calls) {
    tool_display_text =
      strappy_client_text_with_tool_calls_display(response_text, tool_calls);
    free(response_text);
    response_text = tool_display_text;
    if (response_text == NULL) {
      cJSON_Delete(root);
      strappy_set_error(error_out, "Could not allocate assistant response.");
      return 0;
    }
  } else if (response_text == NULL) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "OpenRouter response did not contain text.");
    return 0;
  }

  raw_json = strappy_string_duplicate(json);
  if (raw_json == NULL) {
    free(response_text);
    cJSON_Delete(root);
    strappy_set_error(error_out, "Could not allocate OpenRouter raw response.");
    return 0;
  }

  finish_reason = cJSON_GetObjectItem(first_choice, "finish_reason");
  finish_reason_text = NULL;
  if (cJSON_IsString(finish_reason) && (finish_reason->valuestring != NULL)) {
    finish_reason_text = strappy_string_duplicate(finish_reason->valuestring);
    if (finish_reason_text == NULL) {
      free(response_text);
      free(raw_json);
      cJSON_Delete(root);
      strappy_set_error(error_out, "Could not allocate finish reason.");
      return 0;
    }
  }

  model = cJSON_GetObjectItem(root, "model");
  model_text = NULL;
  if (cJSON_IsString(model) && (model->valuestring != NULL)) {
    model_text = strappy_string_duplicate(model->valuestring);
    if (model_text == NULL) {
      free(response_text);
      free(raw_json);
      free(finish_reason_text);
      cJSON_Delete(root);
      strappy_set_error(error_out, "Could not allocate response model.");
      return 0;
    }
  }

  if (!strappy_client_capture_response_metadata(root, first_choice, result)) {
    free(response_text);
    free(raw_json);
    free(finish_reason_text);
    free(model_text);
    cJSON_Delete(root);
    strappy_set_error(error_out, "Could not allocate response metadata.");
    return 0;
  }

  if (!strappy_client_capture_message_json(message, result)) {
    free(response_text);
    free(raw_json);
    free(finish_reason_text);
    free(model_text);
    cJSON_Delete(root);
    strappy_set_error(error_out, "Could not allocate assistant message JSON.");
    return 0;
  }

  result->response_text = response_text;
  result->raw_json = raw_json;
  result->finish_reason = finish_reason_text;
  result->model = model_text;
  result->http_status = http_status;

  cJSON_Delete(root);
  return 1;
}

typedef struct strappy_stream_context {
  strappy_http_buffer raw_body;
  strappy_http_buffer line_buffer;
  strappy_http_buffer event_data;
  strappy_http_buffer content;
  strappy_http_buffer reasoning;
  cJSON *reasoning_details;
  cJSON *tool_calls;
  int *tool_call_stream_indices;
  size_t tool_call_stream_index_count;
  strappy_chat_result *result;
  strappy_chat_stream_callback callback;
  void *callback_data;
  char *message_role;
  char *stream_error;
  time_t started_at;
  int saw_event;
  int done;
  int cancelled;
} strappy_stream_context;

static void strappy_stream_context_init(strappy_stream_context *context)
{
  if (context == NULL) {
    return;
  }

  strappy_http_buffer_init(&context->raw_body);
  strappy_http_buffer_init(&context->line_buffer);
  strappy_http_buffer_init(&context->event_data);
  strappy_http_buffer_init(&context->content);
  strappy_http_buffer_init(&context->reasoning);
  context->reasoning_details = NULL;
  context->tool_calls = NULL;
  context->tool_call_stream_indices = NULL;
  context->tool_call_stream_index_count = 0U;
  context->result = NULL;
  context->callback = NULL;
  context->callback_data = NULL;
  context->message_role = NULL;
  context->stream_error = NULL;
  context->started_at = 0;
  context->saw_event = 0;
  context->done = 0;
  context->cancelled = 0;
}

static void strappy_stream_context_destroy(strappy_stream_context *context)
{
  if (context == NULL) {
    return;
  }

  strappy_http_buffer_destroy(&context->raw_body);
  strappy_http_buffer_destroy(&context->line_buffer);
  strappy_http_buffer_destroy(&context->event_data);
  strappy_http_buffer_destroy(&context->content);
  strappy_http_buffer_destroy(&context->reasoning);
  cJSON_Delete(context->reasoning_details);
  cJSON_Delete(context->tool_calls);
  free(context->tool_call_stream_indices);
  free(context->message_role);
  free(context->stream_error);
  strappy_stream_context_init(context);
}

static int strappy_client_stream_set_error(strappy_stream_context *context,
                                           const char *message)
{
  if (context == NULL) {
    return 0;
  }

  free(context->stream_error);
  context->stream_error = strappy_string_duplicate(message);
  if (context->stream_error == NULL) {
    context->stream_error = strappy_string_duplicate("OpenRouter stream failed.");
  }
  return 0;
}

static int strappy_client_stream_mark_cancelled(strappy_stream_context *context)
{
  if (context == NULL) {
    return 0;
  }

  context->cancelled = 1;
  if (context->result != NULL) {
    context->result->cancelled = 1;
  }
  return 0;
}

static int strappy_client_stream_set_formatted_error(
  strappy_stream_context *context,
  const char *format,
  const char *detail)
{
  char buffer[1024];
  int result;

  if (detail == NULL) {
    detail = "";
  }

  result = snprintf(buffer, sizeof(buffer), format, detail);
  if (result < 0) {
    return strappy_client_stream_set_error(context, "OpenRouter stream failed.");
  }

  buffer[sizeof(buffer) - 1U] = '\0';
  return strappy_client_stream_set_error(context, buffer);
}

static int strappy_client_stream_replace_result_string(char **target,
                                                       const char *value,
                                                       strappy_stream_context *context)
{
  char *copy;

  if (target == NULL) {
    return strappy_client_stream_set_error(context, "OpenRouter stream result is incomplete.");
  }

  if (value == NULL) {
    return 1;
  }

  copy = strappy_string_duplicate(value);
  if (copy == NULL) {
    return strappy_client_stream_set_error(context, "Could not allocate OpenRouter stream result.");
  }

  free(*target);
  *target = copy;
  return 1;
}

static int strappy_client_stream_replace_context_string(
  char **target,
  const char *value,
  strappy_stream_context *context)
{
  char *copy;

  if (target == NULL) {
    return strappy_client_stream_set_error(context, "OpenRouter stream context is incomplete.");
  }

  if (value == NULL) {
    return 1;
  }

  copy = strappy_string_duplicate(value);
  if (copy == NULL) {
    return strappy_client_stream_set_error(context, "Could not allocate OpenRouter stream context.");
  }

  free(*target);
  *target = copy;
  return 1;
}

static cJSON *strappy_client_stream_ensure_object_member(cJSON *object,
                                                        const char *key)
{
  cJSON *member;

  if ((object == NULL) || (key == NULL)) {
    return NULL;
  }

  member = cJSON_GetObjectItem(object, key);
  if (cJSON_IsObject(member)) {
    return member;
  }

  member = cJSON_CreateObject();
  if (member == NULL) {
    return NULL;
  }

  cJSON_DeleteItemFromObjectCaseSensitive(object, key);
  if (!cJSON_AddItemToObject(object, key, member)) {
    cJSON_Delete(member);
    return NULL;
  }

  return member;
}

static int strappy_client_json_replace_string(cJSON *object,
                                              const char *key,
                                              const char *value)
{
  cJSON *item;

  if ((object == NULL) || (key == NULL) || (value == NULL)) {
    return 1;
  }

  item = cJSON_CreateString(value);
  if (item == NULL) {
    return 0;
  }

  cJSON_DeleteItemFromObjectCaseSensitive(object, key);
  if (!cJSON_AddItemToObject(object, key, item)) {
    cJSON_Delete(item);
    return 0;
  }

  return 1;
}

static int strappy_client_json_append_string_member(cJSON *object,
                                                   const char *key,
                                                   const char *value)
{
  cJSON *existing;
  char *combined;
  int ok;

  if ((object == NULL) || (key == NULL) || (value == NULL) ||
      (value[0] == '\0')) {
    return 1;
  }

  existing = cJSON_GetObjectItem(object, key);
  if (!cJSON_IsString(existing) || (existing->valuestring == NULL)) {
    return strappy_client_json_replace_string(object, key, value);
  }

  combined = strappy_join_strings(existing->valuestring, value);
  if (combined == NULL) {
    return 0;
  }

  ok = strappy_client_json_replace_string(object, key, combined);
  free(combined);
  return ok;
}

static int strappy_client_stream_tool_call_count(
  const strappy_stream_context *context)
{
  if ((context == NULL) || (context->tool_calls == NULL)) {
    return 0;
  }

  return cJSON_GetArraySize(context->tool_calls);
}

static int strappy_client_stream_find_tool_call_index_by_stream_index(
  const strappy_stream_context *context,
  int stream_index)
{
  size_t index;

  if ((context == NULL) || (context->tool_call_stream_indices == NULL)) {
    return -1;
  }

  for (index = 0U; index < context->tool_call_stream_index_count; index++) {
    if (context->tool_call_stream_indices[index] == stream_index) {
      return (int)index;
    }
  }

  return -1;
}

static int strappy_client_stream_find_tool_call_index_by_id(
  const strappy_stream_context *context,
  const char *id)
{
  int count;
  int index;

  if ((context == NULL) || (context->tool_calls == NULL) ||
      (id == NULL) || (id[0] == '\0')) {
    return -1;
  }

  count = cJSON_GetArraySize(context->tool_calls);
  for (index = 0; index < count; index++) {
    cJSON *call;
    cJSON *existing_id;

    call = cJSON_GetArrayItem(context->tool_calls, index);
    existing_id = cJSON_GetObjectItem(call, "id");
    if (cJSON_IsString(existing_id) &&
        (existing_id->valuestring != NULL) &&
        (strcmp(existing_id->valuestring, id) == 0)) {
      return index;
    }
  }

  return -1;
}

static cJSON *strappy_client_stream_append_tool_call(
  strappy_stream_context *context,
  int stream_index)
{
  cJSON *call;
  int *next_indices;
  size_t next_count;

  if (context == NULL) {
    return NULL;
  }

  if (context->tool_calls == NULL) {
    context->tool_calls = cJSON_CreateArray();
    if (context->tool_calls == NULL) {
      return NULL;
    }
  }

  if (context->tool_call_stream_index_count >
      (((size_t)-1) / sizeof(int)) - 1U) {
    return NULL;
  }

  next_count = context->tool_call_stream_index_count + 1U;
  next_indices = (int *)realloc(context->tool_call_stream_indices,
                                next_count * sizeof(int));
  if (next_indices == NULL) {
    return NULL;
  }
  context->tool_call_stream_indices = next_indices;

  call = cJSON_CreateObject();
  if (!cJSON_IsObject(call)) {
    return NULL;
  }
  if (!cJSON_AddItemToArray(context->tool_calls, call)) {
    cJSON_Delete(call);
    return NULL;
  }

  context->tool_call_stream_indices[context->tool_call_stream_index_count] =
    stream_index;
  context->tool_call_stream_index_count = next_count;

  return call;
}

static cJSON *strappy_client_stream_get_tool_call(
  strappy_stream_context *context,
  int stream_index,
  int has_stream_index,
  int position,
  const char *id)
{
  int target_index;
  int count;

  target_index =
    strappy_client_stream_find_tool_call_index_by_id(context, id);
  if ((target_index < 0) && has_stream_index) {
    target_index =
      strappy_client_stream_find_tool_call_index_by_stream_index(
        context,
        stream_index);
  }
  if ((target_index < 0) && !has_stream_index) {
    count = strappy_client_stream_tool_call_count(context);
    if ((position >= 0) && (position < count)) {
      target_index = position;
    }
  }

  if (target_index >= 0) {
    return cJSON_GetArrayItem(context->tool_calls, target_index);
  }

  return strappy_client_stream_append_tool_call(
    context,
    has_stream_index ? stream_index : position);
}

static int strappy_client_stream_capture_tool_calls(
  strappy_stream_context *context,
  cJSON *tool_calls)
{
  int count;
  int position;

  if (!cJSON_IsArray(tool_calls)) {
    return 1;
  }

  count = cJSON_GetArraySize(tool_calls);
  for (position = 0; position < count; position++) {
    cJSON *delta;
    cJSON *index_item;
    cJSON *target;
    cJSON *function_delta;
    cJSON *target_function;
    cJSON *id;
    cJSON *type;
    cJSON *name;
    cJSON *arguments;
    int index;
    int has_index;
    const char *id_text;

    delta = cJSON_GetArrayItem(tool_calls, position);
    if (!cJSON_IsObject(delta)) {
      continue;
    }

    index = position;
    has_index = 0;
    index_item = cJSON_GetObjectItem(delta, "index");
    if (cJSON_IsNumber(index_item) && (index_item->valueint >= 0)) {
      index = index_item->valueint;
      has_index = 1;
    }

    id = cJSON_GetObjectItem(delta, "id");
    id_text = (cJSON_IsString(id) && (id->valuestring != NULL)) ?
      id->valuestring : NULL;

    target = strappy_client_stream_get_tool_call(context,
                                                 index,
                                                 has_index,
                                                 position,
                                                 id_text);
    if (target == NULL) {
      return strappy_client_stream_set_error(context, "Could not allocate streamed tool calls.");
    }

    if ((id_text != NULL) &&
        !strappy_client_json_replace_string(target, "id", id_text)) {
      return strappy_client_stream_set_error(context, "Could not allocate streamed tool call id.");
    }

    type = cJSON_GetObjectItem(delta, "type");
    if (cJSON_IsString(type) && (type->valuestring != NULL) &&
        !strappy_client_json_replace_string(target, "type", type->valuestring)) {
      return strappy_client_stream_set_error(context, "Could not allocate streamed tool call type.");
    }

    function_delta = cJSON_GetObjectItem(delta, "function");
    if (!cJSON_IsObject(function_delta)) {
      continue;
    }

    target_function = strappy_client_stream_ensure_object_member(target,
                                                                "function");
    if (target_function == NULL) {
      return strappy_client_stream_set_error(context, "Could not allocate streamed function call.");
    }

    name = cJSON_GetObjectItem(function_delta, "name");
    if (cJSON_IsString(name) && (name->valuestring != NULL) &&
        !strappy_client_json_replace_string(target_function,
                                            "name",
                                            name->valuestring)) {
      return strappy_client_stream_set_error(context, "Could not allocate streamed function name.");
    }

    arguments = cJSON_GetObjectItem(function_delta, "arguments");
    if (cJSON_IsString(arguments) && (arguments->valuestring != NULL) &&
        !strappy_client_json_append_string_member(target_function,
                                                  "arguments",
                                                  arguments->valuestring)) {
      return strappy_client_stream_set_error(context, "Could not allocate streamed function arguments.");
    }
  }

  return 1;
}

static const char *strappy_client_skip_json_whitespace(const char *text)
{
  while ((text != NULL) && (*text != '\0') &&
         isspace((unsigned char)*text)) {
    text++;
  }
  return text;
}

static void strappy_client_free_string_array(char **values, size_t count)
{
  size_t index;

  if (values == NULL) {
    return;
  }

  for (index = 0U; index < count; index++) {
    free(values[index]);
  }
  free(values);
}

static int strappy_client_split_concatenated_json_values(
  const char *text,
  char ***values_out,
  size_t *count_out,
  strappy_stream_context *context)
{
  char **values;
  size_t count;
  const char *cursor;

  if (values_out != NULL) {
    *values_out = NULL;
  }
  if (count_out != NULL) {
    *count_out = 0U;
  }

  if ((text == NULL) || (values_out == NULL) || (count_out == NULL)) {
    return 1;
  }

  values = NULL;
  count = 0U;
  cursor = strappy_client_skip_json_whitespace(text);
  while ((cursor != NULL) && (*cursor != '\0')) {
    const char *end;
    cJSON *value;
    char *segment;
    char **next_values;

    end = NULL;
    value = cJSON_ParseWithOpts(cursor, &end, 0);
    if ((value == NULL) || (end == NULL) || (end <= cursor)) {
      cJSON_Delete(value);
      strappy_client_free_string_array(values, count);
      return 1;
    }
    cJSON_Delete(value);

    segment = strappy_string_duplicate_length(cursor, (size_t)(end - cursor));
    if (segment == NULL) {
      strappy_client_free_string_array(values, count);
      return strappy_client_stream_set_error(
        context,
        "Could not allocate normalized tool arguments.");
    }

    if (count >= (((size_t)-1) / sizeof(char *))) {
      free(segment);
      strappy_client_free_string_array(values, count);
      return strappy_client_stream_set_error(
        context,
        "Streamed tool argument list is too large.");
    }
    next_values = (char **)realloc(values, (count + 1U) * sizeof(char *));
    if (next_values == NULL) {
      free(segment);
      strappy_client_free_string_array(values, count);
      return strappy_client_stream_set_error(
        context,
        "Could not allocate normalized tool arguments.");
    }

    values = next_values;
    values[count] = segment;
    count++;
    cursor = strappy_client_skip_json_whitespace(end);
  }

  *values_out = values;
  *count_out = count;
  return 1;
}

static int strappy_client_tool_call_copy_with_arguments(
  cJSON *normalized,
  cJSON *tool_call,
  const char *arguments,
  size_t split_index,
  strappy_stream_context *context)
{
  cJSON *copy;
  cJSON *function;
  cJSON *id;
  const char *id_text;

  copy = cJSON_Duplicate(tool_call, 1);
  if (copy == NULL) {
    return strappy_client_stream_set_error(
      context,
      "Could not allocate normalized tool call.");
  }

  function = cJSON_GetObjectItem(copy, "function");
  if (!cJSON_IsObject(function) ||
      !strappy_client_json_replace_string(function, "arguments", arguments)) {
    cJSON_Delete(copy);
    return strappy_client_stream_set_error(
      context,
      "Could not normalize streamed tool arguments.");
  }

  if (split_index > 0U) {
    char suffix[64];
    char *synthetic_id;

    id = cJSON_GetObjectItem(copy, "id");
    id_text = (cJSON_IsString(id) && (id->valuestring != NULL) &&
               (id->valuestring[0] != '\0')) ?
      id->valuestring : "tool-call";
    snprintf(suffix, sizeof(suffix), "-split-%lu",
             (unsigned long)split_index);
    suffix[sizeof(suffix) - 1U] = '\0';
    synthetic_id = strappy_join_strings(id_text, suffix);
    if (synthetic_id == NULL) {
      cJSON_Delete(copy);
      return strappy_client_stream_set_error(
        context,
        "Could not allocate normalized tool call id.");
    }
    if (!strappy_client_json_replace_string(copy, "id", synthetic_id)) {
      free(synthetic_id);
      cJSON_Delete(copy);
      return strappy_client_stream_set_error(
        context,
        "Could not normalize streamed tool call id.");
    }
    free(synthetic_id);
  }

  if (!cJSON_AddItemToArray(normalized, copy)) {
    cJSON_Delete(copy);
    return strappy_client_stream_set_error(
      context,
      "Could not store normalized tool call.");
  }

  return 1;
}

static int strappy_client_stream_normalize_tool_calls(
  strappy_stream_context *context)
{
  cJSON *normalized;
  int count;
  int index;
  int changed;

  if ((context == NULL) || (context->tool_calls == NULL) ||
      !cJSON_IsArray(context->tool_calls)) {
    return 1;
  }

  count = cJSON_GetArraySize(context->tool_calls);
  if (count <= 0) {
    return 1;
  }

  normalized = cJSON_CreateArray();
  if (normalized == NULL) {
    return strappy_client_stream_set_error(
      context,
      "Could not allocate normalized tool calls.");
  }

  changed = 0;
  for (index = 0; index < count; index++) {
    cJSON *tool_call;
    cJSON *function;
    cJSON *arguments;
    char **segments;
    size_t segment_count;
    size_t segment_index;

    tool_call = cJSON_GetArrayItem(context->tool_calls, index);
    function = cJSON_GetObjectItem(tool_call, "function");
    arguments = cJSON_GetObjectItem(function, "arguments");
    segments = NULL;
    segment_count = 0U;

    if (cJSON_IsString(arguments) && (arguments->valuestring != NULL) &&
        !strappy_client_split_concatenated_json_values(arguments->valuestring,
                                                       &segments,
                                                       &segment_count,
                                                       context)) {
      cJSON_Delete(normalized);
      return 0;
    }

    if (segment_count > 1U) {
      changed = 1;
      for (segment_index = 0U; segment_index < segment_count; segment_index++) {
        if (!strappy_client_tool_call_copy_with_arguments(normalized,
                                                          tool_call,
                                                          segments[segment_index],
                                                          segment_index,
                                                          context)) {
          strappy_client_free_string_array(segments, segment_count);
          cJSON_Delete(normalized);
          return 0;
        }
      }
      strappy_client_free_string_array(segments, segment_count);
      continue;
    }
    strappy_client_free_string_array(segments, segment_count);

    tool_call = cJSON_Duplicate(tool_call, 1);
    if (tool_call == NULL) {
      cJSON_Delete(normalized);
      return strappy_client_stream_set_error(
        context,
        "Could not allocate normalized tool call.");
    }
    if (!cJSON_AddItemToArray(normalized, tool_call)) {
      cJSON_Delete(tool_call);
      cJSON_Delete(normalized);
      return strappy_client_stream_set_error(
        context,
        "Could not store normalized tool call.");
    }
  }

  if (!changed) {
    cJSON_Delete(normalized);
    return 1;
  }

  cJSON_Delete(context->tool_calls);
  context->tool_calls = normalized;
  free(context->tool_call_stream_indices);
  context->tool_call_stream_indices = NULL;
  context->tool_call_stream_index_count = 0U;
  return 1;
}

static int strappy_client_stream_capture_reasoning_details(
  strappy_stream_context *context,
  cJSON *details,
  const char *skip_first,
  const char *skip_second)
{
  int count;
  int index;

  if (!cJSON_IsArray(details)) {
    return 1;
  }

  if (context->reasoning_details == NULL) {
    context->reasoning_details = cJSON_CreateArray();
    if (context->reasoning_details == NULL) {
      return strappy_client_stream_set_error(context, "Could not allocate streamed reasoning details.");
    }
  }

  count = cJSON_GetArraySize(details);
  for (index = 0; index < count; index++) {
    cJSON *item;
    cJSON *copy;
    const char *text;

    item = cJSON_GetArrayItem(details, index);
    if (item == NULL) {
      continue;
    }

    copy = cJSON_Duplicate(item, 1);
    if (copy == NULL) {
      return strappy_client_stream_set_error(context, "Could not allocate streamed reasoning detail.");
    }

    if (!cJSON_AddItemToArray(context->reasoning_details, copy)) {
      cJSON_Delete(copy);
      return strappy_client_stream_set_error(context, "Could not store streamed reasoning detail.");
    }

    text = strappy_client_reasoning_detail_text(item);
    if ((text != NULL) &&
        ((skip_first == NULL) || (strcmp(text, skip_first) != 0)) &&
        ((skip_second == NULL) || (strcmp(text, skip_second) != 0)) &&
        !strappy_client_reasoning_append_delta(&context->reasoning, text)) {
      return strappy_client_stream_set_error(context, "Could not allocate streamed reasoning text.");
    }
  }

  return 1;
}

static int strappy_client_stream_finalize_message(strappy_stream_context *context)
{
  cJSON *message;
  cJSON *copy;
  char *message_json;
  char *reasoning_text;
  const char *role;
  const char *content;
  int has_content;
  int has_tool_calls;

  if ((context == NULL) || (context->result == NULL)) {
    return 1;
  }

  if (!strappy_client_stream_normalize_tool_calls(context)) {
    return 0;
  }

  message = cJSON_CreateObject();
  if (message == NULL) {
    return strappy_client_stream_set_error(context, "Could not allocate assistant message JSON.");
  }

  role = ((context->message_role != NULL) && (context->message_role[0] != '\0'))
           ? context->message_role
           : "assistant";
  content = (context->content.data != NULL) ? context->content.data : "";
  has_content = (content[0] != '\0') ? 1 : 0;
  has_tool_calls = ((context->tool_calls != NULL) &&
                    (cJSON_GetArraySize(context->tool_calls) > 0)) ? 1 : 0;

  if (cJSON_AddStringToObject(message, "role", role) == NULL) {
    cJSON_Delete(message);
    return strappy_client_stream_set_error(context, "Could not build assistant message JSON.");
  }

  if (has_content || !has_tool_calls) {
    if (cJSON_AddStringToObject(message, "content", content) == NULL) {
      cJSON_Delete(message);
      return strappy_client_stream_set_error(context, "Could not build assistant message JSON.");
    }
  } else if (cJSON_AddNullToObject(message, "content") == NULL) {
    cJSON_Delete(message);
    return strappy_client_stream_set_error(context, "Could not build assistant message JSON.");
  }

  if ((context->reasoning.data != NULL) && (context->reasoning.length > 0U)) {
    if (cJSON_AddStringToObject(message,
                                "reasoning",
                                context->reasoning.data) == NULL) {
      cJSON_Delete(message);
      return strappy_client_stream_set_error(context, "Could not build assistant reasoning JSON.");
    }
  }

  if ((context->reasoning_details != NULL) &&
      (cJSON_GetArraySize(context->reasoning_details) > 0)) {
    copy = cJSON_Duplicate(context->reasoning_details, 1);
    if ((copy == NULL) ||
        !cJSON_AddItemToObject(message, "reasoning_details", copy)) {
      cJSON_Delete(copy);
      cJSON_Delete(message);
      return strappy_client_stream_set_error(context, "Could not build assistant reasoning details JSON.");
    }
  }

  if (has_tool_calls && !context->cancelled) {
    copy = cJSON_Duplicate(context->tool_calls, 1);
    if ((copy == NULL) || !cJSON_AddItemToObject(message, "tool_calls", copy)) {
      cJSON_Delete(copy);
      cJSON_Delete(message);
      return strappy_client_stream_set_error(context, "Could not build assistant tool call JSON.");
    }
  }

  message_json = cJSON_PrintUnformatted(message);
  cJSON_Delete(message);
  if (message_json == NULL) {
    return strappy_client_stream_set_error(context, "Could not serialize assistant message JSON.");
  }

  free(context->result->message_json);
  context->result->message_json = message_json;

  if ((context->reasoning.data != NULL) && (context->reasoning.length > 0U)) {
    reasoning_text = strappy_string_duplicate(context->reasoning.data);
    if (reasoning_text == NULL) {
      return strappy_client_stream_set_error(context, "Could not allocate assistant reasoning text.");
    }
    free(context->result->reasoning_text);
    context->result->reasoning_text = reasoning_text;
  }

  return 1;
}

static int strappy_client_stream_emit_delta(strappy_stream_context *context,
                                            strappy_chat_stream_event_type type,
                                            const char *text)
{
  strappy_chat_stream_event event;

  if ((context == NULL) || (text == NULL) || (text[0] == '\0')) {
    return 1;
  }

  if (context->callback == NULL) {
    return 1;
  }

  memset(&event, 0, sizeof(event));
  event.type = type;
  event.text = text;
  if (!context->callback(&event, context->callback_data)) {
    return strappy_client_stream_mark_cancelled(context);
  }

  return 1;
}

static int strappy_client_stream_poll_cancelled(strappy_stream_context *context)
{
  strappy_chat_stream_event event;

  if ((context == NULL) || (context->callback == NULL)) {
    return 1;
  }

  memset(&event, 0, sizeof(event));
  event.type = STRAPPY_CHAT_STREAM_EVENT_CONTENT_DELTA;
  event.text = "";
  if (!context->callback(&event, context->callback_data)) {
    return strappy_client_stream_mark_cancelled(context);
  }

  return 1;
}

static int strappy_client_stream_progress_callback(void *clientp,
                                                   double dltotal,
                                                   double dlnow,
                                                   double ultotal,
                                                   double ulnow)
{
  (void)dltotal;
  (void)dlnow;
  (void)ultotal;
  (void)ulnow;

  return strappy_client_stream_poll_cancelled(
    (strappy_stream_context *)clientp) ? 0 : 1;
}

static unsigned long strappy_client_stream_elapsed_seconds(
  const strappy_stream_context *context)
{
  time_t now;
  double elapsed;

  if ((context == NULL) || (context->started_at == 0)) {
    return 0UL;
  }

  now = time(NULL);
  elapsed = difftime(now, context->started_at);
  if (elapsed < 0.0) {
    elapsed = 0.0;
  }
  return (unsigned long)elapsed;
}

static char *strappy_client_cancelled_text(unsigned long elapsed_seconds)
{
  char buffer[64];

  snprintf(buffer,
           sizeof(buffer),
           "Cancelled - %lu seconds",
           elapsed_seconds);
  buffer[sizeof(buffer) - 1U] = '\0';
  return strappy_string_duplicate(buffer);
}

static int strappy_client_stream_append_cancelled_text(
  strappy_stream_context *context)
{
  char *text;
  int ok;

  if (context == NULL) {
    return 0;
  }

  text = strappy_client_cancelled_text(
    strappy_client_stream_elapsed_seconds(context));
  if (text == NULL) {
    return strappy_client_stream_set_error(
      context,
      "Could not allocate cancelled stream text.");
  }

  ok = 1;
  if ((context->content.data != NULL) && (context->content.length > 0U)) {
    ok = strappy_http_buffer_append_cstring(&context->content, "\n\n");
  }
  if (ok) {
    ok = strappy_http_buffer_append_cstring(&context->content, text);
  }
  free(text);

  if (!ok) {
    return strappy_client_stream_set_error(
      context,
      "Could not allocate cancelled stream text.");
  }

  return 1;
}

static int strappy_client_stream_append_reasoning_delta(
  strappy_stream_context *context,
  const char *text)
{
  if ((text == NULL) || (text[0] == '\0')) {
    return 1;
  }

  if (!strappy_client_reasoning_append_delta(&context->reasoning, text)) {
    return strappy_client_stream_set_error(context, "Could not allocate streamed reasoning text.");
  }

  return strappy_client_stream_emit_delta(
    context,
    STRAPPY_CHAT_STREAM_EVENT_REASONING_DELTA,
    text);
}

static int strappy_client_stream_emit_reasoning_details(
  strappy_stream_context *context,
  cJSON *details,
  const char *skip_first,
  const char *skip_second)
{
  int count;
  int index;

  if (!cJSON_IsArray(details)) {
    return 1;
  }

  count = cJSON_GetArraySize(details);
  for (index = 0; index < count; index++) {
    cJSON *item;
    const char *text;

    item = cJSON_GetArrayItem(details, index);
    text = strappy_client_reasoning_detail_text(item);
    if ((text == NULL) ||
        ((skip_first != NULL) && (strcmp(text, skip_first) == 0)) ||
        ((skip_second != NULL) && (strcmp(text, skip_second) == 0))) {
      continue;
    }

    if (!strappy_client_stream_emit_delta(
          context,
          STRAPPY_CHAT_STREAM_EVENT_REASONING_DELTA,
          text)) {
      return 0;
    }
  }

  return 1;
}

static int strappy_client_stream_parse_json_event(strappy_stream_context *context,
                                                  const char *json)
{
  cJSON *root;
  cJSON *choices;
  cJSON *first_choice;
  cJSON *delta;
  cJSON *content;
  cJSON *role;
  cJSON *reasoning;
  cJSON *reasoning_content;
  cJSON *reasoning_details;
  cJSON *tool_calls;
  cJSON *finish_reason;
  cJSON *model;
  char *api_error;
  char *content_text;
  const char *reasoning_text;
  const char *reasoning_content_text;
  int ok;

  if ((context == NULL) || (json == NULL) || (json[0] == '\0')) {
    return 1;
  }

  root = cJSON_Parse(json);
  if (root == NULL) {
    return strappy_client_stream_set_error(context, "OpenRouter stream returned invalid JSON.");
  }

  api_error = strappy_client_extract_api_error(root);
  if (api_error != NULL) {
    strappy_client_stream_set_formatted_error(context,
                                             "OpenRouter stream failed: %s",
                                             api_error);
    free(api_error);
    cJSON_Delete(root);
    return 0;
  }

  ok = 1;
  choices = cJSON_GetObjectItem(root, "choices");
  first_choice = cJSON_GetArrayItem(choices, 0);
  if (!strappy_client_capture_response_metadata(root,
                                                first_choice,
                                                context->result)) {
    cJSON_Delete(root);
    return strappy_client_stream_set_error(context,
                                           "Could not allocate stream metadata.");
  }
  delta = cJSON_GetObjectItem(first_choice, "delta");
  if (!cJSON_IsObject(delta)) {
    model = cJSON_GetObjectItem(root, "model");
    if (cJSON_IsString(model) && (model->valuestring != NULL) &&
        !strappy_client_stream_replace_result_string(&context->result->model,
                                                     model->valuestring,
                                                     context)) {
      ok = 0;
    }
    if (ok && !strappy_client_stream_poll_cancelled(context)) {
      ok = 0;
    }
    cJSON_Delete(root);
    return ok;
  }

  role = cJSON_GetObjectItem(delta, "role");
  if (cJSON_IsString(role) && (role->valuestring != NULL)) {
    ok = strappy_client_stream_replace_context_string(&context->message_role,
                                                      role->valuestring,
                                                      context);
  }

  content = cJSON_GetObjectItem(delta, "content");
  content_text = strappy_client_extract_assistant_content(content);
  if (content_text != NULL) {
    if (ok) {
      if (!strappy_http_buffer_append_cstring(&context->content, content_text)) {
        ok = strappy_client_stream_set_error(context, "Could not allocate streamed assistant text.");
      } else if (!strappy_client_stream_emit_delta(
                   context,
                   STRAPPY_CHAT_STREAM_EVENT_CONTENT_DELTA,
                   content_text)) {
        ok = 0;
      }
    }
    free(content_text);
  }

  reasoning_text = NULL;
  reasoning_content_text = NULL;

  reasoning = cJSON_GetObjectItem(delta, "reasoning");
  if (ok && cJSON_IsString(reasoning) && (reasoning->valuestring != NULL)) {
    reasoning_text = reasoning->valuestring;
    ok = strappy_client_stream_append_reasoning_delta(context, reasoning_text);
  }

  reasoning_content = cJSON_GetObjectItem(delta, "reasoning_content");
  if (ok && cJSON_IsString(reasoning_content) &&
      (reasoning_content->valuestring != NULL)) {
    reasoning_content_text = reasoning_content->valuestring;
    if ((reasoning_text == NULL) ||
        (strcmp(reasoning_text, reasoning_content_text) != 0)) {
      ok = strappy_client_stream_append_reasoning_delta(
        context,
        reasoning_content_text);
    }
  }

  reasoning_details = cJSON_GetObjectItem(delta, "reasoning_details");
  if (ok) {
    ok = strappy_client_stream_capture_reasoning_details(context,
                                                         reasoning_details,
                                                         reasoning_text,
                                                         reasoning_content_text) &&
         strappy_client_stream_emit_reasoning_details(context,
                                                      reasoning_details,
                                                      reasoning_text,
                                                      reasoning_content_text);
  }

  tool_calls = cJSON_GetObjectItem(delta, "tool_calls");
  if (ok) {
    ok = strappy_client_stream_capture_tool_calls(context, tool_calls);
  }

  finish_reason = cJSON_GetObjectItem(first_choice, "finish_reason");
  if (ok && cJSON_IsString(finish_reason) &&
      (finish_reason->valuestring != NULL)) {
    ok = strappy_client_stream_replace_result_string(&context->result->finish_reason,
                                                     finish_reason->valuestring,
                                                     context);
  }

  model = cJSON_GetObjectItem(root, "model");
  if (ok && cJSON_IsString(model) && (model->valuestring != NULL)) {
    ok = strappy_client_stream_replace_result_string(&context->result->model,
                                                     model->valuestring,
                                                     context);
  }

  if (ok && !strappy_client_stream_poll_cancelled(context)) {
    ok = 0;
  }

  cJSON_Delete(root);
  return ok;
}

static int strappy_client_stream_dispatch_event(strappy_stream_context *context)
{
  size_t length;

  if (context == NULL) {
    return 0;
  }

  length = context->event_data.length;
  if (length == 0U) {
    return 1;
  }

  if ((context->event_data.data != NULL) &&
      (context->event_data.data[length - 1U] == '\n')) {
    context->event_data.data[length - 1U] = '\0';
    context->event_data.length--;
  }

  if ((context->event_data.data != NULL) &&
      (strcmp(context->event_data.data, "[DONE]") == 0)) {
    context->done = 1;
    strappy_http_buffer_clear(&context->event_data);
    return 1;
  }

  context->saw_event = 1;
  if (!strappy_client_stream_parse_json_event(context, context->event_data.data)) {
    strappy_http_buffer_clear(&context->event_data);
    return 0;
  }

  strappy_http_buffer_clear(&context->event_data);
  return 1;
}

static int strappy_client_stream_process_line(strappy_stream_context *context,
                                              const char *line,
                                              size_t line_length)
{
  const char *value;
  size_t value_length;

  if (context == NULL) {
    return 0;
  }

  if ((line != NULL) && (line_length > 0U) && (line[line_length - 1U] == '\r')) {
    line_length--;
  }

  if (line_length == 0U) {
    return strappy_client_stream_dispatch_event(context);
  }

  if (line == NULL) {
    return strappy_client_stream_set_error(context, "OpenRouter stream line is incomplete.");
  }

  if ((line != NULL) && (line[0] == ':')) {
    return 1;
  }

  if ((line_length < 5U) || (memcmp(line, "data:", 5U) != 0)) {
    return 1;
  }

  value = line + 5U;
  value_length = line_length - 5U;
  if ((value_length > 0U) && (value[0] == ' ')) {
    value++;
    value_length--;
  }

  if (!strappy_http_buffer_append(&context->event_data, value, value_length) ||
      !strappy_http_buffer_append(&context->event_data, "\n", 1U)) {
    return strappy_client_stream_set_error(context, "Could not allocate OpenRouter stream event.");
  }

  return 1;
}

static int strappy_client_stream_feed(strappy_stream_context *context,
                                      const char *data,
                                      size_t length)
{
  size_t start;
  size_t index;

  if ((context == NULL) || ((data == NULL) && (length > 0U))) {
    return 0;
  }

  if (!strappy_http_buffer_append(&context->line_buffer, data, length)) {
    return strappy_client_stream_set_error(context, "Could not allocate OpenRouter stream buffer.");
  }

  start = 0U;
  for (index = 0U; index < context->line_buffer.length; index++) {
    if (context->line_buffer.data[index] == '\n') {
      if (!strappy_client_stream_process_line(context,
                                             context->line_buffer.data + start,
                                             index - start)) {
        return 0;
      }
      start = index + 1U;
    }
  }

  if (start > 0U) {
    size_t remaining;

    remaining = context->line_buffer.length - start;
    if (remaining > 0U) {
      memmove(context->line_buffer.data,
              context->line_buffer.data + start,
              remaining);
    }
    context->line_buffer.length = remaining;
    if (context->line_buffer.data != NULL) {
      context->line_buffer.data[remaining] = '\0';
    }
  }

  return 1;
}

static size_t strappy_client_stream_write_callback(void *contents,
                                                  size_t size,
                                                  size_t nmemb,
                                                  void *userp)
{
  strappy_stream_context *context;
  size_t real_size;

  if ((contents == NULL) || (userp == NULL)) {
    return 0U;
  }

  if ((size != 0U) && (nmemb > (((size_t)-1) / size))) {
    return 0U;
  }

  real_size = size * nmemb;
  context = (strappy_stream_context *)userp;

  if (!strappy_http_buffer_append(&context->raw_body,
                                  (const char *)contents,
                                  real_size)) {
    strappy_client_stream_set_error(context, "Could not allocate OpenRouter raw stream.");
    return 0U;
  }

  if (!strappy_client_stream_feed(context, (const char *)contents, real_size)) {
    return 0U;
  }

  return real_size;
}

void strappy_chat_result_init(strappy_chat_result *result)
{
  if (result == NULL) {
    return;
  }

  result->response_text = NULL;
  result->message_json = NULL;
  result->raw_json = NULL;
  result->response_id = NULL;
  result->finish_reason = NULL;
  result->model = NULL;
  result->created = NULL;
  result->native_finish_reason = NULL;
  result->service_tier = NULL;
  result->system_fingerprint = NULL;
  result->usage_json = NULL;
  result->generation_metadata_json = NULL;
  result->metadata_json = NULL;
  result->metadata_text = NULL;
  result->reasoning_text = NULL;
  result->http_status = 0L;
  result->cancelled = 0;
}

void strappy_chat_result_destroy(strappy_chat_result *result)
{
  if (result == NULL) {
    return;
  }

  free(result->response_text);
  free(result->message_json);
  free(result->raw_json);
  free(result->response_id);
  free(result->finish_reason);
  free(result->model);
  free(result->created);
  free(result->native_finish_reason);
  free(result->service_tier);
  free(result->system_fingerprint);
  free(result->usage_json);
  free(result->generation_metadata_json);
  free(result->metadata_json);
  free(result->metadata_text);
  free(result->reasoning_text);

  strappy_chat_result_init(result);
}

static char *strappy_client_build_endpoint_base(const char *endpoint)
{
  const char *path = "/chat/completions";
  size_t endpoint_length;

  if (endpoint == NULL) {
    return NULL;
  }

  endpoint_length = strlen(endpoint);
  while ((endpoint_length > 0U) && (endpoint[endpoint_length - 1U] == '/')) {
    endpoint_length--;
  }

  if ((endpoint_length >= strlen(path)) &&
      (strncmp(endpoint + endpoint_length - strlen(path),
               path,
               strlen(path)) == 0)) {
    endpoint_length -= strlen(path);
  }

  return strappy_string_duplicate_length(endpoint, endpoint_length);
}

static char *strappy_client_build_openrouter_generation_url(
  const strappy_config *config,
  const char *generation_id,
  CURL *curl)
{
  char *base;
  char *generation_url;
  char *query_prefix;
  char *url;
  char *escaped_id;

  if ((config == NULL) || (generation_id == NULL) ||
      (generation_id[0] == '\0') || (curl == NULL)) {
    return NULL;
  }

  base = strappy_client_build_endpoint_base(config->api_endpoint);
  if (base == NULL) {
    return NULL;
  }

  generation_url = strappy_join_strings(base, "/generation");
  free(base);
  if (generation_url == NULL) {
    return NULL;
  }

  escaped_id = curl_easy_escape(curl, generation_id, 0);
  if (escaped_id == NULL) {
    free(generation_url);
    return NULL;
  }

  query_prefix = strappy_join_strings(generation_url, "?id=");
  free(generation_url);
  if (query_prefix == NULL) {
    curl_free(escaped_id);
    return NULL;
  }

  url = strappy_join_strings(query_prefix, escaped_id);
  free(query_prefix);
  curl_free(escaped_id);
  return url;
}

static void strappy_client_sleep_milliseconds(unsigned int milliseconds)
{
  struct timeval delay;
  int result;

  do {
    delay.tv_sec = (long)(milliseconds / 1000U);
    delay.tv_usec = (long)((milliseconds % 1000U) * 1000U);
    result = select(0, NULL, NULL, NULL, &delay);
  } while ((result < 0) && (errno == EINTR));
}

static int strappy_client_generation_has_field(cJSON *data, const char *key)
{
  cJSON *item;

  if (!cJSON_IsObject(data) || (key == NULL)) {
    return 0;
  }

  item = cJSON_GetObjectItem(data, key);
  return (item != NULL) && !cJSON_IsNull(item);
}

static int strappy_client_generation_metadata_is_ready(const char *json,
                                                       int require_usage)
{
  cJSON *root;
  cJSON *data;
  int ready;

  if ((json == NULL) || (json[0] == '\0')) {
    return 0;
  }

  root = cJSON_Parse(json);
  if (root == NULL) {
    return 0;
  }

  data = cJSON_GetObjectItem(root, "data");
  if (!cJSON_IsObject(data)) {
    data = root;
  }

  ready = cJSON_IsObject(data) ? 1 : 0;
  if (ready && require_usage) {
    int has_cost;
    int has_prompt_tokens;
    int has_completion_tokens;

    has_cost =
      strappy_client_generation_has_field(data, "total_cost") ||
      strappy_client_generation_has_field(data, "usage") ||
      strappy_client_generation_has_field(data, "upstream_inference_cost");
    has_prompt_tokens =
      strappy_client_generation_has_field(data, "tokens_prompt") ||
      strappy_client_generation_has_field(data, "native_tokens_prompt");
    has_completion_tokens =
      strappy_client_generation_has_field(data, "tokens_completion") ||
      strappy_client_generation_has_field(data, "native_tokens_completion");
    ready = has_cost && has_prompt_tokens && has_completion_tokens;
  }

  cJSON_Delete(root);
  return ready;
}

static void strappy_client_fetch_openrouter_generation_metadata(
  const strappy_config *config,
  strappy_chat_result *result)
{
  CURL *curl;
  CURLcode code;
  struct curl_slist *headers;
  strappy_http_buffer response_buffer;
  char *auth_header;
  char *url;
  long http_status;
  unsigned int attempt;
  unsigned int max_attempts;
  unsigned int delay_ms;
  int require_usage;

  if (!strappy_client_is_openrouter_endpoint(config) ||
      (result == NULL) ||
      (result->response_id == NULL) ||
      (result->response_id[0] == '\0') ||
      (config->api_token == NULL)) {
    return;
  }

  curl = curl_easy_init();
  if (curl == NULL) {
    return;
  }

  url = strappy_client_build_openrouter_generation_url(config,
                                                       result->response_id,
                                                       curl);
  if (url == NULL) {
    curl_easy_cleanup(curl);
    return;
  }

  auth_header = strappy_join_strings("Authorization: Bearer ", config->api_token);
  if (auth_header == NULL) {
    free(url);
    curl_easy_cleanup(curl);
    return;
  }

  headers = NULL;
  if (!strappy_client_add_header(&headers, "Accept: application/json", NULL) ||
      !strappy_client_add_header(&headers, auth_header, NULL)) {
    free(auth_header);
    curl_slist_free_all(headers);
    free(url);
    curl_easy_cleanup(curl);
    return;
  }
  free(auth_header);

  strappy_http_buffer_init(&response_buffer);
  http_status = 0L;
  require_usage =
    (result->cancelled ||
     (result->usage_json == NULL) ||
     (result->usage_json[0] == '\0')) ? 1 : 0;
  max_attempts = require_usage ? 8U : 1U;
  delay_ms = 100U;

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, strappy_client_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response_buffer);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "Strappy/0.1");
  curl_easy_setopt(curl,
                   CURLOPT_TIMEOUT,
                   STRAPPY_CLIENT_GENERATION_METADATA_TIMEOUT_SECONDS);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  if ((strappy_cainfo_path != NULL) && (strappy_cainfo_path[0] != '\0')) {
    curl_easy_setopt(curl, CURLOPT_CAINFO, strappy_cainfo_path);
  }

  for (attempt = 0U; attempt < max_attempts; attempt++) {
    strappy_http_buffer_clear(&response_buffer);
    http_status = 0L;
    code = curl_easy_perform(curl);
    if (code == CURLE_OK) {
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    }

    if ((code == CURLE_OK) &&
        (http_status >= 200L) &&
        (http_status < 300L) &&
        (response_buffer.data != NULL) &&
        (response_buffer.data[0] != '\0')) {
      char *metadata_json;
      int ready;

      ready = strappy_client_generation_metadata_is_ready(response_buffer.data,
                                                          require_usage);
      if (ready || (attempt + 1U >= max_attempts)) {
        metadata_json = strappy_string_duplicate(response_buffer.data);
        if (metadata_json != NULL) {
          free(result->generation_metadata_json);
          result->generation_metadata_json = metadata_json;
        }
        break;
      }
    }

    if (attempt + 1U >= max_attempts) {
      break;
    }
    strappy_client_sleep_milliseconds(delay_ms);
    if (delay_ms < 400U) {
      delay_ms *= 2U;
    }
  }

  strappy_http_buffer_destroy(&response_buffer);
  curl_slist_free_all(headers);
  free(url);
  curl_easy_cleanup(curl);
}

static void strappy_client_finalize_metadata(const strappy_config *config,
                                             strappy_chat_result *result)
{
  if (result == NULL) {
    return;
  }

  strappy_client_fetch_openrouter_generation_metadata(config, result);
  if (!strappy_client_build_metadata_json(result)) {
    free(result->metadata_json);
    result->metadata_json = NULL;
  }
  if (!strappy_client_build_metadata_text(result)) {
    free(result->metadata_text);
    result->metadata_text = NULL;
  }
}

static int strappy_client_mark_cancelled_result(strappy_chat_result *result,
                                                char **error_out)
{
  if (result == NULL) {
    strappy_set_error(error_out, "Cancelled OpenRouter stream has no result.");
    return 0;
  }

  result->cancelled = 1;
  if (!strappy_client_replace_result_string(&result->finish_reason,
                                            "cancelled")) {
    strappy_set_error(error_out, "Could not allocate cancelled stream metadata.");
    return 0;
  }
  if (!strappy_client_replace_result_string(&result->native_finish_reason,
                                            "cancelled")) {
    strappy_set_error(error_out, "Could not allocate cancelled stream metadata.");
    return 0;
  }

  return 1;
}

static int strappy_client_send_request_json(const strappy_config *config,
                                            char *request_json,
                                            strappy_chat_result *result,
                                            char **error_out)
{
  CURL *curl;
  CURLcode code;
  struct curl_slist *headers;
  strappy_http_buffer response_buffer;
  char *auth_header;
  char *url;
  long http_status;
  int ok;

  url = strappy_client_build_chat_url(config->api_endpoint);
  if (url == NULL) {
    free(request_json);
    strappy_set_error(error_out, "Could not allocate OpenRouter URL.");
    return 0;
  }

  auth_header = strappy_join_strings("Authorization: Bearer ", config->api_token);
  if (auth_header == NULL) {
    free(url);
    free(request_json);
    strappy_set_error(error_out, "Could not allocate OpenRouter auth header.");
    return 0;
  }

  headers = NULL;
  ok = strappy_client_add_header(&headers, "Content-Type: application/json", error_out) &&
       strappy_client_add_header(&headers, "Accept: application/json", error_out) &&
       strappy_client_add_header(&headers, "X-OpenRouter-Title: Strappy", error_out) &&
       strappy_client_add_header(&headers, auth_header, error_out);
  free(auth_header);

  if (!ok) {
    curl_slist_free_all(headers);
    free(url);
    free(request_json);
    return 0;
  }

  curl = curl_easy_init();
  if (curl == NULL) {
    curl_slist_free_all(headers);
    free(url);
    free(request_json);
    strappy_set_error(error_out, "Could not create curl handle.");
    return 0;
  }

  response_buffer.data = NULL;
  response_buffer.length = 0U;
  http_status = 0L;

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_json);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, strappy_client_header_callback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)result);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, strappy_client_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response_buffer);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "Strappy/0.1");
  curl_easy_setopt(curl,
                   CURLOPT_TIMEOUT,
                   STRAPPY_CLIENT_NON_STREAMING_TIMEOUT_SECONDS);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  if ((strappy_cainfo_path != NULL) && (strappy_cainfo_path[0] != '\0')) {
    curl_easy_setopt(curl, CURLOPT_CAINFO, strappy_cainfo_path);
  }

  code = curl_easy_perform(curl);
  if (code == CURLE_OK) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
  }

  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);
  free(url);
  free(request_json);

  if (code != CURLE_OK) {
    free(response_buffer.data);
    strappy_set_formatted_error(error_out,
                             "OpenRouter request failed: %s",
                             curl_easy_strerror(code));
    return 0;
  }

  ok = strappy_client_parse_response(response_buffer.data,
                                      http_status,
                                      result,
                                      error_out);
  free(response_buffer.data);

  if (!ok) {
    strappy_chat_result_destroy(result);
    return 0;
  }

  strappy_client_finalize_metadata(config, result);
  return 1;
}

static int strappy_client_stream_request_json(const strappy_config *config,
                                              char *request_json,
                                              strappy_chat_result *result,
                                              strappy_chat_stream_callback callback,
                                              void *callback_data,
                                              char **error_out)
{
  CURL *curl;
  CURLcode code;
  struct curl_slist *headers;
  strappy_stream_context stream_context;
  char *auth_header;
  char *url;
  char *tool_display_text;
  long http_status;
  int ok;

  url = strappy_client_build_chat_url(config->api_endpoint);
  if (url == NULL) {
    free(request_json);
    strappy_set_error(error_out, "Could not allocate OpenRouter URL.");
    return 0;
  }

  auth_header = strappy_join_strings("Authorization: Bearer ", config->api_token);
  if (auth_header == NULL) {
    free(url);
    free(request_json);
    strappy_set_error(error_out, "Could not allocate OpenRouter auth header.");
    return 0;
  }

  headers = NULL;
  ok = strappy_client_add_header(&headers, "Content-Type: application/json", error_out) &&
       strappy_client_add_header(&headers, "Accept: text/event-stream", error_out) &&
       strappy_client_add_header(&headers, "X-OpenRouter-Title: Strappy", error_out) &&
       strappy_client_add_header(&headers, auth_header, error_out);
  free(auth_header);

  if (!ok) {
    curl_slist_free_all(headers);
    free(url);
    free(request_json);
    return 0;
  }

  curl = curl_easy_init();
  if (curl == NULL) {
    curl_slist_free_all(headers);
    free(url);
    free(request_json);
    strappy_set_error(error_out, "Could not create curl handle.");
    return 0;
  }

  strappy_stream_context_init(&stream_context);
  stream_context.result = result;
  stream_context.callback = callback;
  stream_context.callback_data = callback_data;
  stream_context.started_at = time(NULL);
  tool_display_text = NULL;
  http_status = 0L;

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_json);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, strappy_client_header_callback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)result);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, strappy_client_stream_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&stream_context);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl,
                   CURLOPT_PROGRESSFUNCTION,
                   strappy_client_stream_progress_callback);
  curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, (void *)&stream_context);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "Strappy/0.1");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(curl,
                   CURLOPT_LOW_SPEED_TIME,
                   STRAPPY_CLIENT_STREAM_LOW_SPEED_TIME_SECONDS);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  if ((strappy_cainfo_path != NULL) && (strappy_cainfo_path[0] != '\0')) {
    curl_easy_setopt(curl, CURLOPT_CAINFO, strappy_cainfo_path);
  }

  code = curl_easy_perform(curl);
  if (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status) != CURLE_OK) {
    http_status = 0L;
  }

  if ((code == CURLE_OK) && (stream_context.line_buffer.length > 0U)) {
    if (!strappy_client_stream_process_line(&stream_context,
                                            stream_context.line_buffer.data,
                                            stream_context.line_buffer.length)) {
      code = CURLE_WRITE_ERROR;
    }
    strappy_http_buffer_clear(&stream_context.line_buffer);
  }

  if ((code == CURLE_OK) && (stream_context.event_data.length > 0U)) {
    if (!strappy_client_stream_dispatch_event(&stream_context)) {
      code = CURLE_WRITE_ERROR;
    }
  }

  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);
  free(url);
  free(request_json);

  result->http_status = http_status;

  if ((code != CURLE_OK) && !stream_context.cancelled) {
    if (stream_context.stream_error != NULL) {
      strappy_set_error(error_out, stream_context.stream_error);
    } else {
      strappy_set_formatted_error(error_out,
                               "OpenRouter stream failed: %s",
                               curl_easy_strerror(code));
    }
    strappy_stream_context_destroy(&stream_context);
    strappy_chat_result_destroy(result);
    return 0;
  }

  if (!stream_context.cancelled &&
      ((http_status < 200L) || (http_status >= 300L))) {
    cJSON *root;
    char *api_error;

    root = cJSON_Parse(stream_context.raw_body.data);
    api_error = strappy_client_extract_api_error(root);
    if (api_error != NULL) {
      strappy_set_formatted_error(error_out,
                               "OpenRouter request failed with HTTP %ld: %s",
                               http_status,
                               api_error);
      free(api_error);
    } else {
      strappy_set_formatted_error(error_out,
                               "OpenRouter request failed with HTTP %ld.",
                               http_status);
    }
    cJSON_Delete(root);
    strappy_stream_context_destroy(&stream_context);
    strappy_chat_result_destroy(result);
    return 0;
  }

  if (!stream_context.cancelled &&
      !stream_context.saw_event &&
      !stream_context.done) {
    if ((stream_context.raw_body.data != NULL) &&
        (stream_context.raw_body.length > 0U)) {
      ok = strappy_client_parse_response(stream_context.raw_body.data,
                                         http_status,
                                         result,
                                         error_out);
      strappy_stream_context_destroy(&stream_context);
      if (!ok) {
        strappy_chat_result_destroy(result);
        return 0;
      }
      strappy_client_finalize_metadata(config, result);
      return 1;
    }

    strappy_set_error(error_out, "OpenRouter returned an empty stream.");
    strappy_stream_context_destroy(&stream_context);
    strappy_chat_result_destroy(result);
    return 0;
  }

  if (stream_context.cancelled &&
      !strappy_client_stream_append_cancelled_text(&stream_context)) {
    if (stream_context.stream_error != NULL) {
      strappy_set_error(error_out, stream_context.stream_error);
    } else {
      strappy_set_error(error_out, "Could not build cancelled stream response.");
    }
    strappy_stream_context_destroy(&stream_context);
    strappy_chat_result_destroy(result);
    return 0;
  }

  if (!stream_context.cancelled &&
      (stream_context.tool_calls != NULL) &&
      (cJSON_GetArraySize(stream_context.tool_calls) > 0)) {
    const char *streamed_content;

    streamed_content =
      (stream_context.content.data != NULL) ? stream_context.content.data : "";
    tool_display_text =
      strappy_client_tool_calls_display_text(stream_context.tool_calls);
    if (tool_display_text == NULL) {
      strappy_set_error(error_out, "Could not allocate streamed tool call text.");
      strappy_stream_context_destroy(&stream_context);
      strappy_chat_result_destroy(result);
      return 0;
    }

    if (streamed_content[0] == '\0') {
      result->response_text = tool_display_text;
      tool_display_text = NULL;
    } else {
      result->response_text =
        strappy_client_text_with_tool_calls_display(streamed_content,
                                                    stream_context.tool_calls);
      free(tool_display_text);
      tool_display_text = NULL;
    }
  } else {
    result->response_text = strappy_string_duplicate(
      (stream_context.content.data != NULL) ? stream_context.content.data : "");
  }
  if (result->response_text == NULL) {
    strappy_set_error(error_out, "Could not allocate streamed assistant response.");
    strappy_stream_context_destroy(&stream_context);
    strappy_chat_result_destroy(result);
    return 0;
  }

  if (stream_context.raw_body.data != NULL) {
    result->raw_json = strappy_string_duplicate(stream_context.raw_body.data);
    if (result->raw_json == NULL) {
      strappy_set_error(error_out, "Could not allocate OpenRouter raw stream.");
      strappy_stream_context_destroy(&stream_context);
      strappy_chat_result_destroy(result);
      return 0;
    }
  }

  if (!strappy_client_stream_finalize_message(&stream_context)) {
    if (stream_context.stream_error != NULL) {
      strappy_set_error(error_out, stream_context.stream_error);
    } else {
      strappy_set_error(error_out, "Could not build streamed assistant message.");
    }
    strappy_stream_context_destroy(&stream_context);
    strappy_chat_result_destroy(result);
    return 0;
  }

  if (stream_context.cancelled &&
      !strappy_client_mark_cancelled_result(result, error_out)) {
    strappy_stream_context_destroy(&stream_context);
    strappy_chat_result_destroy(result);
    return 0;
  }

  strappy_stream_context_destroy(&stream_context);
  strappy_client_finalize_metadata(config, result);
  return 1;
}

int strappy_client_send_messages(const strappy_config *config,
                                 const strappy_chat_message *messages,
                                 size_t message_count,
                                 strappy_chat_result *result,
                                 char **error_out)
{
  char *request_json;

  if (result == NULL) {
    strappy_set_error(error_out, "strappy_client_send_messages received no result.");
    return 0;
  }
  strappy_chat_result_init(result);

  if ((config == NULL) || (messages == NULL) || (message_count == 0U)) {
    strappy_set_error(error_out, "OpenRouter message request is incomplete.");
    return 0;
  }

  if ((config->api_endpoint == NULL) || (config->api_endpoint[0] == '\0')) {
    strappy_set_error(error_out, "APIENDPOINT is not configured.");
    return 0;
  }

  if ((config->api_token == NULL) || (config->api_token[0] == '\0')) {
    strappy_set_error(error_out, "APITOKEN is not configured.");
    return 0;
  }

  if ((config->api_model == NULL) || (config->api_model[0] == '\0')) {
    strappy_set_error(error_out, "API model is not configured.");
    return 0;
  }

  if (!strappy_client_ensure_curl_initialized(error_out)) {
    return 0;
  }

  request_json =
    strappy_client_build_messages_request_json(config,
                                               messages,
                                               message_count,
                                               0,
                                               error_out);
  if (request_json == NULL) {
    return 0;
  }

  return strappy_client_send_request_json(config, request_json, result, error_out);
}

int strappy_client_fetch_openrouter_user_models_json(
  const strappy_config *config,
  char **json_out,
  long *http_status_out,
  char **error_out)
{
  CURL *curl;
  CURLcode code;
  struct curl_slist *headers;
  strappy_http_buffer response_buffer;
  char *auth_header;
  char *url;
  long http_status;
  int ok;

  if (json_out == NULL) {
    strappy_set_error(error_out, "OpenRouter model JSON output is missing.");
    return 0;
  }
  *json_out = NULL;
  if (http_status_out != NULL) {
    *http_status_out = 0L;
  }

  if (config == NULL) {
    strappy_set_error(error_out, "OpenRouter model request is incomplete.");
    return 0;
  }
  if ((config->api_endpoint == NULL) || (config->api_endpoint[0] == '\0')) {
    strappy_set_error(error_out, "APIENDPOINT is not configured.");
    return 0;
  }
  if ((config->api_token == NULL) || (config->api_token[0] == '\0')) {
    strappy_set_error(error_out, "APITOKEN is not configured.");
    return 0;
  }

  if (!strappy_client_ensure_curl_initialized(error_out)) {
    return 0;
  }

  url = strappy_client_build_openrouter_user_models_url(config->api_endpoint);
  if (url == NULL) {
    strappy_set_error(error_out, "Could not allocate OpenRouter model URL.");
    return 0;
  }

  auth_header = strappy_join_strings("Authorization: Bearer ", config->api_token);
  if (auth_header == NULL) {
    free(url);
    strappy_set_error(error_out, "Could not allocate OpenRouter auth header.");
    return 0;
  }

  headers = NULL;
  ok = strappy_client_add_header(&headers, "Accept: application/json", error_out) &&
       strappy_client_add_header(&headers, "X-OpenRouter-Title: Strappy", error_out) &&
       strappy_client_add_header(&headers, auth_header, error_out);
  free(auth_header);
  if (!ok) {
    curl_slist_free_all(headers);
    free(url);
    return 0;
  }

  curl = curl_easy_init();
  if (curl == NULL) {
    curl_slist_free_all(headers);
    free(url);
    strappy_set_error(error_out, "Could not create curl handle.");
    return 0;
  }

  strappy_http_buffer_init(&response_buffer);
  http_status = 0L;

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, strappy_client_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response_buffer);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "Strappy/0.1");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  if ((strappy_cainfo_path != NULL) && (strappy_cainfo_path[0] != '\0')) {
    curl_easy_setopt(curl, CURLOPT_CAINFO, strappy_cainfo_path);
  }

  code = curl_easy_perform(curl);
  if (code == CURLE_OK) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
  }

  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);
  free(url);
  if (http_status_out != NULL) {
    *http_status_out = http_status;
  }

  if (code != CURLE_OK) {
    strappy_http_buffer_destroy(&response_buffer);
    strappy_set_formatted_error(error_out,
                                "OpenRouter model request failed: %s",
                                curl_easy_strerror(code));
    return 0;
  }

  if ((http_status < 200L) || (http_status >= 300L)) {
    cJSON *root;
    char *api_error;

    api_error = NULL;
    root = cJSON_Parse(response_buffer.data);
    if (root != NULL) {
      api_error = strappy_client_extract_api_error(root);
      cJSON_Delete(root);
    }
    if (api_error != NULL) {
      strappy_set_formatted_error(error_out,
                                  "OpenRouter model request failed (%ld): %s",
                                  http_status,
                                  api_error);
      free(api_error);
    } else {
      strappy_set_formatted_error(error_out,
                                  "OpenRouter model request failed with HTTP %ld.",
                                  http_status);
    }
    strappy_http_buffer_destroy(&response_buffer);
    return 0;
  }

  if ((response_buffer.data == NULL) || (response_buffer.data[0] == '\0')) {
    strappy_http_buffer_destroy(&response_buffer);
    strappy_set_error(error_out, "OpenRouter model response was empty.");
    return 0;
  }

  *json_out = response_buffer.data;
  response_buffer.data = NULL;
  response_buffer.length = 0U;
  strappy_http_buffer_destroy(&response_buffer);
  return 1;
}

int strappy_client_stream_messages(const strappy_config *config,
                                   const strappy_chat_message *messages,
                                   size_t message_count,
                                   strappy_chat_result *result,
                                   strappy_chat_stream_callback callback,
                                   void *callback_data,
                                   char **error_out)
{
  char *request_json;

  if (result == NULL) {
    strappy_set_error(error_out, "strappy_client_stream_messages received no result.");
    return 0;
  }
  strappy_chat_result_init(result);

  if ((config == NULL) || (messages == NULL) || (message_count == 0U)) {
    strappy_set_error(error_out, "OpenRouter message request is incomplete.");
    return 0;
  }

  if ((config->api_endpoint == NULL) || (config->api_endpoint[0] == '\0')) {
    strappy_set_error(error_out, "APIENDPOINT is not configured.");
    return 0;
  }

  if ((config->api_token == NULL) || (config->api_token[0] == '\0')) {
    strappy_set_error(error_out, "APITOKEN is not configured.");
    return 0;
  }

  if ((config->api_model == NULL) || (config->api_model[0] == '\0')) {
    strappy_set_error(error_out, "API model is not configured.");
    return 0;
  }

  if (!strappy_client_ensure_curl_initialized(error_out)) {
    return 0;
  }

  request_json =
    strappy_client_build_messages_request_json(config,
                                               messages,
                                               message_count,
                                               1,
                                               error_out);
  if (request_json == NULL) {
    return 0;
  }

  return strappy_client_stream_request_json(config,
                                            request_json,
                                            result,
                                            callback,
                                            callback_data,
                                            error_out);
}
