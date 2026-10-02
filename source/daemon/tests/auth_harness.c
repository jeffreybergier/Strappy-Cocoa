/* Test the real daemon authorization manager with deterministic, cancellable
 * provider and credential-store boundaries. No network or user Keychain. */
#include "strappy_auth.h"
#include "strappy_openai_oauth.h"
#include "strappy_db.h"
#include "strappy_core.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t fixture_mutex = PTHREAD_MUTEX_INITIALIZER;
static int starts, saves, finish, fail;
static void pause_fixture(void) { struct timespec delay = {0,1000000}; nanosleep(&delay,NULL); }
const char *strappy_command_text(const cJSON *c, const char *key)
{
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(c,key);
    return cJSON_IsString(v) ? v->valuestring : "";
}
void *strappy_service_worker_enter(void) { return NULL; }
void strappy_service_worker_leave(void *pool) { (void)pool; }
int strappy_ipc_server_committed(strappy_ipc_server *server, uint32_t event)
{ (void)server; (void)event; return 1; }
int strappy_service_account_command(const char *db,const cJSON *c,char **result)
{
    (void)db; (void)result;
    assert(!strcmp(strappy_command_text(c,"action"),"save_oauth"));
    assert(!strcmp(strappy_command_text(c,"access"),"SECRET_ACCESS"));
    assert(!strcmp(strappy_command_text(c,"refresh"),"SECRET_REFRESH"));
    pthread_mutex_lock(&fixture_mutex); ++saves; pthread_mutex_unlock(&fixture_mutex);
    return 1;
}
void strappy_openai_oauth_default_configuration(strappy_openai_oauth_configuration *c)
{ memset(c,0,sizeof(*c)); c->verification_url="https://example.invalid/verify"; }
void strappy_openai_oauth_device_init(strappy_openai_oauth_device *d) { memset(d,0,sizeof(*d)); }
void strappy_openai_oauth_device_destroy(strappy_openai_oauth_device *d) { free(d->device_auth_id); free(d->user_code); }
void strappy_openai_oauth_credentials_init(strappy_openai_oauth_credentials *c) { memset(c,0,sizeof(*c)); }
void strappy_openai_oauth_credentials_destroy(strappy_openai_oauth_credentials *c)
{ free(c->access_token); free(c->refresh_token); free(c->account_id); }
int strappy_openai_oauth_start_device_authorization(const strappy_openai_oauth_configuration *c,
    strappy_openai_oauth_device *d,strappy_openai_oauth_cancel_callback cancel,void *context,char **error)
{
    (void)c; (void)error;
    pthread_mutex_lock(&fixture_mutex); ++starts; pthread_mutex_unlock(&fixture_mutex);
    if (cancel(context)) return 0;
    d->user_code=strappy_string_duplicate("TEST-CODE");
    d->device_auth_id=strappy_string_duplicate("SECRET_DEVICE");
    return 1;
}
int strappy_openai_oauth_complete_device_authorization(const strappy_openai_oauth_configuration *c,
    const strappy_openai_oauth_device *d,strappy_openai_oauth_credentials *out,
    strappy_openai_oauth_cancel_callback cancel,void *context,char **error)
{
    int ready, failure;
    (void)c; (void)d; (void)error;
    for (;;) {
        if (cancel(context)) return 0;
        pthread_mutex_lock(&fixture_mutex); ready=finish; failure=fail; pthread_mutex_unlock(&fixture_mutex);
        if (ready) break;
        pause_fixture();
    }
    if (failure) return 0;
    out->access_token=strappy_string_duplicate("SECRET_ACCESS");
    out->refresh_token=strappy_string_duplicate("SECRET_REFRESH");
    out->account_id=strappy_string_duplicate("fixture-identity"); out->expires_at_milliseconds=1234567890000LL;
    return 1;
}
static cJSON *call(strappy_auth *auth,const char *action,const char *account,int key,unsigned int expected)
{
    cJSON *command=cJSON_CreateObject(), *snapshot=NULL;
    char retry[65], *result=NULL;
    memset(retry,'a'+key,64); retry[64]=0;
    cJSON_AddStringToObject(command,"action",action); cJSON_AddStringToObject(command,"id",account);
    cJSON_AddStringToObject(command,"key",retry);
    assert(strappy_auth_command(auth,command,&result)==expected);
    if (result) {
        assert(!strstr(result,"SECRET") && !strstr(result,"access_token") && !strstr(result,"refresh_token"));
        snapshot=cJSON_Parse(result); assert(snapshot);
    }
    free(result); cJSON_Delete(command); return snapshot;
}
static void wait_state(strappy_auth *auth,const char *account,int key,const char *state)
{
    int i;
    for (i=0;i<5000;++i) {
        cJSON *snapshot=call(auth,"auth_status",account,key,STRAPPY_IPC_OK);
        int match=!strcmp(strappy_command_text(snapshot,"state"),state);
        cJSON_Delete(snapshot);
        if (match) return;
        pause_fixture();
    }
    assert(0 && "authorization state timed out");
}
int main(void)
{
    char path[]="/tmp/strappy-auth-XXXXXX",db[512],*account=NULL,*other=NULL;
    strappy_auth *auth;
    int total;
    assert(mkdtemp(path)); snprintf(db,sizeof(db),"%s/strappy.sqlite",path);
    assert(strappy_db_initialize(db,NULL));
    assert(strappy_db_create_provider_account(db,"openai_chatgpt","Auth fixture",NULL,&account,NULL));
    assert(strappy_db_create_provider_account(db,"other","Other fixture",NULL,&other,NULL));
    auth=strappy_auth_open(db,NULL); assert(auth);
    cJSON_Delete(call(auth,"auth_start",other,0,STRAPPY_IPC_INVALID));
    cJSON_Delete(call(auth,"auth_start",account,0,STRAPPY_IPC_OK));
    wait_state(auth,account,0,"awaiting_user");
    cJSON_Delete(call(auth,"auth_start",account,0,STRAPPY_IPC_OK));
    cJSON_Delete(call(auth,"auth_start",account,1,STRAPPY_IPC_BUSY));
    cJSON_Delete(call(auth,"auth_status",other,0,STRAPPY_IPC_CONFLICT));
    pthread_mutex_lock(&fixture_mutex); assert(starts==1); finish=1; pthread_mutex_unlock(&fixture_mutex);
    wait_state(auth,account,0,"signed_in");
    cJSON_Delete(call(auth,"auth_start",account,0,STRAPPY_IPC_OK));
    strappy_auth_close(auth);
    pthread_mutex_lock(&fixture_mutex); assert(saves==1 && starts==1); finish=0; pthread_mutex_unlock(&fixture_mutex);
    auth=strappy_auth_open(db,NULL);
    cJSON_Delete(call(auth,"auth_start",account,1,STRAPPY_IPC_OK));
    wait_state(auth,account,1,"awaiting_user");
    cJSON_Delete(call(auth,"auth_cancel",account,1,STRAPPY_IPC_OK));
    wait_state(auth,account,1,"cancelled"); strappy_auth_close(auth);
    pthread_mutex_lock(&fixture_mutex); assert(saves==1); pthread_mutex_unlock(&fixture_mutex);
    auth=strappy_auth_open(db,NULL);
    cJSON_Delete(call(auth,"auth_start",account,2,STRAPPY_IPC_OK));
    wait_state(auth,account,2,"awaiting_user"); strappy_auth_cancel_account(auth,account);
    wait_state(auth,account,2,"cancelled"); strappy_auth_close(auth);
    auth=strappy_auth_open(db,NULL);
    cJSON_Delete(call(auth,"auth_start",account,3,STRAPPY_IPC_OK));
    wait_state(auth,account,3,"awaiting_user"); strappy_auth_stop(auth);
    cJSON_Delete(call(auth,"auth_start",account,4,STRAPPY_IPC_NOT_READY)); strappy_auth_close(auth);
    pthread_mutex_lock(&fixture_mutex); assert(saves==1); finish=1; fail=1; pthread_mutex_unlock(&fixture_mutex);
    auth=strappy_auth_open(db,NULL);
    cJSON_Delete(call(auth,"auth_start",account,4,STRAPPY_IPC_OK));
    wait_state(auth,account,4,"failed"); strappy_auth_close(auth);
    pthread_mutex_lock(&fixture_mutex); total=saves; pthread_mutex_unlock(&fixture_mutex); assert(total==1);
    free(account); free(other); strappy_db_shutdown_connection();
    unlink(db); snprintf(db,sizeof(db),"%s/strappy.sqlite-wal",path); unlink(db);
    snprintf(db,sizeof(db),"%s/strappy.sqlite-shm",path); unlink(db); rmdir(path);
    puts("auth_harness: acceptance, deduplication, isolation, cancellation, sign-out, shutdown, failure and secret-free replies passed");
    return 0;
}
