#ifndef STRAPPY_RUN_INTERNAL_H
#define STRAPPY_RUN_INTERNAL_H
#include "strappy_run.h"
#include <sqlite3.h>
#include <cJSON.h>
#define RUN_COLUMNS "id,session_id,state,cancel_requested,request_key,prompt,status_kind,status_reason,status_started_ms,retry_until_ms,status_key,retry_after_seconds,retry_attempt,retry_max_attempts,updated_at_ms"
int strappy_run_read(sqlite3_stmt *q, strappy_run_record *r);
#endif
