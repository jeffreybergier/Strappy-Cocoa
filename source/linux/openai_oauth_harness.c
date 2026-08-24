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
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../shared/strappy_core.h"
#include "../shared/strappy_openai_oauth.h"

#define OAUTH_HARNESS_MAX_REQUEST_BYTES (64U * 1024U)

static int oauth_harness_send_all(int socket_fd,
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

static int oauth_harness_content_length(const char *headers,
                                        size_t headers_length,
                                        size_t *length_out)
{
  static const char field_name[] = "Content-Length:";
  const char *cursor;
  const char *headers_end;

  if ((headers == NULL) || (length_out == NULL)) {
    return 0;
  }
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
          (parsed > (unsigned long)OAUTH_HARNESS_MAX_REQUEST_BYTES)) {
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

static char *oauth_harness_read_request(int socket_fd)
{
  char *request;
  size_t length;
  size_t headers_length;
  size_t body_length;

  request = (char *)malloc(OAUTH_HARNESS_MAX_REQUEST_BYTES + 1U);
  if (request == NULL) {
    return NULL;
  }
  length = 0U;
  headers_length = 0U;
  body_length = 0U;
  while (length < OAUTH_HARNESS_MAX_REQUEST_BYTES) {
    ssize_t received;

    received = recv(socket_fd,
                    request + length,
                    OAUTH_HARNESS_MAX_REQUEST_BYTES - length,
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
        if (!oauth_harness_content_length(request,
                                          headers_length,
                                          &body_length) ||
            (body_length >
             (OAUTH_HARNESS_MAX_REQUEST_BYTES - headers_length))) {
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

static int oauth_harness_send_json(int socket_fd,
                                   long status,
                                   const char *json)
{
  char headers[512];
  const char *reason;
  int written;

  reason = (status == 200L) ? "OK" : "Forbidden";
  written = snprintf(headers,
                     sizeof(headers),
                     "HTTP/1.1 %ld %s\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: %lu\r\n"
                     "Connection: close\r\n\r\n",
                     status,
                     reason,
                     (unsigned long)strlen(json));
  if ((written < 0) || ((size_t)written >= sizeof(headers))) {
    return 0;
  }
  return oauth_harness_send_all(socket_fd, headers, (size_t)written) &&
    oauth_harness_send_all(socket_fd, json, strlen(json));
}

static int oauth_harness_request_is_valid(const char *request,
                                          const char *path,
                                          const char *content_type,
                                          const char *body_fragment)
{
  char request_line[256];
  int written;

  if ((request == NULL) || (path == NULL) || (content_type == NULL) ||
      (body_fragment == NULL)) {
    return 0;
  }
  written = snprintf(request_line,
                     sizeof(request_line),
                     "POST %s HTTP/",
                     path);
  return (written > 0) && ((size_t)written < sizeof(request_line)) &&
    (strncmp(request, request_line, (size_t)written) == 0) &&
    (strstr(request, content_type) != NULL) &&
    (strstr(request, "User-Agent: Strappy/") != NULL) &&
    (strstr(request, body_fragment) != NULL);
}

static int oauth_harness_serve_one(int listener_fd,
                                   const char *path,
                                   const char *content_type,
                                   const char *body_fragment,
                                   long status,
                                   const char *response)
{
  int client_fd;
  char *request;
  int ok;

  client_fd = accept(listener_fd, NULL, NULL);
  if (client_fd < 0) {
    return 0;
  }
  request = oauth_harness_read_request(client_fd);
  ok = oauth_harness_request_is_valid(request,
                                      path,
                                      content_type,
                                      body_fragment) &&
    oauth_harness_send_json(client_fd, status, response);
  free(request);
  close(client_fd);
  return ok;
}

static int oauth_harness_run_server(int listener_fd)
{
  static const char access_payload[] =
    "eyJodHRwczovL2FwaS5vcGVuYWkuY29tL2F1dGgiOnsiY2hhdGdwdF9hY2NvdW50"
    "X2lkIjoiYWNjb3VudC10ZXN0In19";
  static const char refreshed_payload[] =
    "eyJodHRwczovL2FwaS5vcGVuYWkuY29tL2F1dGgiOnsiY2hhdGdwdF9hY2NvdW50"
    "X2lkIjoiYWNjb3VudC1yZWZyZXNoZWQifX0";
  char token_response[1024];
  char refresh_response[1024];
  int written;

  written = snprintf(token_response,
                     sizeof(token_response),
                     "{\"access_token\":\"e30.%s.sig\","
                     "\"refresh_token\":\"refresh-one\","
                     "\"expires_in\":3600}",
                     access_payload);
  if ((written < 0) || ((size_t)written >= sizeof(token_response))) {
    return 0;
  }
  written = snprintf(refresh_response,
                     sizeof(refresh_response),
                     "{\"access_token\":\"e30.%s.sig\","
                     "\"refresh_token\":\"refresh-two\","
                     "\"expires_in\":7200}",
                     refreshed_payload);
  if ((written < 0) || ((size_t)written >= sizeof(refresh_response))) {
    return 0;
  }

  return oauth_harness_serve_one(
           listener_fd,
           "/start",
           "Content-Type: application/json",
           "{\"client_id\":\"test-client\"}",
           200L,
           "{\"device_auth_id\":\"device-test\","
           "\"user_code\":\"ABCD-EFGH\",\"interval\":1}") &&
    oauth_harness_serve_one(
      listener_fd,
      "/poll",
      "Content-Type: application/json",
      "{\"device_auth_id\":\"device-test\","
      "\"user_code\":\"ABCD-EFGH\"}",
      403L,
      "{}") &&
    oauth_harness_serve_one(
      listener_fd,
      "/poll",
      "Content-Type: application/json",
      "{\"device_auth_id\":\"device-test\","
      "\"user_code\":\"ABCD-EFGH\"}",
      400L,
      "{\"error\":\"deviceauth_authorization_pending\"}") &&
    oauth_harness_serve_one(
      listener_fd,
      "/poll",
      "Content-Type: application/json",
      "{\"device_auth_id\":\"device-test\","
      "\"user_code\":\"ABCD-EFGH\"}",
      400L,
      "{\"error\":\"slow_down\"}") &&
    oauth_harness_serve_one(
      listener_fd,
      "/poll",
      "Content-Type: application/json",
      "{\"device_auth_id\":\"device-test\","
      "\"user_code\":\"ABCD-EFGH\"}",
      200L,
      "{\"authorization_code\":\"auth-code\","
      "\"code_verifier\":\"verifier\"}") &&
    oauth_harness_serve_one(
      listener_fd,
      "/token",
      "Content-Type: application/x-www-form-urlencoded",
      "grant_type=authorization_code&client_id=test-client&code=auth-code&"
      "code_verifier=verifier&redirect_uri=http%3A%2F%2F127.0.0.1%2F"
      "redirect",
      200L,
      token_response) &&
    oauth_harness_serve_one(
      listener_fd,
      "/token",
      "Content-Type: application/x-www-form-urlencoded",
      "grant_type=refresh_token&refresh_token=refresh-one&client_id="
      "test-client",
      200L,
      refresh_response);
}

static int oauth_harness_open_listener(unsigned short *port_out)
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
      (listen(listener_fd, 5) != 0)) {
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

static int oauth_harness_cancel_immediately(void *user_data)
{
  (void)user_data;
  return 1;
}

static long long oauth_harness_now_milliseconds(void)
{
  struct timeval now;

  if (gettimeofday(&now, NULL) != 0) {
    return 0LL;
  }
  return ((long long)now.tv_sec * 1000LL) +
    ((long long)now.tv_usec / 1000LL);
}

static void oauth_harness_stop_server(pid_t pid)
{
  int status;

  (void)kill(pid, SIGTERM);
  while ((waitpid(pid, &status, 0) < 0) && (errno == EINTR)) {
  }
}

static int oauth_harness_wait_server(pid_t pid)
{
  int status;
  pid_t waited;

  status = 0;
  do {
    waited = waitpid(pid, &status, 0);
  } while ((waited < 0) && (errno == EINTR));
  if (waited != pid) {
    return 0;
  }
  return WIFEXITED(status) && (WEXITSTATUS(status) == 0);
}

static int oauth_harness_configure_loopback(
  unsigned short port,
  strappy_openai_oauth_configuration *configuration,
  char *start_url,
  size_t start_url_size,
  char *poll_url,
  size_t poll_url_size,
  char *token_url,
  size_t token_url_size)
{
  int start_length;
  int poll_length;
  int token_length;

  start_length = snprintf(start_url,
                          start_url_size,
                          "http://127.0.0.1:%u/start",
                          (unsigned int)port);
  poll_length = snprintf(poll_url,
                         poll_url_size,
                         "http://127.0.0.1:%u/poll",
                         (unsigned int)port);
  token_length = snprintf(token_url,
                          token_url_size,
                          "http://127.0.0.1:%u/token",
                          (unsigned int)port);
  if ((start_length < 0) || ((size_t)start_length >= start_url_size) ||
      (poll_length < 0) || ((size_t)poll_length >= poll_url_size) ||
      (token_length < 0) || ((size_t)token_length >= token_url_size)) {
    return 0;
  }
  configuration->client_id = "test-client";
  configuration->device_start_url = start_url;
  configuration->device_poll_url = poll_url;
  configuration->token_url = token_url;
  configuration->verification_url = "http://127.0.0.1/verify";
  configuration->device_redirect_uri = "http://127.0.0.1/redirect";
  return 1;
}

static int oauth_harness_run_start_error_case(long response_status,
                                               const char *response_json,
                                               const char *expected_error)
{
  unsigned short port;
  int listener_fd;
  pid_t server_pid;
  strappy_openai_oauth_configuration configuration;
  strappy_openai_oauth_device device;
  char start_url[256];
  char poll_url[256];
  char token_url[256];
  char *error;
  int server_ok;
  int ok;

  listener_fd = oauth_harness_open_listener(&port);
  if (listener_fd < 0) {
    return 0;
  }
  server_pid = fork();
  if (server_pid < 0) {
    close(listener_fd);
    return 0;
  }
  if (server_pid == 0) {
    int server_ok;

    (void)alarm(10U);
    server_ok = oauth_harness_serve_one(listener_fd,
                                        "/start",
                                        "Content-Type: application/json",
                                        "{\"client_id\":\"test-client\"}",
                                        response_status,
                                        response_json);
    close(listener_fd);
    _exit(server_ok ? 0 : 1);
  }
  close(listener_fd);

  if (!oauth_harness_configure_loopback(port,
                                        &configuration,
                                        start_url,
                                        sizeof(start_url),
                                        poll_url,
                                        sizeof(poll_url),
                                        token_url,
                                        sizeof(token_url))) {
    oauth_harness_stop_server(server_pid);
    return 0;
  }
  strappy_openai_oauth_device_init(&device);
  error = NULL;
  ok = !strappy_openai_oauth_start_device_authorization(&configuration,
                                                         &device,
                                                         NULL,
                                                         NULL,
                                                         &error) &&
    (error != NULL) && (strstr(error, expected_error) != NULL);
  server_ok = oauth_harness_wait_server(server_pid);
  ok = ok && server_ok;
  if (!ok) {
    fprintf(stderr,
            "OAuth start error case '%s' failed: %s\n",
            expected_error,
            (error != NULL) ? error : "no diagnostic");
  }
  strappy_free_string(error);
  strappy_openai_oauth_device_destroy(&device);
  return ok;
}

static int oauth_harness_run_poll_error_case(const char *response_json,
                                              const char *expected_error)
{
  unsigned short port;
  int listener_fd;
  pid_t server_pid;
  strappy_openai_oauth_configuration configuration;
  strappy_openai_oauth_device device;
  strappy_openai_oauth_credentials credentials;
  char start_url[256];
  char poll_url[256];
  char token_url[256];
  char *error;
  int server_ok;
  int ok;

  listener_fd = oauth_harness_open_listener(&port);
  if (listener_fd < 0) {
    return 0;
  }
  server_pid = fork();
  if (server_pid < 0) {
    close(listener_fd);
    return 0;
  }
  if (server_pid == 0) {
    int server_ok;

    (void)alarm(10U);
    server_ok = oauth_harness_serve_one(
      listener_fd,
      "/start",
      "Content-Type: application/json",
      "{\"client_id\":\"test-client\"}",
      200L,
      "{\"device_auth_id\":\"device-error\","
      "\"user_code\":\"ERROR-CODE\",\"interval\":1}") &&
      oauth_harness_serve_one(
        listener_fd,
        "/poll",
        "Content-Type: application/json",
        "{\"device_auth_id\":\"device-error\","
        "\"user_code\":\"ERROR-CODE\"}",
        400L,
        response_json);
    close(listener_fd);
    _exit(server_ok ? 0 : 1);
  }
  close(listener_fd);

  if (!oauth_harness_configure_loopback(port,
                                        &configuration,
                                        start_url,
                                        sizeof(start_url),
                                        poll_url,
                                        sizeof(poll_url),
                                        token_url,
                                        sizeof(token_url))) {
    oauth_harness_stop_server(server_pid);
    return 0;
  }
  strappy_openai_oauth_device_init(&device);
  strappy_openai_oauth_credentials_init(&credentials);
  error = NULL;
  ok = strappy_openai_oauth_start_device_authorization(&configuration,
                                                        &device,
                                                        NULL,
                                                        NULL,
                                                        &error);
  if (ok) {
    ok = !strappy_openai_oauth_complete_device_authorization(
      &configuration,
      &device,
      &credentials,
      NULL,
      NULL,
      &error) && (error != NULL) &&
      (strstr(error, expected_error) != NULL);
  }
  server_ok = oauth_harness_wait_server(server_pid);
  ok = ok && server_ok;
  if (!ok) {
    fprintf(stderr,
            "OAuth poll error case '%s' failed: %s\n",
            expected_error,
            (error != NULL) ? error : "no diagnostic");
  }
  strappy_free_string(error);
  strappy_openai_oauth_credentials_destroy(&credentials);
  strappy_openai_oauth_device_destroy(&device);
  return ok;
}

static int oauth_harness_run_refresh_error_case(void)
{
  static const char refresh_secret[] = "refresh-secret-value";
  unsigned short port;
  int listener_fd;
  pid_t server_pid;
  strappy_openai_oauth_configuration configuration;
  strappy_openai_oauth_credentials credentials;
  char start_url[256];
  char poll_url[256];
  char token_url[256];
  char *error;
  int server_ok;
  int ok;

  listener_fd = oauth_harness_open_listener(&port);
  if (listener_fd < 0) {
    return 0;
  }
  server_pid = fork();
  if (server_pid < 0) {
    close(listener_fd);
    return 0;
  }
  if (server_pid == 0) {
    int server_ok;

    (void)alarm(10U);
    server_ok = oauth_harness_serve_one(
      listener_fd,
      "/token",
      "Content-Type: application/x-www-form-urlencoded",
      "grant_type=refresh_token&refresh_token=refresh-secret-value&"
      "client_id=test-client",
      400L,
      "{\"error\":\"invalid_grant\","
      "\"error_description\":\"refresh-secret-value\"}");
    close(listener_fd);
    _exit(server_ok ? 0 : 1);
  }
  close(listener_fd);

  if (!oauth_harness_configure_loopback(port,
                                        &configuration,
                                        start_url,
                                        sizeof(start_url),
                                        poll_url,
                                        sizeof(poll_url),
                                        token_url,
                                        sizeof(token_url))) {
    oauth_harness_stop_server(server_pid);
    return 0;
  }
  strappy_openai_oauth_credentials_init(&credentials);
  error = NULL;
  ok = !strappy_openai_oauth_refresh_credentials(&configuration,
                                                  refresh_secret,
                                                  &credentials,
                                                  NULL,
                                                  NULL,
                                                  &error) &&
    (error != NULL) && (strstr(error, "invalid_grant") != NULL) &&
    (strstr(error, refresh_secret) == NULL);
  server_ok = oauth_harness_wait_server(server_pid);
  ok = ok && server_ok;
  if (!ok) {
    fprintf(stderr,
            "OAuth invalid_grant case failed: %s\n",
            (error != NULL) ? error : "no diagnostic");
  }
  strappy_free_string(error);
  strappy_openai_oauth_credentials_destroy(&credentials);
  return ok;
}

static int oauth_harness_run_network_loss_case(void)
{
  unsigned short port;
  int listener_fd;
  strappy_openai_oauth_configuration configuration;
  strappy_openai_oauth_device device;
  char start_url[256];
  char poll_url[256];
  char token_url[256];
  char *error;
  int ok;

  listener_fd = oauth_harness_open_listener(&port);
  if (listener_fd < 0) {
    return 0;
  }
  close(listener_fd);
  if (!oauth_harness_configure_loopback(port,
                                        &configuration,
                                        start_url,
                                        sizeof(start_url),
                                        poll_url,
                                        sizeof(poll_url),
                                        token_url,
                                        sizeof(token_url))) {
    return 0;
  }
  strappy_openai_oauth_device_init(&device);
  error = NULL;
  ok = !strappy_openai_oauth_start_device_authorization(&configuration,
                                                         &device,
                                                         NULL,
                                                         NULL,
                                                         &error) &&
    (error != NULL) && (strstr(error, "network request failed") != NULL);
  if (!ok) {
    fprintf(stderr,
            "OAuth network-loss case failed: %s\n",
            (error != NULL) ? error : "no diagnostic");
  }
  strappy_free_string(error);
  strappy_openai_oauth_device_destroy(&device);
  return ok;
}

static int oauth_harness_run_oversized_field_case(void)
{
  static const char prefix[] = "{\"device_auth_id\":\"";
  static const char suffix[] =
    "\",\"user_code\":\"ABCD-EFGH\",\"interval\":1}";
  size_t field_length;
  size_t response_length;
  char *response;
  int ok;

  field_length = 4097U;
  response_length = (sizeof(prefix) - 1U) + field_length +
    (sizeof(suffix) - 1U);
  response = (char *)malloc(response_length + 1U);
  if (response == NULL) {
    return 0;
  }
  memcpy(response, prefix, sizeof(prefix) - 1U);
  memset(response + sizeof(prefix) - 1U, 'x', field_length);
  memcpy(response + sizeof(prefix) - 1U + field_length,
         suffix,
         sizeof(suffix));
  ok = oauth_harness_run_start_error_case(200L,
                                            response,
                                            "missing fields");
  free(response);
  return ok;
}

int main(void)
{
  unsigned short port;
  int listener_fd;
  pid_t server_pid;
  char start_url[256];
  char poll_url[256];
  char token_url[256];
  strappy_openai_oauth_configuration configuration;
  strappy_openai_oauth_device device;
  strappy_openai_oauth_credentials credentials;
  strappy_openai_oauth_credentials refreshed;
  char *error;
  long long before_exchange;
  int status;
  int ok;
  int start_url_length;
  int poll_url_length;
  int token_url_length;

  (void)signal(SIGPIPE, SIG_IGN);
  listener_fd = oauth_harness_open_listener(&port);
  if (listener_fd < 0) {
    fprintf(stderr, "Could not open OAuth harness listener.\n");
    return 1;
  }
  server_pid = fork();
  if (server_pid < 0) {
    close(listener_fd);
    fprintf(stderr, "Could not fork OAuth harness server.\n");
    return 1;
  }
  if (server_pid == 0) {
    int server_ok;

    (void)alarm(20U);
    server_ok = oauth_harness_run_server(listener_fd);
    close(listener_fd);
    _exit(server_ok ? 0 : 1);
  }
  close(listener_fd);

  start_url_length = snprintf(start_url,
                              sizeof(start_url),
                              "http://127.0.0.1:%u/start",
                              (unsigned int)port);
  poll_url_length = snprintf(poll_url,
                             sizeof(poll_url),
                             "http://127.0.0.1:%u/poll",
                             (unsigned int)port);
  token_url_length = snprintf(token_url,
                              sizeof(token_url),
                              "http://127.0.0.1:%u/token",
                              (unsigned int)port);
  if ((start_url_length < 0) ||
      ((size_t)start_url_length >= sizeof(start_url)) ||
      (poll_url_length < 0) ||
      ((size_t)poll_url_length >= sizeof(poll_url)) ||
      (token_url_length < 0) ||
      ((size_t)token_url_length >= sizeof(token_url))) {
    oauth_harness_stop_server(server_pid);
    return 1;
  }

  configuration.client_id = "test-client";
  configuration.device_start_url = start_url;
  configuration.device_poll_url = poll_url;
  configuration.token_url = token_url;
  configuration.verification_url = "http://127.0.0.1/verify";
  configuration.device_redirect_uri = "http://127.0.0.1/redirect";
  strappy_openai_oauth_device_init(&device);
  strappy_openai_oauth_credentials_init(&credentials);
  strappy_openai_oauth_credentials_init(&refreshed);
  error = NULL;
  ok = strappy_openai_oauth_start_device_authorization(&configuration,
                                                        &device,
                                                        NULL,
                                                        NULL,
                                                        &error) &&
    (strcmp(device.device_auth_id, "device-test") == 0) &&
    (strcmp(device.user_code, "ABCD-EFGH") == 0) &&
    (device.interval_seconds == 1L);
  if (!ok) {
    fprintf(stderr,
            "OAuth device start failed: %s\n",
            (error != NULL) ? error : "invalid response");
    goto failure;
  }
  strappy_free_string(error);
  error = NULL;
  before_exchange = oauth_harness_now_milliseconds();
  ok = strappy_openai_oauth_complete_device_authorization(
    &configuration,
    &device,
    &credentials,
    NULL,
    NULL,
    &error) &&
    (strcmp(credentials.refresh_token, "refresh-one") == 0) &&
    (strcmp(credentials.account_id, "account-test") == 0) &&
    (credentials.expires_at_milliseconds > before_exchange);
  if (!ok) {
    fprintf(stderr,
            "OAuth device completion failed: %s\n",
            (error != NULL) ? error : "invalid credentials");
    goto failure;
  }
  strappy_free_string(error);
  error = NULL;
  ok = strappy_openai_oauth_refresh_credentials(&configuration,
                                                 credentials.refresh_token,
                                                 &refreshed,
                                                 NULL,
                                                 NULL,
                                                 &error) &&
    (strcmp(refreshed.refresh_token, "refresh-two") == 0) &&
    (strcmp(refreshed.account_id, "account-refreshed") == 0) &&
    (refreshed.expires_at_milliseconds >
     credentials.expires_at_milliseconds);
  if (!ok) {
    fprintf(stderr,
            "OAuth refresh failed: %s\n",
            (error != NULL) ? error : "invalid refreshed credentials");
    goto failure;
  }
  strappy_free_string(error);
  error = NULL;
  while ((waitpid(server_pid, &status, 0) < 0) && (errno == EINTR)) {
  }
  if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
    fprintf(stderr, "OAuth harness server rejected a request.\n");
    goto cleanup_failure;
  }

  strappy_openai_oauth_device_destroy(&device);
  strappy_openai_oauth_device_init(&device);
  ok = !strappy_openai_oauth_start_device_authorization(
      &configuration,
      &device,
      oauth_harness_cancel_immediately,
      NULL,
      &error) &&
    (error != NULL) && (strstr(error, "cancelled") != NULL);
  if (!ok) {
    fprintf(stderr, "OAuth cancellation did not stop the request.\n");
    goto cleanup_failure;
  }

  strappy_free_string(error);
  error = NULL;
  ok = oauth_harness_run_start_error_case(
         404L,
         "{\"error\":\"device_login_disabled\"}",
         "disabled") &&
    oauth_harness_run_start_error_case(200L, "{", "invalid JSON") &&
    oauth_harness_run_start_error_case(
      200L,
      "{\"device_auth_id\":7,\"user_code\":false,\"interval\":\"x\"}",
      "missing fields") &&
    oauth_harness_run_oversized_field_case() &&
    oauth_harness_run_poll_error_case(
      "{\"error\":{\"code\":\"access_denied\","
      "\"message\":\"authorization denied\"}}",
      "access_denied") &&
    oauth_harness_run_poll_error_case(
      "{\"error\":{\"code\":\"expired_token\","
      "\"message\":\"device code expired\"}}",
      "expired_token") &&
    oauth_harness_run_refresh_error_case() &&
    oauth_harness_run_network_loss_case();
  if (!ok) {
    goto cleanup_failure;
  }

  strappy_openai_oauth_credentials_destroy(&refreshed);
  strappy_openai_oauth_credentials_destroy(&credentials);
  strappy_openai_oauth_device_destroy(&device);
  printf("openai_oauth_harness: PASS\n");
  return 0;

failure:
  oauth_harness_stop_server(server_pid);
cleanup_failure:
  strappy_free_string(error);
  strappy_openai_oauth_credentials_destroy(&refreshed);
  strappy_openai_oauth_credentials_destroy(&credentials);
  strappy_openai_oauth_device_destroy(&device);
  return 1;
}
