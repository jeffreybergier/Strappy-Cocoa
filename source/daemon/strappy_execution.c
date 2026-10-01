#include "strappy_execution.h"
#include "strappy_mutations.h"
#include "../shared/strappy_file_scanner.h"
#include "../shared/strappy_study.h"
#include "../shared/strappy_core.h"
#include "../shared/strappy_session.h"
#include "../shared/strappy_run.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>

struct strappy_execution {
    strappy_ipc_server *server;
    char *db, *resources, *prompt;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    pthread_t worker;
    long long run, session;
    int stopping, cancel;
    unsigned int job, last_job, last_result;
    cJSON *command;
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
static int strappy_scan_progress(const strappy_file_scanner_progress *progress, void *context)
{
    (void)progress; (void)context;
    return !strappy_service_should_cancel();
}
static int strappy_scan_batch(strappy_file_scanner_record_list *list, void *context, char **error)
{
    strappy_execution *e = context;
    if (!strappy_file_scanner_save_discovered_database_batch(e->db,list,
        strappy_command_text(e->command,"path"),error)) return 0;
    strappy_ipc_server_committed(e->server,STRAPPY_IPC_CATALOG_CHANGED);
    return !strappy_service_should_cancel();
}
static int strappy_scan(strappy_execution *e)
{
    strappy_file_scanner_options options;
    strappy_file_scanner_record_list list;
    char *error = NULL; int ok;
    strappy_file_scanner_options_init(&options);
    strappy_file_scanner_record_list_init(&list);
    options.root_path = strappy_command_text(e->command,"path");
    options.platform_profile = (strappy_file_scanner_platform_profile)strappy_service_scanner_profile();
    options.validate_candidates = 1;
    options.use_filename_filter = strappy_command_number(e->command,"quick") == 1;
    options.record_batch_size = 100;
    options.record_batch_callback = strappy_scan_batch; options.record_batch_user_data = e;
    options.progress_callback = strappy_scan_progress;
    ok = strappy_file_scanner_scan_and_save_discovered_databases(e->db,&options,&list,&error);
    free(error); strappy_file_scanner_record_list_destroy(&list);
    return ok;
}
static int strappy_execute_study(strappy_execution *e)
{
    unsigned int stalled = 0;
    while (!strappy_service_should_cancel()) {
        strappy_study_batch batch;
        strappy_study_database_id_list after;
        strappy_session_record record;
        char *error = NULL;
        size_t i, j; int ok, progressed = 0;
        strappy_study_batch_init(&batch);
        if (!strappy_study_next_batch(e->db,&batch,&error)) {
            free(error); strappy_study_batch_destroy(&batch); return 0;
        }
        if (!batch.pending_database_ids.count) { strappy_study_batch_destroy(&batch); return 1; }
        strappy_session_record_init(&record);
        ok = strappy_session_send_isolated_prompt_with_events_and_load(batch.prompt,NULL,NULL,
            e->resources,e->db,e->session,strappy_execution_event,e,&record,&error);
        strappy_session_record_destroy(&record); free(error); error = NULL;
        strappy_study_database_id_list_init(&after);
        if (ok) ok = strappy_study_list_unstudied_database_ids(e->db,&after,&error);
        if (ok) for (i = 0; i < batch.pending_database_ids.count; ++i) {
            for (j = 0; j < after.count; ++j)
                if (!strcmp(batch.pending_database_ids.database_ids[i],after.database_ids[j])) break;
            if (j == after.count) progressed = 1;
        }
        free(error); strappy_study_database_id_list_destroy(&after); strappy_study_batch_destroy(&batch);
        strappy_ipc_server_committed(e->server,STRAPPY_IPC_CATALOG_CHANGED);
        if (!ok || (!progressed && ++stalled >= 3)) return 0;
        if (progressed) stalled = 0;
    }
    return 0;
}
static int strappy_prepare_study(strappy_execution *e, const cJSON *c)
{
    strappy_session_options options, saved;
    strappy_session_option_mask changed;
    strappy_study_database_id_list pending;
    strappy_run_record run;
    char *error = NULL, *model = NULL;
    int ok;
    strappy_study_database_id_list_init(&pending);
    memset(&run,0,sizeof(run));
    strappy_session_options_init(&options); strappy_session_options_init(&saved);
    ok = strappy_study_list_unstudied_database_ids(e->db,&pending,&error) && pending.count > 0 &&
        strappy_session_get_default_model(e->db,&model,&error) &&
        strappy_session_create_with_working_directory(e->db,strappy_command_text(c,"directory"),&e->session,&error);
    if (ok) {
        ok = strappy_session_load_options(e->db,e->session,&options,&error);
        free(options.model_id); options.model_id = model; model = NULL;
        free(options.assistant_set_id);
        options.assistant_set_id = strappy_string_duplicate(STRAPPY_ASSISTANT_SET_DATABASE_STUDY);
        free(options.working_directory);
        options.working_directory = strappy_string_duplicate(strappy_command_text(c,"directory"));
        options.web_provider = STRAPPY_WEB_PROVIDER_AUTO; options.web_search_enabled = 1;
        options.round_limit = STRAPPY_SESSION_DEFAULT_ROUND_LIMIT;
        options.bash_enabled = 0; options.limit_to_one_tool = 0;
        ok = ok && options.assistant_set_id && options.working_directory && strappy_db_update_session_name(e->db,e->session,STRAPPY_ASSISTANT_SET_DATABASE_STUDY_SESSION_NAME,&error) &&
            strappy_session_update_options(e->db,e->session,e->resources,&options,
                STRAPPY_SESSION_OPTION_ALL,&saved,&changed,&error);
        if (ok) ok = strappy_run_accept(e->db,e->session,strappy_command_text(c,"key"),
            "Database Study",&run) == STRAPPY_IPC_OK;
        if (!ok) strappy_session_delete(e->db,e->session,NULL);
        else e->run = run.id;
    }
    strappy_run_destroy(&run); free(model); free(error);
    strappy_session_options_destroy(&options); strappy_session_options_destroy(&saved); strappy_study_database_id_list_destroy(&pending);
    return ok;
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
        while ((!e->job || e->job == STRAPPY_IPC_MUTATE) && !e->stopping) pthread_cond_wait(&e->condition, &e->mutex);
        if (!e->job || e->job == STRAPPY_IPC_MUTATE) { pthread_mutex_unlock(&e->mutex); break; }
        pthread_mutex_unlock(&e->mutex);
        pool = strappy_service_worker_enter();
        strappy_session_record_init(&record);
        if (e->job == STRAPPY_IPC_START_SCAN) ok = strappy_scan(e);
        else if (e->job == STRAPPY_IPC_REFRESH_CATALOG)
            ok = strappy_service_refresh_catalog(e->db,strappy_command_text(e->command,"id"));
        else if (!strappy_service_should_cancel() &&
            strappy_run_update(e->db, e->run, STRAPPY_RUN_RUNNING, 0, "request", "", 0, 0)) {
            strappy_ipc_server_committed(e->server, STRAPPY_IPC_RUN_CHANGED);
            ok = e->job == STRAPPY_IPC_START_STUDY ? strappy_execute_study(e) :
                strappy_session_send_prompt_with_events_and_load(e->prompt, NULL, NULL,
                e->resources, e->db, e->session, strappy_execution_event, e, &record, &error);
        }
        /* Provider/tool diagnostics remain in the normalized ledger. Never log
         * errors here: they can contain provider content or credentials. */
        free(error); strappy_session_record_destroy(&record);
        pthread_mutex_lock(&e->mutex);
        state = e->stopping ? STRAPPY_RUN_INTERRUPTED :
            (e->cancel ? STRAPPY_RUN_CANCELLED : (ok ? STRAPPY_RUN_COMPLETED : STRAPPY_RUN_FAILED));
        if (e->run && strappy_run_update(e->db, e->run, state, e->cancel, "", "", 0, 0))
            strappy_ipc_server_committed(e->server, STRAPPY_IPC_RUN_CHANGED);
        free(e->prompt); e->prompt = NULL; e->run = 0; e->cancel = 0;
        e->last_job = e->job; e->last_result = (unsigned int)state; e->job = 0;
        cJSON_Delete(e->command); e->command = NULL;
        strappy_ipc_server_committed(e->server,STRAPPY_IPC_CATALOG_CHANGED);
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
    if (request->operation != STRAPPY_IPC_SUBMIT_PROMPT && request->operation != STRAPPY_IPC_CANCEL_RUN) {
        cJSON *command = NULL;
        char *result = NULL;
        void *pool = strappy_service_worker_enter();
        reply->status = STRAPPY_IPC_INVALID;
        if (request->operation != STRAPPY_IPC_WORK_STATUS) {
            char json[STRAPPY_IPC_MAX_PAYLOAD + 1];
            const char *end = NULL;
            unsigned int i;
            if (!request->length || memchr(request->payload,0,request->length) ||
                !strappy_utf8_validate((const char *)request->payload,request->length)) goto mutation_done;
            memcpy(json,request->payload,request->length); json[request->length] = 0;
            if (strstr(json,"\\u0000")) goto mutation_done;
            command = cJSON_ParseWithOpts(json,&end,1);
            if (!cJSON_IsObject(command)) goto mutation_done;
            for (i = 0; i < (unsigned int)cJSON_GetArraySize(command); ++i) {
                cJSON *item = cJSON_GetArrayItem(command,(int)i), *earlier;
                if (!cJSON_IsString(item)) goto mutation_done;
                for (earlier = command->child; earlier != item; earlier = earlier->next)
                    if (!strcmp(earlier->string,item->string)) goto mutation_done;
            }
            if ((request->operation == STRAPPY_IPC_SET_SESSION_OPTIONS && strcmp(strappy_command_text(command,"action"),"options")) ||
                (request->operation == STRAPPY_IPC_CREATE_SESSION && strcmp(strappy_command_text(command,"action"),"create_session")) ||
                (request->operation == STRAPPY_IPC_DELETE_SESSION && strcmp(strappy_command_text(command,"action"),"delete_session")) ||
                (request->operation == STRAPPY_IPC_APPROVE_MODEL && strcmp(strappy_command_text(command,"action"),"approve_model")) ||
                (request->operation == STRAPPY_IPC_APPROVE_DATABASE && strcmp(strappy_command_text(command,"action"),"approve_database"))) goto mutation_done;
        } else if (request->length) goto mutation_done;
        pthread_mutex_lock(&e->mutex);
        if (e->stopping) reply->status = STRAPPY_IPC_NOT_READY;
        else if (request->operation == STRAPPY_IPC_WORK_STATUS) {
            strappy_ipc_put32(reply->payload,e->job);
            strappy_ipc_put32(reply->payload+4,e->last_job);
            strappy_ipc_put32(reply->payload+8,e->last_result);
            reply->length = 12; reply->status = STRAPPY_IPC_OK;
        } else if (e->job) reply->status = STRAPPY_IPC_BUSY;
        else if (request->operation == STRAPPY_IPC_START_SCAN || request->operation == STRAPPY_IPC_REFRESH_CATALOG ||
                 request->operation == STRAPPY_IPC_START_STUDY) {
            int valid = 1;
            if (request->operation == STRAPPY_IPC_START_SCAN)
                valid = *strappy_command_text(command,"path") == '/' &&
                    (strappy_command_number(command,"quick") == 0 || strappy_command_number(command,"quick") == 1);
            if (request->operation == STRAPPY_IPC_START_STUDY) {
                const char *key = strappy_command_text(command,"key");
                size_t i;
                valid = strlen(key) == 64;
                for (i = 0; valid && i < 64; ++i) valid = (key[i] >= '0' && key[i] <= '9') || (key[i] >= 'a' && key[i] <= 'f');
                if (valid) valid = strappy_prepare_study(e,command);
            }
            if (valid) {
                char number[32];
                e->command = command; command = NULL; e->job = request->operation; e->cancel = 0;
                if (e->job == STRAPPY_IPC_START_STUDY) {
                    snprintf(number,sizeof(number),"%lld",e->session);
                    reply->length = (uint32_t)strlen(number); memcpy(reply->payload,number,reply->length);
                    strappy_ipc_server_committed(e->server,STRAPPY_IPC_SESSION_CHANGED);
                }
                reply->status = STRAPPY_IPC_OK;
                pthread_cond_signal(&e->condition);
                strappy_ipc_server_committed(e->server,STRAPPY_IPC_CATALOG_CHANGED);
            }
        } else {
            /* Reserve the single runtime owner, then release the mutex so status,
             * shutdown and cancellation remain responsive during Keychain/network work. */
            e->job = STRAPPY_IPC_MUTATE;
            pthread_mutex_unlock(&e->mutex);
            reply->status = strappy_mutation(e->db,e->resources,command,&result) ? STRAPPY_IPC_OK : STRAPPY_IPC_INVALID;
            pthread_mutex_lock(&e->mutex); e->job = 0;
            if (reply->status == STRAPPY_IPC_OK) {
                if (result && strlen(result) <= STRAPPY_IPC_MAX_PAYLOAD) {
                    reply->length = (uint32_t)strlen(result); memcpy(reply->payload,result,reply->length);
                }
                strappy_ipc_server_committed(e->server,STRAPPY_IPC_SESSION_CHANGED);
                strappy_ipc_server_committed(e->server,STRAPPY_IPC_CATALOG_CHANGED);
                strappy_ipc_server_committed(e->server,STRAPPY_IPC_LEDGER_CHANGED);
            }
        }
        pthread_mutex_unlock(&e->mutex);
mutation_done:
        free(result); cJSON_Delete(command); strappy_service_worker_leave(pool); return;
    }
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
        reply->status = strappy_run_accept_if_idle(e->db,session,key,prompt,&r,
            !e->job || e->job == STRAPPY_IPC_SUBMIT_PROMPT);
        if (reply->status != STRAPPY_IPC_OK) goto done;
        if (r.state == STRAPPY_RUN_ACCEPTED && !e->prompt && !e->job) {
            e->prompt = strappy_string_duplicate(prompt);
            if (!e->prompt) {
                strappy_run_update(e->db, r.id, STRAPPY_RUN_FAILED, 0, "", "allocation_failed", 0, 0);
                r.state = STRAPPY_RUN_FAILED;
            } else {
                e->run = r.id; e->session = session; e->cancel = 0; e->job = STRAPPY_IPC_SUBMIT_PROMPT;
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
