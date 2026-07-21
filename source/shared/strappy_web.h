#ifndef STRAPPY_WEB_H
#define STRAPPY_WEB_H

#include <stddef.h>

#define STRAPPY_WEB_MAX_ARGUMENT_BYTES 512U
#define STRAPPY_WEB_MAX_SEARCH_PAGE 10

#ifdef __cplusplus
extern "C" {
#endif

typedef struct strappy_web_result {
  char *body;
  char *content_type;
  size_t body_length;
  long status;
  int cancelled;
} strappy_web_result;

typedef int (*strappy_web_continue_callback)(void *user_data);

void strappy_web_result_init(strappy_web_result *result);
void strappy_web_result_destroy(strappy_web_result *result);
int strappy_web_set_cainfo(const char *path, char **error_out);
int strappy_web_search_response_is_rate_limited(
  long status,
  const char *body,
  size_t body_length);
int strappy_web_search(
  const char *session_db_path,
  const char *query,
  int page,
  strappy_web_continue_callback continue_callback,
  void *continue_callback_data,
  strappy_web_result *result,
  char **error_out);
int strappy_web_fetch(
  const char *session_db_path,
  const char *url,
  strappy_web_continue_callback continue_callback,
  void *continue_callback_data,
  strappy_web_result *result,
  char **error_out);

#ifdef __cplusplus
}
#endif

#endif
