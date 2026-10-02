/* Production client against a deterministic transport peer; no provider or DB. */
#include "strappy_store_client.h"
#include "strappy_ipc.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static int ready;
static void changed(unsigned int topics)
{
    (void)topics;
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
    result = strappy_store_client_mutation(STRAPPY_IPC_MUTATE,command,&status);
    assert(result && status == STRAPPY_IPC_OK);
    free(result); cJSON_Delete(command); return NULL;
}
int main(int argc, char **argv)
{
    unsigned int status, i;
    pthread_t refresher;
    cJSON *command;
    char *result;
    assert(argc == 3);
    assert(strappy_store_client_start(argv[1],argv[2],changed));
    pthread_mutex_lock(&mutex);
    while (!ready) pthread_cond_wait(&condition,&mutex);
    pthread_mutex_unlock(&mutex);
    assert(pthread_create(&refresher,NULL,refresh,NULL) == 0);
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
    pthread_join(refresher,NULL);
    puts("control client: persistent connection, ordered replies, no mutation replay, reconnect, independent OAuth lane passed");
    return 0;
}
