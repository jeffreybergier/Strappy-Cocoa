#include "strappy_run_internal.h"
#include "strappy_db_internal.h"
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>

void strappy_run_destroy(strappy_run_record *r)
{
    free(r->status_key); free(r->request_key); free(r->prompt); free(r->status_kind); free(r->status_reason);
    memset(r, 0, sizeof(*r));
}
int strappy_run_read(sqlite3_stmt *q, strappy_run_record *r)
{
    r->id = sqlite3_column_int64(q, 0); r->session_id = sqlite3_column_int64(q, 1);
    r->state = sqlite3_column_int(q, 2); r->cancel_requested = sqlite3_column_int(q, 3);
    r->request_key = strappy_db_column_string(q, 4); r->prompt = strappy_db_column_string(q, 5);
    r->status_kind = strappy_db_column_string(q, 6); r->status_reason = strappy_db_column_string(q, 7);
    r->status_started_ms = sqlite3_column_int64(q, 8); r->retry_until_ms = sqlite3_column_int64(q, 9);
    r->status_key = strappy_db_column_string(q, 10);
    r->retry_after_seconds = (unsigned int)sqlite3_column_int(q, 11);
    r->retry_attempt = (unsigned int)sqlite3_column_int(q, 12);
    r->retry_max_attempts = (unsigned int)sqlite3_column_int(q, 13);
    r->updated_ms = sqlite3_column_int64(q, 14);
    return r->request_key && r->prompt && r->status_kind && r->status_reason && r->status_key;
}
int strappy_run_latest(const char *path, long long session, strappy_run_record *r)
{
    sqlite3 *db = NULL; sqlite3_stmt *q = NULL; int rc;
    memset(r, 0, sizeof(*r));
    if (!strappy_db_open_reader(path, &db, NULL)) return 0;
    rc = sqlite3_prepare_v2(db, "SELECT " RUN_COLUMNS " FROM prompt_runs WHERE session_id=? ORDER BY id DESC LIMIT 1", -1, &q, NULL);
    if (rc == SQLITE_OK) { sqlite3_bind_int64(q, 1, session); rc = sqlite3_step(q); }
    if (rc == SQLITE_ROW && !strappy_run_read(q, r)) { strappy_run_destroy(r); rc = SQLITE_NOMEM; }
    sqlite3_finalize(q); sqlite3_close(db);
    return rc == SQLITE_ROW || rc == SQLITE_DONE;
}
/* Complete dangling function-call pairs with an application-owned diagnostic,
 * not a fabricated tool result. This keeps the next prompt's context valid. */

unsigned int strappy_run_active_count(const char *path)
{
    sqlite3 *db = NULL; sqlite3_stmt *q = NULL; unsigned int count = 0;
    if (!strappy_db_open_reader(path, &db, NULL)) return 0;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM prompt_runs WHERE state IN (1,2)", -1, &q, NULL) == SQLITE_OK && sqlite3_step(q) == SQLITE_ROW)
        count = (unsigned int)sqlite3_column_int(q, 0);
    sqlite3_finalize(q); sqlite3_close(db); return count;
}
char *strappy_run_status_json(const strappy_run_record *r)
{
    cJSON *root = cJSON_CreateObject();
    char *json;
    int active = r->state == STRAPPY_RUN_ACCEPTED || r->state == STRAPPY_RUN_RUNNING;
    if (!root) return NULL;
    cJSON_AddBoolToObject(root, "active", active);
    if (active) {
        cJSON_AddStringToObject(root, "message_key", r->status_key && r->status_key[0] ? r->status_key : "responses-processing");
        cJSON_AddStringToObject(root, "status_kind", r->status_kind && r->status_kind[0] ? r->status_kind : "thinking");
        cJSON_AddStringToObject(root, "retry_reason", r->status_reason ? r->status_reason : "");
        cJSON_AddNumberToObject(root, "started_ms", (double)r->status_started_ms);
        cJSON_AddNumberToObject(root, "updated_ms", (double)r->updated_ms);
        cJSON_AddNumberToObject(root, "retry_after_seconds", (double)r->retry_after_seconds);
        cJSON_AddNumberToObject(root, "retry_attempt", (double)r->retry_attempt);
        cJSON_AddNumberToObject(root, "retry_max_attempts", (double)r->retry_max_attempts);
        cJSON_AddNumberToObject(root, "retry_until_ms", (double)r->retry_until_ms);
    }
    json = cJSON_PrintUnformatted(root); cJSON_Delete(root); return json;
}
