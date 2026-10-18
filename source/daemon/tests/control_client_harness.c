/* Production client against a deterministic transport peer; no provider or DB. */
#include "strappy_store_client.h"
#include "strappy_ipc.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static pthread_t main_thread;
int __real_strappy_ipc_send(int fd, const strappy_ipc_message *message, int timeout);
int __wrap_strappy_ipc_send(int fd, const strappy_ipc_message *message, int timeout)
{
    if (message->operation == STRAPPY_IPC_MUTATE &&
        strstr((const char *)message->payload,"\"fixture\""))
        assert(pthread_equal(pthread_self(),main_thread));
    return __real_strappy_ipc_send(fd,message,timeout);
}
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static int ready;
static void changed(unsigned int topics, long long session)
{
    (void)session; (void)topics;
    pthread_mutex_lock(&mutex); ready = 1;
    pthread_cond_signal(&condition); pthread_mutex_unlock(&mutex);
}
static void *refresh(void *unused)
{
    unsigned int status;
    char *result;
    cJSON *command = cJSON_CreateObject();
    (void)unused;
    cJSON_AddStringToObject(command,"action","refresh_oauth");
    cJSON_AddStringToObject(command,"id","fixture-account");
    result = strappy_store_client_credential(command,&status);
    assert(result && status == STRAPPY_IPC_OK);
    free(result);
    result = strappy_store_client_credential(command,&status);
    assert(!result && status == STRAPPY_IPC_NOT_READY); /* daemon forgot job */
    result = strappy_store_client_credential(command,&status);
    assert(!result && status == STRAPPY_IPC_INVALID); /* definite terminal failure */
    cJSON_Delete(command); return NULL;
}
static void *background(void *unused)
{
    unsigned int status;
    cJSON *command = cJSON_CreateObject();
    char *result;
    (void)unused;
    cJSON_AddStringToObject(command,"action","background_fixture");
    result = strappy_store_client_mutation(STRAPPY_IPC_MUTATE,command,&status);
    assert(result && status == STRAPPY_IPC_OK);
    free(result); cJSON_Delete(command); return NULL;
}
static void *maintenance(void *unused)
{
    char key[65];
    const unsigned int expected[] = {4,3,0,4,3};
    unsigned int i;
    (void)unused;
    key[64] = 0;
    for (i = 0; i < 5; ++i) {
        memset(key,(int)('a'+i),64);
        assert(strappy_store_client_wait_maintenance(key) == expected[i]);
    }
    return NULL;
}
int main(int argc, char **argv)
{
    unsigned int status, i;
    pthread_t refresher, background_worker;
    cJSON *command;
    char *result;
    main_thread = pthread_self();
    assert(argc == 3);
    assert(strappy_store_client_start(argv[1],argv[2],changed));
    pthread_mutex_lock(&mutex);
    while (!ready) pthread_cond_wait(&condition,&mutex);
    pthread_mutex_unlock(&mutex);
    assert(pthread_create(&refresher,NULL,refresh,NULL) == 0);
    assert(pthread_create(&background_worker,NULL,background,NULL) == 0);
    command = cJSON_CreateObject();
    cJSON_AddStringToObject(command,"action","fixture");
    for (i = 0; i < 32; ++i) {
        result = strappy_store_client_mutation(STRAPPY_IPC_MUTATE,command,&status);
        assert(result && status == STRAPPY_IPC_OK && !strcmp(result,"ok")); free(result);
    }
    /* A lost mutation acknowledgement must not replay that mutation. */
    result = strappy_store_client_mutation(STRAPPY_IPC_MUTATE,command,&status);
    assert(!result && status == STRAPPY_IPC_NOT_READY);
    result = strappy_store_client_mutation(STRAPPY_IPC_MUTATE,command,&status);
    assert(result && status == STRAPPY_IPC_OK); free(result);
    cJSON_Delete(command);
    /* Production client returns unknown acceptance without automatic replay.
     * An explicit retry sends the same maximum-sized body and request key. */
    {
        unsigned int length = STRAPPY_IPC_PROMPT_PREFIX + STRAPPY_IPC_MAX_PROMPT_BYTES;
        unsigned char *payload = malloc(length);
        assert(payload);
        memset(payload,'x',length);
        strappy_ipc_put32(payload,0); strappy_ipc_put32(payload+4,1);
        memset(payload+8,'a',64);
        assert(!strappy_store_client_command(STRAPPY_IPC_SUBMIT_PROMPT,payload,length,&status));
        assert(strappy_store_client_command(STRAPPY_IPC_SUBMIT_PROMPT,payload,length,&status));
        assert(status == STRAPPY_IPC_OK);
        free(payload);
    }
    pthread_t maintenance_worker;
    assert(pthread_create(&maintenance_worker,NULL,maintenance,NULL) == 0);
    pthread_join(maintenance_worker,NULL);
    pthread_join(refresher,NULL);
    pthread_join(background_worker,NULL);
    puts("maintenance results: immediate failure/success, unknown, lost read reply and running-to-terminal passed");
    puts("control client: caller-thread transport, independent background connection, persistent connection, ordered replies, no mutation replay, reconnect, credential acceptance recovery, restart unknown, terminal failure and no credential replay passed");
    return 0;
}
