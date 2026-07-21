#include "strappy_web.h"

#include "strappy_core.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define STRAPPY_WEB_SEARCH_URL "https://lite.duckduckgo.com/lite/"
#define STRAPPY_WEB_MAX_RESPONSE_BYTES (256U * 1024U)
#define STRAPPY_WEB_COOKIE_FILENAME "strappy-web-cookies.txt"
#define STRAPPY_WEB_TIMEOUT_SECONDS 30L
#define STRAPPY_WEB_CONNECT_TIMEOUT_SECONDS 10L
#define STRAPPY_WEB_MAX_REDIRECTS 5L
#define STRAPPY_WEB_USER_AGENT \
  "Mozilla/5.0 (iPhone; CPU iPhone OS 6_0 like Mac OS X) " \
  "AppleWebKit/536.26 (KHTML, like Gecko) Version/6.0 Mobile/10A405 " \
  "Safari/8536.25"
#define STRAPPY_WEB_SEARCH_RATE_LIMIT_ERROR \
  "web_search is temporarily rate limited by DuckDuckGo (CAPTCHA " \
  "challenge). Do not retry immediately; continue without web search or " \
  "try again later."

typedef struct strappy_web_buffer {
  char *data;
  size_t length;
} strappy_web_buffer;

typedef struct strappy_web_http_buffer {
  char *data;
  size_t length;
  size_t maximum_length;
  int too_large;
} strappy_web_http_buffer;

typedef struct strappy_web_http_result {
  char *body;
  char *content_type;
  char *effective_url;
  size_t body_length;
  long http_status;
  int cancelled;
} strappy_web_http_result;

typedef struct strappy_web_transfer_context {
  strappy_web_continue_callback continue_callback;
  void *continue_callback_data;
  strappy_web_http_buffer *response_buffer;
  int cancelled;
  int invalid_redirect;
} strappy_web_transfer_context;

static int strappy_web_curl_initialized = 0;
static char *strappy_web_cainfo_path = NULL;
static pthread_mutex_t strappy_web_request_mutex = PTHREAD_MUTEX_INITIALIZER;

static void strappy_web_buffer_destroy(strappy_web_buffer *buffer)
{
  if (buffer == NULL) {
    return;
  }
  free(buffer->data);
  buffer->data = NULL;
  buffer->length = 0U;
}

static int strappy_web_buffer_append_length(strappy_web_buffer *buffer,
                                            const char *value,
                                            size_t length)
{
  char *next;

  if ((buffer == NULL) || ((value == NULL) && (length > 0U)) ||
      (buffer->length > ((size_t)-1) - length - 1U)) {
    return 0;
  }
  next = (char *)realloc(buffer->data, buffer->length + length + 1U);
  if (next == NULL) {
    return 0;
  }
  buffer->data = next;
  if (length > 0U) {
    memcpy(buffer->data + buffer->length, value, length);
  }
  buffer->length += length;
  buffer->data[buffer->length] = '\0';
  return 1;
}

static int strappy_web_buffer_append(strappy_web_buffer *buffer,
                                     const char *value)
{
  return (value != NULL) ?
    strappy_web_buffer_append_length(buffer, value, strlen(value)) : 0;
}

void strappy_web_result_init(strappy_web_result *result)
{
  if (result != NULL) {
    memset(result, 0, sizeof(*result));
  }
}

void strappy_web_result_destroy(strappy_web_result *result)
{
  if (result == NULL) {
    return;
  }
  free(result->body);
  free(result->content_type);
  strappy_web_result_init(result);
}

int strappy_web_set_cainfo(const char *path, char **error_out)
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
  free(strappy_web_cainfo_path);
  strappy_web_cainfo_path = copy;
  return 1;
}

static void strappy_web_http_buffer_init(strappy_web_http_buffer *buffer)
{
  if (buffer == NULL) {
    return;
  }
  buffer->data = NULL;
  buffer->length = 0U;
  buffer->maximum_length = (size_t)-1;
  buffer->too_large = 0;
}

static void strappy_web_http_buffer_destroy(strappy_web_http_buffer *buffer)
{
  if (buffer == NULL) {
    return;
  }
  free(buffer->data);
  strappy_web_http_buffer_init(buffer);
}

static int strappy_web_http_buffer_append(strappy_web_http_buffer *buffer,
                                          const char *data,
                                          size_t length)
{
  char *next_data;

  if ((buffer == NULL) || ((data == NULL) && (length > 0U)) ||
      (buffer->length > (((size_t)-1) - length - 1U))) {
    return 0;
  }
  if (length > (buffer->maximum_length - buffer->length)) {
    buffer->too_large = 1;
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

static size_t strappy_web_write_callback(void *contents,
                                         size_t size,
                                         size_t nmemb,
                                         void *userp)
{
  strappy_web_http_buffer *buffer;
  size_t real_size;

  if ((contents == NULL) || (userp == NULL) ||
      ((size != 0U) && (nmemb > (((size_t)-1) / size)))) {
    return 0U;
  }

  real_size = size * nmemb;
  buffer = (strappy_web_http_buffer *)userp;
  return strappy_web_http_buffer_append(buffer,
                                       (const char *)contents,
                                       real_size) ? real_size : 0U;
}

static int strappy_web_ensure_curl_initialized(char **error_out)
{
  CURLcode code;

  if (strappy_web_curl_initialized) {
    return 1;
  }
  code = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (code != CURLE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not initialize curl: %s",
                                curl_easy_strerror(code));
    return 0;
  }
  strappy_web_curl_initialized = 1;
  return 1;
}

static void strappy_web_http_result_init(strappy_web_http_result *result)
{
  if (result != NULL) {
    memset(result, 0, sizeof(*result));
  }
}

static void strappy_web_http_result_destroy(strappy_web_http_result *result)
{
  if (result == NULL) {
    return;
  }
  free(result->body);
  free(result->content_type);
  free(result->effective_url);
  strappy_web_http_result_init(result);
}

static int strappy_web_ipv4_is_public(unsigned long address)
{
  if (((address & 0xff000000UL) == 0x00000000UL) ||
      ((address & 0xff000000UL) == 0x0a000000UL) ||
      ((address & 0xffc00000UL) == 0x64400000UL) ||
      ((address & 0xff000000UL) == 0x7f000000UL) ||
      ((address & 0xffff0000UL) == 0xa9fe0000UL) ||
      ((address & 0xfff00000UL) == 0xac100000UL) ||
      ((address & 0xffffff00UL) == 0xc0000000UL) ||
      ((address & 0xffffff00UL) == 0xc0000200UL) ||
      ((address & 0xffff0000UL) == 0xc0a80000UL) ||
      ((address & 0xffffff00UL) == 0xc0586300UL) ||
      ((address & 0xfffe0000UL) == 0xc6120000UL) ||
      ((address & 0xffffff00UL) == 0xc6336400UL) ||
      ((address & 0xffffff00UL) == 0xcb007100UL) ||
      ((address & 0xf0000000UL) == 0xe0000000UL) ||
      ((address & 0xf0000000UL) == 0xf0000000UL)) {
    return 0;
  }
  return 1;
}

static int strappy_web_sockaddr_is_public(const struct sockaddr *address)
{
  if (address == NULL) {
    return 0;
  }
  if (address->sa_family == AF_INET) {
    const struct sockaddr_in *ipv4;
    unsigned long host_address;

    ipv4 = (const struct sockaddr_in *)address;
    host_address = (unsigned long)ntohl(ipv4->sin_addr.s_addr);
    return strappy_web_ipv4_is_public(host_address);
  }
#ifdef AF_INET6
  if (address->sa_family == AF_INET6) {
    const struct sockaddr_in6 *ipv6;
    const unsigned char *bytes;

    ipv6 = (const struct sockaddr_in6 *)address;
    bytes = (const unsigned char *)&ipv6->sin6_addr;
    if ((bytes[0] == 0U) && (bytes[1] == 0U) && (bytes[2] == 0U) &&
        (bytes[3] == 0U) && (bytes[4] == 0U) && (bytes[5] == 0U) &&
        (bytes[6] == 0U) && (bytes[7] == 0U) && (bytes[8] == 0U) &&
        (bytes[9] == 0U) && (bytes[10] == 0xffU) &&
        (bytes[11] == 0xffU)) {
      unsigned long mapped;

      mapped = ((unsigned long)bytes[12] << 24) |
        ((unsigned long)bytes[13] << 16) |
        ((unsigned long)bytes[14] << 8) |
        (unsigned long)bytes[15];
      return strappy_web_ipv4_is_public(mapped);
    }
    if ((bytes[0] & 0xe0U) != 0x20U) {
      return 0;
    }
    if ((bytes[0] == 0x20U) && (bytes[1] == 0x01U) &&
        (bytes[2] == 0x0dU) && (bytes[3] == 0xb8U)) {
      return 0;
    }
    return 1;
  }
#endif
  return 0;
}

static curl_socket_t strappy_web_open_socket(void *clientp,
                                             curlsocktype purpose,
                                             struct curl_sockaddr *address)
{
  strappy_web_transfer_context *context;
  curl_socket_t descriptor;

  context = (strappy_web_transfer_context *)clientp;
  if ((purpose != CURLSOCKTYPE_IPCXN) || (address == NULL) ||
      !strappy_web_sockaddr_is_public(&address->addr)) {
    if (context != NULL) {
      context->cancelled = -1;
    }
    return CURL_SOCKET_BAD;
  }
  descriptor = socket(address->family, address->socktype, address->protocol);
  return descriptor;
}

static int strappy_web_progress_callback(void *clientp,
                                         double download_total,
                                         double download_now,
                                         double upload_total,
                                         double upload_now)
{
  strappy_web_transfer_context *context;

  (void)download_total;
  (void)download_now;
  (void)upload_total;
  (void)upload_now;
  context = (strappy_web_transfer_context *)clientp;
  if ((context != NULL) && (context->continue_callback != NULL) &&
      !context->continue_callback(context->continue_callback_data)) {
    context->cancelled = 1;
    return 1;
  }
  return 0;
}

static int strappy_web_redirect_is_allowed(const char *value, size_t length)
{
  const char *authority;
  const char *authority_end;
  const char *cursor;

  while ((length > 0U) && isspace((unsigned char)value[0])) {
    value++;
    length--;
  }
  while ((length > 0U) && isspace((unsigned char)value[length - 1U])) {
    length--;
  }
  if ((length == 0U) || (length > STRAPPY_WEB_MAX_ARGUMENT_BYTES)) {
    return 0;
  }
  for (cursor = value; cursor < value + length; cursor++) {
    if (((unsigned char)*cursor <= 0x20U) ||
        ((unsigned char)*cursor == 0x7fU) || (*cursor == '\\')) {
      return 0;
    }
  }
  authority = NULL;
  if ((length >= 8U) && (strncasecmp(value, "https://", 8U) == 0)) {
    authority = value + 8U;
  } else if ((length >= 7U) &&
             (strncasecmp(value, "http://", 7U) == 0)) {
    authority = value + 7U;
  } else if ((length >= 2U) && (value[0] == '/') && (value[1] == '/')) {
    authority = value + 2U;
  } else {
    for (cursor = value; cursor < value + length; cursor++) {
      if ((*cursor == '/') || (*cursor == '?') || (*cursor == '#')) {
        break;
      }
      if (*cursor == ':') {
        return 0;
      }
    }
    return 1;
  }
  authority_end = authority;
  while ((authority_end < value + length) && (*authority_end != '/') &&
         (*authority_end != '?') && (*authority_end != '#')) {
    authority_end++;
  }
  return (authority_end > authority) &&
    (memchr(authority, '@', (size_t)(authority_end - authority)) == NULL);
}

static size_t strappy_web_header_callback(void *contents,
                                          size_t size,
                                          size_t nmemb,
                                          void *userp)
{
  strappy_web_transfer_context *context;
  const char *line;
  size_t length;

  if ((contents == NULL) || (userp == NULL) ||
      ((size != 0U) && (nmemb > ((size_t)-1) / size))) {
    return 0U;
  }
  length = size * nmemb;
  line = (const char *)contents;
  context = (strappy_web_transfer_context *)userp;
  if ((length >= 5U) && (strncasecmp(line, "HTTP/", 5U) == 0) &&
      (context->response_buffer != NULL)) {
    free(context->response_buffer->data);
    context->response_buffer->data = NULL;
    context->response_buffer->length = 0U;
    context->response_buffer->too_large = 0;
  }
  if ((length >= 9U) && (strncasecmp(line, "Location:", 9U) == 0) &&
      !strappy_web_redirect_is_allowed(line + 9U, length - 9U)) {
    context->invalid_redirect = 1;
    return 0U;
  }
  return length;
}

static int strappy_web_append_header(struct curl_slist **headers,
                                     const char *value)
{
  struct curl_slist *next;

  next = curl_slist_append(*headers, value);
  if (next == NULL) {
    return 0;
  }
  *headers = next;
  return 1;
}

static int strappy_web_send_public_request(
  const char *url,
  const char *post_fields,
  const char *cookie_jar_path,
  size_t maximum_body_bytes,
  strappy_web_continue_callback continue_callback,
  void *continue_callback_data,
  strappy_web_http_result *result,
  char **error_out)
{
  CURL *curl;
  CURLcode code;
  struct curl_slist *headers;
  strappy_web_http_buffer response_buffer;
  strappy_web_transfer_context transfer_context;
  char curl_error[CURL_ERROR_SIZE];
  char *content_type;
  char *effective_url;
  size_t post_length;
  int mutex_locked;
  int ok;

  if (result == NULL) {
    strappy_set_error(error_out, "Web request result is missing.");
    return 0;
  }
  strappy_web_http_result_init(result);
  if ((url == NULL) || (url[0] == '\0') || (cookie_jar_path == NULL) ||
      (cookie_jar_path[0] == '\0') || (maximum_body_bytes == 0U)) {
    strappy_set_error(error_out, "Web request configuration is incomplete.");
    return 0;
  }
  post_length = (post_fields != NULL) ? strlen(post_fields) : 0U;
  if (post_length > (size_t)LONG_MAX) {
    strappy_set_error(error_out, "Web request body is too large.");
    return 0;
  }

  curl = NULL;
  headers = NULL;
  content_type = NULL;
  effective_url = NULL;
  mutex_locked = 0;
  ok = 0;
  strappy_web_http_buffer_init(&response_buffer);
  response_buffer.maximum_length = maximum_body_bytes;
  memset(&transfer_context, 0, sizeof(transfer_context));
  transfer_context.continue_callback = continue_callback;
  transfer_context.continue_callback_data = continue_callback_data;
  transfer_context.response_buffer = &response_buffer;
  memset(curl_error, 0, sizeof(curl_error));

  if (pthread_mutex_lock(&strappy_web_request_mutex) != 0) {
    strappy_set_error(error_out, "Could not lock the web cookie store.");
    goto cleanup;
  }
  mutex_locked = 1;
  if (!strappy_web_ensure_curl_initialized(error_out)) {
    goto cleanup;
  }

  if (!strappy_web_append_header(
        &headers,
        "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8") ||
      !strappy_web_append_header(&headers,
                                 "Accept-Language: en-US,en;q=0.9") ||
      ((post_fields != NULL) &&
       (!strappy_web_append_header(
          &headers,
          "Content-Type: application/x-www-form-urlencoded") ||
        !strappy_web_append_header(
          &headers,
          "Origin: https://lite.duckduckgo.com") ||
        !strappy_web_append_header(
          &headers,
          "Referer: https://lite.duckduckgo.com/")))) {
    strappy_set_error(error_out, "Could not allocate web request headers.");
    goto cleanup;
  }

  curl = curl_easy_init();
  if (curl == NULL) {
    strappy_set_error(error_out, "Could not create web request handle.");
    goto cleanup;
  }
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, strappy_web_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response_buffer);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, strappy_web_header_callback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)&transfer_context);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, STRAPPY_WEB_USER_AGENT);
  curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
#if LIBCURL_VERSION_NUM >= 0x071506
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
#else
  curl_easy_setopt(curl, CURLOPT_ENCODING, "gzip, deflate");
#endif
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, STRAPPY_WEB_MAX_REDIRECTS);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,
                   STRAPPY_WEB_CONNECT_TIMEOUT_SECONDS);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, STRAPPY_WEB_TIMEOUT_SECONDS);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_COOKIEFILE, cookie_jar_path);
  curl_easy_setopt(curl, CURLOPT_COOKIEJAR, cookie_jar_path);
  curl_easy_setopt(curl, CURLOPT_PROXY, "");
  curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, strappy_web_open_socket);
  curl_easy_setopt(curl, CURLOPT_OPENSOCKETDATA, (void *)&transfer_context);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION,
                   strappy_web_progress_callback);
  curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, (void *)&transfer_context);
#if LIBCURL_VERSION_NUM >= 0x071304
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS,
                   (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
  curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS,
                   (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
  if (post_fields != NULL) {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)post_length);
  }
  if ((strappy_web_cainfo_path != NULL) &&
      (strappy_web_cainfo_path[0] != '\0')) {
    curl_easy_setopt(curl, CURLOPT_CAINFO, strappy_web_cainfo_path);
  }

  if ((continue_callback != NULL) &&
      !continue_callback(continue_callback_data)) {
    transfer_context.cancelled = 1;
    code = CURLE_ABORTED_BY_CALLBACK;
  } else {
    code = curl_easy_perform(curl);
  }
  result->cancelled = (transfer_context.cancelled == 1) ? 1 : 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result->http_status);
  curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type);
  curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective_url);

  if (response_buffer.too_large) {
    strappy_set_formatted_error(error_out,
                                "Web response exceeds %lu bytes.",
                                (unsigned long)maximum_body_bytes);
    goto cleanup;
  }
  if (transfer_context.cancelled == -1) {
    strappy_set_error(error_out,
                      "Web request destination is not a public address.");
    goto cleanup;
  }
  if (transfer_context.invalid_redirect) {
    strappy_set_error(error_out, "Web redirect URL is not allowed.");
    goto cleanup;
  }
  if (code != CURLE_OK) {
    if (result->cancelled) {
      strappy_set_error(error_out, "Web request was cancelled.");
    } else {
      strappy_set_formatted_error(
        error_out,
        "Web request failed: %s",
        (curl_error[0] != '\0') ? curl_error : curl_easy_strerror(code));
    }
    goto cleanup;
  }

  result->body = strappy_string_duplicate(
    (response_buffer.data != NULL) ? response_buffer.data : "");
  result->body_length = response_buffer.length;
  result->content_type = strappy_string_duplicate(
    (content_type != NULL) ? content_type : "");
  result->effective_url = strappy_string_duplicate(
    (effective_url != NULL) ? effective_url : "");
  if ((result->body == NULL) || (result->content_type == NULL) ||
      (result->effective_url == NULL)) {
    strappy_set_error(error_out, "Could not allocate web response.");
    goto cleanup;
  }
  ok = 1;

cleanup:
  if (curl != NULL) {
    curl_easy_cleanup(curl);
  }
  curl_slist_free_all(headers);
  strappy_web_http_buffer_destroy(&response_buffer);
  if (mutex_locked) {
    (void)pthread_mutex_unlock(&strappy_web_request_mutex);
  }
  if (!ok) {
    int was_cancelled;

    was_cancelled = result->cancelled;
    strappy_web_http_result_destroy(result);
    result->cancelled = was_cancelled;
  }
  return ok;
}

static int strappy_web_ascii_equal_length(const char *first,
                                          size_t first_length,
                                          const char *second)
{
  size_t index;
  size_t second_length;

  if ((first == NULL) || (second == NULL)) {
    return 0;
  }
  second_length = strlen(second);
  if (first_length != second_length) {
    return 0;
  }
  for (index = 0U; index < first_length; index++) {
    if (tolower((unsigned char)first[index]) !=
        tolower((unsigned char)second[index])) {
      return 0;
    }
  }
  return 1;
}

static const char *strappy_web_ascii_find(const char *start,
                                          const char *end,
                                          const char *needle)
{
  size_t needle_length;
  const char *cursor;

  if ((start == NULL) || (end == NULL) || (needle == NULL) || (end < start)) {
    return NULL;
  }
  needle_length = strlen(needle);
  if (needle_length == 0U) {
    return start;
  }
  for (cursor = start; cursor < end; cursor++) {
    size_t index;

    if ((size_t)(end - cursor) < needle_length) {
      break;
    }
    for (index = 0U; index < needle_length; index++) {
      if (tolower((unsigned char)cursor[index]) !=
          tolower((unsigned char)needle[index])) {
        break;
      }
    }
    if (index == needle_length) {
      return cursor;
    }
  }
  return NULL;
}

int strappy_web_search_response_is_rate_limited(long status,
                                                const char *body,
                                                size_t body_length)
{
  const char *end;
  int has_challenge_form;
  int has_anomaly_action;
  int has_bot_message;

  if (status == 202L) {
    return 1;
  }
  if (body == NULL) {
    return 0;
  }
  end = body + body_length;
  has_challenge_form =
    strappy_web_ascii_find(body, end, "id=\"challenge-form\"") != NULL;
  has_anomaly_action =
    strappy_web_ascii_find(body, end, "duckduckgo.com/anomaly.js") != NULL;
  has_bot_message = strappy_web_ascii_find(
    body,
    end,
    "Unfortunately, bots use DuckDuckGo too.") != NULL;
  return has_challenge_form && (has_anomaly_action || has_bot_message);
}

static int strappy_web_utf8_is_valid(const char *value, size_t length)
{
  size_t index;

  if ((value == NULL) && (length > 0U)) {
    return 0;
  }
  index = 0U;
  while (index < length) {
    unsigned char first;

    first = (unsigned char)value[index];
    if (first == 0U) {
      return 0;
    }
    if (first <= 0x7fU) {
      index++;
      continue;
    }
    if ((first >= 0xc2U) && (first <= 0xdfU)) {
      if ((index + 1U >= length) ||
          (((unsigned char)value[index + 1U] & 0xc0U) != 0x80U)) {
        return 0;
      }
      index += 2U;
      continue;
    }
    if ((first >= 0xe0U) && (first <= 0xefU)) {
      unsigned char second;
      unsigned char third;

      if (index + 2U >= length) {
        return 0;
      }
      second = (unsigned char)value[index + 1U];
      third = (unsigned char)value[index + 2U];
      if (((third & 0xc0U) != 0x80U) ||
          ((first == 0xe0U) && ((second < 0xa0U) || (second > 0xbfU))) ||
          ((first == 0xedU) && ((second < 0x80U) || (second > 0x9fU))) ||
          (((first != 0xe0U) && (first != 0xedU)) &&
           ((second & 0xc0U) != 0x80U))) {
        return 0;
      }
      index += 3U;
      continue;
    }
    if ((first >= 0xf0U) && (first <= 0xf4U)) {
      unsigned char second;
      unsigned char third;
      unsigned char fourth;

      if (index + 3U >= length) {
        return 0;
      }
      second = (unsigned char)value[index + 1U];
      third = (unsigned char)value[index + 2U];
      fourth = (unsigned char)value[index + 3U];
      if (((third & 0xc0U) != 0x80U) || ((fourth & 0xc0U) != 0x80U) ||
          ((first == 0xf0U) && ((second < 0x90U) || (second > 0xbfU))) ||
          ((first == 0xf4U) && ((second < 0x80U) || (second > 0x8fU))) ||
          (((first != 0xf0U) && (first != 0xf4U)) &&
           ((second & 0xc0U) != 0x80U))) {
        return 0;
      }
      index += 4U;
      continue;
    }
    return 0;
  }
  return 1;
}

static int strappy_web_content_type_is_text(const char *content_type)
{
  const char *cursor;

  if ((content_type == NULL) || (content_type[0] == '\0')) {
    return 0;
  }
  for (cursor = content_type; *cursor != '\0'; cursor++) {
    if ((strlen(cursor) >= 4U) && (strncasecmp(cursor, "text", 4U) == 0)) {
      return 1;
    }
  }
  return 0;
}

static int strappy_web_url_is_public_http_candidate(const char *url,
                                                    char **error_out)
{
  const char *authority;
  const char *authority_end;
  const char *host;
  const char *host_end;
  size_t length;
  size_t host_length;
  const char *cursor;

  if (url == NULL) {
    strappy_set_error(error_out, "Web URL is missing.");
    return 0;
  }
  length = strlen(url);
  if ((length < 8U) || (length > STRAPPY_WEB_MAX_ARGUMENT_BYTES)) {
    strappy_set_error(error_out, "Web URL must contain 8 to 512 bytes.");
    return 0;
  }
  if (strncasecmp(url, "https://", 8U) == 0) {
    authority = url + 8U;
  } else if (strncasecmp(url, "http://", 7U) == 0) {
    authority = url + 7U;
  } else {
    strappy_set_error(error_out, "Web URL must use HTTP or HTTPS.");
    return 0;
  }
  for (cursor = url; *cursor != '\0'; cursor++) {
    if (((unsigned char)*cursor <= 0x20U) ||
        ((unsigned char)*cursor == 0x7fU) || (*cursor == '\\')) {
      strappy_set_error(error_out,
                        "Web URL contains whitespace, control characters, or "
                        "backslashes.");
      return 0;
    }
  }
  authority_end = authority + strcspn(authority, "/?#");
  if ((authority_end == authority) ||
      (memchr(authority, '@', (size_t)(authority_end - authority)) != NULL)) {
    strappy_set_error(error_out,
                      "Web URL host is missing or contains credentials.");
    return 0;
  }
  host = authority;
  if (*host == '[') {
    host++;
    host_end = (const char *)memchr(host,
                                   ']',
                                   (size_t)(authority_end - host));
    if ((host_end == NULL) ||
        ((host_end + 1 < authority_end) && (host_end[1] != ':'))) {
      strappy_set_error(error_out, "Web URL has an invalid IPv6 host.");
      return 0;
    }
  } else {
    host_end = (const char *)memchr(host,
                                   ':',
                                   (size_t)(authority_end - host));
    if (host_end == NULL) {
      host_end = authority_end;
    }
  }
  host_length = (size_t)(host_end - host);
  if ((host_length == 0U) ||
      strappy_web_ascii_equal_length(host, host_length, "localhost") ||
      ((host_length > 10U) &&
       strappy_web_ascii_equal_length(host + host_length - 10U,
                                      10U,
                                      ".localhost")) ||
      ((host_length > 6U) &&
       strappy_web_ascii_equal_length(host + host_length - 6U,
                                      6U,
                                      ".local"))) {
    strappy_set_error(error_out,
                      "Web URL must identify a public Internet host.");
    return 0;
  }
  return 1;
}

static char *strappy_web_cookie_jar_path(const char *session_db_path,
                                         char **error_out)
{
  const char *slash;
  size_t directory_length;
  size_t filename_length;
  char *path;
  int descriptor;

  if ((session_db_path == NULL) || (session_db_path[0] == '\0')) {
    strappy_set_error(error_out, "Session database path is missing.");
    return NULL;
  }
  slash = strrchr(session_db_path, '/');
  directory_length = (slash != NULL) ?
    (size_t)(slash - session_db_path + 1) : 0U;
  filename_length = strlen(STRAPPY_WEB_COOKIE_FILENAME);
  if (directory_length > (size_t)-1 - filename_length - 1U) {
    strappy_set_error(error_out, "Web cookie path is too long.");
    return NULL;
  }
  path = (char *)malloc(directory_length + filename_length + 1U);
  if (path == NULL) {
    strappy_set_error(error_out, "Could not allocate web cookie path.");
    return NULL;
  }
  if (directory_length > 0U) {
    memcpy(path, session_db_path, directory_length);
  }
  memcpy(path + directory_length,
         STRAPPY_WEB_COOKIE_FILENAME,
         filename_length + 1U);
  descriptor = open(path, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
  if (descriptor < 0) {
    strappy_set_formatted_error(error_out,
                                "Could not open web cookie store: %s",
                                strerror(errno));
    free(path);
    return NULL;
  }
  (void)close(descriptor);
  if (chmod(path, S_IRUSR | S_IWUSR) != 0) {
    strappy_set_formatted_error(error_out,
                                "Could not protect web cookie store: %s",
                                strerror(errno));
    free(path);
    return NULL;
  }
  return path;
}

static char *strappy_web_form_encode(const char *value)
{
  static const char hexadecimal[] = "0123456789ABCDEF";
  strappy_web_buffer buffer;
  const unsigned char *cursor;

  buffer.data = NULL;
  buffer.length = 0U;
  if (value == NULL) {
    return NULL;
  }
  for (cursor = (const unsigned char *)value; *cursor != 0U; cursor++) {
    char encoded[3];

    if (isalnum(*cursor) || (*cursor == '-') || (*cursor == '_') ||
        (*cursor == '.') || (*cursor == '*')) {
      encoded[0] = (char)*cursor;
      if (!strappy_web_buffer_append_length(&buffer, encoded, 1U)) {
        strappy_web_buffer_destroy(&buffer);
        return NULL;
      }
    } else if (*cursor == ' ') {
      if (!strappy_web_buffer_append_length(&buffer, "+", 1U)) {
        strappy_web_buffer_destroy(&buffer);
        return NULL;
      }
    } else {
      encoded[0] = '%';
      encoded[1] = hexadecimal[(*cursor >> 4) & 0x0fU];
      encoded[2] = hexadecimal[*cursor & 0x0fU];
      if (!strappy_web_buffer_append_length(&buffer, encoded, 3U)) {
        strappy_web_buffer_destroy(&buffer);
        return NULL;
      }
    }
  }
  if (buffer.data == NULL) {
    buffer.data = strappy_string_duplicate("");
  }
  return buffer.data;
}

static int strappy_web_append_utf8_codepoint(strappy_web_buffer *buffer,
                                             unsigned long codepoint)
{
  char bytes[4];
  size_t length;

  if ((codepoint == 0UL) || (codepoint > 0x10ffffUL) ||
      ((codepoint >= 0xd800UL) && (codepoint <= 0xdfffUL))) {
    return 0;
  }
  if (codepoint <= 0x7fUL) {
    bytes[0] = (char)codepoint;
    length = 1U;
  } else if (codepoint <= 0x7ffUL) {
    bytes[0] = (char)(0xc0UL | (codepoint >> 6));
    bytes[1] = (char)(0x80UL | (codepoint & 0x3fUL));
    length = 2U;
  } else if (codepoint <= 0xffffUL) {
    bytes[0] = (char)(0xe0UL | (codepoint >> 12));
    bytes[1] = (char)(0x80UL | ((codepoint >> 6) & 0x3fUL));
    bytes[2] = (char)(0x80UL | (codepoint & 0x3fUL));
    length = 3U;
  } else {
    bytes[0] = (char)(0xf0UL | (codepoint >> 18));
    bytes[1] = (char)(0x80UL | ((codepoint >> 12) & 0x3fUL));
    bytes[2] = (char)(0x80UL | ((codepoint >> 6) & 0x3fUL));
    bytes[3] = (char)(0x80UL | (codepoint & 0x3fUL));
    length = 4U;
  }
  return strappy_web_buffer_append_length(buffer, bytes, length);
}

static char *strappy_web_html_decode(const char *value, size_t length)
{
  strappy_web_buffer buffer;
  size_t index;

  buffer.data = NULL;
  buffer.length = 0U;
  for (index = 0U; index < length;) {
    if (value[index] == '&') {
      const char *semicolon;
      size_t entity_length;

      semicolon = (const char *)memchr(value + index,
                                      ';',
                                      length - index);
      entity_length = (semicolon != NULL) ?
        (size_t)(semicolon - (value + index)) + 1U : 0U;
      if ((entity_length == 5U) &&
          (memcmp(value + index, "&amp;", 5U) == 0)) {
        if (!strappy_web_buffer_append_length(&buffer, "&", 1U)) {
          goto allocation_failure;
        }
        index += entity_length;
        continue;
      }
      if ((entity_length == 6U) &&
          (memcmp(value + index, "&quot;", 6U) == 0)) {
        if (!strappy_web_buffer_append_length(&buffer, "\"", 1U)) {
          goto allocation_failure;
        }
        index += entity_length;
        continue;
      }
      if ((entity_length == 6U) &&
          (memcmp(value + index, "&apos;", 6U) == 0)) {
        if (!strappy_web_buffer_append_length(&buffer, "'", 1U)) {
          goto allocation_failure;
        }
        index += entity_length;
        continue;
      }
      if ((entity_length == 4U) &&
          (memcmp(value + index, "&lt;", 4U) == 0)) {
        if (!strappy_web_buffer_append_length(&buffer, "<", 1U)) {
          goto allocation_failure;
        }
        index += entity_length;
        continue;
      }
      if ((entity_length == 4U) &&
          (memcmp(value + index, "&gt;", 4U) == 0)) {
        if (!strappy_web_buffer_append_length(&buffer, ">", 1U)) {
          goto allocation_failure;
        }
        index += entity_length;
        continue;
      }
      if ((entity_length >= 4U) && (value[index + 1U] == '#')) {
        int base;
        size_t digit_start;
        size_t digit_end;
        size_t digit_index;
        unsigned long codepoint;
        int valid;

        base = 10;
        digit_start = index + 2U;
        if ((digit_start < length) &&
            ((value[digit_start] == 'x') ||
             (value[digit_start] == 'X'))) {
          base = 16;
          digit_start++;
        }
        digit_end = index + entity_length - 1U;
        codepoint = 0UL;
        valid = (digit_start < digit_end) ? 1 : 0;
        for (digit_index = digit_start;
             valid && (digit_index < digit_end);
             digit_index++) {
          int digit;

          if ((value[digit_index] >= '0') &&
              (value[digit_index] <= '9')) {
            digit = value[digit_index] - '0';
          } else if ((base == 16) && (value[digit_index] >= 'a') &&
                     (value[digit_index] <= 'f')) {
            digit = value[digit_index] - 'a' + 10;
          } else if ((base == 16) && (value[digit_index] >= 'A') &&
                     (value[digit_index] <= 'F')) {
            digit = value[digit_index] - 'A' + 10;
          } else {
            valid = 0;
            break;
          }
          if (codepoint > (0x10ffffUL - (unsigned long)digit) /
                          (unsigned long)base) {
            valid = 0;
            break;
          }
          codepoint = (codepoint * (unsigned long)base) +
            (unsigned long)digit;
        }
        if (valid && strappy_web_append_utf8_codepoint(&buffer, codepoint)) {
          index += entity_length;
          continue;
        }
      }
    }
    if (!strappy_web_buffer_append_length(&buffer, value + index, 1U)) {
      goto allocation_failure;
    }
    index++;
  }
  if (buffer.data == NULL) {
    buffer.data = strappy_string_duplicate("");
  }
  return buffer.data;

allocation_failure:
  strappy_web_buffer_destroy(&buffer);
  return NULL;
}

static char *strappy_web_html_attribute(const char *tag_start,
                                        const char *tag_end,
                                        const char *attribute_name)
{
  const char *cursor;

  if ((tag_start == NULL) || (tag_end == NULL) || (tag_end <= tag_start)) {
    return NULL;
  }
  cursor = tag_start + 1;
  while ((cursor < tag_end) && !isspace((unsigned char)*cursor)) {
    cursor++;
  }
  while (cursor < tag_end) {
    const char *name_start;
    const char *name_end;
    const char *value_start;
    const char *value_end;
    char quote;

    while ((cursor < tag_end) &&
           (isspace((unsigned char)*cursor) || (*cursor == '/'))) {
      cursor++;
    }
    name_start = cursor;
    while ((cursor < tag_end) &&
           !isspace((unsigned char)*cursor) && (*cursor != '=') &&
           (*cursor != '>') && (*cursor != '/')) {
      cursor++;
    }
    name_end = cursor;
    while ((cursor < tag_end) && isspace((unsigned char)*cursor)) {
      cursor++;
    }
    if ((cursor >= tag_end) || (*cursor != '=')) {
      while ((cursor < tag_end) && !isspace((unsigned char)*cursor)) {
        cursor++;
      }
      continue;
    }
    cursor++;
    while ((cursor < tag_end) && isspace((unsigned char)*cursor)) {
      cursor++;
    }
    quote = ((cursor < tag_end) && ((*cursor == '\"') || (*cursor == '\''))) ?
      *cursor++ : '\0';
    value_start = cursor;
    if (quote != '\0') {
      while ((cursor < tag_end) && (*cursor != quote)) {
        cursor++;
      }
    } else {
      while ((cursor < tag_end) && !isspace((unsigned char)*cursor) &&
             (*cursor != '>')) {
        cursor++;
      }
    }
    value_end = cursor;
    if ((quote != '\0') && (cursor < tag_end)) {
      cursor++;
    }
    if (strappy_web_ascii_equal_length(name_start,
                                       (size_t)(name_end - name_start),
                                       attribute_name)) {
      return strappy_web_html_decode(value_start,
                                     (size_t)(value_end - value_start));
    }
  }
  return NULL;
}

static int strappy_web_class_has_token(const char *value, const char *token)
{
  const char *cursor;
  size_t token_length;

  if ((value == NULL) || (token == NULL)) {
    return 0;
  }
  token_length = strlen(token);
  cursor = value;
  while (*cursor != '\0') {
    const char *start;

    while ((*cursor != '\0') && isspace((unsigned char)*cursor)) {
      cursor++;
    }
    start = cursor;
    while ((*cursor != '\0') && !isspace((unsigned char)*cursor)) {
      cursor++;
    }
    if (strappy_web_ascii_equal_length(start,
                                       (size_t)(cursor - start),
                                       token) &&
        ((size_t)(cursor - start) == token_length)) {
      return 1;
    }
  }
  return 0;
}

static char *strappy_web_search_next_post_body(const char *html,
                                               size_t html_length,
                                               char **error_out)
{
  const char *html_end;
  const char *form;

  html_end = html + html_length;
  form = html;
  while ((form = strappy_web_ascii_find(form, html_end, "<form")) != NULL) {
    const char *tag_end;
    const char *form_end;
    char *class_name;

    tag_end = (const char *)memchr(form, '>', (size_t)(html_end - form));
    if (tag_end == NULL) {
      break;
    }
    class_name = strappy_web_html_attribute(form, tag_end, "class");
    if (!strappy_web_class_has_token(class_name, "next_form")) {
      free(class_name);
      form = tag_end + 1;
      continue;
    }
    free(class_name);
    form_end = strappy_web_ascii_find(tag_end + 1, html_end, "</form");
    if (form_end == NULL) {
      break;
    }
    {
      strappy_web_buffer post_body;
      const char *input;
      int field_count;

      post_body.data = NULL;
      post_body.length = 0U;
      input = tag_end + 1;
      field_count = 0;
      while ((input = strappy_web_ascii_find(input,
                                             form_end,
                                             "<input")) != NULL) {
        const char *input_end;
        char *name;
        char *value;
        char *encoded_name;
        char *encoded_value;
        int appended;

        input_end = (const char *)memchr(input,
                                        '>',
                                        (size_t)(form_end - input));
        if (input_end == NULL) {
          break;
        }
        name = strappy_web_html_attribute(input, input_end, "name");
        value = strappy_web_html_attribute(input, input_end, "value");
        encoded_name = ((name != NULL) && (name[0] != '\0')) ?
          strappy_web_form_encode(name) : NULL;
        encoded_value = (value != NULL) ? strappy_web_form_encode(value) :
          strappy_string_duplicate("");
        appended = ((name == NULL) || (name[0] == '\0')) ? 1 : 0;
        if ((name != NULL) && (name[0] != '\0') &&
            (encoded_name != NULL) && (encoded_value != NULL)) {
          appended = ((field_count == 0) ||
                      strappy_web_buffer_append(&post_body, "&")) &&
            strappy_web_buffer_append(&post_body, encoded_name) &&
            strappy_web_buffer_append(&post_body, "=") &&
            strappy_web_buffer_append(&post_body, encoded_value);
          if (appended) {
            field_count++;
          }
        }
        free(name);
        free(value);
        free(encoded_name);
        free(encoded_value);
        if (!appended) {
          strappy_web_buffer_destroy(&post_body);
          strappy_set_error(error_out,
                            "Could not allocate web search pagination data.");
          return NULL;
        }
        input = input_end + 1;
      }
      if (field_count > 0) {
        return post_body.data;
      }
      strappy_web_buffer_destroy(&post_body);
    }
    break;
  }
  strappy_set_error(error_out, "The requested web search page is unavailable.");
  return NULL;
}

static int strappy_web_validate_response(const strappy_web_http_result *result,
                                         char **error_out)
{
  if ((result == NULL) ||
      !strappy_web_content_type_is_text(result->content_type) ||
      !strappy_web_utf8_is_valid(result->content_type,
                                 strlen(result->content_type))) {
    strappy_set_error(error_out,
                      "Web response Content-Type is missing or is not valid "
                      "UTF-8 text.");
    return 0;
  }
  if ((result->effective_url == NULL) ||
      !strappy_web_url_is_public_http_candidate(result->effective_url,
                                                error_out)) {
    return 0;
  }
  if ((result->body == NULL) ||
      (strlen(result->body) != result->body_length) ||
      !strappy_web_utf8_is_valid(result->body, result->body_length)) {
    strappy_set_error(error_out,
                      "Web response body is not valid UTF-8 text.");
    return 0;
  }
  return 1;
}

static void strappy_web_result_take_http(strappy_web_result *result,
                                         strappy_web_http_result *http_result)
{
  result->body = http_result->body;
  result->content_type = http_result->content_type;
  result->body_length = http_result->body_length;
  result->status = http_result->http_status;
  result->cancelled = http_result->cancelled;
  http_result->body = NULL;
  http_result->content_type = NULL;
  http_result->body_length = 0U;
}

int strappy_web_search(
  const char *session_db_path,
  const char *query,
  int page,
  strappy_web_continue_callback continue_callback,
  void *continue_callback_data,
  strappy_web_result *result,
  char **error_out)
{
  int current_page;
  char *encoded_query;
  strappy_web_buffer initial_body;
  char *post_body;
  char *cookie_path;
  int succeeded;

  if (result == NULL) {
    strappy_set_error(error_out, "Web search result is missing.");
    return 0;
  }
  strappy_web_result_init(result);
  if ((query == NULL) || (query[0] == '\0') ||
      (strlen(query) > STRAPPY_WEB_MAX_ARGUMENT_BYTES)) {
    strappy_set_error(error_out, "Web search query must contain 1 to 512 bytes.");
    return 0;
  }
  if ((page < 1) || (page > STRAPPY_WEB_MAX_SEARCH_PAGE)) {
    strappy_set_error(error_out, "Web search page must be from 1 through 10.");
    return 0;
  }
  encoded_query = strappy_web_form_encode(query);
  initial_body.data = NULL;
  initial_body.length = 0U;
  if ((encoded_query == NULL) ||
      !strappy_web_buffer_append(&initial_body, "q=") ||
      !strappy_web_buffer_append(&initial_body, encoded_query) ||
      !strappy_web_buffer_append(&initial_body, "&kl=&df=")) {
    free(encoded_query);
    strappy_web_buffer_destroy(&initial_body);
    strappy_set_error(error_out, "Could not allocate web_search request.");
    return 0;
  }
  free(encoded_query);
  post_body = initial_body.data;
  initial_body.data = NULL;
  initial_body.length = 0U;
  cookie_path = strappy_web_cookie_jar_path(session_db_path, error_out);
  if (cookie_path == NULL) {
    free(post_body);
    return 0;
  }
  succeeded = 0;
  for (current_page = 1; current_page <= page; current_page++) {
    strappy_web_http_result http_result;
    char *next_body;
    int ok;

    strappy_web_http_result_init(&http_result);
    ok = strappy_web_send_public_request(
      STRAPPY_WEB_SEARCH_URL,
      post_body,
      cookie_path,
      STRAPPY_WEB_MAX_RESPONSE_BYTES,
      continue_callback,
      continue_callback_data,
      &http_result,
      error_out);
    free(post_body);
    post_body = NULL;
    if (!ok) {
      result->cancelled = http_result.cancelled;
      strappy_web_http_result_destroy(&http_result);
      break;
    }
    if (!strappy_web_validate_response(&http_result, error_out)) {
      strappy_web_http_result_destroy(&http_result);
      break;
    }
    if (strappy_web_search_response_is_rate_limited(
          http_result.http_status,
          http_result.body,
          http_result.body_length)) {
      strappy_set_error(error_out, STRAPPY_WEB_SEARCH_RATE_LIMIT_ERROR);
      strappy_web_http_result_destroy(&http_result);
      break;
    }
    if (current_page == page) {
      strappy_web_result_take_http(result, &http_result);
      strappy_web_http_result_destroy(&http_result);
      succeeded = 1;
      break;
    }
    next_body = strappy_web_search_next_post_body(http_result.body,
                                                  http_result.body_length,
                                                  error_out);
    strappy_web_http_result_destroy(&http_result);
    if (next_body == NULL) {
      break;
    }
    post_body = next_body;
  }
  free(post_body);
  free(cookie_path);
  return succeeded;
}

int strappy_web_fetch(
  const char *session_db_path,
  const char *url,
  strappy_web_continue_callback continue_callback,
  void *continue_callback_data,
  strappy_web_result *result,
  char **error_out)
{
  char *cookie_path;
  strappy_web_http_result http_result;

  if (result == NULL) {
    strappy_set_error(error_out, "Web fetch result is missing.");
    return 0;
  }
  strappy_web_result_init(result);
  if (!strappy_web_url_is_public_http_candidate(url, error_out)) {
    return 0;
  }
  cookie_path = strappy_web_cookie_jar_path(session_db_path, error_out);
  if (cookie_path == NULL) {
    return 0;
  }
  strappy_web_http_result_init(&http_result);
  if (!strappy_web_send_public_request(
        url,
        NULL,
        cookie_path,
        STRAPPY_WEB_MAX_RESPONSE_BYTES,
        continue_callback,
        continue_callback_data,
        &http_result,
        error_out)) {
    result->cancelled = http_result.cancelled;
    strappy_web_http_result_destroy(&http_result);
    free(cookie_path);
    return 0;
  }
  free(cookie_path);
  if (!strappy_web_validate_response(&http_result, error_out)) {
    strappy_web_http_result_destroy(&http_result);
    return 0;
  }
  strappy_web_result_take_http(result, &http_result);
  strappy_web_http_result_destroy(&http_result);
  return 1;
}
