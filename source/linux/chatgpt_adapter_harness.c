#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../shared/strappy_client.h"
#include "../shared/strappy_core.h"
#include "../shared/strappy_sse.h"

#define CHATGPT_HARNESS_MAX_REQUEST_BYTES (64U * 1024U)

static int chatgpt_harness_send_all(int socket_fd,
                                    const char *data,
                                    size_t length)
{
  size_t sent;

  sent = 0U;
  while (sent < length) {
    ssize_t written;

    written = send(socket_fd, data + sent, length - sent, 0);
    if (written <= 0) {
      return 0;
    }
    sent += (size_t)written;
  }
  return 1;
}

static int chatgpt_harness_content_length(const char *headers,
                                          size_t headers_length,
                                          size_t *length_out)
{
  static const char field_name[] = "Content-Length:";
  const char *cursor;
  const char *headers_end;

  cursor = headers;
  headers_end = headers + headers_length;
  while (cursor < headers_end) {
    const char *line_end;
    size_t line_length;

    line_end = strstr(cursor, "\r\n");
    if ((line_end == NULL) || (line_end > headers_end)) {
      line_end = headers_end;
    }
    line_length = (size_t)(line_end - cursor);
    if ((line_length > (sizeof(field_name) - 1U)) &&
        (strncasecmp(cursor,
                     field_name,
                     sizeof(field_name) - 1U) == 0)) {
      const char *value;
      char *parse_end;
      unsigned long parsed;

      value = cursor + sizeof(field_name) - 1U;
      errno = 0;
      parsed = strtoul(value, &parse_end, 10);
      if ((errno != 0) || (parse_end == value) ||
          (parsed > (unsigned long)CHATGPT_HARNESS_MAX_REQUEST_BYTES)) {
        return 0;
      }
      *length_out = (size_t)parsed;
      return 1;
    }
    if (line_end == headers_end) {
      break;
    }
    cursor = line_end + 2;
  }
  return 0;
}

static char *chatgpt_harness_read_request(int socket_fd)
{
  char *request;
  size_t length;
  size_t headers_length;
  size_t body_length;

  request = (char *)malloc(CHATGPT_HARNESS_MAX_REQUEST_BYTES + 1U);
  if (request == NULL) {
    return NULL;
  }
  length = 0U;
  headers_length = 0U;
  body_length = 0U;
  while (length < CHATGPT_HARNESS_MAX_REQUEST_BYTES) {
    ssize_t received;

    received = recv(socket_fd,
                    request + length,
                    CHATGPT_HARNESS_MAX_REQUEST_BYTES - length,
                    0);
    if (received <= 0) {
      free(request);
      return NULL;
    }
    length += (size_t)received;
    request[length] = '\0';
    if (headers_length == 0U) {
      char *separator;

      separator = strstr(request, "\r\n\r\n");
      if (separator != NULL) {
        headers_length = (size_t)(separator - request) + 4U;
        if (!chatgpt_harness_content_length(request,
                                            headers_length,
                                            &body_length) ||
            (body_length >
             (CHATGPT_HARNESS_MAX_REQUEST_BYTES - headers_length))) {
          free(request);
          return NULL;
        }
      }
    }
    if ((headers_length > 0U) &&
        (length >= (headers_length + body_length))) {
      request[headers_length + body_length] = '\0';
      return request;
    }
  }
  free(request);
  return NULL;
}

static int chatgpt_harness_open_listener(unsigned short *port_out)
{
  struct sockaddr_in address;
  socklen_t address_length;
  int listener_fd;
  int reuse;

  listener_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listener_fd < 0) {
    return -1;
  }
  reuse = 1;
  (void)setsockopt(listener_fd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &reuse,
                   sizeof(reuse));
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(0U);
  if ((bind(listener_fd,
            (const struct sockaddr *)&address,
            sizeof(address)) != 0) ||
      (listen(listener_fd, 1) != 0)) {
    close(listener_fd);
    return -1;
  }
  address_length = (socklen_t)sizeof(address);
  if (getsockname(listener_fd,
                  (struct sockaddr *)&address,
                  &address_length) != 0) {
    close(listener_fd);
    return -1;
  }
  *port_out = ntohs(address.sin_port);
  return listener_fd;
}

static int chatgpt_harness_request_is_valid(const char *request,
                                            int uses_sse)
{
  static const char streaming_request_json[] =
    "{\"model\":\"gpt-5.6-luna\",\"stream\":true}";
  static const char json_request_json[] =
    "{\"model\":\"gpt-5.6-luna\",\"stream\":false}";
  const char *request_json;

  request_json = uses_sse ? streaming_request_json : json_request_json;

  return (request != NULL) &&
    (strncmp(request, "POST /responses HTTP/", 21U) == 0) &&
    (strstr(request, "Content-Type: application/json") != NULL) &&
    (strstr(request,
            uses_sse ? "Accept: text/event-stream" :
              "Accept: application/json") != NULL) &&
    (strstr(request,
            "Authorization: Bearer access-test") != NULL) &&
    (strstr(request, "OpenAI-Beta: responses=experimental") != NULL) &&
    (strstr(request, "originator: strappy") != NULL) &&
    (strstr(request, "chatgpt-account-id: account-test") != NULL) &&
    (strstr(request, "session-id: request-test") != NULL) &&
    (strstr(request, "x-client-request-id: request-test") != NULL) &&
    (strstr(request, "X-OpenRouter-") == NULL) &&
    (strstr(request, request_json) != NULL);
}

static int chatgpt_harness_serve_response(int listener_fd, int uses_sse)
{
  static const char streaming_body[] =
    ": keepalive\r\n"
    "event: response.output_text.delta\r\n"
    "data: {\"type\":\"response.output_text.delta\","
      "\"delta\":\"hello\"}\r\n\r\n"
    "data: {\"type\":\"response.output_item.done\","
      "\"output_index\":0,\"item\":{\"type\":\"message\","
      "\"id\":\"msg-test\",\"role\":\"assistant\","
      "\"status\":\"completed\",\"content\":[{"
      "\"type\":\"output_text\",\"text\":\"hello\"}]}}\r\n\r\n"
    "data: {\"type\":\"response.completed\",\"response\":{"
      "\"id\":\"resp-test\",\"status\":\"completed\","
      "\"output\":[]}}\r\n\r\n"
    "data: this trailing event must not be parsed\r\n\r\n";
  static const char json_body[] =
    "{\"id\":\"resp-test\",\"status\":\"completed\",\"output\":[]}";
  const char *body;
  const char *content_type;
  char headers[512];
  char *request;
  int client_fd;
  int written;
  int ok;

  client_fd = accept(listener_fd, NULL, NULL);
  if (client_fd < 0) {
    return 0;
  }
  body = uses_sse ? streaming_body : json_body;
  content_type = uses_sse ? "text/event-stream" : "application/json";
  request = chatgpt_harness_read_request(client_fd);
  ok = chatgpt_harness_request_is_valid(request, uses_sse);
  free(request);
  written = snprintf(headers,
                     sizeof(headers),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: %s\r\n"
                     "X-Request-Id: req-server\r\n"
                     "Content-Length: %lu\r\n"
                     "Connection: close\r\n\r\n",
                     content_type,
                     (unsigned long)strlen(body));
  if (ok && (written > 0) && ((size_t)written < sizeof(headers))) {
    ok = chatgpt_harness_send_all(client_fd,
                                  headers,
                                  (size_t)written) &&
      chatgpt_harness_send_all(client_fd, body, strlen(body));
  } else {
    ok = 0;
  }
  close(client_fd);
  return ok;
}

static void chatgpt_harness_stop_server(pid_t pid)
{
  int status;

  (void)kill(pid, SIGTERM);
  while ((waitpid(pid, &status, 0) < 0) && (errno == EINTR)) {
  }
}

static int chatgpt_harness_feed_chunks(strappy_sse_parser *parser,
                                       const char *stream,
                                       char **error_out)
{
  static const size_t chunks[] = {1U, 2U, 7U, 3U, 19U, 5U};
  size_t length;
  size_t offset;
  size_t chunk_index;

  length = strlen(stream);
  offset = 0U;
  chunk_index = 0U;
  while (offset < length) {
    size_t chunk_length;

    chunk_length = chunks[chunk_index %
      (sizeof(chunks) / sizeof(chunks[0]))];
    if (chunk_length > (length - offset)) {
      chunk_length = length - offset;
    }
    if (!strappy_sse_parser_feed(parser,
                                 stream + offset,
                                 chunk_length,
                                 error_out)) {
      return 0;
    }
    offset += chunk_length;
    chunk_index++;
  }
  return 1;
}

static int chatgpt_harness_run_parser_matrix(void)
{
  static const char *terminal_types[] = {
    "response.completed",
    "response.done",
    "response.incomplete",
    "response.failed"
  };
  size_t index;

  for (index = 0U;
       index < (sizeof(terminal_types) / sizeof(terminal_types[0]));
       index++) {
    char stream[512];
    strappy_sse_parser parser;
    char *terminal_json;
    char *error;
    int written;
    int ok;

    written = snprintf(stream,
                       sizeof(stream),
                       ": comment\r\n"
                       "event: ignored\r\n"
                       "data: {\"type\":\"response.created\"}\r\n\r\n"
                       "data: {\"type\":\"%s\",\"response\":{"
                       "\"id\":\"fixture\",\"status\":\"terminal\"}}"
                       "\r\n\r\n",
                       terminal_types[index]);
    if ((written < 0) || ((size_t)written >= sizeof(stream))) {
      return 0;
    }
    strappy_sse_parser_init(&parser);
    error = NULL;
    ok = chatgpt_harness_feed_chunks(&parser, stream, &error) &&
      strappy_sse_parser_is_terminal(&parser) &&
      strappy_sse_parser_finish(&parser, &error);
    terminal_json = strappy_sse_parser_take_terminal_json(&parser);
    ok = ok && (terminal_json != NULL) &&
      (strcmp(terminal_json,
              "{\"id\":\"fixture\",\"status\":\"terminal\"}") == 0);
    free(terminal_json);
    strappy_free_string(error);
    strappy_sse_parser_destroy(&parser);
    if (!ok) {
      fprintf(stderr,
              "SSE terminal fixture failed for %s.\n",
              terminal_types[index]);
      return 0;
    }
  }

  {
    static const char stream[] =
      "data: {\"type\":\"response.output_text.delta\","
        "\"delta\":\"One \\ue200cite\"}\n\n"
      "data: {\"type\":\"response.output_text.delta\","
        "\"delta\":\"\\ue202\"}\n\n"
      "data: {\"type\":\"response.output_text.delta\","
        "\"delta\":\"turn0search0\"}\n\n"
      "data: {\"type\":\"response.output_text.delta\","
        "\"delta\":\"\\ue201.\"}\n\n"
      "data: {\"type\":\"response.output_item.done\","
        "\"output_index\":0,\"item\":{\"type\":\"message\","
        "\"id\":\"msg-citation\",\"role\":\"assistant\","
        "\"status\":\"completed\",\"content\":[{"
        "\"type\":\"output_text\","
        "\"text\":\"One \\ue200cite\\ue202turn0search0\\ue201.\","
        "\"annotations\":[]}]}}\n\n"
      "data: {\"type\":\"response.completed\",\"response\":{"
        "\"id\":\"resp-citation\",\"status\":\"completed\","
        "\"output\":[]}}\n\n";
    strappy_sse_parser parser;
    char *terminal_json;
    char *error;
    int ok;

    strappy_sse_parser_init(&parser);
    error = NULL;
    ok = chatgpt_harness_feed_chunks(&parser, stream, &error) &&
      strappy_sse_parser_finish(&parser, &error);
    terminal_json = strappy_sse_parser_take_terminal_json(&parser);
    ok = ok && (terminal_json != NULL) &&
      (strstr(terminal_json,
              "\"text\":\"One **`[Citation Missing]`**.\"") !=
       NULL) &&
      (strstr(terminal_json, "\\ue200cite") == NULL) &&
      (strstr(terminal_json, "\356\210\200cite") == NULL);
    free(terminal_json);
    strappy_free_string(error);
    strappy_sse_parser_destroy(&parser);
    if (!ok) {
      fprintf(stderr, "Missing citation fallback fixture failed.\n");
      return 0;
    }
  }

  if (!strappy_provider_response_is_plan_limit(
        STRAPPY_PROVIDER_KIND_OPENAI_CHATGPT,
        429L,
        "rate_limit_exceeded",
        NULL) ||
      !strappy_provider_response_is_plan_limit(
        STRAPPY_PROVIDER_KIND_OPENAI_CHATGPT,
        400L,
        "usage_not_included",
        NULL) ||
      !strappy_provider_response_is_plan_limit(
        STRAPPY_PROVIDER_KIND_OPENAI_CHATGPT,
        400L,
        NULL,
        "Monthly usage limit reached") ||
      strappy_provider_response_is_plan_limit(
        STRAPPY_PROVIDER_KIND_OPENROUTER,
        429L,
        "rate_limit_exceeded",
        NULL) ||
      strappy_provider_response_is_plan_limit(
        STRAPPY_PROVIDER_KIND_OPENAI_CHATGPT,
        503L,
        "provider_overloaded",
        NULL)) {
    fprintf(stderr, "ChatGPT plan-limit classification failed.\n");
    return 0;
  }

  {
    strappy_sse_parser parser;
    char *terminal_json;
    char *error;
    int ok;

    strappy_sse_parser_init(&parser);
    error = NULL;
    ok = chatgpt_harness_feed_chunks(
      &parser,
      "data: {\"type\":\"error\",\"error\":{\"code\":\"quota\"}}\n\n",
      &error) && strappy_sse_parser_finish(&parser, &error);
    terminal_json = strappy_sse_parser_take_terminal_json(&parser);
    ok = ok && (terminal_json != NULL) &&
      (strstr(terminal_json, "\"code\":\"quota\"") != NULL);
    free(terminal_json);
    strappy_free_string(error);
    strappy_sse_parser_destroy(&parser);
    if (!ok) {
      fprintf(stderr, "SSE error terminal fixture failed.\n");
      return 0;
    }
  }

  {
    strappy_sse_parser parser;
    char *error;
    int ok;

    strappy_sse_parser_init(&parser);
    error = NULL;
    ok = !chatgpt_harness_feed_chunks(
      &parser,
      "data: not-json\n\n",
      &error) && (error != NULL) &&
      (strstr(error, "valid JSON") != NULL);
    strappy_free_string(error);
    strappy_sse_parser_destroy(&parser);
    if (!ok) {
      fprintf(stderr, "Malformed SSE fixture was not rejected.\n");
      return 0;
    }
  }

  {
    strappy_sse_parser parser;
    char *error;
    int ok;

    strappy_sse_parser_init(&parser);
    error = NULL;
    ok = chatgpt_harness_feed_chunks(
      &parser,
      "data: {\"type\":\"response.created\"}\n\n",
      &error) && !strappy_sse_parser_finish(&parser, &error) &&
      (error != NULL) && (strstr(error, "terminal response") != NULL);
    strappy_free_string(error);
    strappy_sse_parser_destroy(&parser);
    if (!ok) {
      fprintf(stderr, "Truncated SSE fixture was not rejected.\n");
      return 0;
    }
  }
  return 1;
}

static int chatgpt_harness_run_transport_fixture(int uses_sse)
{
  static const char streaming_request_json[] =
    "{\"model\":\"gpt-5.6-luna\",\"stream\":true}";
  static const char json_request_json[] =
    "{\"model\":\"gpt-5.6-luna\",\"stream\":false}";
  const char *request_json;
  unsigned short port;
  int listener_fd;
  pid_t server_pid;
  char endpoint[256];
  int endpoint_length;
  strappy_config config;
  strappy_responses_http_result result;
  char *error;
  int status;
  int ok;

  request_json = uses_sse ? streaming_request_json : json_request_json;
  listener_fd = chatgpt_harness_open_listener(&port);
  if (listener_fd < 0) {
    fprintf(stderr, "Could not open ChatGPT adapter listener.\n");
    return 0;
  }
  server_pid = fork();
  if (server_pid < 0) {
    close(listener_fd);
    fprintf(stderr, "Could not fork ChatGPT adapter server.\n");
    return 0;
  }
  if (server_pid == 0) {
    int server_ok;

    (void)alarm(10U);
    server_ok = chatgpt_harness_serve_response(listener_fd, uses_sse);
    close(listener_fd);
    _exit(server_ok ? 0 : 1);
  }
  close(listener_fd);

  endpoint_length = snprintf(endpoint,
                             sizeof(endpoint),
                             "http://127.0.0.1:%u",
                             (unsigned int)port);
  if ((endpoint_length < 0) ||
      ((size_t)endpoint_length >= sizeof(endpoint))) {
    chatgpt_harness_stop_server(server_pid);
    return 0;
  }
  strappy_config_init(&config);
  config.api_endpoint = strappy_string_duplicate(endpoint);
  strappy_responses_http_result_init(&result);
  error = NULL;
  if (uses_sse) {
    ok = (config.api_endpoint != NULL) &&
      strappy_client_send_provider_responses_json(
        &config,
        STRAPPY_PROVIDER_KIND_OPENAI_CHATGPT,
        "access-test",
        "account-test",
        "request-test",
        request_json,
        &result,
        NULL,
        NULL,
        &error);
  } else {
    ok = (config.api_endpoint != NULL) &&
      strappy_client_send_provider_responses_json_with_transport(
        &config,
        STRAPPY_PROVIDER_KIND_OPENAI_CHATGPT,
        STRAPPY_RESPONSES_RESPONSE_TRANSPORT_JSON,
        "access-test",
        "account-test",
        "request-test",
        request_json,
        &result,
        NULL,
        NULL,
        &error);
  }
  ok = ok &&
    (result.http_status == 200L) &&
    (result.curl_code == 0L) &&
    (result.transport_error == NULL) &&
    (result.response_event_received == (uses_sse ? 1 : 0)) &&
    (result.request_id != NULL) &&
    (strcmp(result.request_id, "req-server") == 0) &&
    (result.response_json != NULL) &&
    ((uses_sse &&
      (strstr(result.response_json, "\"type\":\"message\"") != NULL) &&
      (strstr(result.response_json, "\"text\":\"hello\"") != NULL)) ||
     (!uses_sse &&
      (strcmp(result.response_json,
              "{\"id\":\"resp-test\",\"status\":\"completed\","
              "\"output\":[]}") == 0)));
  while ((waitpid(server_pid, &status, 0) < 0) && (errno == EINTR)) {
  }
  ok = ok && WIFEXITED(status) && (WEXITSTATUS(status) == 0);
  if (!ok) {
    fprintf(stderr,
            "ChatGPT %s adapter fixture failed: %s\n",
            uses_sse ? "SSE" : "JSON",
            (error != NULL) ? error :
              ((result.transport_error != NULL) ? result.transport_error :
                "invalid request or response"));
  }
  strappy_free_string(error);
  strappy_responses_http_result_destroy(&result);
  strappy_config_destroy(&config);
  return ok;
}

int main(void)
{
  (void)signal(SIGPIPE, SIG_IGN);
  if (!chatgpt_harness_run_parser_matrix() ||
      !chatgpt_harness_run_transport_fixture(1) ||
      !chatgpt_harness_run_transport_fixture(0)) {
    return 1;
  }
  printf("chatgpt_adapter_harness: PASS\n");
  return 0;
}
