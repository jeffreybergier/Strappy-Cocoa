#include "strappy_client_transport.h"

#include "strappy_core.h"

#include <emscripten/emscripten.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

extern int strappy_client_fetch_execute(const char *url,
                                        const char *method,
                                        const char *headers,
                                        const void *body,
                                        size_t body_length,
                                        long timeout_seconds);
extern long strappy_client_fetch_http_status(void);
extern long strappy_client_fetch_transport_code(void);
extern int strappy_client_fetch_cancelled(void);
extern double strappy_client_fetch_total_seconds(void);
extern size_t strappy_client_fetch_value_length(int value_kind);
extern int strappy_client_fetch_copy_value(int value_kind,
                                           void *target,
                                           size_t capacity);
extern void strappy_client_fetch_cancel(void);

enum {
  STRAPPY_FETCH_VALUE_BODY = 1,
  STRAPPY_FETCH_VALUE_HEADERS = 2,
  STRAPPY_FETCH_VALUE_EFFECTIVE_URL = 3,
  STRAPPY_FETCH_VALUE_CONTENT_TYPE = 4,
  STRAPPY_FETCH_VALUE_ERROR = 5
};

static char *strappy_client_web_copy_fetch_value(int value_kind,
                                                 char **error_out)
{
  size_t length;
  char *copy;

  length = strappy_client_fetch_value_length(value_kind);
  if (length > (size_t)INT_MAX) {
    strappy_set_error(error_out, "Browser HTTP result is too large.");
    return NULL;
  }
  copy = (char *)malloc(length + 1U);
  if (copy == NULL) {
    strappy_set_error(error_out, "Could not allocate browser HTTP result.");
    return NULL;
  }
  if (!strappy_client_fetch_copy_value(value_kind, copy, length + 1U)) {
    free(copy);
    strappy_set_error(error_out, "Could not copy browser HTTP result.");
    return NULL;
  }
  copy[length] = '\0';
  return copy;
}

static char *strappy_client_web_build_header_block(
  const strappy_client_transport_request *request,
  char **error_out)
{
  size_t index;
  size_t length;
  size_t offset;
  char *block;

  length = 0U;
  for (index = 0U; index < request->header_count; index++) {
    size_t item_length;

    item_length = strlen(request->headers[index]);
    if (length > (((size_t)-1) - item_length - 2U)) {
      strappy_set_error(error_out, "Browser HTTP headers are too large.");
      return NULL;
    }
    length += item_length + 2U;
  }
  block = (char *)malloc(length + 1U);
  if (block == NULL) {
    strappy_set_error(error_out, "Could not allocate browser HTTP headers.");
    return NULL;
  }
  offset = 0U;
  for (index = 0U; index < request->header_count; index++) {
    size_t item_length;

    item_length = strlen(request->headers[index]);
    memcpy(block + offset, request->headers[index], item_length);
    offset += item_length;
    block[offset++] = '\r';
    block[offset++] = '\n';
  }
  block[offset] = '\0';
  return block;
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
  (void)path;
  strappy_set_error(error_out,
                    "Browser Fetch does not support a CA certificate path.");
  return 0;
}

int strappy_client_transport_supports_request_header(const char *name)
{
  if (name == NULL) {
    return 0;
  }
  return (strcasecmp(name, "X-OpenRouter-Metadata") != 0) &&
    (strcasecmp(name, "User-Agent") != 0);
}

char *strappy_client_transport_copy_user_agent(char **error_out)
{
  char *user_agent;

  user_agent = strappy_string_duplicate("");
  if (user_agent == NULL) {
    strappy_set_error(error_out, "Could not allocate browser user agent.");
  }
  return user_agent;
}

int strappy_client_transport_execute(
  const strappy_client_transport_request *request,
  strappy_client_transport_result *result,
  char **error_out)
{
  char *header_block;
  char *body;
  char *raw_headers;
  size_t body_length;
  size_t header_length;
  int fetch_ok;
  int ok;

  if ((request == NULL) || (result == NULL) || (request->url == NULL) ||
      (request->method == NULL)) {
    strappy_set_error(error_out, "Browser HTTP request is not configured.");
    return 0;
  }
  strappy_client_transport_result_init(result);
  header_block = strappy_client_web_build_header_block(request, error_out);
  if (header_block == NULL) {
    return 0;
  }
  fetch_ok = strappy_client_fetch_execute(request->url,
                                          request->method,
                                          header_block,
                                          request->body,
                                          request->body_length,
                                          request->timeout_seconds);
  free(header_block);
  if (!fetch_ok) {
    strappy_set_error(error_out, "Could not execute browser Fetch request.");
    return 0;
  }

  result->http_status = strappy_client_fetch_http_status();
  result->transport_code = strappy_client_fetch_transport_code();
  result->cancelled = strappy_client_fetch_cancelled();
  result->total_seconds = strappy_client_fetch_total_seconds();
  result->effective_url = strappy_client_web_copy_fetch_value(
    STRAPPY_FETCH_VALUE_EFFECTIVE_URL, error_out);
  result->content_type = strappy_client_web_copy_fetch_value(
    STRAPPY_FETCH_VALUE_CONTENT_TYPE, error_out);
  result->error_message = strappy_client_web_copy_fetch_value(
    STRAPPY_FETCH_VALUE_ERROR, error_out);
  body = strappy_client_web_copy_fetch_value(
    STRAPPY_FETCH_VALUE_BODY, error_out);
  raw_headers = strappy_client_web_copy_fetch_value(
    STRAPPY_FETCH_VALUE_HEADERS, error_out);
  ok = (result->effective_url != NULL) && (result->content_type != NULL) &&
    (result->error_message != NULL) && (body != NULL) &&
    (raw_headers != NULL);
  if (!ok) {
    free(body);
    free(raw_headers);
    strappy_client_transport_result_destroy(result);
    return 0;
  }
  if (result->error_message[0] == '\0') {
    free(result->error_message);
    result->error_message = NULL;
  }
  body_length = strlen(body);
  header_length = strlen(raw_headers);
  if ((request->write_header != NULL) && (header_length > 0U) &&
      (request->write_header(raw_headers, header_length,
                             request->write_header_context) != header_length)) {
    ok = 0;
  }
  if (ok && (request->write_body != NULL) && (body_length > 0U) &&
      (request->write_body(body, body_length,
                           request->write_body_context) != body_length) &&
      ((request->write_is_complete == NULL) ||
       !request->write_is_complete(request->write_complete_context))) {
    ok = 0;
  }
  free(body);
  free(raw_headers);
  if (!ok) {
    strappy_client_transport_result_destroy(result);
    strappy_set_error(error_out,
                      "Browser HTTP response exceeded shared client limits.");
    return 0;
  }
  return 1;
}

EMSCRIPTEN_KEEPALIVE
void strappy_client_web_cancel_active_request(void)
{
  strappy_client_fetch_cancel();
}
