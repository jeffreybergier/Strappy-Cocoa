#ifndef STRAPPY_RUN_H
#define STRAPPY_RUN_H
#include "strappy_ipc.h"
#include "strappy_client.h"
#include "strappy_db.h"
/* Semantic, durable work state. Zero denotes no submitted work. */
enum strappy_run_state { STRAPPY_RUN_NONE, STRAPPY_RUN_ACCEPTED,
    STRAPPY_RUN_RUNNING, STRAPPY_RUN_COMPLETED, STRAPPY_RUN_FAILED,
    STRAPPY_RUN_CANCELLED, STRAPPY_RUN_INTERRUPTED };
typedef struct strappy_run_record {
    long long id, session_id;
    int state, cancel_requested;
    char *request_key, *prompt, *status_kind, *status_reason, *status_key;
    long long status_started_ms, retry_until_ms, updated_ms;
    unsigned int retry_after_seconds, retry_attempt, retry_max_attempts;
} strappy_run_record;
unsigned int strappy_run_active_count(const char *path);
char *strappy_run_status_json(const strappy_run_record *run);
void strappy_run_destroy(strappy_run_record *run);
int strappy_run_latest(const char *path, long long session, strappy_run_record *run);
/* Writer-only calls. accept returns an IPC status; retries return the same ID. */
unsigned int strappy_run_accept(const char *path, long long session,
    const char *key, const char *prompt, strappy_run_record *run);
/* Resolve durable retries even when the caller cannot accept a new run.
 * New runs exclude only active work in the same session. */
unsigned int strappy_run_accept_if_idle(const char *path, long long session,
    const char *key, const char *prompt, strappy_run_record *run, int accept_new);
/* Settings and acceptance commit together. A durable retry never reapplies settings. */
unsigned int strappy_run_accept_with_options(const char *path, long long session,
    const char *key, const char *prompt, strappy_run_record *run, int accept_new,
    const strappy_session_options *options, strappy_session_option_mask mask);
int strappy_run_update(const char *path, long long id, int state, int cancel,
    const char *kind, const char *reason, long long started, long long retry);
int strappy_run_progress(const char *path, long long id, const strappy_responses_event *event);
int strappy_run_reconcile(const char *path);
#endif
