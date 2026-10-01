#ifndef STRAPPY_STORE_CLIENT_H
#define STRAPPY_STORE_CLIENT_H
enum {
    STRAPPY_STORE_SESSION = 1U << 0,
    STRAPPY_STORE_LEDGER = 1U << 1,
    STRAPPY_STORE_CATALOG = 1U << 2,
    STRAPPY_STORE_RUN = 1U << 3,
    STRAPPY_STORE_ALL = 15U
};
/* Starts one process-lifetime service monitor. Callback runs on its worker and
 * is a refresh hint; consumers reload committed data on their own UI thread. */
int strappy_store_client_start(const char *socket_path, const char *database_path,
                               void (*changed)(unsigned int topics));
/* One command per private connection; reconnect retry uses caller's durable key. */
int strappy_store_client_command(unsigned int operation, const unsigned char *payload,
    unsigned int length, unsigned int *status);
int strappy_store_client_request_key(char key[65]);

#include "cJSON.h"
/* One-shot mutations are never retried implicitly after a lost acknowledgment. */
char *strappy_store_client_mutation(unsigned int operation, const cJSON *command, unsigned int *status);
void strappy_store_client_work_status(unsigned int *active, unsigned int *last, unsigned int *result);
#endif
