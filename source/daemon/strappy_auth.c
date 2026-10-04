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
#include <syslog.h>

#define STRAPPY_AUTH_JOBS 64
struct strappy_auth_lane {
    struct strappy_auth_lane *next;
    char *account;
    pthread_mutex_t mutex;
};
struct strappy_auth_job {
    struct strappy_auth *owner;
    pthread_t thread;
    int used, done, cancelled, credential;
    unsigned long long order;
    struct strappy_auth_lane *lane;
    cJSON *command;
    char *result;
    char *account, *key, *url, *code, *identity;
    const char *state;
};
struct strappy_auth {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    unsigned long long order;
    struct strappy_auth_lane *lanes;
    char *db;
    strappy_ipc_server *server;
    int stopping;
    struct strappy_auth_job jobs[STRAPPY_AUTH_JOBS];
};
static void strappy_auth_command_destroy(cJSON *command)
{
    cJSON *item;
    for (item = command ? command->child : NULL; item; item = item->next)
        if (item->valuestring) memset(item->valuestring,0,strlen(item->valuestring));
    cJSON_Delete(command);
}
static int strappy_auth_credential_action(const char *action)
{
    return !strcmp(action,"credential_status") || !strcmp(action,"update_account") ||
        !strcmp(action,"archive_account") || !strcmp(action,"delete_oauth") ||
        !strcmp(action,"refresh_oauth");
}
/* auth->mutex protects only in-memory state. Each account has a separate commit
 * lock; Keychain and provider calls must never run under the global mutex. */
static struct strappy_auth_lane *strappy_auth_lane_get(strappy_auth *auth, const char *account)
{
    struct strappy_auth_lane *lane;
    for (lane=auth->lanes; lane; lane=lane->next)
        if (!strcmp(lane->account,account)) return lane;
    lane=calloc(1,sizeof(*lane));
    if (!lane) return NULL;
    lane->account=strappy_string_duplicate(account);
    if (!lane->account || pthread_mutex_init(&lane->mutex,NULL)) {
        free(lane->account); free(lane); return NULL;
    }
    lane->next=auth->lanes; auth->lanes=lane; return lane;
}
static void strappy_auth_cancel_locked(strappy_auth *auth, const char *account)
{
    int i;
    for (i=0; i<STRAPPY_AUTH_JOBS; ++i) {
        struct strappy_auth_job *job=&auth->jobs[i];
        if (job->used && !job->credential && !job->done && !strcmp(job->account,account)) {
            job->cancelled=1; job->state="cancelled";
            free(job->identity); job->identity=NULL;
        }
    }
}
static void *strappy_auth_credential_work(void *context)
{
    struct strappy_auth_job *job=context;
    strappy_auth *auth=job->owner;
    int i, waiting, ok=0;
    char *result=NULL;
    void *pool=strappy_service_worker_enter();
    pthread_mutex_lock(&auth->mutex);
    do {
        waiting=0;
        for (i=0; i<STRAPPY_AUTH_JOBS; ++i) {
            struct strappy_auth_job *other=&auth->jobs[i];
            if (other->used && !other->done && other->credential &&
                other->lane==job->lane && other->order<job->order) waiting=1;
        }
        if (waiting && !auth->stopping) pthread_cond_wait(&auth->condition,&auth->mutex);
    } while (waiting && !auth->stopping);
    if (!auth->stopping) {
        job->state="running";
        pthread_mutex_unlock(&auth->mutex);
        pthread_mutex_lock(&job->lane->mutex);
        ok=strappy_service_account_command(auth->db,job->command,&result);
        pthread_mutex_unlock(&job->lane->mutex);
        if (ok && strcmp(strappy_command_text(job->command,"action"),"credential_status") && auth->server)
            strappy_ipc_server_committed(auth->server,STRAPPY_IPC_CATALOG_CHANGED);
        pthread_mutex_lock(&auth->mutex);
    }
    job->result=result;
    job->state=ok ? "completed" : "failed";
    strappy_auth_command_destroy(job->command); job->command=NULL;
    /* Leave the pool before publishing done: a reusable slot must be joinable
     * without waiting for code that needs the auth mutex. */
    pthread_mutex_unlock(&auth->mutex);
    strappy_service_worker_leave(pool);
    pthread_mutex_lock(&auth->mutex);
    job->done=1; pthread_cond_broadcast(&auth->condition);
    pthread_mutex_unlock(&auth->mutex);
    return NULL;
}
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
    const char *phase = "request_code";
    int ok, cancelled;
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
    if (ok) {
        syslog(LOG_NOTICE, "Strappy OAuth: verification code issued");
        phase = "poll_or_exchange";
        ok = strappy_openai_oauth_complete_device_authorization(&configuration,
            &device, &credentials, strappy_auth_cancelled, job, &error);
    }
    /* A sign-out accepted before this lock/check cancels the save. If the save
     * has already started, deletion waits on this account's lock and runs last. */
    pthread_mutex_lock(&job->lane->mutex);
    pthread_mutex_lock(&auth->mutex);
    ok = ok && !job->cancelled && !auth->stopping;
    pthread_mutex_unlock(&auth->mutex);
    if (ok) {
        phase = "save_credentials";
        cJSON *command = cJSON_CreateObject();
        char expiry[32];
        snprintf(expiry,sizeof(expiry),"%lld",credentials.expires_at_milliseconds);
        ok = command &&
            cJSON_AddStringToObject(command,"action","save_oauth") &&
            cJSON_AddStringToObject(command,"id",job->account) &&
            cJSON_AddStringToObject(command,"access",credentials.access_token) &&
            cJSON_AddStringToObject(command,"refresh",credentials.refresh_token) &&
            cJSON_AddStringToObject(command,"identity",credentials.account_id) &&
            cJSON_AddStringToObject(command,"expiry",expiry) &&
            strappy_service_account_command(auth->db,command,&ignored);
        strappy_auth_command_destroy(command);
        if (ok && auth->server) strappy_ipc_server_committed(auth->server,STRAPPY_IPC_CATALOG_CHANGED);
    }
    pthread_mutex_unlock(&job->lane->mutex);
    pthread_mutex_lock(&auth->mutex);
    cancelled = job->cancelled || auth->stopping;
    job->state = cancelled ? "cancelled" : (ok ? "signed_in" : "failed");
    if (ok && !cancelled)
        job->identity=strappy_string_duplicate(credentials.account_id);
    pthread_mutex_unlock(&auth->mutex);
    if (cancelled)
        syslog(LOG_NOTICE, "Strappy OAuth: device flow cancelled");
    else if (ok)
        syslog(LOG_NOTICE, "Strappy OAuth: credentials stored");
    else if (strcmp(phase,"save_credentials") == 0)
        syslog(LOG_ERR, "Strappy OAuth: credential save failed");
    else
        syslog(LOG_ERR, "Strappy OAuth: %s failed: %s", phase,
            error ? error : "unknown error");
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
    if (pthread_cond_init(&auth->condition,NULL)) {
        pthread_mutex_destroy(&auth->mutex); free(auth->db); free(auth); return NULL;
    }
    return auth;
}
static void strappy_auth_job_clear(struct strappy_auth_job *job)
{
    pthread_join(job->thread,NULL);
    free(job->account); free(job->key); free(job->url); free(job->code); free(job->identity);
    free(job->result); strappy_auth_command_destroy(job->command);
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
    if (strcmp(action,"credential_start") && strcmp(action,"auth_start") &&
        strcmp(action,"auth_status") && strcmp(action,"auth_cancel")) return status;
    if (!strcmp(action,"credential_start") &&
        !strappy_auth_credential_action(strappy_command_text(command,"credential_action"))) return status;
    if (strlen(key) != 64 || strspn(key,"0123456789abcdef") != 64) return status;
    pthread_mutex_lock(&auth->mutex);
    if (auth->stopping) { status = STRAPPY_IPC_NOT_READY; goto done; }
    for (i=0; i<STRAPPY_AUTH_JOBS; ++i) {
        struct strappy_auth_job *candidate = &auth->jobs[i];
        if (candidate->used && !strcmp(candidate->key,key)) {
            if (strcmp(candidate->account,account)) { status = STRAPPY_IPC_CONFLICT; goto done; }
            job = candidate;
        }
        if (!candidate->used) slot = candidate;
        else if (candidate->done && (!slot || (slot->used && candidate->order<slot->order))) slot=candidate;
    }
    if (job && !strcmp(action,"credential_start")) { status=STRAPPY_IPC_CONFLICT; goto done; }
    if (!job && (!strcmp(action,"auth_start") || !strcmp(action,"credential_start"))) {
        int credential=!strcmp(action,"credential_start");
        strappy_provider_account_record record;
        int valid;
        for (i=0; i<STRAPPY_AUTH_JOBS; ++i)
            if (!credential && auth->jobs[i].used && !auth->jobs[i].done && !strcmp(auth->jobs[i].account,account)) {
                status = STRAPPY_IPC_BUSY; goto done;
            }
        if (!slot) { status = STRAPPY_IPC_BUSY; goto done; }
        strappy_provider_account_record_init(&record);
        valid = strappy_db_get_provider_account(auth->db,account,&record,NULL) &&
            (credential || !strcmp(record.provider_id,STRAPPY_PROVIDER_OPENAI_CHATGPT)) &&
            !strcmp(record.lifecycle_state,STRAPPY_PROVIDER_ACCOUNT_ACTIVE);
        strappy_provider_account_record_destroy(&record);
        if (!valid) goto done;
        if (slot->used) strappy_auth_job_clear(slot);
        job = slot; job->owner = auth;
        job->account = strappy_string_duplicate(account); job->key = strappy_string_duplicate(key);
        job->credential=credential; job->order=++auth->order;
        job->lane=strappy_auth_lane_get(auth,account);
        job->state = credential ? "queued" : "requesting_code";
        if (credential) {
            job->command=cJSON_Duplicate(command,1);
            if (job->command && !cJSON_ReplaceItemInObjectCaseSensitive(job->command,"action",
                cJSON_CreateString(strappy_command_text(command,"credential_action")))) {
                strappy_auth_command_destroy(job->command); job->command=NULL;
            }
        }
        if (!job->account || !job->key || !job->lane || (credential && !job->command) ||
            pthread_create(&job->thread,NULL,credential ? strappy_auth_credential_work : strappy_auth_work,job)) {
            strappy_auth_command_destroy(job->command);
            free(job->account); free(job->key); memset(job,0,sizeof(*job)); goto done;
        }
        job->used = 1;
        if (credential && (!strcmp(strappy_command_text(command,"credential_action"),"delete_oauth") ||
            !strcmp(strappy_command_text(command,"credential_action"),"archive_account")))
            strappy_auth_cancel_locked(auth,account);
    }
    if (!job) {
        if (!strcmp(action,"auth_status")) {
            *result=strappy_string_duplicate("{\"state\":\"unknown\"}");
            if (*result) status=STRAPPY_IPC_OK;
        }
        goto done;
    }
    if (!strcmp(action,"auth_cancel") && !job->credential) {
        job->cancelled = 1; job->state = "cancelled";
        syslog(LOG_NOTICE, "Strappy OAuth: UI requested cancellation");
        free(job->identity); job->identity = NULL;
    }
    snapshot = cJSON_CreateObject();
    if (snapshot && cJSON_AddStringToObject(snapshot,"state",job->state) &&
        cJSON_AddStringToObject(snapshot,"url",job->url ? job->url : "") &&
        cJSON_AddStringToObject(snapshot,"code",job->code ? job->code : "") &&
        cJSON_AddStringToObject(snapshot,"identity",job->identity ? job->identity : "") &&
        cJSON_AddStringToObject(snapshot,"result",job->result ? job->result : ""))
        *result = cJSON_PrintUnformatted(snapshot);
    cJSON_Delete(snapshot);
    if (*result && strlen(*result) <= STRAPPY_IPC_MAX_PAYLOAD) status = STRAPPY_IPC_OK;
    else { free(*result); *result = NULL; status = STRAPPY_IPC_NOT_READY; }
done:
    pthread_mutex_unlock(&auth->mutex); return status;
}
void strappy_auth_cancel_account(strappy_auth *auth, const char *account)
{
    pthread_mutex_lock(&auth->mutex);
    strappy_auth_cancel_locked(auth,account);
    pthread_mutex_unlock(&auth->mutex);
}
void strappy_auth_stop(strappy_auth *auth)
{
    if (!auth) return;
    pthread_mutex_lock(&auth->mutex); auth->stopping = 1;
    pthread_cond_broadcast(&auth->condition); pthread_mutex_unlock(&auth->mutex);
}
void strappy_auth_close(strappy_auth *auth)
{
    int i;
    if (!auth) return;
    strappy_auth_stop(auth);
    for (i=0; i<STRAPPY_AUTH_JOBS; ++i) if (auth->jobs[i].used) pthread_join(auth->jobs[i].thread,NULL);
    for (i=0; i<STRAPPY_AUTH_JOBS; ++i) if (auth->jobs[i].used) {
        struct strappy_auth_job *job=&auth->jobs[i];
        free(job->account); free(job->key); free(job->url); free(job->code); free(job->identity);
        free(job->result); strappy_auth_command_destroy(job->command);
    }
    while (auth->lanes) {
        struct strappy_auth_lane *lane=auth->lanes; auth->lanes=lane->next;
        pthread_mutex_destroy(&lane->mutex); free(lane->account); free(lane);
    }
    pthread_cond_destroy(&auth->condition);
    pthread_mutex_destroy(&auth->mutex); free(auth->db); free(auth);
}
