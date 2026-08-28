#ifndef STRAPPY_CLIENT_TRANSPORT_H
#define STRAPPY_CLIENT_TRANSPORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef size_t (*strappy_client_transport_write_callback)(
  const void *bytes,
  size_t length,
  void *context);

typedef int (*strappy_client_transport_poll_callback)(void *context);
typedef int (*strappy_client_transport_write_complete_callback)(void *context);

typedef struct strappy_client_transport_request {
  const char *url;
  const char *method;
  const char *const *headers;
  size_t header_count;
  const char *user_agent;
  const void *body;
  size_t body_length;
  long timeout_seconds;
  strappy_client_transport_write_callback write_body;
  void *write_body_context;
  strappy_client_transport_write_callback write_header;
  void *write_header_context;
  strappy_client_transport_poll_callback poll_cancelled;
  void *poll_context;
  strappy_client_transport_write_complete_callback write_is_complete;
  void *write_complete_context;
} strappy_client_transport_request;

typedef struct strappy_client_transport_result {
  char *effective_url;
  char *error_message;
  char *content_type;
  long http_status;
  long transport_code;
  double name_lookup_seconds;
  double connect_seconds;
  double start_transfer_seconds;
  double total_seconds;
  int cancelled;
} strappy_client_transport_result;

void strappy_client_transport_result_init(
  strappy_client_transport_result *result);
void strappy_client_transport_result_destroy(
  strappy_client_transport_result *result);

/* Configures a native trust-store override. Browser transports reject it. */
int strappy_client_transport_set_cainfo(const char *path, char **error_out);

/* Returns true when this backend can send the named request header. Shared C
 * uses this capability to omit optional provider headers before transport. */
int strappy_client_transport_supports_request_header(const char *name);
char *strappy_client_transport_copy_user_agent(char **error_out);

/* A completed call may still describe a network failure in result. False is
 * reserved for invalid input or an allocation/setup failure. */
int strappy_client_transport_execute(
  const strappy_client_transport_request *request,
  strappy_client_transport_result *result,
  char **error_out);

#ifdef __cplusplus
}
#endif

#endif
