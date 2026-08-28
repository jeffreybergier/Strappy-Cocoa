#include "strappy_client_transport.h"

#include "strappy_core.h"
#include "strappy_identity.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct strappy_client_curl_context {
  const strappy_client_transport_request *request;
  int cancelled;
} strappy_client_curl_context;

static int strappy_curl_initialized = 0;
static char *strappy_cainfo_path = NULL;

static int strappy_client_curl_ensure_initialized(char **error_out)
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

static int strappy_client_curl_require_https(CURL *curl,
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
  code = curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR,
                          is_loopback_test ? "http,https" : "https");
#else
  code = curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
#endif
  if (code == CURLE_OK) {
#if defined(STRAPPY_ENABLE_LOOPBACK_HTTP_TESTS)
    code = curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR,
                            is_loopback_test ? "http,https" : "https");
#else
    code = curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#endif
  }
#else
#if defined(STRAPPY_ENABLE_LOOPBACK_HTTP_TESTS)
  code = curl_easy_setopt(curl, CURLOPT_PROTOCOLS,
                          is_loopback_test ?
                            (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS) :
                            (long)CURLPROTO_HTTPS);
#else
  code = curl_easy_setopt(curl, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
#endif
  if (code == CURLE_OK) {
#if defined(STRAPPY_ENABLE_LOOPBACK_HTTP_TESTS)
    code = curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS,
                            is_loopback_test ?
                              (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS) :
                              (long)CURLPROTO_HTTPS);
#else
    code = curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS,
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

static size_t strappy_client_curl_write_body(void *bytes,
                                             size_t size,
                                             size_t count,
                                             void *context_value)
{
  strappy_client_curl_context *context;
  size_t length;

  if ((bytes == NULL) || (context_value == NULL) ||
      ((size != 0U) && (count > (((size_t)-1) / size)))) {
    return 0U;
  }
  context = (strappy_client_curl_context *)context_value;
  length = size * count;
  if (context->request->write_body == NULL) {
    return length;
  }
  return context->request->write_body(
    bytes, length, context->request->write_body_context);
}

static size_t strappy_client_curl_write_header(void *bytes,
                                               size_t size,
                                               size_t count,
                                               void *context_value)
{
  strappy_client_curl_context *context;
  size_t length;

  if ((bytes == NULL) || (context_value == NULL) ||
      ((size != 0U) && (count > (((size_t)-1) / size)))) {
    return 0U;
  }
  context = (strappy_client_curl_context *)context_value;
  length = size * count;
  if (context->request->write_header == NULL) {
    return length;
  }
  return context->request->write_header(
    bytes, length, context->request->write_header_context);
}

#if LIBCURL_VERSION_NUM >= 0x072000
static int strappy_client_curl_progress(void *context_value,
                                       curl_off_t download_total,
                                       curl_off_t download_now,
                                       curl_off_t upload_total,
                                       curl_off_t upload_now)
#else
static int strappy_client_curl_progress(void *context_value,
                                       double download_total,
                                       double download_now,
                                       double upload_total,
                                       double upload_now)
#endif
{
  strappy_client_curl_context *context;

  (void)download_total;
  (void)download_now;
  (void)upload_total;
  (void)upload_now;
  context = (strappy_client_curl_context *)context_value;
  if ((context != NULL) && (context->request->poll_cancelled != NULL) &&
      context->request->poll_cancelled(context->request->poll_context)) {
    context->cancelled = 1;
    return 1;
  }
  return 0;
}

static int strappy_client_curl_copy_info(CURL *curl,
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

static void strappy_client_curl_destroy_headers(struct curl_slist *headers)
{
  struct curl_slist *item;

  for (item = headers; item != NULL; item = item->next) {
    if ((item->data != NULL) &&
        (strncasecmp(item->data, "Authorization:", 14U) == 0)) {
      volatile unsigned char *cursor;
      size_t remaining;

      cursor = (volatile unsigned char *)item->data;
      remaining = strlen(item->data);
      while (remaining > 0U) {
        *cursor++ = 0U;
        remaining--;
      }
    }
  }
  curl_slist_free_all(headers);
}

void strappy_client_transport_result_init(
  strappy_client_transport_result *result)
{
  if (result != NULL) {
    memset(result, 0, sizeof(*result));
  }
}

void strappy_client_transport_result_destroy(
  strappy_client_transport_result *result)
{
  if (result == NULL) {
    return;
  }
  free(result->effective_url);
  free(result->error_message);
  free(result->content_type);
  strappy_client_transport_result_init(result);
}

int strappy_client_transport_set_cainfo(const char *path, char **error_out)
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

int strappy_client_transport_supports_request_header(const char *name)
{
  (void)name;
  return 1;
}

char *strappy_client_transport_copy_user_agent(char **error_out)
{
  return strappy_identity_copy_user_agent(error_out);
}

int strappy_client_transport_execute(
  const strappy_client_transport_request *request,
  strappy_client_transport_result *result,
  char **error_out)
{
  CURL *curl;
  CURLcode code;
  struct curl_slist *headers;
  strappy_client_curl_context context;
  char curl_error[CURL_ERROR_SIZE];
  size_t index;
  int ok;
  int write_completed;

  if ((request == NULL) || (result == NULL) || (request->url == NULL) ||
      (request->method == NULL)) {
    strappy_set_error(error_out, "HTTP transport request is not configured.");
    return 0;
  }
  strappy_client_transport_result_init(result);
  if (!strappy_client_curl_ensure_initialized(error_out)) {
    return 0;
  }
  headers = NULL;
  for (index = 0U; index < request->header_count; index++) {
    struct curl_slist *next;

    next = curl_slist_append(headers, request->headers[index]);
    if (next == NULL) {
      strappy_client_curl_destroy_headers(headers);
      strappy_set_error(error_out, "Could not allocate HTTP headers.");
      return 0;
    }
    headers = next;
  }
  curl = curl_easy_init();
  if (curl == NULL) {
    strappy_client_curl_destroy_headers(headers);
    strappy_set_error(error_out, "Could not create curl handle.");
    return 0;
  }
  if (!strappy_client_curl_require_https(curl, request->url, error_out)) {
    curl_easy_cleanup(curl);
    strappy_client_curl_destroy_headers(headers);
    return 0;
  }
  memset(&context, 0, sizeof(context));
  context.request = request;
  memset(curl_error, 0, sizeof(curl_error));

  curl_easy_setopt(curl, CURLOPT_URL, request->url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  if (strcmp(request->method, "POST") == 0) {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request->body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)request->body_length);
  }
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, strappy_client_curl_write_body);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&context);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION,
                   strappy_client_curl_write_header);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)&context);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
#if LIBCURL_VERSION_NUM >= 0x072000
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,
                   strappy_client_curl_progress);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void *)&context);
#else
  curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION,
                   strappy_client_curl_progress);
  curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, (void *)&context);
#endif
  if ((request->user_agent != NULL) && (request->user_agent[0] != '\0')) {
    curl_easy_setopt(curl, CURLOPT_USERAGENT, request->user_agent);
  }
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, request->timeout_seconds);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  if ((strappy_cainfo_path != NULL) && (strappy_cainfo_path[0] != '\0')) {
    curl_easy_setopt(curl, CURLOPT_CAINFO, strappy_cainfo_path);
  }

  if ((request->poll_cancelled != NULL) &&
      request->poll_cancelled(request->poll_context)) {
    context.cancelled = 1;
    code = CURLE_ABORTED_BY_CALLBACK;
  } else {
    code = curl_easy_perform(curl);
  }
  if (code == CURLE_ABORTED_BY_CALLBACK) {
    context.cancelled = 1;
  }
  write_completed = (request->write_is_complete != NULL) &&
    request->write_is_complete(request->write_complete_context);
  if ((code == CURLE_WRITE_ERROR) && write_completed) {
    code = CURLE_OK;
  }

  result->transport_code = (long)code;
  result->cancelled = context.cancelled;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result->http_status);
  curl_easy_getinfo(curl, CURLINFO_NAMELOOKUP_TIME,
                    &result->name_lookup_seconds);
  curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &result->connect_seconds);
  curl_easy_getinfo(curl, CURLINFO_STARTTRANSFER_TIME,
                    &result->start_transfer_seconds);
  curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &result->total_seconds);
  ok = strappy_client_curl_copy_info(curl, CURLINFO_EFFECTIVE_URL,
                                     &result->effective_url) &&
       strappy_client_curl_copy_info(curl, CURLINFO_CONTENT_TYPE,
                                     &result->content_type);
  if ((code != CURLE_OK) && !write_completed) {
    const char *message;

    message = context.cancelled ? "Responses request was cancelled." :
      ((curl_error[0] != '\0') ? curl_error : curl_easy_strerror(code));
    result->error_message = strappy_string_duplicate(message);
    ok = ok && (result->error_message != NULL);
  }
  curl_easy_cleanup(curl);
  strappy_client_curl_destroy_headers(headers);
  if (!ok) {
    strappy_client_transport_result_destroy(result);
    strappy_set_error(error_out, "Could not allocate HTTP transport result.");
    return 0;
  }
  return 1;
}
