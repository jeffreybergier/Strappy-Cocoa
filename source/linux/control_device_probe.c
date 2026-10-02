/* Run against an isolated fixture daemon, never the user's application store. */
#include "strappy_store_client.h"
#include "strappy_ipc.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static int ready;
static void changed(unsigned int topics)
{
    (void)topics;
    pthread_mutex_lock(&mutex); ready = 1; pthread_cond_signal(&condition); pthread_mutex_unlock(&mutex);
}
static char *command(const char *json)
{
    unsigned int status;
    cJSON *object = cJSON_Parse(json);
    char *result = strappy_store_client_mutation(STRAPPY_IPC_MUTATE,object,&status);
    cJSON_Delete(object);
    assert(result && status == STRAPPY_IPC_OK);
    return result;
}
static double milliseconds(void)
{
    struct timeval now;
    gettimeofday(&now,NULL);
    return (double)now.tv_sec*1000.0+(double)now.tv_usec/1000.0;
}
int main(int argc, char **argv)
{
    char json[4096], *account, *model, *session, *result;
    int i;
    double start, elapsed, maximum = 0, total = 0;
    assert(argc == 4);
    assert(!strchr(argv[3],'"') && !strchr(argv[3],'\\'));
    assert(strappy_store_client_start(argv[1],argv[2],changed));
    pthread_mutex_lock(&mutex);
    while (!ready) pthread_cond_wait(&condition,&mutex);
    pthread_mutex_unlock(&mutex);
    account = command("{\"action\":\"create_account\",\"id\":\"other\",\"name\":\"Control fixture\"}");
    model = command("{\"action\":\"create_model\",\"id\":\"other\",\"wire\":\"control-fixture\",\"name\":\"Control fixture\",\"context\":\"8192\",\"output\":\"4096\",\"reasoning\":\"0\",\"image\":\"0\",\"functions\":\"1\"}");
    snprintf(json,sizeof(json),"{\"action\":\"approve_model\",\"id\":\"%s\",\"value\":\"1\"}",model);
    free(command(json));
    snprintf(json,sizeof(json),"{\"action\":\"defaults\",\"model\":\"%s\",\"account\":\"%s\",\"assistant\":\"world_knowledge\",\"directory\":\"%s\",\"fallback\":\"%s\",\"mask\":\"1023\",\"rounds\":\"3\",\"web_provider\":\"none\",\"web\":\"0\",\"bash\":\"0\",\"one_tool\":\"0\",\"quality\":\"0\"}",model,account,argv[3],argv[3]);
    free(command(json)); free(model); free(account);
    for (i = 0; i < 30; ++i) {
        start = milliseconds();
        snprintf(json,sizeof(json),"{\"action\":\"create_session\",\"directory\":\"%s\"}",argv[3]);
        session = command(json);
        snprintf(json,sizeof(json),"{\"action\":\"delete_session\",\"session\":\"%s\"}",session);
        result = command(json); free(result); free(session);
        elapsed = milliseconds()-start;
        total += elapsed; if (elapsed > maximum) maximum = elapsed;
    }
    printf("PASS: 30 committed create/delete pairs; mean %.3f ms/pair, max %.3f ms/pair\n",total/30,maximum);
    return 0;
}
