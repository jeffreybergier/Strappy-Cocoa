/* Device authorization and credentials remain entirely in the service. Only
 * display metadata and an opaque client request key cross the UI boundary. */
#include "strappy_auth.h"
#include "strappy_mutations.h"
#include "strappy_execution.h"
#include "strappy_openai_oauth.h"
#include "strappy_core.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRAPPY_AUTH_JOBS 16
struct strappy_auth_job {
    struct strappy_auth *owner;
    pthread_t thread;
    int used, done, cancelled;
    char *account, *key, *url, *code, *identity;
    const char *state;
};
struct strappy_auth {
    pthread_mutex_t mutex;
    char *db;
    strappy_ipc_server *server;
    int stopping;
    struct strappy_auth_job jobs[STRAPPY_AUTH_JOBS];
};
static int strappy_auth_cancelled(void *context)
{
    struct strappy_auth_job *job = context;
    int cancelled;
    pthread_mutex_lock(&job->owner->mutex);
    cancelled = job->cancelled || job->owner->stopping;
    pthread_mutex_unlock(&job->owner->mutex);
    return cancelled;
}
static void *strappy_auth_work(void *context)
{
    struct strappy_auth_job *job = context;
    strappy_auth *auth = job->owner;
    strappy_openai_oauth_configuration configuration;
    strappy_openai_oauth_device device;
    strappy_openai_oauth_credentials credentials;
    char *error = NULL, *ignored = NULL;
    int ok;
    void *pool = strappy_service_worker_enter();
    strappy_openai_oauth_default_configuration(&configuration);
    strappy_openai_oauth_device_init(&device);
    strappy_openai_oauth_credentials_init(&credentials);
    ok = strappy_openai_oauth_start_device_authorization(&configuration, &device,
        strappy_auth_cancelled, job, &error);
    pthread_mutex_lock(&auth->mutex);
    if (ok && !job->cancelled && !auth->stopping) {
        job->url = strappy_string_duplicate(configuration.verification_url);
        job->code = strappy_string_duplicate(device.user_code);
        ok = job->url && job->code;
        if (ok) job->state = "awaiting_user";
    } else ok = 0;
    pthread_mutex_unlock(&auth->mutex);
    if (ok) ok = strappy_openai_oauth_complete_device_authorization(&configuration,
        &device, &credentials, strappy_auth_cancelled, job, &error);
    /* Cancellation/sign-out serializes with the credential commit. A cancelled
     * job can never save late credentials after the user's sign-out returns. */
    pthread_mutex_lock(&auth->mutex);
    if (ok && !job->cancelled && !auth->stopping) {
        cJSON *command = cJSON_CreateObject();
        char expiry[32];
        snprintf(expiry,sizeof(expiry),"%lld",credentials.expires_at_milliseconds);
        job->identity = strappy_string_duplicate(credentials.account_id);
        ok = command && job->identity &&
            cJSON_AddStringToObject(command,"action","save_oauth") &&
            cJSON_AddStringToObject(command,"id",job->account) &&
            cJSON_AddStringToObject(command,"access",credentials.access_token) &&
            cJSON_AddStringToObject(command,"refresh",credentials.refresh_token) &&
            cJSON_AddStringToObject(command,"identity",credentials.account_id) &&
            cJSON_AddStringToObject(command,"expiry",expiry) &&
            strappy_service_account_command(auth->db,command,&ignored);
        if (command) {
            cJSON *item;
            for (item=command->child; item; item=item->next)
                if (item->valuestring) memset(item->valuestring,0,strlen(item->valuestring));
        }
        cJSON_Delete(command);
        if (ok && auth->server) strappy_ipc_server_committed(auth->server,STRAPPY_IPC_CATALOG_CHANGED);
    } else ok = 0;
    job->state = job->cancelled || auth->stopping ? "cancelled" : (ok ? "signed_in" : "failed");
    if (!ok) { free(job->identity); job->identity = NULL; }
    pthread_mutex_unlock(&auth->mutex);
    free(error); free(ignored);
    strappy_openai_oauth_credentials_destroy(&credentials);
    strappy_openai_oauth_device_destroy(&device);
    strappy_service_worker_leave(pool);
    pthread_mutex_lock(&auth->mutex); job->done = 1; pthread_mutex_unlock(&auth->mutex);
    return NULL;
}
strappy_auth *strappy_auth_open(const char *db, strappy_ipc_server *server)
{
    strappy_auth *auth = calloc(1,sizeof(*auth));
    if (!auth) return NULL;
    auth->db = strappy_string_duplicate(db); auth->server = server;
    if (!auth->db || pthread_mutex_init(&auth->mutex,NULL)) { free(auth->db); free(auth); return NULL; }
    return auth;
}
static void strappy_auth_job_clear(struct strappy_auth_job *job)
{
    pthread_join(job->thread,NULL);
    free(job->account); free(job->key); free(job->url); free(job->code); free(job->identity);
    memset(job,0,sizeof(*job));
}
unsigned int strappy_auth_command(strappy_auth *auth, const cJSON *command, char **result)
{
    const char *action = strappy_command_text(command,"action");
    const char *account = strappy_command_text(command,"id");
    const char *key = strappy_command_text(command,"key");
    struct strappy_auth_job *job = NULL, *slot = NULL;
    unsigned int status = STRAPPY_IPC_INVALID;
    int i;
    cJSON *snapshot;
    *result = NULL;
    if (!auth || !*account) return status;
    if (!strcmp(action,"credential_status")) {
        int ok = strappy_service_account_command(auth->db,command,result);
        if (ok && *result && strlen(*result) <= STRAPPY_IPC_MAX_PAYLOAD) return STRAPPY_IPC_OK;
        free(*result); *result = NULL; return STRAPPY_IPC_INVALID;
    }
    if (strcmp(action,"auth_start") && strcmp(action,"auth_status") && strcmp(action,"auth_cancel")) return status;
    if (strlen(key) != 64 || strspn(key,"0123456789abcdef") != 64) return status;
    pthread_mutex_lock(&auth->mutex);
    if (auth->stopping) { status = STRAPPY_IPC_NOT_READY; goto done; }
    for (i=0; i<STRAPPY_AUTH_JOBS; ++i) {
        struct strappy_auth_job *candidate = &auth->jobs[i];
        if (candidate->used && !strcmp(candidate->key,key)) {
            if (strcmp(candidate->account,account)) { status = STRAPPY_IPC_CONFLICT; goto done; }
            job = candidate;
        }
        if (!candidate->used || candidate->done) slot = candidate;
    }
    if (!job && !strcmp(action,"auth_start")) {
        strappy_provider_account_record record;
        int valid;
        for (i=0; i<STRAPPY_AUTH_JOBS; ++i)
            if (auth->jobs[i].used && !auth->jobs[i].done && !strcmp(auth->jobs[i].account,account)) {
                status = STRAPPY_IPC_BUSY; goto done;
            }
        if (!slot) { status = STRAPPY_IPC_BUSY; goto done; }
        strappy_provider_account_record_init(&record);
        valid = strappy_db_get_provider_account(auth->db,account,&record,NULL) &&
            !strcmp(record.provider_id,STRAPPY_PROVIDER_OPENAI_CHATGPT) &&
            !strcmp(record.lifecycle_state,STRAPPY_PROVIDER_ACCOUNT_ACTIVE);
        strappy_provider_account_record_destroy(&record);
        if (!valid) goto done;
        if (slot->used) strappy_auth_job_clear(slot);
        job = slot; job->owner = auth;
        job->account = strappy_string_duplicate(account); job->key = strappy_string_duplicate(key);
        job->state = "requesting_code";
        if (!job->account || !job->key || pthread_create(&job->thread,NULL,strappy_auth_work,job)) {
            free(job->account); free(job->key); memset(job,0,sizeof(*job)); goto done;
        }
        job->used = 1;
    }
    if (!job) goto done;
    if (!strcmp(action,"auth_cancel")) {
        job->cancelled = 1; job->state = "cancelled";
        free(job->identity); job->identity = NULL;
    }
    snapshot = cJSON_CreateObject();
    if (snapshot && cJSON_AddStringToObject(snapshot,"state",job->state) &&
        cJSON_AddStringToObject(snapshot,"url",job->url ? job->url : "") &&
        cJSON_AddStringToObject(snapshot,"code",job->code ? job->code : "") &&
        cJSON_AddStringToObject(snapshot,"identity",job->identity ? job->identity : ""))
        *result = cJSON_PrintUnformatted(snapshot);
    cJSON_Delete(snapshot);
    if (*result && strlen(*result) <= STRAPPY_IPC_MAX_PAYLOAD) status = STRAPPY_IPC_OK;
    else { free(*result); *result = NULL; }
done:
    pthread_mutex_unlock(&auth->mutex); return status;
}
void strappy_auth_cancel_account(strappy_auth *auth, const char *account)
{
    int i;
    pthread_mutex_lock(&auth->mutex);
    for (i=0; i<STRAPPY_AUTH_JOBS; ++i)
        if (auth->jobs[i].used && !strcmp(auth->jobs[i].account,account)) {
            auth->jobs[i].cancelled = 1; auth->jobs[i].state = "cancelled";
            free(auth->jobs[i].identity); auth->jobs[i].identity = NULL;
        }
    pthread_mutex_unlock(&auth->mutex);
}
void strappy_auth_stop(strappy_auth *auth)
{
    if (!auth) return;
    pthread_mutex_lock(&auth->mutex); auth->stopping = 1; pthread_mutex_unlock(&auth->mutex);
}
void strappy_auth_close(strappy_auth *auth)
{
    int i;
    if (!auth) return;
    strappy_auth_stop(auth);
    for (i=0; i<STRAPPY_AUTH_JOBS; ++i) if (auth->jobs[i].used) strappy_auth_job_clear(&auth->jobs[i]);
    pthread_mutex_destroy(&auth->mutex); free(auth->db); free(auth);
}
