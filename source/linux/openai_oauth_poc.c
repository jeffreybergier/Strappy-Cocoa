#define _POSIX_C_SOURCE 200809L

#include "strappy_identity.h"

#include <cJSON.h>
#include <curl/curl.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Manual, non-shipping compatibility probe for ChatGPT device OAuth.
 *
 * The endpoint and field details below are not documented as a public OpenAI
 * third-party API. They are isolated here so the experiment cannot affect the
 * app or the automated Linux test suite. The default public client ID and wire
 * behavior were independently implemented from the temporary Pi reference at
 * commit 94373d815d2b4a3a48864d5341afc824b8db45e3.
 */

#define POC_REFERENCE_CLIENT_ID "app_EMoamEEZ73f0CkXaXp7hrann"
#define POC_DEVICE_START_URL \
  "https://auth.openai.com/api/accounts/deviceauth/usercode"
#define POC_DEVICE_POLL_URL \
  "https://auth.openai.com/api/accounts/deviceauth/token"
#define POC_TOKEN_URL "https://auth.openai.com/oauth/token"
#define POC_DEVICE_VERIFICATION_URL "https://auth.openai.com/codex/device"
#define POC_DEVICE_REDIRECT_URI \
  "https://auth.openai.com/deviceauth/callback"
#define POC_RESPONSES_URL \
  "https://chatgpt.com/backend-api/codex/responses"
#define POC_MODEL "gpt-5.6-luna"
#define POC_ORIGINATOR "strappy"

#define POC_DEVICE_TIMEOUT_SECONDS 900L
#define POC_HTTP_TIMEOUT_SECONDS 60L
#define POC_RESPONSE_TIMEOUT_SECONDS 900L
#define POC_CONNECT_TIMEOUT_SECONDS 30L
#define POC_MIN_POLL_INTERVAL_SECONDS 1L
#define POC_MAX_POLL_INTERVAL_SECONDS 300L
#define POC_MAX_HTTP_BODY_BYTES (1024U * 1024U)
#define POC_MAX_SSE_PENDING_BYTES (2U * 1024U * 1024U)
#define POC_MAX_OUTPUT_BYTES (1024U * 1024U)
#define POC_MAX_TOKEN_BYTES (512U * 1024U)
#define POC_MAX_DEVICE_ID_BYTES 4096U
#define POC_MAX_USER_CODE_BYTES 256U
#define POC_MAX_AUTH_CODE_BYTES (64U * 1024U)
#define POC_MAX_ACCOUNT_ID_BYTES 4096U

#define POC_REQUEST_JSON \
  "{" \
  "\"model\":\"" POC_MODEL "\"," \
  "\"store\":false," \
  "\"stream\":true," \
  "\"instructions\":\"You are a concise test assistant. Follow the " \
    "user's exact response-format request.\"," \
  "\"input\":[{" \
    "\"role\":\"user\"," \
    "\"content\":[{" \
      "\"type\":\"input_text\"," \
      "\"text\":\"Reply with exactly this text and nothing else: " \
        "Strappy OAuth PoC succeeded.\"" \
    "}]" \
  "}]," \
  "\"text\":{\"verbosity\":\"low\"}," \
  "\"include\":[\"reasoning.encrypted_content\"]," \
  "\"tool_choice\":\"none\"," \
  "\"parallel_tool_calls\":true," \
  "\"reasoning\":{\"effort\":\"low\",\"summary\":\"auto\"}" \
  "}"

typedef struct poc_buffer {
  char *data;
  size_t length;
  size_t capacity;
  size_t limit;
  int overflowed;
} poc_buffer;

typedef struct poc_http_response {
  long status;
  poc_buffer body;
} poc_http_response;

typedef struct poc_device_authorization {
  char *device_id;
  char *user_code;
  long interval_seconds;
} poc_device_authorization;

typedef struct poc_credentials {
  char *access_token;
  char *refresh_token;
  char *account_id;
  long expires_in_seconds;
} poc_credentials;

typedef struct poc_sse_context {
  poc_buffer pending;
  poc_buffer output;
  int terminal;
  int incomplete;
  int failed;
  char error[512];
} poc_sse_context;

static volatile sig_atomic_t poc_cancelled = 0;

static void poc_signal_handler(int signal_number)
{
  (void)signal_number;
  poc_cancelled = 1;
}

static void poc_secure_wipe(void *memory, size_t length)
{
  volatile unsigned char *bytes;

  if (memory == NULL) {
    return;
  }
  bytes = (volatile unsigned char *)memory;
  while (length > 0U) {
    *bytes = 0U;
    bytes++;
    length--;
  }
}

static void poc_secure_free(char *value)
{
  if (value != NULL) {
    poc_secure_wipe(value, strlen(value));
    free(value);
  }
}

static void poc_buffer_init(poc_buffer *buffer, size_t limit)
{
  if (buffer != NULL) {
    memset(buffer, 0, sizeof(*buffer));
    buffer->limit = limit;
  }
}

static void poc_buffer_destroy(poc_buffer *buffer, int sensitive)
{
  if (buffer == NULL) {
    return;
  }
  if (sensitive && (buffer->data != NULL)) {
    poc_secure_wipe(buffer->data, buffer->capacity);
  }
  free(buffer->data);
  poc_buffer_init(buffer, buffer->limit);
}

static int poc_buffer_reserve(poc_buffer *buffer, size_t additional)
{
  size_t needed;
  size_t capacity;
  char *next_data;

  if ((buffer == NULL) ||
      (buffer->length > (((size_t)-1) - additional - 1U))) {
    return 0;
  }
  needed = buffer->length + additional + 1U;
  if ((needed - 1U) > buffer->limit) {
    buffer->overflowed = 1;
    return 0;
  }
  if (needed <= buffer->capacity) {
    return 1;
  }

  capacity = (buffer->capacity > 0U) ? buffer->capacity : 256U;
  while (capacity < needed) {
    if (capacity > (((size_t)-1) / 2U)) {
      capacity = needed;
      break;
    }
    capacity *= 2U;
  }
  if (capacity > (buffer->limit + 1U)) {
    capacity = buffer->limit + 1U;
  }
  if (capacity < needed) {
    buffer->overflowed = 1;
    return 0;
  }

  next_data = (char *)realloc(buffer->data, capacity);
  if (next_data == NULL) {
    return 0;
  }
  buffer->data = next_data;
  buffer->capacity = capacity;
  return 1;
}

static int poc_buffer_append(poc_buffer *buffer,
                             const char *data,
                             size_t length)
{
  if ((buffer == NULL) || ((data == NULL) && (length > 0U)) ||
      !poc_buffer_reserve(buffer, length)) {
    return 0;
  }
  if (length > 0U) {
    memcpy(buffer->data + buffer->length, data, length);
  }
  buffer->length += length;
  buffer->data[buffer->length] = '\0';
  return 1;
}

static int poc_buffer_append_character(poc_buffer *buffer, char value)
{
  return poc_buffer_append(buffer, &value, 1U);
}

static void poc_buffer_consume(poc_buffer *buffer, size_t length)
{
  if ((buffer == NULL) || (length > buffer->length)) {
    return;
  }
  if (length < buffer->length) {
    memmove(buffer->data,
            buffer->data + length,
            buffer->length - length);
  }
  buffer->length -= length;
  if (buffer->data != NULL) {
    buffer->data[buffer->length] = '\0';
  }
}

static char *poc_duplicate_bounded(const char *value, size_t limit)
{
  size_t length;
  char *copy;

  if (value == NULL) {
    return NULL;
  }
  length = strlen(value);
  if ((length == 0U) || (length > limit)) {
    return NULL;
  }
  copy = (char *)malloc(length + 1U);
  if (copy == NULL) {
    return NULL;
  }
  memcpy(copy, value, length + 1U);
  return copy;
}

static void poc_http_response_init(poc_http_response *response)
{
  if (response != NULL) {
    response->status = 0L;
    poc_buffer_init(&response->body, POC_MAX_HTTP_BODY_BYTES);
  }
}

static void poc_http_response_destroy(poc_http_response *response,
                                      int sensitive)
{
  if (response != NULL) {
    poc_buffer_destroy(&response->body, sensitive);
    response->status = 0L;
  }
}

static size_t poc_http_write_callback(void *contents,
                                      size_t size,
                                      size_t count,
                                      void *user_data)
{
  poc_http_response *response;
  size_t length;

  if ((contents == NULL) || (user_data == NULL) ||
      ((size != 0U) && (count > (((size_t)-1) / size)))) {
    return 0U;
  }
  length = size * count;
  response = (poc_http_response *)user_data;
  return poc_buffer_append(&response->body,
                           (const char *)contents,
                           length) ? length : 0U;
}

#if LIBCURL_VERSION_NUM >= 0x072000
static int poc_http_progress_callback(void *client_data,
                                      curl_off_t download_total,
                                      curl_off_t download_now,
                                      curl_off_t upload_total,
                                      curl_off_t upload_now)
#else
static int poc_http_progress_callback(void *client_data,
                                      double download_total,
                                      double download_now,
                                      double upload_total,
                                      double upload_now)
#endif
{
  (void)client_data;
  (void)download_total;
  (void)download_now;
  (void)upload_total;
  (void)upload_now;
  return poc_cancelled ? 1 : 0;
}

static int poc_add_header(struct curl_slist **headers, const char *value)
{
  struct curl_slist *next_headers;

  if ((headers == NULL) || (value == NULL)) {
    return 0;
  }
  next_headers = curl_slist_append(*headers, value);
  if (next_headers == NULL) {
    return 0;
  }
  *headers = next_headers;
  return 1;
}

static int poc_configure_tls(CURL *curl)
{
  const char *cainfo;
  CURLcode code;

#if LIBCURL_VERSION_NUM >= 0x075500
  code = curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
  if (code == CURLE_OK) {
    code = curl_easy_setopt(curl,
                            CURLOPT_REDIR_PROTOCOLS_STR,
                            "https");
  }
#else
  code = curl_easy_setopt(curl, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
  if (code == CURLE_OK) {
    code = curl_easy_setopt(curl,
                            CURLOPT_REDIR_PROTOCOLS,
                            (long)CURLPROTO_HTTPS);
  }
#endif
  if (code == CURLE_OK) {
    code = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  }
  if (code == CURLE_OK) {
    code = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  }
  cainfo = getenv("STRAPPY_OAUTH_CAINFO");
  if ((code == CURLE_OK) &&
      (cainfo != NULL) &&
      (cainfo[0] != '\0')) {
    code = curl_easy_setopt(curl, CURLOPT_CAINFO, cainfo);
  }
  if (code != CURLE_OK) {
    fprintf(stderr,
            "Could not configure HTTPS: %s\n",
            curl_easy_strerror(code));
    return 0;
  }
  return 1;
}

static char *poc_copy_user_agent(void)
{
  char *error;
  char *user_agent;

  error = NULL;
  user_agent = strappy_identity_copy_user_agent(&error);
  if (user_agent == NULL) {
    fprintf(stderr,
            "Could not load the Strappy user agent: %s\n",
            (error != NULL) ? error : "Unknown identity error.");
  }
  free(error);
  return user_agent;
}

static int poc_http_post(const char *url,
                         const char *content_type_header,
                         const char *accept_header,
                         const char *body,
                         long timeout_seconds,
                         poc_http_response *response)
{
  CURL *curl;
  CURLcode code;
  struct curl_slist *headers;
  size_t body_length;
  char *user_agent;
  char curl_error[CURL_ERROR_SIZE];

  if ((url == NULL) || (content_type_header == NULL) ||
      (accept_header == NULL) || (body == NULL) || (response == NULL)) {
    fprintf(stderr, "Invalid HTTP request configuration.\n");
    return 0;
  }
  body_length = strlen(body);
  if (body_length > (size_t)LONG_MAX) {
    fprintf(stderr, "HTTP request body is too large.\n");
    return 0;
  }

  poc_http_response_init(response);
  headers = NULL;
  if (!poc_add_header(&headers, content_type_header) ||
      !poc_add_header(&headers, accept_header) ||
      !poc_add_header(&headers, "Expect:")) {
    fprintf(stderr, "Could not allocate HTTP headers.\n");
    curl_slist_free_all(headers);
    return 0;
  }

  user_agent = poc_copy_user_agent();
  if (user_agent == NULL) {
    curl_slist_free_all(headers);
    return 0;
  }

  curl = curl_easy_init();
  if (curl == NULL) {
    fprintf(stderr, "Could not create curl handle.\n");
    free(user_agent);
    curl_slist_free_all(headers);
    return 0;
  }

  memset(curl_error, 0, sizeof(curl_error));
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_length);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, poc_http_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)response);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,
                   POC_CONNECT_TIMEOUT_SECONDS);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
#if LIBCURL_VERSION_NUM >= 0x072000
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,
                   poc_http_progress_callback);
#else
  curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION,
                   poc_http_progress_callback);
#endif
  if (!poc_configure_tls(curl)) {
    curl_easy_cleanup(curl);
    free(user_agent);
    curl_slist_free_all(headers);
    poc_http_response_destroy(response);
    return 0;
  }

  code = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response->status);
  curl_easy_cleanup(curl);
  free(user_agent);
  curl_slist_free_all(headers);

  if (code != CURLE_OK) {
    if (poc_cancelled) {
      fprintf(stderr, "Request cancelled.\n");
    } else if (response->body.overflowed) {
      fprintf(stderr, "HTTP response exceeded the PoC size limit.\n");
    } else {
      fprintf(stderr,
              "HTTP request failed: %s\n",
              (curl_error[0] != '\0') ? curl_error :
                curl_easy_strerror(code));
    }
    return 0;
  }
  return 1;
}

static const char *poc_error_detail(cJSON *root)
{
  cJSON *error;
  cJSON *detail;
  cJSON *response;

  if (!cJSON_IsObject(root)) {
    return NULL;
  }
  error = cJSON_GetObjectItem(root, "error");
  if (cJSON_IsString(error) && (error->valuestring != NULL)) {
    return error->valuestring;
  }
  if (cJSON_IsObject(error)) {
    detail = cJSON_GetObjectItem(error, "message");
    if (cJSON_IsString(detail) && (detail->valuestring != NULL)) {
      return detail->valuestring;
    }
    detail = cJSON_GetObjectItem(error, "code");
    if (cJSON_IsString(detail) && (detail->valuestring != NULL)) {
      return detail->valuestring;
    }
  }
  detail = cJSON_GetObjectItem(root, "error_description");
  if (cJSON_IsString(detail) && (detail->valuestring != NULL)) {
    return detail->valuestring;
  }
  detail = cJSON_GetObjectItem(root, "message");
  if (cJSON_IsString(detail) && (detail->valuestring != NULL)) {
    return detail->valuestring;
  }

  response = cJSON_GetObjectItem(root, "response");
  if (cJSON_IsObject(response)) {
    error = cJSON_GetObjectItem(response, "error");
    if (cJSON_IsObject(error)) {
      detail = cJSON_GetObjectItem(error, "message");
      if (cJSON_IsString(detail) && (detail->valuestring != NULL)) {
        return detail->valuestring;
      }
      detail = cJSON_GetObjectItem(error, "code");
      if (cJSON_IsString(detail) && (detail->valuestring != NULL)) {
        return detail->valuestring;
      }
    }
  }
  return NULL;
}

static const char *poc_error_code(cJSON *root)
{
  cJSON *error;
  cJSON *code;

  if (!cJSON_IsObject(root)) {
    return NULL;
  }
  error = cJSON_GetObjectItem(root, "error");
  if (cJSON_IsString(error) && (error->valuestring != NULL)) {
    return error->valuestring;
  }
  if (cJSON_IsObject(error)) {
    code = cJSON_GetObjectItem(error, "code");
    if (cJSON_IsString(code) && (code->valuestring != NULL)) {
      return code->valuestring;
    }
  }
  return NULL;
}

static void poc_print_http_error(const char *operation,
                                 const poc_http_response *response)
{
  cJSON *root;
  const char *detail;
  char *copy;

  detail = NULL;
  copy = NULL;
  root = NULL;
  if ((response != NULL) && (response->body.data != NULL)) {
    root = cJSON_Parse(response->body.data);
    detail = poc_error_detail(root);
    if (detail != NULL) {
      copy = poc_duplicate_bounded(detail, 500U);
    }
  }
  if (copy != NULL) {
    fprintf(stderr,
            "%s failed with HTTP %ld: %s\n",
            operation,
            (response != NULL) ? response->status : 0L,
            copy);
  } else {
    fprintf(stderr,
            "%s failed with HTTP %ld.\n",
            operation,
            (response != NULL) ? response->status : 0L);
  }
  free(copy);
  cJSON_Delete(root);
}

static void poc_wipe_json_strings(cJSON *value)
{
  cJSON *child;

  if (value == NULL) {
    return;
  }
  if (cJSON_IsString(value) && (value->valuestring != NULL)) {
    poc_secure_wipe(value->valuestring, strlen(value->valuestring));
  }
  for (child = value->child; child != NULL; child = child->next) {
    poc_wipe_json_strings(child);
  }
}

static int poc_parse_poll_interval(cJSON *item, long *interval_out)
{
  long interval;

  if (interval_out == NULL) {
    return 0;
  }
  if (cJSON_IsNumber(item)) {
    if (!(item->valuedouble >= 0.0) ||
        (item->valuedouble > (double)POC_MAX_POLL_INTERVAL_SECONDS)) {
      return 0;
    }
    interval = (long)item->valuedouble;
    if ((double)interval != item->valuedouble) {
      return 0;
    }
  } else if (cJSON_IsString(item) && (item->valuestring != NULL)) {
    char *end;

    errno = 0;
    end = NULL;
    interval = strtol(item->valuestring, &end, 10);
    if ((errno != 0) || (end == item->valuestring) ||
        (end == NULL) || (*end != '\0') || (interval < 0L) ||
        (interval > POC_MAX_POLL_INTERVAL_SECONDS)) {
      return 0;
    }
  } else {
    return 0;
  }
  if (interval < POC_MIN_POLL_INTERVAL_SECONDS) {
    interval = POC_MIN_POLL_INTERVAL_SECONDS;
  }
  *interval_out = interval;
  return 1;
}

static void poc_device_authorization_init(poc_device_authorization *device)
{
  if (device != NULL) {
    memset(device, 0, sizeof(*device));
  }
}

static void poc_device_authorization_destroy(
  poc_device_authorization *device)
{
  if (device == NULL) {
    return;
  }
  poc_secure_free(device->device_id);
  poc_secure_free(device->user_code);
  poc_device_authorization_init(device);
}

static int poc_start_device_authorization(
  const char *client_id,
  poc_device_authorization *device)
{
  cJSON *request;
  cJSON *root;
  cJSON *item;
  char *request_json;
  poc_http_response response;
  int ok;

  if ((client_id == NULL) || (device == NULL)) {
    return 0;
  }
  poc_device_authorization_init(device);
  request = cJSON_CreateObject();
  if ((request == NULL) ||
      (cJSON_AddStringToObject(request, "client_id", client_id) == NULL)) {
    fprintf(stderr, "Could not build device authorization request.\n");
    cJSON_Delete(request);
    return 0;
  }
  request_json = cJSON_PrintUnformatted(request);
  cJSON_Delete(request);
  if (request_json == NULL) {
    fprintf(stderr, "Could not serialize device authorization request.\n");
    return 0;
  }

  ok = poc_http_post(POC_DEVICE_START_URL,
                     "Content-Type: application/json",
                     "Accept: application/json",
                     request_json,
                     POC_HTTP_TIMEOUT_SECONDS,
                     &response);
  free(request_json);
  if (!ok) {
    poc_http_response_destroy(&response, 0);
    return 0;
  }
  if ((response.status < 200L) || (response.status >= 300L)) {
    poc_print_http_error("Device authorization", &response);
    if (response.status == 404L) {
      fprintf(stderr,
              "Enable device code login in ChatGPT security settings or "
              "workspace permissions, then try again.\n");
    }
    poc_http_response_destroy(&response, 0);
    return 0;
  }

  root = cJSON_Parse(response.body.data);
  if (!cJSON_IsObject(root)) {
    fprintf(stderr, "Device authorization returned invalid JSON.\n");
    cJSON_Delete(root);
    poc_http_response_destroy(&response, 0);
    return 0;
  }

  item = cJSON_GetObjectItem(root, "device_auth_id");
  if (cJSON_IsString(item) && (item->valuestring != NULL)) {
    device->device_id = poc_duplicate_bounded(item->valuestring,
                                               POC_MAX_DEVICE_ID_BYTES);
  }
  item = cJSON_GetObjectItem(root, "user_code");
  if (cJSON_IsString(item) && (item->valuestring != NULL)) {
    device->user_code = poc_duplicate_bounded(item->valuestring,
                                               POC_MAX_USER_CODE_BYTES);
  }
  item = cJSON_GetObjectItem(root, "interval");
  ok = (device->device_id != NULL) && (device->user_code != NULL) &&
    poc_parse_poll_interval(item, &device->interval_seconds);
  cJSON_Delete(root);
  poc_http_response_destroy(&response, 0);
  if (!ok) {
    fprintf(stderr,
            "Device authorization response is missing required fields.\n");
    poc_device_authorization_destroy(device);
    return 0;
  }
  return 1;
}

static int poc_now_milliseconds(long long *milliseconds_out)
{
  struct timespec now;

  if ((milliseconds_out == NULL) ||
      (clock_gettime(CLOCK_MONOTONIC, &now) != 0)) {
    return 0;
  }
  *milliseconds_out = ((long long)now.tv_sec * 1000LL) +
    ((long long)now.tv_nsec / 1000000LL);
  return 1;
}

static int poc_sleep_seconds(long seconds)
{
  struct timespec remaining;

  if (seconds <= 0L) {
    return !poc_cancelled;
  }
  remaining.tv_sec = (time_t)seconds;
  remaining.tv_nsec = 0L;
  while (!poc_cancelled &&
         (nanosleep(&remaining, &remaining) != 0)) {
    if (errno != EINTR) {
      return 0;
    }
  }
  return !poc_cancelled;
}

static int poc_build_device_poll_json(
  const poc_device_authorization *device,
  char **json_out)
{
  cJSON *root;

  if ((device == NULL) || (json_out == NULL)) {
    return 0;
  }
  *json_out = NULL;
  root = cJSON_CreateObject();
  if ((root == NULL) ||
      (cJSON_AddStringToObject(root,
                               "device_auth_id",
                               device->device_id) == NULL) ||
      (cJSON_AddStringToObject(root,
                               "user_code",
                               device->user_code) == NULL)) {
    cJSON_Delete(root);
    return 0;
  }
  *json_out = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return (*json_out != NULL) ? 1 : 0;
}

static int poc_poll_device_authorization(
  const poc_device_authorization *device,
  char **authorization_code_out,
  char **code_verifier_out)
{
  char *poll_json;
  long interval_seconds;
  long long deadline;
  long long now;
  long long next_notice;

  if ((device == NULL) || (authorization_code_out == NULL) ||
      (code_verifier_out == NULL)) {
    return 0;
  }
  *authorization_code_out = NULL;
  *code_verifier_out = NULL;
  if (!poc_build_device_poll_json(device, &poll_json)) {
    fprintf(stderr, "Could not build device polling request.\n");
    return 0;
  }
  if (!poc_now_milliseconds(&now)) {
    fprintf(stderr, "Could not start device authorization timer.\n");
    poc_secure_free(poll_json);
    return 0;
  }
  deadline = now + (POC_DEVICE_TIMEOUT_SECONDS * 1000LL);
  next_notice = now + 30000LL;
  interval_seconds = device->interval_seconds;

  while (!poc_cancelled && poc_now_milliseconds(&now) &&
         (now < deadline)) {
    poc_http_response response;
    cJSON *root;
    const char *error_code;
    cJSON *item;
    int pending;
    int slow_down;
    int ok;

    ok = poc_http_post(POC_DEVICE_POLL_URL,
                       "Content-Type: application/json",
                       "Accept: application/json",
                       poll_json,
                       POC_HTTP_TIMEOUT_SECONDS,
                       &response);
    if (!ok) {
      poc_http_response_destroy(&response, 1);
      poc_secure_free(poll_json);
      return 0;
    }

    root = cJSON_Parse(response.body.data);
    pending = (response.status == 403L) || (response.status == 404L);
    slow_down = 0;
    error_code = poc_error_code(root);
    if ((error_code != NULL) &&
        (strcmp(error_code, "deviceauth_authorization_pending") == 0)) {
      pending = 1;
    } else if ((error_code != NULL) &&
               (strcmp(error_code, "slow_down") == 0)) {
      pending = 1;
      slow_down = 1;
    }

    if ((response.status >= 200L) && (response.status < 300L)) {
      item = cJSON_GetObjectItem(root, "authorization_code");
      if (cJSON_IsString(item) && (item->valuestring != NULL)) {
        *authorization_code_out =
          poc_duplicate_bounded(item->valuestring,
                                POC_MAX_AUTH_CODE_BYTES);
      }
      item = cJSON_GetObjectItem(root, "code_verifier");
      if (cJSON_IsString(item) && (item->valuestring != NULL)) {
        *code_verifier_out =
          poc_duplicate_bounded(item->valuestring,
                                POC_MAX_AUTH_CODE_BYTES);
      }
      if ((*authorization_code_out != NULL) &&
          (*code_verifier_out != NULL)) {
        poc_wipe_json_strings(root);
        cJSON_Delete(root);
        poc_http_response_destroy(&response, 1);
        poc_secure_free(poll_json);
        return 1;
      }
      poc_secure_free(*authorization_code_out);
      poc_secure_free(*code_verifier_out);
      *authorization_code_out = NULL;
      *code_verifier_out = NULL;
      pending = 0;
    }

    if (!pending) {
      poc_print_http_error("Device authorization polling", &response);
      poc_wipe_json_strings(root);
      cJSON_Delete(root);
      poc_http_response_destroy(&response, 1);
      poc_secure_free(poll_json);
      return 0;
    }
    if (slow_down) {
      if (interval_seconds <= (POC_MAX_POLL_INTERVAL_SECONDS - 5L)) {
        interval_seconds += 5L;
      } else {
        interval_seconds = POC_MAX_POLL_INTERVAL_SECONDS;
      }
    }
    poc_wipe_json_strings(root);
    cJSON_Delete(root);
    poc_http_response_destroy(&response, 1);

    if ((now >= next_notice) && !poc_cancelled) {
      printf("Still waiting for browser authorization...\n");
      fflush(stdout);
      next_notice = now + 30000LL;
    }
    if (!poc_sleep_seconds(interval_seconds)) {
      break;
    }
  }

  poc_secure_free(poll_json);
  if (poc_cancelled) {
    fprintf(stderr, "Device authorization cancelled.\n");
  } else {
    fprintf(stderr, "Device authorization timed out after 15 minutes.\n");
  }
  return 0;
}

static char *poc_form_encode(const char *value)
{
  static const char hex[] = "0123456789ABCDEF";
  size_t input_length;
  size_t index;
  size_t output_length;
  char *encoded;
  char *cursor;

  if (value == NULL) {
    return NULL;
  }
  input_length = strlen(value);
  if (input_length > ((((size_t)-1) - 1U) / 3U)) {
    return NULL;
  }
  encoded = (char *)malloc((input_length * 3U) + 1U);
  if (encoded == NULL) {
    return NULL;
  }
  cursor = encoded;
  for (index = 0U; index < input_length; index++) {
    unsigned char byte;

    byte = (unsigned char)value[index];
    if (((byte >= (unsigned char)'a') &&
         (byte <= (unsigned char)'z')) ||
        ((byte >= (unsigned char)'A') &&
         (byte <= (unsigned char)'Z')) ||
        ((byte >= (unsigned char)'0') &&
         (byte <= (unsigned char)'9')) ||
        (byte == (unsigned char)'-') || (byte == (unsigned char)'_') ||
        (byte == (unsigned char)'.') || (byte == (unsigned char)'~')) {
      *cursor = (char)byte;
      cursor++;
    } else {
      *cursor++ = '%';
      *cursor++ = hex[(byte >> 4U) & 0x0fU];
      *cursor++ = hex[byte & 0x0fU];
    }
  }
  *cursor = '\0';
  output_length = (size_t)(cursor - encoded);
  encoded[output_length] = '\0';
  return encoded;
}

static char *poc_build_token_form(const char *client_id,
                                  const char *authorization_code,
                                  const char *code_verifier)
{
  static const char prefix[] =
    "grant_type=authorization_code&client_id=";
  static const char code_label[] = "&code=";
  static const char verifier_label[] = "&code_verifier=";
  static const char redirect_label[] = "&redirect_uri=";
  char *encoded_client_id;
  char *encoded_code;
  char *encoded_verifier;
  char *encoded_redirect;
  size_t length;
  char *form;

  encoded_client_id = poc_form_encode(client_id);
  encoded_code = poc_form_encode(authorization_code);
  encoded_verifier = poc_form_encode(code_verifier);
  encoded_redirect = poc_form_encode(POC_DEVICE_REDIRECT_URI);
  if ((encoded_client_id == NULL) || (encoded_code == NULL) ||
      (encoded_verifier == NULL) || (encoded_redirect == NULL)) {
    free(encoded_client_id);
    poc_secure_free(encoded_code);
    poc_secure_free(encoded_verifier);
    free(encoded_redirect);
    return NULL;
  }

  length = strlen(prefix);
#define POC_ADD_FORM_LENGTH(value) \
  do { \
    if (length > (((size_t)-1) - strlen(value))) { \
      free(encoded_client_id); \
      poc_secure_free(encoded_code); \
      poc_secure_free(encoded_verifier); \
      free(encoded_redirect); \
      return NULL; \
    } \
    length += strlen(value); \
  } while (0)
  POC_ADD_FORM_LENGTH(encoded_client_id);
  POC_ADD_FORM_LENGTH(code_label);
  POC_ADD_FORM_LENGTH(encoded_code);
  POC_ADD_FORM_LENGTH(verifier_label);
  POC_ADD_FORM_LENGTH(encoded_verifier);
  POC_ADD_FORM_LENGTH(redirect_label);
  POC_ADD_FORM_LENGTH(encoded_redirect);
#undef POC_ADD_FORM_LENGTH
  if (length == (size_t)-1) {
    free(encoded_client_id);
    poc_secure_free(encoded_code);
    poc_secure_free(encoded_verifier);
    free(encoded_redirect);
    return NULL;
  }

  form = (char *)malloc(length + 1U);
  if (form != NULL) {
    snprintf(form,
             length + 1U,
             "%s%s%s%s%s%s%s%s",
             prefix,
             encoded_client_id,
             code_label,
             encoded_code,
             verifier_label,
             encoded_verifier,
             redirect_label,
             encoded_redirect);
  }
  free(encoded_client_id);
  poc_secure_free(encoded_code);
  poc_secure_free(encoded_verifier);
  free(encoded_redirect);
  return form;
}

static int poc_base64url_value(unsigned char byte)
{
  if ((byte >= (unsigned char)'A') && (byte <= (unsigned char)'Z')) {
    return (int)(byte - (unsigned char)'A');
  }
  if ((byte >= (unsigned char)'a') && (byte <= (unsigned char)'z')) {
    return (int)(byte - (unsigned char)'a') + 26;
  }
  if ((byte >= (unsigned char)'0') && (byte <= (unsigned char)'9')) {
    return (int)(byte - (unsigned char)'0') + 52;
  }
  if ((byte == (unsigned char)'-') || (byte == (unsigned char)'+')) {
    return 62;
  }
  if ((byte == (unsigned char)'_') || (byte == (unsigned char)'/')) {
    return 63;
  }
  return -1;
}

static unsigned char *poc_base64url_decode(const char *value,
                                           size_t length,
                                           size_t *decoded_length_out)
{
  unsigned char *decoded;
  size_t capacity;
  size_t index;
  size_t decoded_length;
  unsigned int accumulator;
  unsigned int bits;
  size_t symbol_count;
  int saw_padding;

  if ((value == NULL) || (decoded_length_out == NULL) ||
      (length == 0U) || (length > POC_MAX_TOKEN_BYTES) ||
      ((length % 4U) == 1U)) {
    return NULL;
  }
  if (length > ((((size_t)-1) - 4U) / 3U)) {
    return NULL;
  }
  capacity = ((length + 3U) / 4U) * 3U;
  decoded = (unsigned char *)malloc(capacity + 1U);
  if (decoded == NULL) {
    return NULL;
  }

  decoded_length = 0U;
  accumulator = 0U;
  bits = 0U;
  symbol_count = 0U;
  saw_padding = 0;
  for (index = 0U; index < length; index++) {
    unsigned char byte;
    int decoded_value;

    byte = (unsigned char)value[index];
    if (byte == (unsigned char)'=') {
      saw_padding = 1;
      continue;
    }
    if (saw_padding) {
      poc_secure_wipe(decoded, capacity + 1U);
      free(decoded);
      return NULL;
    }
    decoded_value = poc_base64url_value(byte);
    if (decoded_value < 0) {
      poc_secure_wipe(decoded, capacity + 1U);
      free(decoded);
      return NULL;
    }
    accumulator = (accumulator << 6U) | (unsigned int)decoded_value;
    bits += 6U;
    symbol_count++;
    if (bits >= 8U) {
      bits -= 8U;
      decoded[decoded_length++] =
        (unsigned char)((accumulator >> bits) & 0xffU);
      if (bits == 0U) {
        accumulator = 0U;
      } else {
        accumulator &= (1U << bits) - 1U;
      }
    }
  }
  if (((symbol_count % 4U) == 1U) ||
      ((bits > 0U) && (accumulator != 0U))) {
    poc_secure_wipe(decoded, capacity + 1U);
    free(decoded);
    return NULL;
  }
  decoded[decoded_length] = '\0';
  *decoded_length_out = decoded_length;
  return decoded;
}

static char *poc_extract_account_id(const char *access_token)
{
  static const char claim_name[] = "https://api.openai.com/auth";
  const char *first_dot;
  const char *second_dot;
  size_t payload_length;
  size_t decoded_length;
  unsigned char *decoded;
  cJSON *root;
  cJSON *auth;
  cJSON *account;
  char *account_id;

  if (access_token == NULL) {
    return NULL;
  }
  first_dot = strchr(access_token, '.');
  if (first_dot == NULL) {
    return NULL;
  }
  second_dot = strchr(first_dot + 1, '.');
  if ((second_dot == NULL) || (second_dot == (first_dot + 1))) {
    return NULL;
  }
  payload_length = (size_t)(second_dot - first_dot - 1);
  decoded = poc_base64url_decode(first_dot + 1,
                                 payload_length,
                                 &decoded_length);
  if (decoded == NULL) {
    return NULL;
  }

  root = cJSON_Parse((const char *)decoded);
  auth = cJSON_GetObjectItem(root, claim_name);
  account = cJSON_GetObjectItem(auth, "chatgpt_account_id");
  account_id = NULL;
  if (cJSON_IsString(account) && (account->valuestring != NULL)) {
    account_id = poc_duplicate_bounded(account->valuestring,
                                        POC_MAX_ACCOUNT_ID_BYTES);
  }
  poc_wipe_json_strings(root);
  cJSON_Delete(root);
  poc_secure_wipe(decoded, decoded_length + 1U);
  free(decoded);
  return account_id;
}

static void poc_credentials_init(poc_credentials *credentials)
{
  if (credentials != NULL) {
    memset(credentials, 0, sizeof(*credentials));
  }
}

static void poc_credentials_destroy(poc_credentials *credentials)
{
  if (credentials == NULL) {
    return;
  }
  poc_secure_free(credentials->access_token);
  poc_secure_free(credentials->refresh_token);
  poc_secure_free(credentials->account_id);
  poc_credentials_init(credentials);
}

static int poc_exchange_authorization_code(const char *client_id,
                                           const char *authorization_code,
                                           const char *code_verifier,
                                           poc_credentials *credentials)
{
  char *form;
  poc_http_response response;
  cJSON *root;
  cJSON *item;
  int ok;

  if ((client_id == NULL) || (authorization_code == NULL) ||
      (code_verifier == NULL) || (credentials == NULL)) {
    return 0;
  }
  poc_credentials_init(credentials);
  form = poc_build_token_form(client_id,
                              authorization_code,
                              code_verifier);
  if (form == NULL) {
    fprintf(stderr, "Could not build OAuth token exchange request.\n");
    return 0;
  }

  ok = poc_http_post(POC_TOKEN_URL,
                     "Content-Type: application/x-www-form-urlencoded",
                     "Accept: application/json",
                     form,
                     POC_HTTP_TIMEOUT_SECONDS,
                     &response);
  poc_secure_free(form);
  if (!ok) {
    poc_http_response_destroy(&response, 1);
    return 0;
  }
  if ((response.status < 200L) || (response.status >= 300L)) {
    poc_print_http_error("OAuth token exchange", &response);
    poc_http_response_destroy(&response, 1);
    return 0;
  }

  root = cJSON_Parse(response.body.data);
  if (!cJSON_IsObject(root)) {
    fprintf(stderr, "OAuth token exchange returned invalid JSON.\n");
    cJSON_Delete(root);
    poc_http_response_destroy(&response, 1);
    return 0;
  }
  item = cJSON_GetObjectItem(root, "access_token");
  if (cJSON_IsString(item) && (item->valuestring != NULL)) {
    credentials->access_token =
      poc_duplicate_bounded(item->valuestring, POC_MAX_TOKEN_BYTES);
  }
  item = cJSON_GetObjectItem(root, "refresh_token");
  if (cJSON_IsString(item) && (item->valuestring != NULL)) {
    credentials->refresh_token =
      poc_duplicate_bounded(item->valuestring, POC_MAX_TOKEN_BYTES);
  }
  item = cJSON_GetObjectItem(root, "expires_in");
  if (cJSON_IsNumber(item) && (item->valuedouble > 0.0) &&
      (item->valuedouble <= (double)LONG_MAX)) {
    credentials->expires_in_seconds = (long)item->valuedouble;
    if ((double)credentials->expires_in_seconds != item->valuedouble) {
      credentials->expires_in_seconds = 0L;
    }
  }
  if (credentials->access_token != NULL) {
    credentials->account_id =
      poc_extract_account_id(credentials->access_token);
  }
  ok = (credentials->access_token != NULL) &&
    (credentials->refresh_token != NULL) &&
    (credentials->account_id != NULL) &&
    (credentials->expires_in_seconds > 0L);
  poc_wipe_json_strings(root);
  cJSON_Delete(root);
  poc_http_response_destroy(&response, 1);
  if (!ok) {
    fprintf(stderr,
            "OAuth token response is missing a token, expiry, or ChatGPT "
            "account claim.\n");
    poc_credentials_destroy(credentials);
    return 0;
  }
  return 1;
}

static void poc_sse_context_init(poc_sse_context *context)
{
  if (context != NULL) {
    memset(context, 0, sizeof(*context));
    poc_buffer_init(&context->pending, POC_MAX_SSE_PENDING_BYTES);
    poc_buffer_init(&context->output, POC_MAX_OUTPUT_BYTES);
  }
}

static void poc_sse_context_destroy(poc_sse_context *context)
{
  if (context == NULL) {
    return;
  }
  poc_buffer_destroy(&context->pending, 0);
  poc_buffer_destroy(&context->output, 0);
  memset(context, 0, sizeof(*context));
}

static void poc_sse_fail(poc_sse_context *context, const char *message)
{
  if ((context == NULL) || context->failed) {
    return;
  }
  context->failed = 1;
  if (message != NULL) {
    snprintf(context->error, sizeof(context->error), "%s", message);
  }
}

static int poc_sse_append_output(poc_sse_context *context,
                                 const char *text)
{
  if ((context == NULL) || (text == NULL)) {
    return 0;
  }
  if (!poc_buffer_append(&context->output, text, strlen(text))) {
    poc_sse_fail(context, "Luna response exceeded the PoC size limit.");
    return 0;
  }
  return 1;
}

static void poc_sse_extract_terminal_output(poc_sse_context *context,
                                            cJSON *root)
{
  cJSON *response;
  cJSON *output;
  cJSON *item;

  if ((context == NULL) || (context->output.length > 0U)) {
    return;
  }
  response = cJSON_GetObjectItem(root, "response");
  output = cJSON_GetObjectItem(response, "output");
  if (!cJSON_IsArray(output)) {
    return;
  }
  for (item = output->child; item != NULL; item = item->next) {
    cJSON *content;
    cJSON *part;

    content = cJSON_GetObjectItem(item, "content");
    if (!cJSON_IsArray(content)) {
      continue;
    }
    for (part = content->child; part != NULL; part = part->next) {
      cJSON *type;
      cJSON *text;

      type = cJSON_GetObjectItem(part, "type");
      text = cJSON_GetObjectItem(part, "text");
      if (cJSON_IsString(type) && (type->valuestring != NULL) &&
          (strcmp(type->valuestring, "output_text") == 0) &&
          cJSON_IsString(text) && (text->valuestring != NULL)) {
        if ((context->output.length > 0U) &&
            !poc_sse_append_output(context, "\n")) {
          return;
        }
        if (!poc_sse_append_output(context, text->valuestring)) {
          return;
        }
      }
    }
  }
}

static void poc_sse_process_json(poc_sse_context *context,
                                 const char *json)
{
  cJSON *root;
  cJSON *type;
  const char *type_value;

  if ((context == NULL) || (json == NULL) || context->failed ||
      context->terminal) {
    return;
  }
  root = cJSON_Parse(json);
  if (!cJSON_IsObject(root)) {
    poc_sse_fail(context, "ChatGPT returned invalid SSE JSON.");
    cJSON_Delete(root);
    return;
  }
  type = cJSON_GetObjectItem(root, "type");
  type_value = (cJSON_IsString(type) && (type->valuestring != NULL)) ?
    type->valuestring : NULL;
  if (type_value == NULL) {
    cJSON_Delete(root);
    return;
  }

  if (strcmp(type_value, "response.output_text.delta") == 0) {
    cJSON *delta;

    delta = cJSON_GetObjectItem(root, "delta");
    if (cJSON_IsString(delta) && (delta->valuestring != NULL)) {
      (void)poc_sse_append_output(context, delta->valuestring);
    }
  } else if ((strcmp(type_value, "response.output_text.done") == 0) &&
             (context->output.length == 0U)) {
    cJSON *text;

    text = cJSON_GetObjectItem(root, "text");
    if (cJSON_IsString(text) && (text->valuestring != NULL)) {
      (void)poc_sse_append_output(context, text->valuestring);
    }
  } else if ((strcmp(type_value, "response.completed") == 0) ||
             (strcmp(type_value, "response.done") == 0) ||
             (strcmp(type_value, "response.incomplete") == 0)) {
    poc_sse_extract_terminal_output(context, root);
    context->terminal = 1;
    context->incomplete =
      (strcmp(type_value, "response.incomplete") == 0) ? 1 : 0;
  } else if ((strcmp(type_value, "error") == 0) ||
             (strcmp(type_value, "response.failed") == 0)) {
    const char *detail;

    detail = poc_error_detail(root);
    poc_sse_fail(context,
                 (detail != NULL) ? detail :
                   "ChatGPT returned an SSE error event.");
  }
  cJSON_Delete(root);
}

static int poc_sse_process_event(poc_sse_context *context,
                                 const char *event,
                                 size_t event_length)
{
  poc_buffer data;
  size_t line_start;

  if ((context == NULL) || ((event == NULL) && (event_length > 0U))) {
    return 0;
  }
  poc_buffer_init(&data, POC_MAX_SSE_PENDING_BYTES);
  line_start = 0U;
  while (line_start < event_length) {
    size_t line_end;
    size_t value_start;

    line_end = line_start;
    while ((line_end < event_length) && (event[line_end] != '\n')) {
      line_end++;
    }
    if (((line_end - line_start) >= 5U) &&
        (memcmp(event + line_start, "data:", 5U) == 0)) {
      value_start = line_start + 5U;
      if ((value_start < line_end) && (event[value_start] == ' ')) {
        value_start++;
      }
      if ((data.length > 0U) &&
          !poc_buffer_append_character(&data, '\n')) {
        poc_sse_fail(context, "Could not allocate SSE event data.");
        poc_buffer_destroy(&data, 0);
        return 0;
      }
      if (!poc_buffer_append(&data,
                             event + value_start,
                             line_end - value_start)) {
        poc_sse_fail(context, "SSE event exceeded the PoC size limit.");
        poc_buffer_destroy(&data, 0);
        return 0;
      }
    }
    line_start = (line_end < event_length) ? line_end + 1U : line_end;
  }

  if ((data.length > 0U) && (strcmp(data.data, "[DONE]") != 0)) {
    poc_sse_process_json(context, data.data);
  }
  poc_buffer_destroy(&data, 0);
  return context->failed ? 0 : 1;
}

static int poc_sse_feed(poc_sse_context *context,
                        const char *data,
                        size_t length)
{
  size_t index;

  if ((context == NULL) || ((data == NULL) && (length > 0U))) {
    return 0;
  }
  for (index = 0U; index < length; index++) {
    if ((data[index] != '\r') &&
        !poc_buffer_append_character(&context->pending, data[index])) {
      poc_sse_fail(context, "SSE buffer exceeded the PoC size limit.");
      return 0;
    }
  }

  while ((context->pending.data != NULL) && !context->terminal &&
         !context->failed) {
    char *separator;
    size_t event_length;

    separator = strstr(context->pending.data, "\n\n");
    if (separator == NULL) {
      break;
    }
    event_length = (size_t)(separator - context->pending.data);
    if (!poc_sse_process_event(context,
                               context->pending.data,
                               event_length)) {
      return 0;
    }
    poc_buffer_consume(&context->pending, event_length + 2U);
  }
  return context->failed ? 0 : 1;
}

static size_t poc_sse_write_callback(void *contents,
                                     size_t size,
                                     size_t count,
                                     void *user_data)
{
  poc_sse_context *context;
  size_t length;

  if ((contents == NULL) || (user_data == NULL) ||
      ((size != 0U) && (count > (((size_t)-1) / size)))) {
    return 0U;
  }
  length = size * count;
  context = (poc_sse_context *)user_data;
  if (!poc_sse_feed(context, (const char *)contents, length) ||
      context->terminal || context->failed || poc_cancelled) {
    return 0U;
  }
  return length;
}

#if LIBCURL_VERSION_NUM >= 0x072000
static int poc_sse_progress_callback(void *client_data,
                                     curl_off_t download_total,
                                     curl_off_t download_now,
                                     curl_off_t upload_total,
                                     curl_off_t upload_now)
#else
static int poc_sse_progress_callback(void *client_data,
                                     double download_total,
                                     double download_now,
                                     double upload_total,
                                     double upload_now)
#endif
{
  poc_sse_context *context;

  (void)download_total;
  (void)download_now;
  (void)upload_total;
  (void)upload_now;
  context = (poc_sse_context *)client_data;
  return (poc_cancelled || (context == NULL) || context->terminal ||
          context->failed) ? 1 : 0;
}

static char *poc_join_header(const char *name, const char *value)
{
  size_t name_length;
  size_t value_length;
  char *header;

  if ((name == NULL) || (value == NULL)) {
    return NULL;
  }
  name_length = strlen(name);
  value_length = strlen(value);
  if (name_length > (((size_t)-1) - value_length - 1U)) {
    return NULL;
  }
  header = (char *)malloc(name_length + value_length + 1U);
  if (header == NULL) {
    return NULL;
  }
  memcpy(header, name, name_length);
  memcpy(header + name_length, value, value_length + 1U);
  return header;
}

static int poc_send_luna_prompt(const poc_credentials *credentials)
{
  CURL *curl;
  CURLcode code;
  struct curl_slist *headers;
  poc_sse_context context;
  char *authorization_header;
  char *account_header;
  char *user_agent;
  char curl_error[CURL_ERROR_SIZE];
  long status;
  int expected_stop;
  int ok;

  if ((credentials == NULL) || (credentials->access_token == NULL) ||
      (credentials->account_id == NULL)) {
    return 0;
  }
  authorization_header =
    poc_join_header("Authorization: Bearer ", credentials->access_token);
  account_header =
    poc_join_header("chatgpt-account-id: ", credentials->account_id);
  if ((authorization_header == NULL) || (account_header == NULL)) {
    fprintf(stderr, "Could not allocate ChatGPT request headers.\n");
    poc_secure_free(authorization_header);
    poc_secure_free(account_header);
    return 0;
  }

  headers = NULL;
  ok = poc_add_header(&headers, authorization_header) &&
    poc_add_header(&headers, account_header) &&
    poc_add_header(&headers, "originator: " POC_ORIGINATOR) &&
    poc_add_header(&headers, "OpenAI-Beta: responses=experimental") &&
    poc_add_header(&headers, "Accept: text/event-stream") &&
    poc_add_header(&headers, "Content-Type: application/json") &&
    poc_add_header(&headers, "Expect:");
  poc_secure_free(authorization_header);
  poc_secure_free(account_header);
  if (!ok) {
    fprintf(stderr, "Could not allocate ChatGPT request headers.\n");
    curl_slist_free_all(headers);
    return 0;
  }

  user_agent = poc_copy_user_agent();
  if (user_agent == NULL) {
    curl_slist_free_all(headers);
    return 0;
  }

  curl = curl_easy_init();
  if (curl == NULL) {
    fprintf(stderr, "Could not create curl handle for Luna request.\n");
    free(user_agent);
    curl_slist_free_all(headers);
    return 0;
  }
  poc_sse_context_init(&context);
  memset(curl_error, 0, sizeof(curl_error));
  status = 0L;
  curl_easy_setopt(curl, CURLOPT_URL, POC_RESPONSES_URL);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, POC_REQUEST_JSON);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                   (long)strlen(POC_REQUEST_JSON));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, poc_sse_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&context);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,
                   POC_CONNECT_TIMEOUT_SECONDS);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, POC_RESPONSE_TIMEOUT_SECONDS);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
#if LIBCURL_VERSION_NUM >= 0x072000
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,
                   poc_sse_progress_callback);
#else
  curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION,
                   poc_sse_progress_callback);
#endif
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void *)&context);
  if (!poc_configure_tls(curl)) {
    curl_easy_cleanup(curl);
    free(user_agent);
    curl_slist_free_all(headers);
    poc_sse_context_destroy(&context);
    return 0;
  }

  code = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_easy_cleanup(curl);
  free(user_agent);
  curl_slist_free_all(headers);

  expected_stop = context.terminal || context.failed;
  if (poc_cancelled) {
    fprintf(stderr, "Luna request cancelled.\n");
    poc_sse_context_destroy(&context);
    return 0;
  }
  if ((status < 200L) || (status >= 300L)) {
    poc_http_response response;

    poc_http_response_init(&response);
    response.status = status;
    if (context.pending.data != NULL) {
      (void)poc_buffer_append(&response.body,
                              context.pending.data,
                              context.pending.length);
    }
    poc_print_http_error("Luna request", &response);
    poc_http_response_destroy(&response, 0);
    poc_sse_context_destroy(&context);
    return 0;
  }
  if ((code != CURLE_OK) && !expected_stop) {
    fprintf(stderr,
            "Luna request failed: %s\n",
            (curl_error[0] != '\0') ? curl_error :
              curl_easy_strerror(code));
    poc_sse_context_destroy(&context);
    return 0;
  }
  if (context.failed) {
    fprintf(stderr,
            "Luna request failed: %s\n",
            (context.error[0] != '\0') ? context.error :
              "ChatGPT returned an SSE error.");
    poc_sse_context_destroy(&context);
    return 0;
  }
  if (!context.terminal) {
    fprintf(stderr,
            "Luna response ended without a terminal response event.\n");
    poc_sse_context_destroy(&context);
    return 0;
  }
  if (context.incomplete) {
    fprintf(stderr, "Luna returned an incomplete response.\n");
    poc_sse_context_destroy(&context);
    return 0;
  }
  if ((context.output.data == NULL) || (context.output.length == 0U)) {
    fprintf(stderr, "Luna returned no assistant text.\n");
    poc_sse_context_destroy(&context);
    return 0;
  }

  printf("\nLuna response:\n%s\n", context.output.data);
  printf("\nPASS: device OAuth and %s response both succeeded.\n",
         POC_MODEL);
  poc_sse_context_destroy(&context);
  return 1;
}

static int poc_self_test(void)
{
  static const char token[] =
    "header."
    "eyJodHRwczovL2FwaS5vcGVuYWkuY29tL2F1dGgiOnsiY2hhdGdwdF9hY2NvdW50"
    "X2lkIjoiYWNjb3VudC10ZXN0In19."
    "signature";
  static const char first_sse_part[] =
    "data: {\"type\":\"response.output_text.del";
  static const char second_sse_part[] =
    "ta\",\"delta\":\"OAuth PoC works.\"}\n\n"
    "data: {\"type\":\"response.completed\",\"response\":{"
      "\"output\":[]}}\n\n";
  char *account_id;
  char *encoded;
  char *user_agent;
  cJSON *request;
  cJSON *model;
  cJSON *tools;
  poc_sse_context context;
  size_t product_length;
  int ok;

  account_id = poc_extract_account_id(token);
  encoded = poc_form_encode("a b+");
  user_agent = poc_copy_user_agent();
  request = cJSON_Parse(POC_REQUEST_JSON);
  model = cJSON_GetObjectItem(request, "model");
  tools = cJSON_GetObjectItem(request, "tools");
  product_length = strlen(STRAPPY_IDENTITY_PRODUCT_NAME);
  poc_sse_context_init(&context);
  ok = (account_id != NULL) &&
    (strcmp(account_id, "account-test") == 0) &&
    (encoded != NULL) && (strcmp(encoded, "a%20b%2B") == 0) &&
    (user_agent != NULL) &&
    (strncmp(user_agent,
             STRAPPY_IDENTITY_PRODUCT_NAME,
             product_length) == 0) &&
    (user_agent[product_length] == '/') &&
    (user_agent[product_length + 1U] != '\0') &&
    cJSON_IsObject(request) && cJSON_IsString(model) &&
    (model->valuestring != NULL) &&
    (strcmp(model->valuestring, POC_MODEL) == 0) && (tools == NULL) &&
    poc_sse_feed(&context, first_sse_part, strlen(first_sse_part)) &&
    !context.terminal &&
    poc_sse_feed(&context, second_sse_part, strlen(second_sse_part)) &&
    context.terminal && !context.failed && !context.incomplete &&
    (context.output.data != NULL) &&
    (strcmp(context.output.data, "OAuth PoC works.") == 0);

  poc_secure_free(account_id);
  free(encoded);
  free(user_agent);
  cJSON_Delete(request);
  poc_sse_context_destroy(&context);
  if (!ok) {
    fprintf(stderr, "OpenAI OAuth PoC self-test failed.\n");
    return 0;
  }
  printf("PASS: OpenAI OAuth PoC parser self-test.\n");
  return 1;
}

int main(int argc, char **argv)
{
  const char *client_id;
  poc_device_authorization device;
  poc_credentials credentials;
  char *authorization_code;
  char *code_verifier;
  CURLcode curl_code;
  int ok;

  if ((argc == 2) && (strcmp(argv[1], "--self-test") == 0)) {
    return poc_self_test() ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  if (argc != 1) {
    fprintf(stderr, "Usage: %s [--self-test]\n", argv[0]);
    return EXIT_FAILURE;
  }

  signal(SIGINT, poc_signal_handler);
  signal(SIGTERM, poc_signal_handler);
  poc_device_authorization_init(&device);
  poc_credentials_init(&credentials);
  authorization_code = NULL;
  code_verifier = NULL;

  client_id = getenv("STRAPPY_OPENAI_OAUTH_CLIENT_ID");
  if ((client_id == NULL) || (client_id[0] == '\0')) {
    client_id = POC_REFERENCE_CLIENT_ID;
  }
  if (strlen(client_id) > 1024U) {
    fprintf(stderr, "OAuth client ID is too long.\n");
    return EXIT_FAILURE;
  }

  printf("Strappy ChatGPT OAuth proof-of-concept\n");
  printf("This manual target uses an undocumented, non-shipping protocol.\n");
  printf("Tokens remain in process memory only and are discarded on exit.\n\n");
  fflush(stdout);

  curl_code = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (curl_code != CURLE_OK) {
    fprintf(stderr,
            "Could not initialize curl: %s\n",
            curl_easy_strerror(curl_code));
    return EXIT_FAILURE;
  }

  ok = poc_start_device_authorization(client_id, &device);
  if (ok) {
    printf("Open this URL in your browser:\n%s\n\n",
           POC_DEVICE_VERIFICATION_URL);
    printf("Enter this one-time code:\n%s\n\n", device.user_code);
    printf("Waiting for authorization (Ctrl-C cancels)...\n");
    fflush(stdout);
    ok = poc_poll_device_authorization(&device,
                                       &authorization_code,
                                       &code_verifier);
  }
  if (ok) {
    printf("Browser authorization received; exchanging tokens...\n");
    fflush(stdout);
    ok = poc_exchange_authorization_code(client_id,
                                         authorization_code,
                                         code_verifier,
                                         &credentials);
  }
  poc_secure_free(authorization_code);
  poc_secure_free(code_verifier);
  authorization_code = NULL;
  code_verifier = NULL;
  poc_device_authorization_destroy(&device);

  if (ok) {
    printf("OAuth login succeeded; sending a no-tools prompt to %s...\n",
           POC_MODEL);
    fflush(stdout);
    ok = poc_send_luna_prompt(&credentials);
  }

  poc_credentials_destroy(&credentials);
  curl_global_cleanup();
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
