#include "strappy_run.h"
#include "strappy_db_internal.h"
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>

void strappy_run_destroy(strappy_run_record *r)
{
    free(r->status_key); free(r->request_key); free(r->prompt); free(r->status_kind); free(r->status_reason);
    memset(r, 0, sizeof(*r));
}
static int strappy_run_read(sqlite3_stmt *q, strappy_run_record *r)
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
#define RUN_COLUMNS "id,session_id,state,cancel_requested,request_key,prompt,status_kind,status_reason,status_started_ms,retry_until_ms,status_key,retry_after_seconds,retry_attempt,retry_max_attempts,updated_at_ms"
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
unsigned int strappy_run_accept(const char *path, long long session,
    const char *key, const char *prompt, strappy_run_record *r)
{
    return strappy_run_accept_if_idle(path,session,key,prompt,r,1);
}
unsigned int strappy_run_accept_if_idle(const char *path, long long session,
    const char *key, const char *prompt, strappy_run_record *r, int accept_new)
{
    sqlite3 *db = NULL; sqlite3_stmt *q = NULL;
    unsigned int status = STRAPPY_IPC_STORAGE_ERROR;
    int rc;
    memset(r, 0, sizeof(*r));
    if (!strappy_db_open(path, &db, NULL)) return status;
    if (sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK) goto done;
    rc = sqlite3_prepare_v2(db, "SELECT " RUN_COLUMNS " FROM prompt_runs WHERE request_key=?", -1, &q, NULL);
    if (rc == SQLITE_OK) { sqlite3_bind_text(q, 1, key, -1, SQLITE_TRANSIENT); rc = sqlite3_step(q); }
    if (rc == SQLITE_ROW) {
        if (!strappy_run_read(q, r)) goto finish;
        status = r->session_id == session && r->prompt && strcmp(r->prompt, prompt) == 0 ? STRAPPY_IPC_OK : STRAPPY_IPC_CONFLICT;
        goto finish;
    }
    if (rc != SQLITE_DONE) goto finish;
    if (!accept_new) { status = STRAPPY_IPC_BUSY; goto finish; }
    sqlite3_finalize(q); q = NULL;
    if (!strappy_db_session_exists(db, session, NULL)) { status = STRAPPY_IPC_INVALID; goto finish; }
    rc = sqlite3_prepare_v2(db, "SELECT 1 FROM prompt_runs WHERE state IN (1,2) LIMIT 1", -1, &q, NULL);
    if (rc == SQLITE_OK) rc = sqlite3_step(q);
    if (rc == SQLITE_ROW) { status = STRAPPY_IPC_BUSY; goto finish; }
    if (rc != SQLITE_DONE) goto finish;
    sqlite3_finalize(q); q = NULL;
    rc = sqlite3_prepare_v2(db, "INSERT INTO prompt_runs(session_id,request_key,prompt,state,created_at_ms,updated_at_ms) VALUES(?,?,?,1,?,?)", -1, &q, NULL);
    if (rc != SQLITE_OK) goto finish;
    sqlite3_bind_int64(q, 1, session); sqlite3_bind_text(q, 2, key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q, 3, prompt, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(q, 4, strappy_db_now_ms()); sqlite3_bind_int64(q, 5, strappy_db_now_ms());
    if (sqlite3_step(q) != SQLITE_DONE) goto finish;
    r->id = sqlite3_last_insert_rowid(db); r->session_id = session; r->state = STRAPPY_RUN_ACCEPTED;
    status = STRAPPY_IPC_OK;
finish:
    sqlite3_finalize(q); q = NULL;
    if (status == STRAPPY_IPC_OK) {
        if (sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) status = STRAPPY_IPC_STORAGE_ERROR;
    }
    if (status != STRAPPY_IPC_OK) sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
done:
    strappy_db_release(db); return status;
}
int strappy_run_update(const char *path, long long id, int state, int cancel,
    const char *kind, const char *reason, long long started, long long retry)
{
    sqlite3 *db = NULL; sqlite3_stmt *q = NULL; int ok;
    if (!strappy_db_open(path, &db, NULL)) return 0;
    ok = sqlite3_prepare_v2(db, "UPDATE prompt_runs SET state=CASE WHEN ?=0 THEN state ELSE ? END,cancel_requested=MAX(cancel_requested,?),status_kind=?,status_reason=?,status_started_ms=?,retry_until_ms=?,updated_at_ms=? WHERE id=? AND state IN (1,2)", -1, &q, NULL) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int(q, 1, state); sqlite3_bind_int(q, 2, state); sqlite3_bind_int(q, 3, cancel);
        sqlite3_bind_text(q, 4, kind ? kind : "", -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(q, 5, reason ? reason : "", -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(q, 6, started); sqlite3_bind_int64(q, 7, retry);
        sqlite3_bind_int64(q, 8, strappy_db_now_ms()); sqlite3_bind_int64(q, 9, id);
        ok = sqlite3_step(q) == SQLITE_DONE;
    }
    sqlite3_finalize(q); strappy_db_release(db); return ok;
}
/* Complete dangling function-call pairs with an application-owned diagnostic,
 * not a fabricated tool result. This keeps the next prompt's context valid. */
static int strappy_run_reconcile_outputs(sqlite3 *db)
{
    sqlite3_stmt *calls = NULL, *item = NULL, *output = NULL;
    int rc, ok = 0;
    const char *message = "Service interrupted before a durable tool result was recorded. "
        "External effects may already have occurred. Do not automatically replay this tool.";
    if (sqlite3_prepare_v2(db,
        "SELECT f.item_id,i.session_id,i.turn_id,i.include_in_context FROM function_calls f "
        "JOIN conversation_items i ON i.id=f.item_id JOIN turns t ON t.id=i.turn_id "
        "WHERE t.state='running' AND i.source_attempt_id IS NOT NULL "
        "AND NOT EXISTS(SELECT 1 FROM function_outputs o WHERE o.function_call_item_id=f.item_id) "
        "ORDER BY i.session_id,i.sequence", -1, &calls, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(db,
        "INSERT INTO conversation_items(session_id,turn_id,sequence,kind,is_error,include_in_context,created_at_ms) "
        "SELECT ?,?,COALESCE(MAX(sequence),0)+1,'function_call_output',1,?,? "
        "FROM conversation_items WHERE session_id=?", -1, &item, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(db,
        "INSERT INTO function_outputs(item_id,function_call_item_id,execution_state,completed_at_ms,"
        "text_output,error_code,error_message) VALUES(?,?,'error',?,?,'service_interrupted',?)",
        -1, &output, NULL) != SQLITE_OK) goto done;
    while ((rc = sqlite3_step(calls)) == SQLITE_ROW) {
        long long now = strappy_db_now_ms();
        sqlite3_bind_int64(item,1,sqlite3_column_int64(calls,1));
        sqlite3_bind_int64(item,2,sqlite3_column_int64(calls,2));
        sqlite3_bind_int(item,3,sqlite3_column_int(calls,3));
        sqlite3_bind_int64(item,4,now);
        sqlite3_bind_int64(item,5,sqlite3_column_int64(calls,1));
        if (sqlite3_step(item) != SQLITE_DONE) goto done;
        sqlite3_bind_int64(output,1,sqlite3_last_insert_rowid(db));
        sqlite3_bind_int64(output,2,sqlite3_column_int64(calls,0));
        sqlite3_bind_int64(output,3,now);
        sqlite3_bind_text(output,4,message,-1,SQLITE_STATIC);
        sqlite3_bind_text(output,5,message,-1,SQLITE_STATIC);
        if (sqlite3_step(output) != SQLITE_DONE) goto done;
        sqlite3_reset(item); sqlite3_reset(output);
    }
    ok = rc == SQLITE_DONE;
done:
    sqlite3_finalize(calls); sqlite3_finalize(item); sqlite3_finalize(output);
    return ok;
}

int strappy_run_reconcile(const char *path)
{
    sqlite3 *db = NULL; int ok;
    if (!strappy_db_open(path, &db, NULL)) return 0;
    /* The exclusive service lock is held and no worker exists yet. Repair all
     * unfinished semantic records atomically, including scans and legacy runs.
     * Error means outcome unknown, never proof that an external effect failed. */
    ok = sqlite3_exec(db,"BEGIN IMMEDIATE",NULL,NULL,NULL) == SQLITE_OK &&
        strappy_run_reconcile_outputs(db) && sqlite3_exec(db,
        "UPDATE http_attempts SET state='transport_error',"
          "transport_error='Service interrupted; external outcome unknown.',"
          "completed_at_ms=CAST(strftime('%s','now') AS INTEGER)*1000 "
          "WHERE state IN ('pending','running');"
        "UPDATE tool_executions SET state='error',error_code='service_interrupted',"
          "error_message='Service interrupted; external outcome unknown. Do not automatically replay.',"
          "completed_at_ms=CAST(strftime('%s','now') AS INTEGER)*1000 "
          "WHERE state IN ('pending','running');"
        "UPDATE model_requests SET state='error',"
          "completed_at_ms=CAST(strftime('%s','now') AS INTEGER)*1000 "
          "WHERE state IN ('pending','running');"
        "UPDATE turns SET state='error',"
          "completed_at_ms=CAST(strftime('%s','now') AS INTEGER)*1000 WHERE state='running';"
        "UPDATE scan_runs SET state='error',error_message='Service interrupted; scan incomplete.',"
          "completed_at_ms=CAST(strftime('%s','now') AS INTEGER)*1000 WHERE state='running';"
        "UPDATE prompt_runs SET state=6,status_kind='',status_reason='service_restarted',"
          "status_key='',status_started_ms=0,retry_until_ms=0,retry_after_seconds=0,"
          "retry_attempt=0,retry_max_attempts=0,"
          "updated_at_ms=CAST(strftime('%s','now') AS INTEGER)*1000 WHERE state IN (1,2);"
        "COMMIT;", NULL, NULL, NULL) == SQLITE_OK;
    if (!ok) sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
    strappy_db_release(db); return ok;
}

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

int strappy_run_progress(const char *path, long long id, const strappy_responses_event *e)
{
    sqlite3 *db = NULL; sqlite3_stmt *q = NULL; int ok;
    if (!strappy_db_open(path, &db, NULL)) return 0;
    ok = sqlite3_prepare_v2(db, "UPDATE prompt_runs SET status_kind=?,status_reason=?,status_started_ms=?,retry_until_ms=?,status_key=?,retry_after_seconds=?,retry_attempt=?,retry_max_attempts=?,updated_at_ms=? WHERE id=? AND state IN (1,2) AND cancel_requested=0", -1, &q, NULL) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_text(q, 1, e->status_kind ? e->status_kind : "", -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(q, 2, e->status_reason ? e->status_reason : "", -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(q, 3, e->status_started_ms); sqlite3_bind_int64(q, 4, e->retry_until_ms);
        sqlite3_bind_text(q, 5, e->prompt_group_key ? e->prompt_group_key : "", -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(q, 6, e->retry_after_seconds); sqlite3_bind_int64(q, 7, e->retry_attempt);
        sqlite3_bind_int64(q, 8, e->retry_max_attempts); sqlite3_bind_int64(q, 9, e->status_updated_ms);
        sqlite3_bind_int64(q, 10, id); ok = sqlite3_step(q) == SQLITE_DONE;
    }
    sqlite3_finalize(q); strappy_db_release(db); return ok;
}
