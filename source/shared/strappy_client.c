#include "strappy_client.h"

#include "strappy_core.h"

#include <curl/curl.h>
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>

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

  if (!strappy_client_capture_json_string(&result->response_id, root, "id") ||
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

static char *strappy_client_build_messages_request_json(
                                     const strappy_config *config,
                                     const strappy_chat_message *chat_messages,
                                     size_t chat_message_count,
                                     int should_stream,
                                     char **error_out)
{
  cJSON *root;
  cJSON *messages;
  char *json;
  size_t index;

  root = cJSON_CreateObject();
  messages = cJSON_CreateArray();

  if ((root == NULL) || (messages == NULL)) {
    cJSON_Delete(root);
    cJSON_Delete(messages);
    strappy_set_error(error_out, "Could not allocate OpenRouter JSON request.");
    return NULL;
  }

  if ((cJSON_AddStringToObject(root, "model", config->api_model) == NULL) ||
      (cJSON_AddBoolToObject(root, "stream", should_stream ? 1 : 0) == NULL)) {
    cJSON_Delete(root);
    cJSON_Delete(messages);
    strappy_set_error(error_out, "Could not build OpenRouter JSON request.");
    return NULL;
  }

  if (should_stream && strappy_client_should_request_reasoning(config)) {
    cJSON *reasoning;

    reasoning = cJSON_CreateObject();
    if (reasoning == NULL) {
      cJSON_Delete(root);
      cJSON_Delete(messages);
      strappy_set_error(error_out, "Could not allocate OpenRouter reasoning request.");
      return NULL;
    }

    if ((cJSON_AddBoolToObject(reasoning, "enabled", 1) == NULL) ||
        (cJSON_AddBoolToObject(reasoning, "exclude", 0) == NULL) ||
        !cJSON_AddItemToObject(root, "reasoning", reasoning)) {
      cJSON_Delete(root);
      cJSON_Delete(messages);
      cJSON_Delete(reasoning);
      strappy_set_error(error_out, "Could not build OpenRouter reasoning request.");
      return NULL;
    }
    reasoning = NULL;
  }

  if (should_stream && strappy_client_should_request_stream_usage(config)) {
    cJSON *stream_options;

    stream_options = cJSON_CreateObject();
    if (stream_options == NULL) {
      cJSON_Delete(root);
      cJSON_Delete(messages);
      strappy_set_error(error_out, "Could not allocate OpenAI stream options.");
      return NULL;
    }

    if ((cJSON_AddBoolToObject(stream_options, "include_usage", 1) == NULL) ||
        !cJSON_AddItemToObject(root, "stream_options", stream_options)) {
      cJSON_Delete(root);
      cJSON_Delete(messages);
      cJSON_Delete(stream_options);
      strappy_set_error(error_out, "Could not build OpenAI stream options.");
      return NULL;
    }
    stream_options = NULL;
  }

  for (index = 0U; index < chat_message_count; index++) {
    cJSON *message;

    if ((chat_messages[index].role == NULL) ||
        (chat_messages[index].role[0] == '\0') ||
        (chat_messages[index].content == NULL)) {
      cJSON_Delete(root);
      cJSON_Delete(messages);
      strappy_set_error(error_out, "OpenRouter chat message is incomplete.");
      return NULL;
    }

    message = cJSON_CreateObject();
    if (message == NULL) {
      cJSON_Delete(root);
      cJSON_Delete(messages);
      strappy_set_error(error_out, "Could not allocate OpenRouter JSON request.");
      return NULL;
    }

    if ((cJSON_AddStringToObject(message, "role", chat_messages[index].role) == NULL) ||
        (cJSON_AddStringToObject(message, "content", chat_messages[index].content) == NULL)) {
      cJSON_Delete(root);
      cJSON_Delete(messages);
      cJSON_Delete(message);
      strappy_set_error(error_out, "Could not build OpenRouter JSON request.");
      return NULL;
    }

    if (!cJSON_AddItemToArray(messages, message)) {
      cJSON_Delete(root);
      cJSON_Delete(messages);
      cJSON_Delete(message);
      strappy_set_error(error_out, "Could not build OpenRouter JSON request.");
      return NULL;
    }
  }

  if (!cJSON_AddItemToObject(root, "messages", messages)) {
    cJSON_Delete(root);
    cJSON_Delete(messages);
    strappy_set_error(error_out, "Could not build OpenRouter JSON request.");
    return NULL;
  }
  messages = NULL;

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);

  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize OpenRouter JSON request.");
    return NULL;
  }

  return json;
}

static char *strappy_client_build_request_json(const strappy_config *config,
                                     const char *prompt,
                                     char **error_out)
{
  strappy_chat_message message;

  message.role = "user";
  message.content = prompt;
  return strappy_client_build_messages_request_json(config,
                                                    &message,
                                                    1U,
                                                    0,
                                                    error_out);
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
  cJSON *finish_reason;
  cJSON *model;
  char *api_error;
  char *response_text;
  char *raw_json;
  char *finish_reason_text;
  char *model_text;

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

  response_text = strappy_client_extract_assistant_content(content);
  if (response_text == NULL) {
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
  strappy_chat_result *result;
  strappy_chat_stream_callback callback;
  void *callback_data;
  char *stream_error;
  int saw_event;
  int done;
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
  context->result = NULL;
  context->callback = NULL;
  context->callback_data = NULL;
  context->stream_error = NULL;
  context->saw_event = 0;
  context->done = 0;
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

  event.type = type;
  event.text = text;
  if (!context->callback(&event, context->callback_data)) {
    return strappy_client_stream_set_error(context, "OpenRouter stream was cancelled.");
  }

  return 1;
}

static int strappy_client_stream_emit_reasoning_details(
  strappy_stream_context *context,
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
      if (!strappy_client_stream_emit_delta(
            context,
            STRAPPY_CHAT_STREAM_EVENT_REASONING_DELTA,
            text->valuestring)) {
        return 0;
      }
      continue;
    }

    summary = cJSON_GetObjectItem(item, "summary");
    if (cJSON_IsString(summary) && (summary->valuestring != NULL)) {
      if (!strappy_client_stream_emit_delta(
            context,
            STRAPPY_CHAT_STREAM_EVENT_REASONING_DELTA,
            summary->valuestring)) {
        return 0;
      }
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
  cJSON *reasoning;
  cJSON *reasoning_content;
  cJSON *reasoning_details;
  cJSON *finish_reason;
  cJSON *model;
  char *api_error;
  char *content_text;
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
    cJSON_Delete(root);
    return ok;
  }

  content = cJSON_GetObjectItem(delta, "content");
  content_text = strappy_client_extract_assistant_content(content);
  if (content_text != NULL) {
    if (!strappy_http_buffer_append_cstring(&context->content, content_text)) {
      ok = strappy_client_stream_set_error(context, "Could not allocate streamed assistant text.");
    } else if (!strappy_client_stream_emit_delta(
                 context,
                 STRAPPY_CHAT_STREAM_EVENT_CONTENT_DELTA,
                 content_text)) {
      ok = 0;
    }
    free(content_text);
  }

  reasoning = cJSON_GetObjectItem(delta, "reasoning");
  if (ok && cJSON_IsString(reasoning) && (reasoning->valuestring != NULL)) {
    ok = strappy_client_stream_emit_delta(
      context,
      STRAPPY_CHAT_STREAM_EVENT_REASONING_DELTA,
      reasoning->valuestring);
  }

  reasoning_content = cJSON_GetObjectItem(delta, "reasoning_content");
  if (ok && cJSON_IsString(reasoning_content) &&
      (reasoning_content->valuestring != NULL)) {
    ok = strappy_client_stream_emit_delta(
      context,
      STRAPPY_CHAT_STREAM_EVENT_REASONING_DELTA,
      reasoning_content->valuestring);
  }

  reasoning_details = cJSON_GetObjectItem(delta, "reasoning_details");
  if (ok) {
    ok = strappy_client_stream_emit_reasoning_details(context, reasoning_details);
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
  result->metadata_text = NULL;
  result->http_status = 0L;
}

void strappy_chat_result_destroy(strappy_chat_result *result)
{
  if (result == NULL) {
    return;
  }

  free(result->response_text);
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
  free(result->metadata_text);

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

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, strappy_client_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response_buffer);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "Strappy/0.1");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  if ((strappy_cainfo_path != NULL) && (strappy_cainfo_path[0] != '\0')) {
    curl_easy_setopt(curl, CURLOPT_CAINFO, strappy_cainfo_path);
  }

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

    metadata_json = strappy_string_duplicate(response_buffer.data);
    if (metadata_json != NULL) {
      free(result->generation_metadata_json);
      result->generation_metadata_json = metadata_json;
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
  if (!strappy_client_build_metadata_text(result)) {
    free(result->metadata_text);
    result->metadata_text = NULL;
  }
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
  http_status = 0L;

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_json);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, strappy_client_stream_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&stream_context);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "Strappy/0.1");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 120L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  if ((strappy_cainfo_path != NULL) && (strappy_cainfo_path[0] != '\0')) {
    curl_easy_setopt(curl, CURLOPT_CAINFO, strappy_cainfo_path);
  }

  code = curl_easy_perform(curl);
  if (code == CURLE_OK) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
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

  if (code != CURLE_OK) {
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

  if ((http_status < 200L) || (http_status >= 300L)) {
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

  if (!stream_context.saw_event && !stream_context.done) {
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

  result->response_text = strappy_string_duplicate(
    (stream_context.content.data != NULL) ? stream_context.content.data : "");
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
    strappy_set_error(error_out, "APIMODEL is not configured.");
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

int strappy_client_send_prompt(const strappy_config *config,
                                const char *prompt,
                                strappy_chat_result *result,
                                char **error_out)
{
  char *request_json;

  if (result == NULL) {
    strappy_set_error(error_out, "strappy_client_send_prompt received no result.");
    return 0;
  }
  strappy_chat_result_init(result);

  if ((config == NULL) || (prompt == NULL) || (prompt[0] == '\0')) {
    strappy_set_error(error_out, "OpenRouter prompt request is incomplete.");
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
    strappy_set_error(error_out, "APIMODEL is not configured.");
    return 0;
  }

  if (!strappy_client_ensure_curl_initialized(error_out)) {
    return 0;
  }

  request_json = strappy_client_build_request_json(config, prompt, error_out);
  if (request_json == NULL) {
    return 0;
  }

  return strappy_client_send_request_json(config, request_json, result, error_out);
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
    strappy_set_error(error_out, "APIMODEL is not configured.");
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

int strappy_client_stream_prompt(const strappy_config *config,
                                 const char *prompt,
                                 strappy_chat_result *result,
                                 strappy_chat_stream_callback callback,
                                 void *callback_data,
                                 char **error_out)
{
  strappy_chat_message message;

  if (result == NULL) {
    strappy_set_error(error_out, "strappy_client_stream_prompt received no result.");
    return 0;
  }
  strappy_chat_result_init(result);

  if ((config == NULL) || (prompt == NULL) || (prompt[0] == '\0')) {
    strappy_set_error(error_out, "OpenRouter prompt request is incomplete.");
    return 0;
  }

  message.role = "user";
  message.content = prompt;

  return strappy_client_stream_messages(config,
                                        &message,
                                        1U,
                                        result,
                                        callback,
                                        callback_data,
                                        error_out);
}
