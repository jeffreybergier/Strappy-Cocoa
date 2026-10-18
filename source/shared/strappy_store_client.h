#ifndef STRAPPY_STORE_CLIENT_H
#define STRAPPY_STORE_CLIENT_H
#include <stdint.h>
enum {
    STRAPPY_STORE_SESSION = 1U << 0,
    STRAPPY_STORE_LEDGER = 1U << 1,
    STRAPPY_STORE_CATALOG = 1U << 2,
    STRAPPY_STORE_RUN = 1U << 3,
    STRAPPY_STORE_ALL = 15U
};
/* Starts one process-lifetime service monitor. Callback runs on its worker and
 * is a refresh hint; consumers schedule reads of committed data and publish UI updates. */
int strappy_store_client_start(const char *socket_path, const char *database_path,
                               void (*changed)(unsigned int topics, long long session));
/* Synchronous caller-thread transport. The start/bootstrap caller (GUI main)
 * owns the permanent FIFO control connection. Other threads use independent
 * persistent connections, closed at thread exit. Durable commands may reconnect/retry;
 * ordinary mutations never replay after an uncertain acknowledgement. */
int strappy_store_client_command(unsigned int operation, const unsigned char *payload,
    unsigned int length, unsigned int *status);
/* Main-thread, bounded observation command; idempotent open/close/renew. */
int strappy_store_client_observe(long long session, uint64_t token, unsigned int mode);
int strappy_store_client_request_key(char key[65]);

#include "cJSON.h"
#include <stdint.h>
#define STRAPPY_STORE_MAX_CHANGES 64
/* Snapshot the monitor's bounded local revision journal before reading SQLite.
 * Returns -1 for full resync, otherwise the number of affected request groups.
 * Cursor zero, reconnect, overflow or missing scope force a full refresh. */
int strappy_store_client_changes(long long session, uint64_t after,
    uint64_t *next, long long requests[STRAPPY_STORE_MAX_CHANGES]);
/* One-shot mutations are never retried implicitly after a lost acknowledgment. */
char *strappy_store_client_mutation(unsigned int operation, const cJSON *command, unsigned int *status);
/* Worker-only: submit once, then poll public job status. NOT_READY means unknown
 * outcome, including daemon restart; never resubmit the mutation automatically. */
char *strappy_store_client_credential(const cJSON *command, unsigned int *status);
/* Worker-only: wait for the scan/catalog command's opaque key. Returns a
 * terminal STRAPPY_RUN_* state or 0 for unknown (restart, eviction, transport
 * failure). Never submits/repeats work. The daemon retains 64 keyed outcomes. */
unsigned int strappy_store_client_wait_maintenance(const char *key);
void strappy_store_client_work_status(unsigned int *active, unsigned int *last, unsigned int *result);
#endif
