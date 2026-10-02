#ifndef STRAPPY_AUTH_H
#define STRAPPY_AUTH_H
#include "strappy_ipc.h"
#include <cJSON.h>
typedef struct strappy_auth strappy_auth;
strappy_auth *strappy_auth_open(const char *db, strappy_ipc_server *server);
unsigned int strappy_auth_command(strappy_auth *auth, const cJSON *command, char **result);
void strappy_auth_cancel_account(strappy_auth *auth, const char *account);
void strappy_auth_stop(strappy_auth *auth);
void strappy_auth_close(strappy_auth *auth);
#endif
