#include "strappy_client.h"

#include "strappy_core.h"
#include "strappy_debug_capture.h"
#include "strappy_identity.h"
#include "strappy_sse.h"

#include <curl/curl.h>
#include <cJSON.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>

#define STRAPPY_CLIENT_MODEL_TIMEOUT_SECONDS 60L
#define STRAPPY_CLIENT_RESPONSES_TIMEOUT_SECONDS 900L
#define STRAPPY_CLIENT_MAX_RESPONSE_BYTES (64U * 1024U * 1024U)

typedef struct strappy_http_buffer {
  char *data;
  size_t length;
} strappy_http_buffer;

typedef struct strappy_responses_header_context {
  strappy_responses_http_result *result;
  strappy_http_buffer raw_headers;
} strappy_responses_header_context;

typedef struct strappy_responses_transfer_context {
  strappy_responses_event_callback callback;
  void *callback_data;
  int cancelled;
} strappy_responses_transfer_context;

typedef struct strappy_responses_sse_write_context {
  strappy_sse_parser parser;
  strappy_http_buffer raw_response;
  char *parse_error;
} strappy_responses_sse_write_context;

static int strappy_curl_initialized = 0;
static char *strappy_cainfo_path = NULL;

static void strappy_client_secure_wipe(void *memory, size_t length)
{
  volatile unsigned char *bytes;

  bytes = (volatile unsigned char *)memory;
  while ((bytes != NULL) && (length > 0U)) {
    *bytes++ = 0U;
    length--;
  }
}

static void strappy_client_secure_free(char *value)
{
  if (value != NULL) {
    strappy_client_secure_wipe(value, strlen(value));
    free(value);
  }
}

static void strappy_client_destroy_headers(struct curl_slist *headers)
{
  struct curl_slist *item;

  for (item = headers; item != NULL; item = item->next) {
    if ((item->data != NULL) &&
        (strncasecmp(item->data,
                     "Authorization:",
                     strlen("Authorization:")) == 0)) {
      strappy_client_secure_wipe(item->data, strlen(item->data));
    }
  }
  curl_slist_free_all(headers);
}

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

static int strappy_http_buffer_append(strappy_http_buffer *buffer,
                                      const char *data,
                                      size_t length)
{
  char *next_data;

  if ((buffer == NULL) || ((data == NULL) && (length > 0U)) ||
      (buffer->length > (((size_t)-1) - length - 1U)) ||
      ((buffer->length + length) > STRAPPY_CLIENT_MAX_RESPONSE_BYTES)) {
    return 0;
  }

  next_data = (char *)realloc(buffer->data,
                              buffer->length + length + 1U);
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

static size_t strappy_client_sse_write_callback(void *contents,
                                                size_t size,
                                                size_t nmemb,
                                                void *userp)
{
  strappy_responses_sse_write_context *context;
  size_t real_size;

  if ((contents == NULL) || (userp == NULL) ||
      ((size != 0U) && (nmemb > (((size_t)-1) / size)))) {
    return 0U;
  }
  real_size = size * nmemb;
  context = (strappy_responses_sse_write_context *)userp;
  if (!strappy_http_buffer_append(&context->raw_response,
                                  (const char *)contents,
                                  real_size) ||
      !strappy_sse_parser_feed(&context->parser,
                               (const char *)contents,
                               real_size,
                               &context->parse_error)) {
    return 0U;
  }
  /* libcurl has no successful early-stop callback. Returning zero closes a
   * server that keeps the SSE connection open after its terminal event; the
   * caller recognizes this exact parser state as success. */
  return strappy_sse_parser_is_terminal(&context->parser) ? 0U : real_size;
}

static size_t strappy_client_write_callback(void *contents,
                                            size_t size,
                                            size_t nmemb,
                                            void *userp)
{
  strappy_http_buffer *buffer;
  size_t real_size;

  if ((contents == NULL) || (userp == NULL) ||
      ((size != 0U) && (nmemb > (((size_t)-1) / size)))) {
    return 0U;
  }

  real_size = size * nmemb;
  buffer = (strappy_http_buffer *)userp;
  return strappy_http_buffer_append(buffer,
                                    (const char *)contents,
                                    real_size) ? real_size : 0U;
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

static int strappy_client_require_https(CURL *curl,
                                        const char *url,
                                        char **error_out)
{
  CURLcode code;
#if defined(STRAPPY_ENABLE_LOOPBACK_HTTP_TESTS)
  int is_loopback_test;

  is_loopback_test = (url != NULL) &&
    (strncmp(url, "http://127.0.0.1:", 17U) == 0);
#else
  (void)url;
#endif

#if LIBCURL_VERSION_NUM >= 0x075500
#if defined(STRAPPY_ENABLE_LOOPBACK_HTTP_TESTS)
  code = curl_easy_setopt(curl,
                          CURLOPT_PROTOCOLS_STR,
                          is_loopback_test ? "http,https" : "https");
#else
  code = curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
#endif
  if (code == CURLE_OK) {
#if defined(STRAPPY_ENABLE_LOOPBACK_HTTP_TESTS)
    code = curl_easy_setopt(curl,
                            CURLOPT_REDIR_PROTOCOLS_STR,
                            is_loopback_test ? "http,https" : "https");
#else
    code = curl_easy_setopt(curl,
                            CURLOPT_REDIR_PROTOCOLS_STR,
                            "https");
#endif
  }
#else
#if defined(STRAPPY_ENABLE_LOOPBACK_HTTP_TESTS)
  code = curl_easy_setopt(curl,
                          CURLOPT_PROTOCOLS,
                          is_loopback_test ?
                            (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS) :
                            (long)CURLPROTO_HTTPS);
#else
  code = curl_easy_setopt(curl, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
#endif
  if (code == CURLE_OK) {
#if defined(STRAPPY_ENABLE_LOOPBACK_HTTP_TESTS)
    code = curl_easy_setopt(curl,
                            CURLOPT_REDIR_PROTOCOLS,
                            is_loopback_test ?
                              (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS) :
                              (long)CURLPROTO_HTTPS);
#else
    code = curl_easy_setopt(curl,
                            CURLOPT_REDIR_PROTOCOLS,
                            (long)CURLPROTO_HTTPS);
#endif
  }
#endif
  if (code != CURLE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not require HTTPS for curl: %s",
                                curl_easy_strerror(code));
    return 0;
  }
  return 1;
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

static int strappy_client_add_header(struct curl_slist **headers,
                                     const char *header,
                                     char **error_out)
{
  struct curl_slist *next_headers;

  next_headers = curl_slist_append(*headers, header);
  if (next_headers == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate provider HTTP headers.");
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

char *strappy_client_build_responses_url(const char *endpoint)
{
  static const char *responses_path = "/responses";
  static const char *chat_path = "/chat/completions";
  static const char *openrouter_marker = "/api/v1";
  static const char *openai_marker = "/v1";
  const char *marker;
  size_t endpoint_length;
  size_t base_length;
  char *base;
  char *url;

  if ((endpoint == NULL) || (endpoint[0] == '\0')) {
    return NULL;
  }
  if (strappy_has_suffix(endpoint, responses_path)) {
    return strappy_string_duplicate(endpoint);
  }

  marker = strstr(endpoint, chat_path);
  if (marker != NULL) {
    base = strappy_string_duplicate_length(endpoint,
                                           (size_t)(marker - endpoint));
    if (base == NULL) {
      return NULL;
    }
    url = strappy_join_strings(base, responses_path);
    free(base);
    return url;
  }

  marker = strstr(endpoint, openrouter_marker);
  if (marker != NULL) {
    base_length = (size_t)(marker - endpoint) + strlen(openrouter_marker);
    base = strappy_string_duplicate_length(endpoint, base_length);
    if (base == NULL) {
      return NULL;
    }
    url = strappy_join_strings(base, responses_path);
    free(base);
    return url;
  }

  marker = strstr(endpoint, openai_marker);
  if (marker != NULL) {
    const char *after_marker;

    after_marker = marker + strlen(openai_marker);
    if ((*after_marker == '\0') || (*after_marker == '/')) {
      base_length = (size_t)(marker - endpoint) + strlen(openai_marker);
      base = strappy_string_duplicate_length(endpoint, base_length);
      if (base == NULL) {
        return NULL;
      }
      url = strappy_join_strings(base, responses_path);
      free(base);
      return url;
    }
  }

  endpoint_length = strlen(endpoint);
  while ((endpoint_length > 0U) &&
         (endpoint[endpoint_length - 1U] == '/')) {
    endpoint_length--;
  }
  base = strappy_string_duplicate_length(endpoint, endpoint_length);
  if (base == NULL) {
    return NULL;
  }
  url = strappy_join_strings(base, responses_path);
  free(base);
  return url;
}

static char *strappy_client_build_openrouter_user_models_url(
  const char *endpoint)
{
  static const char *api_marker = "/api/v1";
  static const char *models_path = "/models/user";
  static const char *default_path = "/api/v1/models/user";
  const char *marker;
  size_t endpoint_length;
  size_t base_length;
  char *base;
  char *url;

  if ((endpoint == NULL) || (endpoint[0] == '\0')) {
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
  while ((endpoint_length > 0U) &&
         (endpoint[endpoint_length - 1U] == '/')) {
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
  char *user_agent;
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
  if ((config == NULL) || (config->api_endpoint == NULL) ||
      (config->api_endpoint[0] == '\0') || (config->api_token == NULL) ||
      (config->api_token[0] == '\0')) {
    strappy_set_error(error_out,
                      "OpenRouter model request is not configured.");
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
  auth_header = strappy_join_strings("Authorization: Bearer ",
                                     config->api_token);
  if (auth_header == NULL) {
    free(url);
    strappy_set_error(error_out,
                      "Could not allocate OpenRouter auth header.");
    return 0;
  }

  headers = NULL;
  ok = strappy_client_add_header(&headers,
                                 "Accept: application/json",
                                 error_out) &&
       strappy_client_add_header(&headers,
                                 "X-OpenRouter-Title: Strappy",
                                 error_out) &&
       strappy_client_add_header(&headers, auth_header, error_out);
  strappy_client_secure_free(auth_header);
  if (!ok) {
    strappy_client_destroy_headers(headers);
    free(url);
    return 0;
  }

  user_agent = strappy_identity_copy_user_agent(error_out);
  if (user_agent == NULL) {
    strappy_client_destroy_headers(headers);
    free(url);
    return 0;
  }

  curl = curl_easy_init();
  if (curl == NULL) {
    free(user_agent);
    strappy_client_destroy_headers(headers);
    free(url);
    strappy_set_error(error_out, "Could not create curl handle.");
    return 0;
  }
  if (!strappy_client_require_https(curl, url, error_out)) {
    curl_easy_cleanup(curl);
    free(user_agent);
    strappy_client_destroy_headers(headers);
    free(url);
    return 0;
  }

  strappy_http_buffer_init(&response_buffer);
  http_status = 0L;
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, strappy_client_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response_buffer);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
  curl_easy_setopt(curl,
                   CURLOPT_TIMEOUT,
                   STRAPPY_CLIENT_MODEL_TIMEOUT_SECONDS);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  if ((strappy_cainfo_path != NULL) && (strappy_cainfo_path[0] != '\0')) {
    curl_easy_setopt(curl, CURLOPT_CAINFO, strappy_cainfo_path);
  }

  code = curl_easy_perform(curl);
  if (code == CURLE_OK) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
  }
  curl_easy_cleanup(curl);
  free(user_agent);
  strappy_client_destroy_headers(headers);
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

    root = cJSON_Parse(response_buffer.data);
    api_error = strappy_client_extract_api_error(root);
    cJSON_Delete(root);
    if (api_error != NULL) {
      strappy_set_formatted_error(error_out,
                                  "OpenRouter model request failed (%ld): %s",
                                  http_status,
                                  api_error);
      free(api_error);
    } else {
      strappy_set_formatted_error(
        error_out,
        "OpenRouter model request failed with HTTP %ld.",
        http_status);
    }
    strappy_http_buffer_destroy(&response_buffer);
    return 0;
  }
  if ((response_buffer.data == NULL) ||
      (response_buffer.data[0] == '\0')) {
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

void strappy_responses_http_result_init(strappy_responses_http_result *result)
{
  if (result != NULL) {
    memset(result, 0, sizeof(*result));
  }
}

void strappy_responses_http_result_destroy(strappy_responses_http_result *result)
{
  if (result == NULL) {
    return;
  }
  free(result->response_json);
  free(result->raw_response_body);
  free(result->response_headers);
  free(result->effective_url);
  free(result->transport_error);
  free(result->content_type);
  free(result->request_id);
  free(result->generation_id);
  free(result->rate_limit_limit);
  free(result->rate_limit_remaining);
  free(result->rate_limit_reset);
  free(result->rate_limit_limit_requests);
  free(result->rate_limit_remaining_requests);
  free(result->rate_limit_reset_requests);
  free(result->rate_limit_limit_tokens);
  free(result->rate_limit_remaining_tokens);
  free(result->rate_limit_reset_tokens);
  strappy_responses_http_result_init(result);
}

static long long strappy_client_now_ms(void)
{
  struct timeval now;

  if (gettimeofday(&now, NULL) != 0) {
    return 0LL;
  }
  return ((long long)now.tv_sec * 1000LL) +
    ((long long)now.tv_usec / 1000LL);
}

static long strappy_client_parse_retry_after_seconds(const char *value,
                                                     size_t length)
{
  const char *cursor;
  const char *end;
  char *number_end;
  long seconds;

  if ((value == NULL) || (length == 0U)) {
    return 0L;
  }
  cursor = value;
  end = value + length;
  while ((cursor < end) && isspace((unsigned char)*cursor)) {
    cursor++;
  }
  if (cursor >= end) {
    return 0L;
  }
  errno = 0;
  seconds = strtol(cursor, &number_end, 10);
  if ((number_end == cursor) || (errno != 0) || (seconds <= 0L)) {
    return 0L;
  }
  return seconds;
}

static int strappy_responses_transfer_poll_cancelled(
  strappy_responses_transfer_context *context)
{
  strappy_responses_event event;

  if ((context == NULL) || (context->callback == NULL)) {
    return 0;
  }
  memset(&event, 0, sizeof(event));
  event.type = STRAPPY_RESPONSES_EVENT_CANCELLATION_POLL;
  if (!context->callback(&event, context->callback_data)) {
    context->cancelled = 1;
    return 1;
  }
  return 0;
}

#if LIBCURL_VERSION_NUM >= 0x072000
static int strappy_responses_transfer_progress_callback(void *clientp,
                                                        curl_off_t dltotal,
                                                        curl_off_t dlnow,
                                                        curl_off_t ultotal,
                                                        curl_off_t ulnow)
#else
static int strappy_responses_transfer_progress_callback(void *clientp,
                                                        double dltotal,
                                                        double dlnow,
                                                        double ultotal,
                                                        double ulnow)
#endif
{
  (void)dltotal;
  (void)dlnow;
  (void)ultotal;
  (void)ulnow;
  return strappy_responses_transfer_poll_cancelled(
    (strappy_responses_transfer_context *)clientp) ? 1 : 0;
}

static int strappy_responses_replace_header_value(char **target,
                                                   const char *contents,
                                                   size_t length)
{
  size_t start;
  size_t end;
  char *copy;

  if ((target == NULL) || (contents == NULL)) {
    return 0;
  }
  start = 0U;
  end = length;
  while ((start < end) && isspace((unsigned char)contents[start])) {
    start++;
  }
  while ((end > start) && isspace((unsigned char)contents[end - 1U])) {
    end--;
  }
  copy = strappy_string_duplicate_length(contents + start, end - start);
  if (copy == NULL) {
    return 0;
  }
  free(*target);
  *target = copy;
  return 1;
}

static int strappy_responses_capture_named_header(
  char *contents,
  size_t length,
  const char *name,
  char **target)
{
  size_t name_length;

  name_length = strlen(name);
  if ((length <= name_length) ||
      (strncasecmp(contents, name, name_length) != 0)) {
    return 1;
  }
  return strappy_responses_replace_header_value(target,
                                                 contents + name_length,
                                                 length - name_length);
}

static size_t strappy_responses_header_callback(char *contents,
                                                 size_t size,
                                                 size_t nmemb,
                                                 void *userp)
{
  strappy_responses_header_context *context;
  strappy_responses_http_result *result;
  size_t real_size;

  if ((contents == NULL) || (userp == NULL) ||
      ((size != 0U) && (nmemb > (((size_t)-1) / size)))) {
    return 0U;
  }
  real_size = size * nmemb;
  context = (strappy_responses_header_context *)userp;
  result = context->result;
  if ((result == NULL) ||
      !strappy_http_buffer_append(&context->raw_headers,
                                  contents,
                                  real_size) ||
      !strappy_responses_capture_named_header(contents,
                                              real_size,
                                              "X-Request-Id:",
                                              &result->request_id) ||
      !strappy_responses_capture_named_header(contents,
                                              real_size,
                                              "OpenRouter-Request-Id:",
                                              &result->request_id) ||
      !strappy_responses_capture_named_header(contents,
                                              real_size,
                                              "X-Generation-Id:",
                                              &result->generation_id) ||
      !strappy_responses_capture_named_header(contents,
                                              real_size,
                                              "X-RateLimit-Limit:",
                                              &result->rate_limit_limit) ||
      !strappy_responses_capture_named_header(contents,
                                              real_size,
                                              "X-RateLimit-Remaining:",
                                              &result->rate_limit_remaining) ||
      !strappy_responses_capture_named_header(contents,
                                              real_size,
                                              "X-RateLimit-Reset:",
                                              &result->rate_limit_reset) ||
      !strappy_responses_capture_named_header(
        contents,
        real_size,
        "X-RateLimit-Limit-Requests:",
        &result->rate_limit_limit_requests) ||
      !strappy_responses_capture_named_header(
        contents,
        real_size,
        "X-RateLimit-Remaining-Requests:",
        &result->rate_limit_remaining_requests) ||
      !strappy_responses_capture_named_header(
        contents,
        real_size,
        "X-RateLimit-Reset-Requests:",
        &result->rate_limit_reset_requests) ||
      !strappy_responses_capture_named_header(
        contents,
        real_size,
        "X-RateLimit-Limit-Tokens:",
        &result->rate_limit_limit_tokens) ||
      !strappy_responses_capture_named_header(
        contents,
        real_size,
        "X-RateLimit-Remaining-Tokens:",
        &result->rate_limit_remaining_tokens) ||
      !strappy_responses_capture_named_header(
        contents,
        real_size,
        "X-RateLimit-Reset-Tokens:",
        &result->rate_limit_reset_tokens)) {
    return 0U;
  }

  if ((real_size > strlen("Retry-After:")) &&
      (strncasecmp(contents,
                   "Retry-After:",
                   strlen("Retry-After:")) == 0)) {
    result->retry_after_seconds =
      strappy_client_parse_retry_after_seconds(
        contents + strlen("Retry-After:"),
        real_size - strlen("Retry-After:"));
  }
  return real_size;
}

static int strappy_responses_copy_curl_info_string(CURL *curl,
                                                   CURLINFO info,
                                                   char **target)
{
  char *value;
  CURLcode code;

  value = NULL;
  code = curl_easy_getinfo(curl, info, &value);
  if ((code != CURLE_OK) || (value == NULL) || (value[0] == '\0')) {
    return 1;
  }
  *target = strappy_string_duplicate(value);
  return (*target != NULL) ? 1 : 0;
}

static int strappy_client_header_value_is_safe(const char *value,
                                                size_t maximum_length)
{
  size_t index;
  size_t length;

  if ((value == NULL) || (value[0] == '\0')) {
    return 0;
  }
  length = strlen(value);
  if (length > maximum_length) {
    return 0;
  }
  for (index = 0U; index < length; index++) {
    if (((unsigned char)value[index] < 0x20U) ||
        ((unsigned char)value[index] == 0x7fU)) {
      return 0;
    }
  }
  return 1;
}

int strappy_client_send_provider_responses_json_with_transport(
  const strappy_config *config,
  strappy_provider_kind provider,
  strappy_responses_response_transport response_transport,
  const char *bearer_token,
  const char *chatgpt_account_id,
  const char *session_request_id,
  const char *request_json,
  strappy_responses_http_result *result,
  strappy_responses_event_callback callback,
  void *callback_data,
  char **error_out)
{
  CURL *curl;
  CURLcode code;
  struct curl_slist *headers;
  strappy_http_buffer response_buffer;
  strappy_responses_header_context header_context;
  strappy_responses_transfer_context transfer_context;
  strappy_responses_sse_write_context sse_context;
  char curl_error[CURL_ERROR_SIZE];
  char *auth_header;
  char *account_header;
  char *session_header;
  char *client_request_header;
  char *user_agent;
  char *url;
  const char *token;
  const strappy_provider_definition *definition;
  size_t request_length;
  int uses_chatgpt_headers;
  int uses_openrouter_headers;
  int token_is_present;
  int uses_sse;
  int sse_ended_early;
  int ok;

  if (result == NULL) {
    strappy_set_error(error_out,
                      "Responses request did not provide an HTTP result.");
    return 0;
  }
  strappy_responses_http_result_init(result);
  definition = strappy_provider_for_kind(provider);
  uses_chatgpt_headers = (definition != NULL) &&
    (definition->request_profile == STRAPPY_PROVIDER_REQUEST_CHATGPT_CODEX);
  uses_openrouter_headers = (definition != NULL) &&
    (definition->request_profile == STRAPPY_PROVIDER_REQUEST_OPENROUTER);
  uses_sse =
    (response_transport == STRAPPY_RESPONSES_RESPONSE_TRANSPORT_SSE) ? 1 : 0;
  token = (bearer_token != NULL) ? bearer_token :
    ((config != NULL) ? config->api_token : NULL);
  token_is_present = (token != NULL) && (token[0] != '\0');
  if (token_is_present &&
      !strappy_client_header_value_is_safe(token, 2U * 1024U * 1024U)) {
    strappy_set_error(
      error_out,
      "Responses bearer credential contains an unsupported control character.");
    return 0;
  }
  if ((config == NULL) || (request_json == NULL) ||
      (request_json[0] == '\0') || (config->api_endpoint == NULL) ||
      (config->api_endpoint[0] == '\0') ||
      (definition == NULL) || !strappy_provider_is_available(definition) ||
      ((response_transport != STRAPPY_RESPONSES_RESPONSE_TRANSPORT_JSON) &&
       (response_transport != STRAPPY_RESPONSES_RESPONSE_TRANSPORT_SSE)) ||
      (!token_is_present &&
       (definition->credential_kind !=
        STRAPPY_PROVIDER_CREDENTIAL_OPTIONAL_BEARER)) ||
      (uses_chatgpt_headers &&
       !strappy_client_header_value_is_safe(chatgpt_account_id, 512U)) ||
      ((session_request_id != NULL) &&
       !strappy_client_header_value_is_safe(session_request_id, 512U))) {
    strappy_set_error(error_out, "Responses request is not configured.");
    return 0;
  }
  if (!strappy_client_ensure_curl_initialized(error_out)) {
    return 0;
  }

  request_length = strlen(request_json);
  if (request_length > (size_t)LONG_MAX) {
    strappy_set_error(error_out, "Responses request body is too large.");
    return 0;
  }
  url = strappy_client_build_responses_url(config->api_endpoint);
  if (url == NULL) {
    strappy_set_error(error_out, "Could not allocate Responses API URL.");
    return 0;
  }
  auth_header = token_is_present ?
    strappy_join_strings("Authorization: Bearer ", token) : NULL;
  if (token_is_present && (auth_header == NULL)) {
    free(url);
    strappy_set_error(error_out,
                      "Could not allocate Responses auth header.");
    return 0;
  }

  account_header = uses_chatgpt_headers ?
    strappy_join_strings("chatgpt-account-id: ", chatgpt_account_id) : NULL;
  session_header = (uses_chatgpt_headers && (session_request_id != NULL)) ?
    strappy_join_strings("session-id: ", session_request_id) : NULL;
  client_request_header =
    (uses_chatgpt_headers && (session_request_id != NULL)) ?
      strappy_join_strings("x-client-request-id: ", session_request_id) : NULL;
  if (uses_chatgpt_headers &&
      ((account_header == NULL) ||
       ((session_request_id != NULL) &&
        ((session_header == NULL) || (client_request_header == NULL))))) {
    strappy_client_secure_free(auth_header);
    free(account_header);
    free(session_header);
    free(client_request_header);
    free(url);
    strappy_set_error(error_out,
                      "Could not allocate ChatGPT request headers.");
    return 0;
  }

  headers = NULL;
  ok = strappy_client_add_header(&headers,
                                 "Content-Type: application/json",
                                 error_out) &&
       strappy_client_add_header(
         &headers,
         uses_sse ? "Accept: text/event-stream" : "Accept: application/json",
         error_out) &&
       (!uses_chatgpt_headers ||
        strappy_client_add_header(&headers,
                                  "OpenAI-Beta: responses=experimental",
                                  error_out)) &&
       (!uses_chatgpt_headers ||
        strappy_client_add_header(&headers,
                                  "originator: strappy",
                                  error_out)) &&
       (!uses_chatgpt_headers ||
        strappy_client_add_header(&headers, account_header, error_out)) &&
       (!uses_chatgpt_headers || (session_header == NULL) ||
        strappy_client_add_header(&headers, session_header, error_out)) &&
       (!uses_chatgpt_headers || (client_request_header == NULL) ||
        strappy_client_add_header(&headers, client_request_header, error_out)) &&
       (!uses_openrouter_headers ||
        strappy_client_add_header(&headers,
                                  "X-OpenRouter-Title: Strappy",
                                  error_out)) &&
       (!uses_openrouter_headers ||
        strappy_client_add_header(&headers,
                                  "X-OpenRouter-Metadata: enabled",
                                  error_out)) &&
       ((auth_header == NULL) ||
        strappy_client_add_header(&headers, auth_header, error_out));
  strappy_client_secure_free(auth_header);
  free(account_header);
  free(session_header);
  free(client_request_header);
  if (!ok) {
    strappy_client_destroy_headers(headers);
    free(url);
    return 0;
  }

  user_agent = strappy_identity_copy_user_agent(error_out);
  if (user_agent == NULL) {
    strappy_client_destroy_headers(headers);
    free(url);
    return 0;
  }

  curl = curl_easy_init();
  if (curl == NULL) {
    free(user_agent);
    strappy_client_destroy_headers(headers);
    free(url);
    strappy_set_error(error_out, "Could not create Responses curl handle.");
    return 0;
  }
  if (!strappy_client_require_https(curl, url, error_out)) {
    curl_easy_cleanup(curl);
    free(user_agent);
    strappy_client_destroy_headers(headers);
    free(url);
    return 0;
  }

  strappy_http_buffer_init(&response_buffer);
  strappy_sse_parser_init(&sse_context.parser);
  strappy_http_buffer_init(&sse_context.raw_response);
  sse_context.parse_error = NULL;
  header_context.result = result;
  strappy_http_buffer_init(&header_context.raw_headers);
  memset(&transfer_context, 0, sizeof(transfer_context));
  transfer_context.callback = callback;
  transfer_context.callback_data = callback_data;
  memset(curl_error, 0, sizeof(curl_error));
  result->started_at_ms = strappy_client_now_ms();
  result->request_bytes = (long long)request_length;

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_json);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)request_length);
  curl_easy_setopt(curl,
                   CURLOPT_HEADERFUNCTION,
                   strappy_responses_header_callback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)&header_context);
  curl_easy_setopt(curl,
                   CURLOPT_WRITEFUNCTION,
                   uses_sse ? strappy_client_sse_write_callback :
                     strappy_client_write_callback);
  curl_easy_setopt(curl,
                   CURLOPT_WRITEDATA,
                   uses_sse ? (void *)&sse_context : (void *)&response_buffer);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
#if LIBCURL_VERSION_NUM >= 0x072000
  curl_easy_setopt(curl,
                   CURLOPT_XFERINFOFUNCTION,
                   strappy_responses_transfer_progress_callback);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void *)&transfer_context);
#else
  curl_easy_setopt(curl,
                   CURLOPT_PROGRESSFUNCTION,
                   strappy_responses_transfer_progress_callback);
  curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, (void *)&transfer_context);
#endif
  curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
  curl_easy_setopt(curl,
                   CURLOPT_TIMEOUT,
                   STRAPPY_CLIENT_RESPONSES_TIMEOUT_SECONDS);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  if ((strappy_cainfo_path != NULL) && (strappy_cainfo_path[0] != '\0')) {
    curl_easy_setopt(curl, CURLOPT_CAINFO, strappy_cainfo_path);
  }

  code = strappy_responses_transfer_poll_cancelled(&transfer_context) ?
    CURLE_ABORTED_BY_CALLBACK : curl_easy_perform(curl);
  sse_ended_early = uses_sse &&
    strappy_sse_parser_is_terminal(&sse_context.parser) &&
    (code == CURLE_WRITE_ERROR);
  if (sse_ended_early) {
    code = CURLE_OK;
  }
  result->completed_at_ms = strappy_client_now_ms();
  result->curl_code = (long)code;
  result->cancelled = transfer_context.cancelled;
  result->response_event_received = uses_sse ?
    strappy_sse_parser_has_events(&sse_context.parser) : 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result->http_status);
  curl_easy_getinfo(curl,
                    CURLINFO_NAMELOOKUP_TIME,
                    &result->name_lookup_seconds);
  curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &result->connect_seconds);
  curl_easy_getinfo(curl,
                    CURLINFO_STARTTRANSFER_TIME,
                    &result->start_transfer_seconds);
  curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &result->total_seconds);

  ok = strappy_responses_copy_curl_info_string(curl,
                                               CURLINFO_EFFECTIVE_URL,
                                               &result->effective_url) &&
       strappy_responses_copy_curl_info_string(curl,
                                               CURLINFO_CONTENT_TYPE,
                                               &result->content_type);
  curl_easy_cleanup(curl);
  free(user_agent);
  strappy_client_destroy_headers(headers);
  free(url);

  if (uses_sse && (result->http_status >= 200L) &&
      (result->http_status < 300L)) {
    char *sse_error;

    sse_error = NULL;
    if ((sse_context.parse_error == NULL) &&
        strappy_sse_parser_finish(&sse_context.parser, &sse_error)) {
      result->response_json =
        strappy_sse_parser_take_terminal_json(&sse_context.parser);
    } else {
      const char *message;

      message = (sse_context.parse_error != NULL) ?
        sse_context.parse_error :
        ((sse_error != NULL) ? sse_error :
          "ChatGPT SSE response could not be parsed.");
      result->transport_error = strappy_string_duplicate(message);
      result->response_json = strappy_string_duplicate(
        (sse_context.raw_response.data != NULL) ?
          sse_context.raw_response.data : "");
    }
    free(sse_error);
  } else {
    result->response_json = strappy_string_duplicate(
      uses_sse ?
        ((sse_context.raw_response.data != NULL) ?
          sse_context.raw_response.data : "") :
        ((response_buffer.data != NULL) ? response_buffer.data : ""));
  }
#if STRAPPY_RAW_JSON_DEBUG_CAPTURE
  {
    const strappy_http_buffer *raw_response;

    raw_response = uses_sse ? &sse_context.raw_response : &response_buffer;
    result->raw_response_body = (char *)malloc(raw_response->length + 1U);
    /* Diagnostic capture is best effort and must not fail the API request. */
    if (result->raw_response_body != NULL) {
      if (raw_response->length > 0U) {
        memcpy(result->raw_response_body,
               raw_response->data,
               raw_response->length);
      }
      result->raw_response_body[raw_response->length] = '\0';
      result->raw_response_body_length = raw_response->length;
    } else {
      printf("Strappy debug capture skipped raw response: could not allocate "
             "%lu bytes.\n",
             (unsigned long)(raw_response->length + 1U));
    }
  }
#endif
  result->response_headers = strappy_string_duplicate(
    (header_context.raw_headers.data != NULL) ?
      header_context.raw_headers.data : "");
  if (uses_sse) {
    result->response_bytes =
      (sse_context.parser.stream_bytes <= (size_t)LLONG_MAX) ?
        (long long)sse_context.parser.stream_bytes : LLONG_MAX;
  } else {
    result->response_bytes = (response_buffer.length <= (size_t)LLONG_MAX) ?
      (long long)response_buffer.length : LLONG_MAX;
  }
  strappy_http_buffer_destroy(&response_buffer);
  strappy_sse_parser_destroy(&sse_context.parser);
  strappy_http_buffer_destroy(&sse_context.raw_response);
  free(sse_context.parse_error);
  strappy_http_buffer_destroy(&header_context.raw_headers);
  if (!ok || (result->response_json == NULL) ||
      (result->response_headers == NULL)) {
    strappy_set_error(error_out,
                      "Could not allocate Responses HTTP result.");
    return 0;
  }

  if ((code != CURLE_OK) && (result->transport_error == NULL)) {
    const char *message;

    message = result->cancelled ? "Responses request was cancelled." :
      ((curl_error[0] != '\0') ? curl_error : curl_easy_strerror(code));
    result->transport_error = strappy_string_duplicate(message);
    if (result->transport_error == NULL) {
      strappy_set_error(error_out,
                        "Could not allocate Responses transport error.");
      return 0;
    }
  }
  return 1;
}

int strappy_client_send_provider_responses_json(
  const strappy_config *config,
  strappy_provider_kind provider,
  const char *bearer_token,
  const char *chatgpt_account_id,
  const char *session_request_id,
  const char *request_json,
  strappy_responses_http_result *result,
  strappy_responses_event_callback callback,
  void *callback_data,
  char **error_out)
{
  const strappy_provider_definition *definition;
  strappy_responses_response_transport response_transport;

  definition = strappy_provider_for_kind(provider);
  if (definition == NULL) {
    strappy_set_error(error_out, "Responses provider is not registered.");
    return 0;
  }
  response_transport =
    (definition->response_transport == STRAPPY_PROVIDER_TRANSPORT_SSE) ?
      STRAPPY_RESPONSES_RESPONSE_TRANSPORT_SSE :
      STRAPPY_RESPONSES_RESPONSE_TRANSPORT_JSON;
  return strappy_client_send_provider_responses_json_with_transport(
    config,
    provider,
    response_transport,
    bearer_token,
    chatgpt_account_id,
    session_request_id,
    request_json,
    result,
    callback,
    callback_data,
    error_out);
}

int strappy_client_send_responses_json(
  const strappy_config *config,
  const char *request_json,
  strappy_responses_http_result *result,
  strappy_responses_event_callback callback,
  void *callback_data,
  char **error_out)
{
  return strappy_client_send_provider_responses_json(
    config,
    STRAPPY_PROVIDER_KIND_OPENROUTER,
    (config != NULL) ? config->api_token : NULL,
    NULL,
    NULL,
    request_json,
    result,
    callback,
    callback_data,
    error_out);
}
