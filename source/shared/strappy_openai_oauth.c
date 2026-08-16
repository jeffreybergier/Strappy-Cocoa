#define _POSIX_C_SOURCE 200809L

#include "strappy_openai_oauth.h"

#include "strappy_core.h"
#include "strappy_identity.h"

#include <cJSON.h>
#include <curl/curl.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define STRAPPY_OPENAI_OAUTH_CLIENT_ID \
  "app_EMoamEEZ73f0CkXaXp7hrann"
#define STRAPPY_OPENAI_OAUTH_DEVICE_START_URL \
  "https://auth.openai.com/api/accounts/deviceauth/usercode"
#define STRAPPY_OPENAI_OAUTH_DEVICE_POLL_URL \
  "https://auth.openai.com/api/accounts/deviceauth/token"
#define STRAPPY_OPENAI_OAUTH_TOKEN_URL \
  "https://auth.openai.com/oauth/token"
#define STRAPPY_OPENAI_OAUTH_VERIFICATION_URL \
  "https://auth.openai.com/codex/device"
#define STRAPPY_OPENAI_OAUTH_DEVICE_REDIRECT_URI \
  "https://auth.openai.com/deviceauth/callback"

/*
 * This experimental wire contract follows Pi's OpenAI Codex device-code
 * implementation at commit 94373d815d2b4a3a48864d5341afc824b8db45e3.
 * The C implementation is independent; the reference records the intended
 * compatibility target for these currently non-public endpoint details.
 */

#define STRAPPY_OPENAI_OAUTH_DEVICE_TIMEOUT_SECONDS 900L
#define STRAPPY_OPENAI_OAUTH_HTTP_TIMEOUT_SECONDS 60L
#define STRAPPY_OPENAI_OAUTH_CONNECT_TIMEOUT_SECONDS 30L
#define STRAPPY_OPENAI_OAUTH_MIN_POLL_INTERVAL_SECONDS 1L
#define STRAPPY_OPENAI_OAUTH_MAX_POLL_INTERVAL_SECONDS 300L
#define STRAPPY_OPENAI_OAUTH_MAX_HTTP_BODY_BYTES (1024U * 1024U)
#define STRAPPY_OPENAI_OAUTH_MAX_TOKEN_BYTES (512U * 1024U)
#define STRAPPY_OPENAI_OAUTH_MAX_DEVICE_ID_BYTES 4096U
#define STRAPPY_OPENAI_OAUTH_MAX_USER_CODE_BYTES 256U
#define STRAPPY_OPENAI_OAUTH_MAX_AUTH_CODE_BYTES (64U * 1024U)
#define STRAPPY_OPENAI_OAUTH_MAX_ACCOUNT_ID_BYTES 4096U

typedef struct strappy_openai_oauth_buffer {
  char *data;
  size_t length;
  size_t capacity;
  size_t limit;
  int overflowed;
} strappy_openai_oauth_buffer;

typedef struct strappy_openai_oauth_http_response {
  long status;
  strappy_openai_oauth_buffer body;
} strappy_openai_oauth_http_response;

typedef struct strappy_openai_oauth_transfer {
  strappy_openai_oauth_cancel_callback cancel_callback;
  void *cancel_user_data;
} strappy_openai_oauth_transfer;

static char *strappy_openai_oauth_cainfo_path = NULL;
static int strappy_openai_oauth_curl_initialized = 0;

static void strappy_openai_oauth_secure_wipe(void *memory, size_t length)
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

static void strappy_openai_oauth_secure_free(char *value)
{
  if (value != NULL) {
    strappy_openai_oauth_secure_wipe(value, strlen(value));
    free(value);
  }
}

static int strappy_openai_oauth_is_cancelled(
  const strappy_openai_oauth_transfer *transfer)
{
  if ((transfer == NULL) || (transfer->cancel_callback == NULL)) {
    return 0;
  }
  return transfer->cancel_callback(transfer->cancel_user_data) ? 1 : 0;
}

static void strappy_openai_oauth_buffer_init(
  strappy_openai_oauth_buffer *buffer,
  size_t limit)
{
  if (buffer != NULL) {
    memset(buffer, 0, sizeof(*buffer));
    buffer->limit = limit;
  }
}

static void strappy_openai_oauth_buffer_destroy(
  strappy_openai_oauth_buffer *buffer,
  int sensitive)
{
  size_t limit;

  if (buffer == NULL) {
    return;
  }
  limit = buffer->limit;
  if (sensitive && (buffer->data != NULL)) {
    strappy_openai_oauth_secure_wipe(buffer->data, buffer->capacity);
  }
  free(buffer->data);
  strappy_openai_oauth_buffer_init(buffer, limit);
}

static int strappy_openai_oauth_buffer_reserve(
  strappy_openai_oauth_buffer *buffer,
  size_t additional)
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

static int strappy_openai_oauth_buffer_append(
  strappy_openai_oauth_buffer *buffer,
  const char *data,
  size_t length)
{
  if ((buffer == NULL) || ((data == NULL) && (length > 0U)) ||
      !strappy_openai_oauth_buffer_reserve(buffer, length)) {
    return 0;
  }
  if (length > 0U) {
    memcpy(buffer->data + buffer->length, data, length);
  }
  buffer->length += length;
  buffer->data[buffer->length] = '\0';
  return 1;
}

static void strappy_openai_oauth_http_response_init(
  strappy_openai_oauth_http_response *response)
{
  if (response != NULL) {
    response->status = 0L;
    strappy_openai_oauth_buffer_init(
      &response->body,
      STRAPPY_OPENAI_OAUTH_MAX_HTTP_BODY_BYTES);
  }
}

static void strappy_openai_oauth_http_response_destroy(
  strappy_openai_oauth_http_response *response,
  int sensitive)
{
  if (response != NULL) {
    strappy_openai_oauth_buffer_destroy(&response->body, sensitive);
    response->status = 0L;
  }
}

static size_t strappy_openai_oauth_http_write_callback(
  void *contents,
  size_t size,
  size_t count,
  void *user_data)
{
  strappy_openai_oauth_http_response *response;
  size_t length;

  if ((contents == NULL) || (user_data == NULL) ||
      ((size != 0U) && (count > (((size_t)-1) / size)))) {
    return 0U;
  }
  length = size * count;
  response = (strappy_openai_oauth_http_response *)user_data;
  return strappy_openai_oauth_buffer_append(&response->body,
                                            (const char *)contents,
                                            length) ? length : 0U;
}

#if LIBCURL_VERSION_NUM >= 0x072000
static int strappy_openai_oauth_http_progress_callback(
  void *client_data,
  curl_off_t download_total,
  curl_off_t download_now,
  curl_off_t upload_total,
  curl_off_t upload_now)
#else
static int strappy_openai_oauth_http_progress_callback(
  void *client_data,
  double download_total,
  double download_now,
  double upload_total,
  double upload_now)
#endif
{
  strappy_openai_oauth_transfer *transfer;

  (void)download_total;
  (void)download_now;
  (void)upload_total;
  (void)upload_now;
  transfer = (strappy_openai_oauth_transfer *)client_data;
  return strappy_openai_oauth_is_cancelled(transfer) ? 1 : 0;
}

static int strappy_openai_oauth_add_header(
  struct curl_slist **headers,
  const char *value,
  char **error_out)
{
  struct curl_slist *next_headers;

  if ((headers == NULL) || (value == NULL)) {
    strappy_set_error(error_out, "OAuth HTTP header is invalid.");
    return 0;
  }
  next_headers = curl_slist_append(*headers, value);
  if (next_headers == NULL) {
    strappy_set_error(error_out, "Could not allocate OAuth HTTP headers.");
    return 0;
  }
  *headers = next_headers;
  return 1;
}

static int strappy_openai_oauth_configuration_is_valid(
  const strappy_openai_oauth_configuration *configuration)
{
  return (configuration != NULL) &&
    (configuration->client_id != NULL) &&
    (configuration->client_id[0] != '\0') &&
    (configuration->device_start_url != NULL) &&
    (configuration->device_start_url[0] != '\0') &&
    (configuration->device_poll_url != NULL) &&
    (configuration->device_poll_url[0] != '\0') &&
    (configuration->token_url != NULL) &&
    (configuration->token_url[0] != '\0') &&
    (configuration->verification_url != NULL) &&
    (configuration->verification_url[0] != '\0') &&
    (configuration->device_redirect_uri != NULL) &&
    (configuration->device_redirect_uri[0] != '\0');
}

static int strappy_openai_oauth_ensure_curl_initialized(char **error_out)
{
  CURLcode code;

  if (strappy_openai_oauth_curl_initialized) {
    return 1;
  }
  code = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (code != CURLE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not initialize OAuth networking: %s",
                                curl_easy_strerror(code));
    return 0;
  }
  strappy_openai_oauth_curl_initialized = 1;
  return 1;
}

static int strappy_openai_oauth_http_post(
  const char *url,
  const char *content_type_header,
  const char *body,
  strappy_openai_oauth_transfer *transfer,
  strappy_openai_oauth_http_response *response,
  char **error_out)
{
  CURL *curl;
  CURLcode code;
  struct curl_slist *headers;
  size_t body_length;
  char *user_agent;
  char *identity_error;
  char curl_error[CURL_ERROR_SIZE];

  if ((url == NULL) || (content_type_header == NULL) || (body == NULL) ||
      (response == NULL)) {
    strappy_set_error(error_out, "OAuth HTTP request is incomplete.");
    return 0;
  }
  strappy_openai_oauth_http_response_init(response);
  if (strappy_openai_oauth_is_cancelled(transfer)) {
    strappy_set_error(error_out, "ChatGPT sign-in was cancelled.");
    return 0;
  }
  if (!strappy_openai_oauth_ensure_curl_initialized(error_out)) {
    return 0;
  }

  body_length = strlen(body);
  if (body_length > (size_t)LONG_MAX) {
    strappy_set_error(error_out, "OAuth HTTP request is too large.");
    return 0;
  }
  headers = NULL;
  if (!strappy_openai_oauth_add_header(&headers,
                                       content_type_header,
                                       error_out) ||
      !strappy_openai_oauth_add_header(&headers,
                                       "Accept: application/json",
                                       error_out) ||
      !strappy_openai_oauth_add_header(&headers, "Expect:", error_out)) {
    curl_slist_free_all(headers);
    return 0;
  }

  identity_error = NULL;
  user_agent = strappy_identity_copy_user_agent(&identity_error);
  if (user_agent == NULL) {
    strappy_set_formatted_error(
      error_out,
      "Could not build the OAuth user agent: %s",
      (identity_error != NULL) ? identity_error : "unknown identity error");
    free(identity_error);
    curl_slist_free_all(headers);
    return 0;
  }
  free(identity_error);

  curl = curl_easy_init();
  if (curl == NULL) {
    free(user_agent);
    curl_slist_free_all(headers);
    strappy_set_error(error_out, "Could not create OAuth network request.");
    return 0;
  }

  memset(curl_error, 0, sizeof(curl_error));
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_length);
  curl_easy_setopt(curl,
                   CURLOPT_WRITEFUNCTION,
                   strappy_openai_oauth_http_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)response);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
  curl_easy_setopt(curl,
                   CURLOPT_CONNECTTIMEOUT,
                   STRAPPY_OPENAI_OAUTH_CONNECT_TIMEOUT_SECONDS);
  curl_easy_setopt(curl,
                   CURLOPT_TIMEOUT,
                   STRAPPY_OPENAI_OAUTH_HTTP_TIMEOUT_SECONDS);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
#if LIBCURL_VERSION_NUM >= 0x072000
  curl_easy_setopt(curl,
                   CURLOPT_XFERINFOFUNCTION,
                   strappy_openai_oauth_http_progress_callback);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void *)transfer);
#else
  curl_easy_setopt(curl,
                   CURLOPT_PROGRESSFUNCTION,
                   strappy_openai_oauth_http_progress_callback);
  curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, (void *)transfer);
#endif
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  if ((strappy_openai_oauth_cainfo_path != NULL) &&
      (strappy_openai_oauth_cainfo_path[0] != '\0')) {
    curl_easy_setopt(curl,
                     CURLOPT_CAINFO,
                     strappy_openai_oauth_cainfo_path);
  }

  code = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response->status);
  curl_easy_cleanup(curl);
  free(user_agent);
  curl_slist_free_all(headers);

  if (code != CURLE_OK) {
    if (strappy_openai_oauth_is_cancelled(transfer)) {
      strappy_set_error(error_out, "ChatGPT sign-in was cancelled.");
    } else if (response->body.overflowed) {
      strappy_set_error(error_out,
                        "OAuth response exceeded the size limit.");
    } else {
      strappy_set_formatted_error(
        error_out,
        "OAuth network request failed: %s",
        (curl_error[0] != '\0') ? curl_error : curl_easy_strerror(code));
    }
    return 0;
  }
  return 1;
}

static const char *strappy_openai_oauth_error_code(cJSON *root)
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

static const char *strappy_openai_oauth_error_detail(cJSON *root)
{
  cJSON *error;
  cJSON *detail;

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
  return NULL;
}

static void strappy_openai_oauth_set_http_error(
  char **error_out,
  const char *operation,
  const strappy_openai_oauth_http_response *response,
  cJSON *root)
{
  const char *detail;
  char *safe_detail;

  detail = strappy_openai_oauth_error_detail(root);
  safe_detail = NULL;
  if (detail != NULL) {
    safe_detail = strappy_string_duplicate_length(detail,
                                                   strlen(detail) > 500U ?
                                                     500U : strlen(detail));
  }
  if ((safe_detail != NULL) && (safe_detail[0] != '\0')) {
    strappy_set_formatted_error(error_out,
                                "%s failed with HTTP %ld: %s",
                                operation,
                                (response != NULL) ? response->status : 0L,
                                safe_detail);
  } else {
    strappy_set_formatted_error(error_out,
                                "%s failed with HTTP %ld.",
                                operation,
                                (response != NULL) ? response->status : 0L);
  }
  free(safe_detail);
}

static void strappy_openai_oauth_wipe_json_strings(cJSON *value)
{
  cJSON *child;

  if (value == NULL) {
    return;
  }
  if (cJSON_IsString(value) && (value->valuestring != NULL)) {
    strappy_openai_oauth_secure_wipe(value->valuestring,
                                     strlen(value->valuestring));
  }
  for (child = value->child; child != NULL; child = child->next) {
    strappy_openai_oauth_wipe_json_strings(child);
  }
}

static char *strappy_openai_oauth_duplicate_bounded(
  const char *value,
  size_t limit)
{
  size_t length;

  if (value == NULL) {
    return NULL;
  }
  length = strlen(value);
  if ((length == 0U) || (length > limit) ||
      !strappy_utf8_validate(value, length)) {
    return NULL;
  }
  return strappy_string_duplicate_length(value, length);
}

static int strappy_openai_oauth_parse_poll_interval(
  cJSON *item,
  long *interval_out)
{
  long interval;

  if (interval_out == NULL) {
    return 0;
  }
  if (cJSON_IsNumber(item)) {
    if (!(item->valuedouble >= 0.0) ||
        (item->valuedouble >
         (double)STRAPPY_OPENAI_OAUTH_MAX_POLL_INTERVAL_SECONDS)) {
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
    if ((errno != 0) || (end == item->valuestring) || (end == NULL) ||
        (*end != '\0') || (interval < 0L) ||
        (interval > STRAPPY_OPENAI_OAUTH_MAX_POLL_INTERVAL_SECONDS)) {
      return 0;
    }
  } else {
    return 0;
  }
  if (interval < STRAPPY_OPENAI_OAUTH_MIN_POLL_INTERVAL_SECONDS) {
    interval = STRAPPY_OPENAI_OAUTH_MIN_POLL_INTERVAL_SECONDS;
  }
  *interval_out = interval;
  return 1;
}

static int strappy_openai_oauth_now_milliseconds(long long *value_out)
{
  struct timeval now;

  if ((value_out == NULL) || (gettimeofday(&now, NULL) != 0) ||
      (now.tv_sec < 0)) {
    return 0;
  }
  if ((long long)now.tv_sec > (LLONG_MAX / 1000LL)) {
    return 0;
  }
  *value_out = ((long long)now.tv_sec * 1000LL) +
    ((long long)now.tv_usec / 1000LL);
  return 1;
}

static int strappy_openai_oauth_wait_seconds(
  long seconds,
  const strappy_openai_oauth_transfer *transfer)
{
  long slices;

  if (seconds <= 0L) {
    return !strappy_openai_oauth_is_cancelled(transfer);
  }
  if (seconds > (LONG_MAX / 5L)) {
    return 0;
  }
  slices = seconds * 5L;
  while (slices > 0L) {
    struct timespec delay;

    if (strappy_openai_oauth_is_cancelled(transfer)) {
      return 0;
    }
    delay.tv_sec = 0;
    delay.tv_nsec = 200000000L;
    while (nanosleep(&delay, &delay) != 0) {
      if (errno != EINTR) {
        return 0;
      }
      if (strappy_openai_oauth_is_cancelled(transfer)) {
        return 0;
      }
    }
    slices--;
  }
  return !strappy_openai_oauth_is_cancelled(transfer);
}

static int strappy_openai_oauth_build_poll_json(
  const strappy_openai_oauth_device *device,
  char **json_out)
{
  cJSON *root;

  if ((device == NULL) || (device->device_auth_id == NULL) ||
      (device->user_code == NULL) || (json_out == NULL)) {
    return 0;
  }
  *json_out = NULL;
  root = cJSON_CreateObject();
  if ((root == NULL) ||
      (cJSON_AddStringToObject(root,
                               "device_auth_id",
                               device->device_auth_id) == NULL) ||
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

static char *strappy_openai_oauth_form_encode(const char *value)
{
  static const char hex[] = "0123456789ABCDEF";
  size_t input_length;
  size_t index;
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
      *cursor++ = (char)byte;
    } else {
      *cursor++ = '%';
      *cursor++ = hex[(byte >> 4U) & 0x0fU];
      *cursor++ = hex[byte & 0x0fU];
    }
  }
  *cursor = '\0';
  return encoded;
}

static int strappy_openai_oauth_add_length(size_t *length, const char *value)
{
  size_t value_length;

  if ((length == NULL) || (value == NULL)) {
    return 0;
  }
  value_length = strlen(value);
  if (*length > (((size_t)-1) - value_length)) {
    return 0;
  }
  *length += value_length;
  return 1;
}

static char *strappy_openai_oauth_build_exchange_form(
  const strappy_openai_oauth_configuration *configuration,
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

  encoded_client_id = strappy_openai_oauth_form_encode(
    configuration->client_id);
  encoded_code = strappy_openai_oauth_form_encode(authorization_code);
  encoded_verifier = strappy_openai_oauth_form_encode(code_verifier);
  encoded_redirect = strappy_openai_oauth_form_encode(
    configuration->device_redirect_uri);
  if ((encoded_client_id == NULL) || (encoded_code == NULL) ||
      (encoded_verifier == NULL) || (encoded_redirect == NULL)) {
    free(encoded_client_id);
    strappy_openai_oauth_secure_free(encoded_code);
    strappy_openai_oauth_secure_free(encoded_verifier);
    free(encoded_redirect);
    return NULL;
  }

  length = strlen(prefix);
  if (!strappy_openai_oauth_add_length(&length, encoded_client_id) ||
      !strappy_openai_oauth_add_length(&length, code_label) ||
      !strappy_openai_oauth_add_length(&length, encoded_code) ||
      !strappy_openai_oauth_add_length(&length, verifier_label) ||
      !strappy_openai_oauth_add_length(&length, encoded_verifier) ||
      !strappy_openai_oauth_add_length(&length, redirect_label) ||
      !strappy_openai_oauth_add_length(&length, encoded_redirect) ||
      (length == (size_t)-1)) {
    free(encoded_client_id);
    strappy_openai_oauth_secure_free(encoded_code);
    strappy_openai_oauth_secure_free(encoded_verifier);
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
  strappy_openai_oauth_secure_free(encoded_code);
  strappy_openai_oauth_secure_free(encoded_verifier);
  free(encoded_redirect);
  return form;
}

static char *strappy_openai_oauth_build_refresh_form(
  const strappy_openai_oauth_configuration *configuration,
  const char *refresh_token)
{
  static const char prefix[] = "grant_type=refresh_token&refresh_token=";
  static const char client_label[] = "&client_id=";
  char *encoded_refresh;
  char *encoded_client_id;
  size_t length;
  char *form;

  encoded_refresh = strappy_openai_oauth_form_encode(refresh_token);
  encoded_client_id = strappy_openai_oauth_form_encode(
    configuration->client_id);
  if ((encoded_refresh == NULL) || (encoded_client_id == NULL)) {
    strappy_openai_oauth_secure_free(encoded_refresh);
    free(encoded_client_id);
    return NULL;
  }
  length = strlen(prefix);
  if (!strappy_openai_oauth_add_length(&length, encoded_refresh) ||
      !strappy_openai_oauth_add_length(&length, client_label) ||
      !strappy_openai_oauth_add_length(&length, encoded_client_id) ||
      (length == (size_t)-1)) {
    strappy_openai_oauth_secure_free(encoded_refresh);
    free(encoded_client_id);
    return NULL;
  }
  form = (char *)malloc(length + 1U);
  if (form != NULL) {
    snprintf(form,
             length + 1U,
             "%s%s%s%s",
             prefix,
             encoded_refresh,
             client_label,
             encoded_client_id);
  }
  strappy_openai_oauth_secure_free(encoded_refresh);
  free(encoded_client_id);
  return form;
}

static int strappy_openai_oauth_base64url_value(unsigned char byte)
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

static unsigned char *strappy_openai_oauth_base64url_decode(
  const char *value,
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

  if ((value == NULL) || (decoded_length_out == NULL) || (length == 0U) ||
      (length > STRAPPY_OPENAI_OAUTH_MAX_TOKEN_BYTES) ||
      ((length % 4U) == 1U) ||
      (length > ((((size_t)-1) - 4U) / 3U))) {
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
      strappy_openai_oauth_secure_wipe(decoded, capacity + 1U);
      free(decoded);
      return NULL;
    }
    decoded_value = strappy_openai_oauth_base64url_value(byte);
    if (decoded_value < 0) {
      strappy_openai_oauth_secure_wipe(decoded, capacity + 1U);
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
    strappy_openai_oauth_secure_wipe(decoded, capacity + 1U);
    free(decoded);
    return NULL;
  }
  decoded[decoded_length] = '\0';
  *decoded_length_out = decoded_length;
  return decoded;
}

static char *strappy_openai_oauth_extract_account_id(
  const char *access_token)
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
  if ((second_dot == NULL) || (second_dot == (first_dot + 1)) ||
      (strchr(second_dot + 1, '.') != NULL)) {
    return NULL;
  }
  payload_length = (size_t)(second_dot - first_dot - 1);
  decoded = strappy_openai_oauth_base64url_decode(first_dot + 1,
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
    account_id = strappy_openai_oauth_duplicate_bounded(
      account->valuestring,
      STRAPPY_OPENAI_OAUTH_MAX_ACCOUNT_ID_BYTES);
  }
  strappy_openai_oauth_wipe_json_strings(root);
  cJSON_Delete(root);
  strappy_openai_oauth_secure_wipe(decoded, decoded_length + 1U);
  free(decoded);
  return account_id;
}

static int strappy_openai_oauth_parse_credentials(
  strappy_openai_oauth_http_response *response,
  strappy_openai_oauth_credentials *credentials,
  const char *operation,
  char **error_out)
{
  cJSON *root;
  cJSON *item;
  long long now_milliseconds;
  long long expires_in_seconds;
  int ok;

  root = cJSON_Parse(response->body.data);
  if (!cJSON_IsObject(root)) {
    strappy_set_formatted_error(error_out,
                                "%s returned invalid JSON.",
                                operation);
    cJSON_Delete(root);
    return 0;
  }
  if ((response->status < 200L) || (response->status >= 300L)) {
    strappy_openai_oauth_set_http_error(error_out,
                                        operation,
                                        response,
                                        root);
    strappy_openai_oauth_wipe_json_strings(root);
    cJSON_Delete(root);
    return 0;
  }

  strappy_openai_oauth_credentials_init(credentials);
  item = cJSON_GetObjectItem(root, "access_token");
  if (cJSON_IsString(item) && (item->valuestring != NULL)) {
    credentials->access_token = strappy_openai_oauth_duplicate_bounded(
      item->valuestring,
      STRAPPY_OPENAI_OAUTH_MAX_TOKEN_BYTES);
  }
  item = cJSON_GetObjectItem(root, "refresh_token");
  if (cJSON_IsString(item) && (item->valuestring != NULL)) {
    credentials->refresh_token = strappy_openai_oauth_duplicate_bounded(
      item->valuestring,
      STRAPPY_OPENAI_OAUTH_MAX_TOKEN_BYTES);
  }
  expires_in_seconds = 0LL;
  item = cJSON_GetObjectItem(root, "expires_in");
  if (cJSON_IsNumber(item) && (item->valuedouble > 0.0) &&
      (item->valuedouble <= (double)(LLONG_MAX / 1000LL))) {
    expires_in_seconds = (long long)item->valuedouble;
    if ((double)expires_in_seconds != item->valuedouble) {
      expires_in_seconds = 0LL;
    }
  }
  if (credentials->access_token != NULL) {
    credentials->account_id = strappy_openai_oauth_extract_account_id(
      credentials->access_token);
  }
  ok = strappy_openai_oauth_now_milliseconds(&now_milliseconds) &&
    (credentials->access_token != NULL) &&
    (credentials->refresh_token != NULL) &&
    (credentials->account_id != NULL) && (expires_in_seconds > 0LL) &&
    (expires_in_seconds <=
     ((LLONG_MAX - now_milliseconds) / 1000LL));
  if (ok) {
    credentials->expires_at_milliseconds =
      now_milliseconds + (expires_in_seconds * 1000LL);
  }
  strappy_openai_oauth_wipe_json_strings(root);
  cJSON_Delete(root);
  if (!ok) {
    strappy_openai_oauth_credentials_destroy(credentials);
    strappy_set_formatted_error(
      error_out,
      "%s response is missing a token, expiry, or ChatGPT account claim.",
      operation);
    return 0;
  }
  return 1;
}

static int strappy_openai_oauth_poll_device_authorization(
  const strappy_openai_oauth_configuration *configuration,
  const strappy_openai_oauth_device *device,
  strappy_openai_oauth_transfer *transfer,
  char **authorization_code_out,
  char **code_verifier_out,
  char **error_out)
{
  char *poll_json;
  long interval_seconds;
  long long deadline;
  long long now;

  *authorization_code_out = NULL;
  *code_verifier_out = NULL;
  if (!strappy_openai_oauth_build_poll_json(device, &poll_json)) {
    strappy_set_error(error_out,
                      "Could not build device authorization polling request.");
    return 0;
  }
  if (!strappy_openai_oauth_now_milliseconds(&now) ||
      (now > (LLONG_MAX -
              (STRAPPY_OPENAI_OAUTH_DEVICE_TIMEOUT_SECONDS * 1000LL)))) {
    strappy_openai_oauth_secure_free(poll_json);
    strappy_set_error(error_out,
                      "Could not start the device authorization timer.");
    return 0;
  }
  deadline = now +
    (STRAPPY_OPENAI_OAUTH_DEVICE_TIMEOUT_SECONDS * 1000LL);
  interval_seconds = device->interval_seconds;

  while (!strappy_openai_oauth_is_cancelled(transfer) &&
         strappy_openai_oauth_now_milliseconds(&now) && (now < deadline)) {
    strappy_openai_oauth_http_response response;
    cJSON *root;
    const char *error_code;
    cJSON *item;
    int pending;
    int slow_down;
    int request_ok;

    request_ok = strappy_openai_oauth_http_post(
      configuration->device_poll_url,
      "Content-Type: application/json",
      poll_json,
      transfer,
      &response,
      error_out);
    if (!request_ok) {
      strappy_openai_oauth_http_response_destroy(&response, 1);
      strappy_openai_oauth_secure_free(poll_json);
      return 0;
    }

    root = cJSON_Parse(response.body.data);
    pending = (response.status == 403L) || (response.status == 404L);
    slow_down = 0;
    error_code = strappy_openai_oauth_error_code(root);
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
          strappy_openai_oauth_duplicate_bounded(
            item->valuestring,
            STRAPPY_OPENAI_OAUTH_MAX_AUTH_CODE_BYTES);
      }
      item = cJSON_GetObjectItem(root, "code_verifier");
      if (cJSON_IsString(item) && (item->valuestring != NULL)) {
        *code_verifier_out = strappy_openai_oauth_duplicate_bounded(
          item->valuestring,
          STRAPPY_OPENAI_OAUTH_MAX_AUTH_CODE_BYTES);
      }
      if ((*authorization_code_out != NULL) &&
          (*code_verifier_out != NULL)) {
        strappy_openai_oauth_wipe_json_strings(root);
        cJSON_Delete(root);
        strappy_openai_oauth_http_response_destroy(&response, 1);
        strappy_openai_oauth_secure_free(poll_json);
        return 1;
      }
      strappy_openai_oauth_secure_free(*authorization_code_out);
      strappy_openai_oauth_secure_free(*code_verifier_out);
      *authorization_code_out = NULL;
      *code_verifier_out = NULL;
      pending = 0;
    }

    if (!pending) {
      if (cJSON_IsObject(root)) {
        strappy_openai_oauth_set_http_error(
          error_out,
          "Device authorization polling",
          &response,
          root);
      } else {
        strappy_set_error(error_out,
                          "Device authorization returned invalid JSON.");
      }
      strappy_openai_oauth_wipe_json_strings(root);
      cJSON_Delete(root);
      strappy_openai_oauth_http_response_destroy(&response, 1);
      strappy_openai_oauth_secure_free(poll_json);
      return 0;
    }
    if (slow_down) {
      if (interval_seconds <=
          (STRAPPY_OPENAI_OAUTH_MAX_POLL_INTERVAL_SECONDS - 5L)) {
        interval_seconds += 5L;
      } else {
        interval_seconds =
          STRAPPY_OPENAI_OAUTH_MAX_POLL_INTERVAL_SECONDS;
      }
    }
    strappy_openai_oauth_wipe_json_strings(root);
    cJSON_Delete(root);
    strappy_openai_oauth_http_response_destroy(&response, 1);
    if (!strappy_openai_oauth_wait_seconds(interval_seconds, transfer)) {
      break;
    }
  }

  strappy_openai_oauth_secure_free(poll_json);
  if (strappy_openai_oauth_is_cancelled(transfer)) {
    strappy_set_error(error_out, "ChatGPT sign-in was cancelled.");
  } else {
    strappy_set_error(error_out,
                      "Device authorization timed out after 15 minutes.");
  }
  return 0;
}

static int strappy_openai_oauth_exchange_authorization_code(
  const strappy_openai_oauth_configuration *configuration,
  const char *authorization_code,
  const char *code_verifier,
  strappy_openai_oauth_transfer *transfer,
  strappy_openai_oauth_credentials *credentials,
  char **error_out)
{
  char *form;
  strappy_openai_oauth_http_response response;
  int ok;

  form = strappy_openai_oauth_build_exchange_form(configuration,
                                                   authorization_code,
                                                   code_verifier);
  if (form == NULL) {
    strappy_set_error(error_out,
                      "Could not build the OAuth token exchange request.");
    return 0;
  }
  ok = strappy_openai_oauth_http_post(
    configuration->token_url,
    "Content-Type: application/x-www-form-urlencoded",
    form,
    transfer,
    &response,
    error_out);
  strappy_openai_oauth_secure_free(form);
  if (ok) {
    ok = strappy_openai_oauth_parse_credentials(&response,
                                                 credentials,
                                                 "OAuth token exchange",
                                                 error_out);
  }
  strappy_openai_oauth_http_response_destroy(&response, 1);
  return ok;
}

void strappy_openai_oauth_default_configuration(
  strappy_openai_oauth_configuration *configuration)
{
  if (configuration == NULL) {
    return;
  }
  configuration->client_id = STRAPPY_OPENAI_OAUTH_CLIENT_ID;
  configuration->device_start_url =
    STRAPPY_OPENAI_OAUTH_DEVICE_START_URL;
  configuration->device_poll_url =
    STRAPPY_OPENAI_OAUTH_DEVICE_POLL_URL;
  configuration->token_url = STRAPPY_OPENAI_OAUTH_TOKEN_URL;
  configuration->verification_url =
    STRAPPY_OPENAI_OAUTH_VERIFICATION_URL;
  configuration->device_redirect_uri =
    STRAPPY_OPENAI_OAUTH_DEVICE_REDIRECT_URI;
}

int strappy_openai_oauth_set_cainfo(const char *path, char **error_out)
{
  char *copy;

  if ((path == NULL) || (path[0] == '\0')) {
    strappy_set_error(error_out,
                      "OAuth CA certificate path is not configured.");
    return 0;
  }
  copy = strappy_string_duplicate(path);
  if (copy == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate OAuth CA certificate path.");
    return 0;
  }
  if (!strappy_openai_oauth_ensure_curl_initialized(error_out)) {
    free(copy);
    return 0;
  }
  free(strappy_openai_oauth_cainfo_path);
  strappy_openai_oauth_cainfo_path = copy;
  return 1;
}

void strappy_openai_oauth_device_init(
  strappy_openai_oauth_device *device)
{
  if (device != NULL) {
    memset(device, 0, sizeof(*device));
  }
}

void strappy_openai_oauth_device_destroy(
  strappy_openai_oauth_device *device)
{
  if (device == NULL) {
    return;
  }
  strappy_openai_oauth_secure_free(device->device_auth_id);
  strappy_openai_oauth_secure_free(device->user_code);
  strappy_openai_oauth_device_init(device);
}

void strappy_openai_oauth_credentials_init(
  strappy_openai_oauth_credentials *credentials)
{
  if (credentials != NULL) {
    memset(credentials, 0, sizeof(*credentials));
  }
}

void strappy_openai_oauth_credentials_destroy(
  strappy_openai_oauth_credentials *credentials)
{
  if (credentials == NULL) {
    return;
  }
  strappy_openai_oauth_secure_free(credentials->access_token);
  strappy_openai_oauth_secure_free(credentials->refresh_token);
  strappy_openai_oauth_secure_free(credentials->account_id);
  strappy_openai_oauth_credentials_init(credentials);
}

int strappy_openai_oauth_start_device_authorization(
  const strappy_openai_oauth_configuration *configuration,
  strappy_openai_oauth_device *device,
  strappy_openai_oauth_cancel_callback cancel_callback,
  void *cancel_user_data,
  char **error_out)
{
  cJSON *request;
  cJSON *root;
  cJSON *item;
  char *request_json;
  strappy_openai_oauth_transfer transfer;
  strappy_openai_oauth_http_response response;
  int ok;

  if (!strappy_openai_oauth_configuration_is_valid(configuration) ||
      (device == NULL)) {
    strappy_set_error(error_out,
                      "Device authorization configuration is invalid.");
    return 0;
  }
  strappy_openai_oauth_device_init(device);
  transfer.cancel_callback = cancel_callback;
  transfer.cancel_user_data = cancel_user_data;
  request = cJSON_CreateObject();
  if ((request == NULL) ||
      (cJSON_AddStringToObject(request,
                               "client_id",
                               configuration->client_id) == NULL)) {
    cJSON_Delete(request);
    strappy_set_error(error_out,
                      "Could not build device authorization request.");
    return 0;
  }
  request_json = cJSON_PrintUnformatted(request);
  cJSON_Delete(request);
  if (request_json == NULL) {
    strappy_set_error(error_out,
                      "Could not serialize device authorization request.");
    return 0;
  }

  ok = strappy_openai_oauth_http_post(
    configuration->device_start_url,
    "Content-Type: application/json",
    request_json,
    &transfer,
    &response,
    error_out);
  free(request_json);
  if (!ok) {
    strappy_openai_oauth_http_response_destroy(&response, 0);
    return 0;
  }
  root = cJSON_Parse(response.body.data);
  if ((response.status < 200L) || (response.status >= 300L)) {
    if (response.status == 404L) {
      strappy_set_error(
        error_out,
        "Device-code login is disabled in ChatGPT security settings or "
        "workspace permissions.");
    } else {
      strappy_openai_oauth_set_http_error(error_out,
                                          "Device authorization",
                                          &response,
                                          root);
    }
    strappy_openai_oauth_wipe_json_strings(root);
    cJSON_Delete(root);
    strappy_openai_oauth_http_response_destroy(&response, 1);
    return 0;
  }
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    strappy_openai_oauth_http_response_destroy(&response, 1);
    strappy_set_error(error_out,
                      "Device authorization returned invalid JSON.");
    return 0;
  }

  item = cJSON_GetObjectItem(root, "device_auth_id");
  if (cJSON_IsString(item) && (item->valuestring != NULL)) {
    device->device_auth_id = strappy_openai_oauth_duplicate_bounded(
      item->valuestring,
      STRAPPY_OPENAI_OAUTH_MAX_DEVICE_ID_BYTES);
  }
  item = cJSON_GetObjectItem(root, "user_code");
  if (cJSON_IsString(item) && (item->valuestring != NULL)) {
    device->user_code = strappy_openai_oauth_duplicate_bounded(
      item->valuestring,
      STRAPPY_OPENAI_OAUTH_MAX_USER_CODE_BYTES);
  }
  item = cJSON_GetObjectItem(root, "interval");
  ok = (device->device_auth_id != NULL) && (device->user_code != NULL) &&
    strappy_openai_oauth_parse_poll_interval(item,
                                             &device->interval_seconds);
  strappy_openai_oauth_wipe_json_strings(root);
  cJSON_Delete(root);
  strappy_openai_oauth_http_response_destroy(&response, 1);
  if (!ok) {
    strappy_openai_oauth_device_destroy(device);
    strappy_set_error(error_out,
                      "Device authorization response is missing fields.");
    return 0;
  }
  return 1;
}

int strappy_openai_oauth_complete_device_authorization(
  const strappy_openai_oauth_configuration *configuration,
  const strappy_openai_oauth_device *device,
  strappy_openai_oauth_credentials *credentials,
  strappy_openai_oauth_cancel_callback cancel_callback,
  void *cancel_user_data,
  char **error_out)
{
  strappy_openai_oauth_transfer transfer;
  char *authorization_code;
  char *code_verifier;
  int ok;

  if (!strappy_openai_oauth_configuration_is_valid(configuration) ||
      (device == NULL) || (device->device_auth_id == NULL) ||
      (device->user_code == NULL) || (device->interval_seconds <= 0L) ||
      (credentials == NULL)) {
    strappy_set_error(error_out,
                      "Device authorization completion is invalid.");
    return 0;
  }
  strappy_openai_oauth_credentials_init(credentials);
  transfer.cancel_callback = cancel_callback;
  transfer.cancel_user_data = cancel_user_data;
  authorization_code = NULL;
  code_verifier = NULL;
  ok = strappy_openai_oauth_poll_device_authorization(
    configuration,
    device,
    &transfer,
    &authorization_code,
    &code_verifier,
    error_out);
  if (ok) {
    ok = strappy_openai_oauth_exchange_authorization_code(
      configuration,
      authorization_code,
      code_verifier,
      &transfer,
      credentials,
      error_out);
  }
  strappy_openai_oauth_secure_free(authorization_code);
  strappy_openai_oauth_secure_free(code_verifier);
  return ok;
}

int strappy_openai_oauth_refresh_credentials(
  const strappy_openai_oauth_configuration *configuration,
  const char *refresh_token,
  strappy_openai_oauth_credentials *credentials,
  strappy_openai_oauth_cancel_callback cancel_callback,
  void *cancel_user_data,
  char **error_out)
{
  strappy_openai_oauth_transfer transfer;
  strappy_openai_oauth_http_response response;
  char *form;
  int ok;

  if (!strappy_openai_oauth_configuration_is_valid(configuration) ||
      (refresh_token == NULL) || (refresh_token[0] == '\0') ||
      (credentials == NULL)) {
    strappy_set_error(error_out, "OAuth refresh request is invalid.");
    return 0;
  }
  strappy_openai_oauth_credentials_init(credentials);
  transfer.cancel_callback = cancel_callback;
  transfer.cancel_user_data = cancel_user_data;
  form = strappy_openai_oauth_build_refresh_form(configuration,
                                                  refresh_token);
  if (form == NULL) {
    strappy_set_error(error_out, "Could not build OAuth refresh request.");
    return 0;
  }
  ok = strappy_openai_oauth_http_post(
    configuration->token_url,
    "Content-Type: application/x-www-form-urlencoded",
    form,
    &transfer,
    &response,
    error_out);
  strappy_openai_oauth_secure_free(form);
  if (ok) {
    ok = strappy_openai_oauth_parse_credentials(&response,
                                                 credentials,
                                                 "OAuth token refresh",
                                                 error_out);
  }
  strappy_openai_oauth_http_response_destroy(&response, 1);
  return ok;
}
