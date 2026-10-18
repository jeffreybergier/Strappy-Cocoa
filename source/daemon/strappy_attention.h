#ifndef STRAPPY_ATTENTION_H
#define STRAPPY_ATTENTION_H
#include "../shared/strappy_ipc.h"
/* One daemon-owned instance. Stop after joining execution/control workers.
 * Serializes observation, terminal completion and deletion; no UI callbacks.
 * Observation belongs to an authenticated connection and expires after 15s.
 * Open (1) acknowledges immediately; renew (2) only acknowledges on reacquire;
 * close (0) affects only the matching connection/session/token. */
int strappy_attention_start(const char *path, strappy_ipc_server *server);
void strappy_attention_stop(void);
unsigned int strappy_attention_observe(const strappy_ipc_message *request);
int strappy_attention_finish(long long run, long long session, int state,
  int cancel, const char *title, const char *response, const char *error);
void strappy_attention_lock(void);
void strappy_attention_unlock(void);
/* Called under the ordering lock after a successful deletion. */
void strappy_attention_deleted(long long session);
#endif
