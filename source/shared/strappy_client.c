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
  char *next_data;

  if ((contents == NULL) || (userp == NULL)) {
    return 0U;
  }

  if ((size != 0U) && (nmemb > (((size_t)-1) / size))) {
    return 0U;
  }

  real_size = size * nmemb;
  buffer = (strappy_http_buffer *)userp;

  if (buffer->length > (((size_t)-1) - real_size - 1U)) {
    return 0U;
  }

  next_data = (char *)realloc(buffer->data, buffer->length + real_size + 1U);
  if (next_data == NULL) {
    return 0U;
  }

  buffer->data = next_data;
  memcpy(buffer->data + buffer->length, contents, real_size);
  buffer->length += real_size;
  buffer->data[buffer->length] = '\0';

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

static char *strappy_client_build_messages_request_json(
                                     const strappy_config *config,
                                     const strappy_chat_message *chat_messages,
                                     size_t chat_message_count,
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
      (cJSON_AddBoolToObject(root, "stream", 0) == NULL)) {
    cJSON_Delete(root);
    cJSON_Delete(messages);
    strappy_set_error(error_out, "Could not build OpenRouter JSON request.");
    return NULL;
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

  result->response_text = response_text;
  result->raw_json = raw_json;
  result->finish_reason = finish_reason_text;
  result->model = model_text;
  result->http_status = http_status;

  cJSON_Delete(root);
  return 1;
}

void strappy_chat_result_init(strappy_chat_result *result)
{
  if (result == NULL) {
    return;
  }

  result->response_text = NULL;
  result->raw_json = NULL;
  result->finish_reason = NULL;
  result->model = NULL;
  result->http_status = 0L;
}

void strappy_chat_result_destroy(strappy_chat_result *result)
{
  if (result == NULL) {
    return;
  }

  free(result->response_text);
  free(result->raw_json);
  free(result->finish_reason);
  free(result->model);

  strappy_chat_result_init(result);
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
