#ifndef STRAPPY_CLIENT_H
#define STRAPPY_CLIENT_H

#include "strappy_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct strappy_chat_result {
  char *response_text;
  char *raw_json;
  char *finish_reason;
  char *model;
  long http_status;
} strappy_chat_result;

void strappy_chat_result_init(strappy_chat_result *result);
void strappy_chat_result_destroy(strappy_chat_result *result);
int strappy_client_send_prompt(const strappy_config *config,
                               const char *prompt,
                               strappy_chat_result *result,
                               char **error_out);

#ifdef __cplusplus
}
#endif

#endif
