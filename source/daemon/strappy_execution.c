#include "strappy_execution.h"
#include "../shared/strappy_core.h"
#include "../shared/strappy_session.h"
#include "../shared/strappy_run.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

struct strappy_execution {
    strappy_ipc_server *server;
    char *db, *resources, *prompt;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    pthread_t worker;
    long long run, session;
    int stopping, cancel;
};
/* Exactly one execution worker: process globals (curl CA, OAuth CA, callback,
 * configuration defaults and localization) are configured before it starts. */
static strappy_execution *strappy_execution_owner;
int strappy_service_should_cancel(void)
{
    int cancel;
    strappy_execution *e = strappy_execution_owner;
    if (!e) return 1;
    pthread_mutex_lock(&e->mutex); cancel = e->cancel || e->stopping;
    pthread_mutex_unlock(&e->mutex); return cancel;
}
static int strappy_execution_event(const strappy_responses_event *event, void *context)
{
    strappy_execution *e = context;
    if (event->type == STRAPPY_RESPONSES_EVENT_PROCESSING_STATUS) {
        if (strappy_run_progress(e->db, e->run, event))
            strappy_ipc_server_committed(e->server, STRAPPY_IPC_RUN_CHANGED);
    } else if (event->type == STRAPPY_RESPONSES_EVENT_SESSION_UPDATED)
        strappy_ipc_server_committed(e->server, STRAPPY_IPC_SESSION_CHANGED);
    else if (event->type == STRAPPY_RESPONSES_EVENT_LEDGER_CHANGED ||
             event->type == STRAPPY_RESPONSES_EVENT_LEDGER_UPDATED)
        strappy_ipc_server_committed(e->server, STRAPPY_IPC_LEDGER_CHANGED);
    return strappy_service_should_cancel() ? 0 : 1;
}
static void *strappy_execution_work(void *context)
{
    strappy_execution *e = context;
    for (;;) {
        strappy_session_record record;
        char *error = NULL;
        int ok = 0, state;
        void *pool;
        pthread_mutex_lock(&e->mutex);
        while (!e->prompt && !e->stopping) pthread_cond_wait(&e->condition, &e->mutex);
        if (!e->prompt) { pthread_mutex_unlock(&e->mutex); break; }
        pthread_mutex_unlock(&e->mutex);
        pool = strappy_service_worker_enter();
        strappy_session_record_init(&record);
        if (!strappy_service_should_cancel() &&
            strappy_run_update(e->db, e->run, STRAPPY_RUN_RUNNING, 0, "request", "", 0, 0)) {
            strappy_ipc_server_committed(e->server, STRAPPY_IPC_RUN_CHANGED);
            ok = strappy_session_send_prompt_with_events_and_load(e->prompt, NULL, NULL,
                e->resources, e->db, e->session, strappy_execution_event, e, &record, &error);
        }
        /* Provider/tool diagnostics remain in the normalized ledger. Never log
         * errors here: they can contain provider content or credentials. */
        free(error); strappy_session_record_destroy(&record);
        pthread_mutex_lock(&e->mutex);
        state = e->stopping ? STRAPPY_RUN_INTERRUPTED :
            (e->cancel ? STRAPPY_RUN_CANCELLED : (ok ? STRAPPY_RUN_COMPLETED : STRAPPY_RUN_FAILED));
        if (strappy_run_update(e->db, e->run, state, e->cancel, "", "", 0, 0))
            strappy_ipc_server_committed(e->server, STRAPPY_IPC_RUN_CHANGED);
        free(e->prompt); e->prompt = NULL; e->run = 0; e->cancel = 0;
        pthread_mutex_unlock(&e->mutex);
        strappy_service_worker_leave(pool);
    }
    return NULL;
}
static void strappy_execution_command(const strappy_ipc_message *request,
    strappy_ipc_message *reply, void *context)
{
    strappy_execution *e = context;
    long long session;
    strappy_run_record r;
    char key[65], prompt[STRAPPY_IPC_MAX_PAYLOAD];
    uint32_t size;
    memset(&r, 0, sizeof(r));
    reply->status = STRAPPY_IPC_INVALID;
    if (request->length < 8 || strappy_ipc_get32(request->payload) > 0x7fffffffU) return;
    session = (long long)(((uint64_t)strappy_ipc_get32(request->payload) << 32) |
        strappy_ipc_get32(request->payload + 4));
    if (session <= 0) return;
    pthread_mutex_lock(&e->mutex);
    if (e->stopping) { reply->status = STRAPPY_IPC_NOT_READY; goto done; }
    if (request->operation == STRAPPY_IPC_CANCEL_RUN) {
        long long run;
        if (request->length != 16 || strappy_ipc_get32(request->payload + 8) > 0x7fffffffU) goto done;
        run = (long long)(((uint64_t)strappy_ipc_get32(request->payload + 8) << 32) |
            strappy_ipc_get32(request->payload + 12));
        if (!strappy_run_latest(e->db, session, &r) || r.id != run) goto done;
        reply->status = STRAPPY_IPC_OK;
        if (e->session == session && e->run == run) {
            if (!strappy_run_update(e->db, run, 0, 1, "cancelling", "", 0, 0)) {
                reply->status = STRAPPY_IPC_STORAGE_ERROR; goto done;
            }
            e->cancel = 1;
            strappy_ipc_server_committed(e->server, STRAPPY_IPC_RUN_CHANGED);
        }
    } else {
        unsigned int i;
        /* 8-byte session, 64 lowercase hex retry-key, UTF-8 prompt. */
        if (request->length <= 72) goto done;
        for (i = 0; i < 64; ++i) if (!((request->payload[8+i] >= '0' && request->payload[8+i] <= '9') ||
            (request->payload[8+i] >= 'a' && request->payload[8+i] <= 'f'))) goto done;
        memcpy(key, request->payload + 8, 64); key[64] = 0;
        size = request->length - 72;
        if (memchr(request->payload + 72, 0, size) ||
            !strappy_utf8_validate((const char *)request->payload + 72, size)) goto done;
        memcpy(prompt, request->payload + 72, size); prompt[size] = 0;
        reply->status = strappy_run_accept(e->db, session, key, prompt, &r);
        if (reply->status != STRAPPY_IPC_OK) goto done;
        if (r.state == STRAPPY_RUN_ACCEPTED && !e->prompt) {
            e->prompt = strappy_string_duplicate(prompt);
            if (!e->prompt) {
                strappy_run_update(e->db, r.id, STRAPPY_RUN_FAILED, 0, "", "allocation_failed", 0, 0);
                r.state = STRAPPY_RUN_FAILED;
            } else {
                e->run = r.id; e->session = session; e->cancel = 0;
                pthread_cond_signal(&e->condition);
            }
            strappy_ipc_server_committed(e->server, STRAPPY_IPC_RUN_CHANGED);
        }
    }
    if (reply->status == STRAPPY_IPC_OK) {
        strappy_ipc_put32(reply->payload, (uint32_t)((uint64_t)r.id >> 32));
        strappy_ipc_put32(reply->payload + 4, (uint32_t)r.id);
        strappy_ipc_put32(reply->payload + 8, (uint32_t)r.state);
        reply->length = 12;
    }
done:
    pthread_mutex_unlock(&e->mutex); strappy_run_destroy(&r);
}
strappy_execution *strappy_execution_open(strappy_ipc_server *server, const char *db, const char *resources)
{
    strappy_execution *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->db = strappy_string_duplicate(db); e->resources = strappy_string_duplicate(resources); e->server = server;
    if (!e->db || !e->resources || !strappy_run_reconcile(db)) goto fail;
    if (pthread_mutex_init(&e->mutex, NULL) != 0) goto fail;
    if (pthread_cond_init(&e->condition, NULL) != 0) { pthread_mutex_destroy(&e->mutex); goto fail; }
    strappy_execution_owner = e;
    if (pthread_create(&e->worker, NULL, strappy_execution_work, e) != 0) {
        strappy_execution_owner = NULL; pthread_mutex_destroy(&e->mutex); pthread_cond_destroy(&e->condition); goto fail;
    }
    strappy_ipc_server_handler(server, strappy_execution_command, e);
    return e;
fail:
    free(e->db); free(e->resources); free(e); return NULL;
}
void strappy_execution_stop(strappy_execution *e)
{
    if (!e) return;
    pthread_mutex_lock(&e->mutex); e->stopping = 1;
    pthread_cond_signal(&e->condition); pthread_mutex_unlock(&e->mutex);
}
void strappy_execution_close(strappy_execution *e)
{
    if (!e) return;
    strappy_execution_stop(e); pthread_join(e->worker, NULL);
    strappy_execution_owner = NULL;
    pthread_mutex_destroy(&e->mutex); pthread_cond_destroy(&e->condition);
    free(e->db); free(e->resources); free(e);
}
