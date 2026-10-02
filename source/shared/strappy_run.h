#ifndef STRAPPY_RUN_H
#define STRAPPY_RUN_H
#include "strappy_ipc.h"
#include "strappy_events.h"
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

#endif
