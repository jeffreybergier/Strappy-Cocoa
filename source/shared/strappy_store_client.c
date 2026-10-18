#include "strappy_store_client.h"
#include <ai_store.h>
#include "strappy_db.h"
#include "strappy_ipc.h"
#include "strappy_run.h"
#include "XPFoundation/strappy_ipc_platform.h"
#include <pthread.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>
#include <time.h>

static char *strappy_command_socket;
static pthread_mutex_t strappy_status_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t strappy_work_refresh_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned int strappy_active_job, strappy_last_job, strappy_job_result;
static int strappy_store_exchange(unsigned int operation, const unsigned char *payload,
    unsigned int length, unsigned int *status, char **response, int attempts, int timeout);
/* The bootstrap caller is the GUI main thread. It owns the permanent control
 * socket and performs send/receive itself. Other threads own independent lanes. */
typedef struct strappy_client_connection {
    int fd;
    uint32_t sequence;
} strappy_client_connection;
static pthread_t strappy_control_owner;
static strappy_client_connection strappy_control_connection = { -1, 0 };
static pthread_key_t strappy_connection_key;
static pthread_once_t strappy_connection_once = PTHREAD_ONCE_INIT;
static int strappy_connection_key_ready;
static void strappy_connection_destroy(void *context)
{
    strappy_client_connection *connection = context;
    if (connection->fd >= 0) close(connection->fd);
    free(connection);
}
static void strappy_connection_key_create(void)
{
    strappy_connection_key_ready = pthread_key_create(&strappy_connection_key,
        strappy_connection_destroy) == 0;
}
static void strappy_store_refresh_work(void)
{
    unsigned int status = 0;
    char *response = NULL;
    unsigned int active = 0, last = 0, result = 0;
    /* Serialize snapshots so an older monitor read cannot overwrite the idle
     * snapshot installed before a keyed completion reaches the GUI. */
    pthread_mutex_lock(&strappy_work_refresh_mutex);
    if (strappy_store_exchange(STRAPPY_IPC_WORK_STATUS,NULL,0,&status,&response,1,2000) &&
        status == STRAPPY_IPC_OK && response) {
        active = strappy_ipc_get32((unsigned char *)response);
        last = strappy_ipc_get32((unsigned char *)response+4);
        result = strappy_ipc_get32((unsigned char *)response+8);
    }
    free(response);
    pthread_mutex_lock(&strappy_status_mutex);
    strappy_active_job = active; strappy_last_job = last; strappy_job_result = result;
    pthread_mutex_unlock(&strappy_status_mutex);
    pthread_mutex_unlock(&strappy_work_refresh_mutex);
}
void strappy_store_client_work_status(unsigned int *active, unsigned int *last, unsigned int *result)
{
    pthread_mutex_lock(&strappy_status_mutex);
    *active = strappy_active_job; *last = strappy_last_job; *result = strappy_job_result;
    pthread_mutex_unlock(&strappy_status_mutex);
}

static ai_store_feed *strappy_changes;
static pthread_once_t strappy_changes_once = PTHREAD_ONCE_INIT;
static void strappy_changes_create(void)
{
    strappy_changes = ai_store_feed_create("strappy.sqlite", 256);
}
static void strappy_store_record_change(unsigned int topics, long long session, long long request)
{
    pthread_once(&strappy_changes_once, strappy_changes_create);
    ai_store_feed_publish(strappy_changes, topics, session, request, !request);
}
int strappy_store_client_changes(long long session, uint64_t after,
    uint64_t *next, long long requests[STRAPPY_STORE_MAX_CHANGES])
{
    ai_store_change changes[STRAPPY_STORE_MAX_CHANGES];
    int count, i;
    pthread_once(&strappy_changes_once, strappy_changes_create);
    *next = 0;
    count = ai_store_feed_read(strappy_changes, after, STRAPPY_STORE_LEDGER,
        session, next, changes, STRAPPY_STORE_MAX_CHANGES);
    for (i = 0; i < count; ++i) requests[i] = changes[i].record;
    return count;
}
typedef struct strappy_store_monitor {
    char *socket_path;
    char *database_path;
    void (*changed)(unsigned int topics, long long session);
} strappy_store_monitor;

static void *strappy_store_monitor_run(void *context)
{
    strappy_store_monitor *monitor = context;
    for (;;) {
        int fd = strappy_ipc_connect(monitor->socket_path, geteuid(), 1000);
        int ready = 0;
        strappy_ipc_message message;
        unsigned int operation;
        for (operation = STRAPPY_IPC_HELLO; fd >= 0 && operation <= STRAPPY_IPC_SUBSCRIBE; ++operation) {
            memset(&message, 0, sizeof(message));
            message.kind = STRAPPY_IPC_REQUEST;
            message.operation = operation;
            message.request_id = operation;
            if (operation == STRAPPY_IPC_HELLO) {
                message.length = 8;
                strappy_ipc_put32(message.payload, STRAPPY_IPC_VERSION);
                strappy_ipc_put32(message.payload + 4, STRAPPY_IPC_VERSION);
            }
            if (strappy_ipc_send(fd, &message, 1000) != 0 ||
                strappy_ipc_receive(fd, &message, 1000) != 0 ||
                message.kind != STRAPPY_IPC_REPLY || message.status != STRAPPY_IPC_OK ||
                message.request_id != operation || message.operation != operation ||
                message.length != 36 || (strappy_ipc_get32(message.payload + 24) & 3U) != 3U ||
                strappy_ipc_get32(message.payload + 32) != STRAPPY_IPC_STORE_ABI) break;
        }
        ready = fd >= 0 && operation > STRAPPY_IPC_SUBSCRIBE;
        if (ready) {
            strappy_db_set_service_ready(monitor->database_path, 1);
            strappy_store_refresh_work();
            strappy_store_record_change(STRAPPY_STORE_ALL,0,0); monitor->changed(STRAPPY_STORE_ALL,0);
        }
        while (ready) {
            struct pollfd item;
            int result;
            item.fd = fd; item.events = POLLIN; item.revents = 0;
            result = poll(&item, 1, 1000);
            if (result == 0) continue;
            if (result < 0 || strappy_ipc_receive(fd, &message, 1000) != 0 ||
                message.kind != STRAPPY_IPC_EVENT) break;
            /* The server coalesces revisions by topic and disconnects slow
             * subscribers instead of dropping events. Reconnect always performs
             * a full refresh; a topic hint never requires unrelated UI work. */
            if (message.operation < STRAPPY_IPC_SESSION_CHANGED ||
                message.operation > STRAPPY_IPC_RUN_CHANGED || (message.length != 36 && message.length != 52)) break;
            if (message.operation == STRAPPY_IPC_RUN_CHANGED ||
                message.operation == STRAPPY_IPC_CATALOG_CHANGED) strappy_store_refresh_work();
            {
                unsigned int topics=1U << (message.operation-STRAPPY_IPC_SESSION_CHANGED);
                long long session=0, request=0;
                if (message.length==52) {
                    if (strappy_ipc_get32(message.payload+36)>0x7fffffffU ||
                        strappy_ipc_get32(message.payload+44)>0x7fffffffU) break;
                    session=(long long)(((uint64_t)strappy_ipc_get32(message.payload+36)<<32)|strappy_ipc_get32(message.payload+40));
                    request=(long long)(((uint64_t)strappy_ipc_get32(message.payload+44)<<32)|strappy_ipc_get32(message.payload+48));
                    if (!session) break;
                }
                strappy_store_record_change(topics,session,request);
                monitor->changed(topics,session);
            }
        }
        strappy_db_set_service_ready(monitor->database_path, 0);
        if (ready) { strappy_store_refresh_work(); strappy_store_record_change(STRAPPY_STORE_ALL,0,0); monitor->changed(STRAPPY_STORE_ALL,0); }
        if (fd >= 0) close(fd);
        {
            struct timeval delay;
            delay.tv_sec = 1; delay.tv_usec = 0;
            (void)select(0, NULL, NULL, NULL, &delay);
        }
    }
    return NULL;
}

int strappy_store_client_start(const char *socket_path, const char *database_path,
                               void (*changed)(unsigned int topics, long long session))
{
    pthread_t thread;
    strappy_store_monitor *monitor;
    if (socket_path == NULL || socket_path[0] != '/' ||
        database_path == NULL || database_path[0] != '/' || changed == NULL) return 0;
    monitor = calloc(1, sizeof(*monitor));
    if (monitor == NULL) return 0;
    monitor->socket_path = malloc(strlen(socket_path) + 1);
    monitor->database_path = malloc(strlen(database_path) + 1);
    monitor->changed = changed;
    if (monitor->socket_path == NULL || monitor->database_path == NULL) goto fail;
    strcpy(monitor->socket_path, socket_path);
    strcpy(monitor->database_path, database_path);
    strappy_control_owner = pthread_self();
    strappy_command_socket = monitor->socket_path;
    if (pthread_create(&thread, NULL, strappy_store_monitor_run, monitor) != 0) { strappy_command_socket = NULL; goto fail; }
    pthread_detach(thread);
    {
        unsigned int status;
        (void)strappy_store_exchange(STRAPPY_IPC_SNAPSHOT,NULL,0,&status,NULL,1,2000);
    }
    return 1;
fail:
    free(monitor->socket_path); free(monitor->database_path); free(monitor);
    return 0;
}

#include <fcntl.h>

int strappy_store_client_request_key(char key[65])
{
    unsigned char bytes[32]; size_t have = 0; int fd = open("/dev/urandom", O_RDONLY);
    static const char hex[] = "0123456789abcdef";
    if (fd < 0) return 0;
    while (have < sizeof(bytes)) {
        ssize_t n = read(fd, bytes + have, sizeof(bytes) - have);
        if (n <= 0) { close(fd); return 0; }
        have += (size_t)n;
    }
    close(fd);
    for (have = 0; have < sizeof(bytes); ++have) {
        key[have*2] = hex[bytes[have] >> 4]; key[have*2+1] = hex[bytes[have] & 15];
    }
    key[64] = 0; return 1;
}
static int strappy_store_exchange_once(unsigned int operation, const unsigned char *payload,
    unsigned int length, unsigned int *status, char **response, int attempts, int timeout,
    int *connection, uint32_t *sequence)
{
    int attempt;
    if (!strappy_command_socket || length > strappy_ipc_payload_limit(STRAPPY_IPC_REQUEST, operation)) return 0;
    for (attempt = 0; attempt < attempts; ++attempt) {
        int fd = *connection;
        if (fd >= 0 && *sequence == UINT32_MAX) { close(fd); fd = *connection = -1; }
        if (fd < 0) fd = strappy_ipc_connect(strappy_command_socket, geteuid(), 2000);
        strappy_ipc_message m;
        int ok;
        if (fd < 0) continue;
        ok = 1;
        if (*connection < 0) {
        memset(&m, 0, sizeof(m)); m.kind = STRAPPY_IPC_REQUEST; m.operation = STRAPPY_IPC_HELLO;
        m.request_id = 1; m.length = 8;
        strappy_ipc_put32(m.payload, STRAPPY_IPC_VERSION); strappy_ipc_put32(m.payload + 4, STRAPPY_IPC_VERSION);
        ok = strappy_ipc_send(fd, &m, 2000) == 0 && strappy_ipc_receive(fd, &m, 2000) == 0 &&
            m.kind == STRAPPY_IPC_REPLY && m.operation == STRAPPY_IPC_HELLO && m.request_id == 1 &&
            m.status == STRAPPY_IPC_OK && m.length == 36 &&
            strappy_ipc_get32(m.payload + 32) == STRAPPY_IPC_STORE_ABI &&
            (strappy_ipc_get32(m.payload + 24) & 7U) == 7U;
        if (ok) { *connection = fd; *sequence = 1; }
        }
        if (ok) {
            uint32_t identifier = ++*sequence;
            memset(&m, 0, sizeof(m)); m.kind = STRAPPY_IPC_REQUEST; m.operation = operation;
            m.request_id = identifier; m.length = length;
            if (length > STRAPPY_IPC_MAX_PAYLOAD) m.external_payload = payload;
            else if (length) memcpy(m.payload, payload, length);
            /* Submission uses the short control budget even for large prompts. */
            ok = strappy_ipc_send(fd, &m,
                operation == STRAPPY_IPC_SUBMIT_WITH_OPTIONS || operation == STRAPPY_IPC_SUBMIT_PROMPT ?
                2000 : (length > STRAPPY_IPC_MAX_PAYLOAD ? 10000 : 2000)) == 0 &&
                strappy_ipc_receive(fd, &m, timeout) == 0 &&
                m.kind == STRAPPY_IPC_REPLY && m.operation == operation && m.request_id == identifier;
        }
        if (!ok) { close(fd); *connection = -1; }
        if (ok) {
            *status = m.status;
            if (response) {
                if (operation == STRAPPY_IPC_WORK_STATUS && m.length != (length ? 4U : 12U)) return 0;
                *response = calloc((size_t)m.length+1,1);
                if (!*response) return 0;
                memcpy(*response,m.payload,m.length);
            }
            return 1;
        }
    }
    return 0;
}

static int strappy_store_exchange(unsigned int operation, const unsigned char *payload,
    unsigned int length, unsigned int *status, char **response, int attempts, int timeout)
{
    strappy_client_connection *connection;
    if (!strappy_command_socket) return 0;
    if (pthread_equal(pthread_self(), strappy_control_owner)) {
        connection = &strappy_control_connection;
    } else {
        pthread_once(&strappy_connection_once, strappy_connection_key_create);
        if (!strappy_connection_key_ready) return 0;
        connection = pthread_getspecific(strappy_connection_key);
        if (!connection) {
            connection = calloc(1,sizeof(*connection));
            if (!connection) return 0;
            connection->fd = -1;
            if (pthread_setspecific(strappy_connection_key,connection) != 0) {
                free(connection); return 0;
            }
        }
    }
    return strappy_store_exchange_once(operation,payload,length,status,response,
        attempts,timeout,&connection->fd,&connection->sequence);
}

int strappy_store_client_command(unsigned int operation, const unsigned char *payload,
    unsigned int length, unsigned int *status)
{
    /* One short submission attempt; the saved key resolves an ambiguous reply. */
    if (operation == STRAPPY_IPC_SUBMIT_WITH_OPTIONS || operation == STRAPPY_IPC_SUBMIT_PROMPT)
        return strappy_store_exchange(operation,payload,length,status,NULL,1,2000);
    return strappy_store_exchange(operation,payload,length,status,NULL,3,10000);
}
char *strappy_store_client_mutation(unsigned int operation, const cJSON *command, unsigned int *status)
{
    char *json = cJSON_PrintUnformatted(command), *result = NULL;
    *status = STRAPPY_IPC_INVALID;
    if (json) {
        if (!strappy_store_exchange(operation,(unsigned char *)json,(unsigned int)strlen(json),status,&result,1,2000))
            *status = STRAPPY_IPC_NOT_READY;
        memset(json,0,strlen(json)); free(json);
    }
    if (*status != STRAPPY_IPC_OK) { free(result); result = NULL; }
    return result;
}

char *strappy_store_client_credential(const cJSON *command, unsigned int *status)
{
    char key[65], *reply=NULL, *result=NULL;
    const cJSON *account=cJSON_GetObjectItemCaseSensitive(command,"id");
    const cJSON *action=cJSON_GetObjectItemCaseSensitive(command,"action");
    cJSON *start=NULL, *query=NULL;
    int misses=0;
    int64_t deadline=strappy_ipc_platform_milliseconds()+300000;
    *status=STRAPPY_IPC_INVALID;
    if (!cJSON_IsString(account) || !cJSON_IsString(action) || !strappy_store_client_request_key(key)) return NULL;
    start=cJSON_Duplicate(command,1); query=cJSON_CreateObject();
    if (!start || !query || !cJSON_AddStringToObject(start,"credential_action",action->valuestring) ||
        !cJSON_ReplaceItemInObjectCaseSensitive(start,"action",cJSON_CreateString("credential_start")) ||
        !cJSON_AddStringToObject(start,"key",key) || !cJSON_AddStringToObject(query,"action","auth_status") ||
        !cJSON_AddStringToObject(query,"id",account->valuestring) || !cJSON_AddStringToObject(query,"key",key)) goto done;
    reply=strappy_store_client_mutation(STRAPPY_IPC_AUTH,start,status);
    /* The submission is sent exactly once. A lost reply triggers only reads. */
    if (!reply && *status!=STRAPPY_IPC_NOT_READY) goto done;
    while (strappy_ipc_platform_milliseconds()<deadline) {
        cJSON *snapshot=reply ? cJSON_Parse(reply) : NULL;
        const cJSON *state=cJSON_GetObjectItemCaseSensitive(snapshot,"state");
        int terminal=0;
        free(reply); reply=NULL;
        if (cJSON_IsString(state)) {
            misses=0;
            if (!strcmp(state->valuestring,"completed")) {
                const cJSON *value=cJSON_GetObjectItemCaseSensitive(snapshot,"result");
                if (cJSON_IsString(value)) result=strdup(value->valuestring);
                *status=result ? STRAPPY_IPC_OK : STRAPPY_IPC_INVALID; terminal=1;
            } else if (!strcmp(state->valuestring,"failed") || !strcmp(state->valuestring,"cancelled")) {
                *status=STRAPPY_IPC_INVALID; terminal=1;
            } else if (!strcmp(state->valuestring,"unknown")) {
                *status=STRAPPY_IPC_NOT_READY; terminal=1;
            }
        } else if (++misses>=3) { *status=STRAPPY_IPC_NOT_READY; terminal=1; }
        cJSON_Delete(snapshot);
        if (terminal) goto done;
        { struct timespec delay={0,250000000}; nanosleep(&delay,NULL); }
        reply=strappy_store_client_mutation(STRAPPY_IPC_AUTH,query,status);
    }
    *status=STRAPPY_IPC_NOT_READY;
done:
    free(reply);
    if (start) {
        cJSON *item;
        for (item=start->child; item; item=item->next)
            if (item->valuestring) memset(item->valuestring,0,strlen(item->valuestring));
    }
    cJSON_Delete(start); cJSON_Delete(query); return result;
}

/* Worker-only wait. The command was submitted once by the caller; every
 * subsequent exchange is a read, including reconnect after a lost reply. */
unsigned int strappy_store_client_wait_maintenance(const char *key)
{
    if (!key || strlen(key) != 64 || strspn(key,"0123456789abcdef") != 64) return 0;
    for (;;) {
        unsigned int status = STRAPPY_IPC_NOT_READY, state = 0;
        char *response = NULL;
        int ok = strappy_store_exchange(STRAPPY_IPC_WORK_STATUS,
            (const unsigned char *)key,64,&status,&response,3,2000);
        if (ok && status == STRAPPY_IPC_OK && response)
            state = strappy_ipc_get32((const unsigned char *)response);
        free(response);
        if (state != STRAPPY_RUN_RUNNING) {
            /* Do not leave a stale running snapshot behind the completion. */
            strappy_store_refresh_work();
            return state == STRAPPY_RUN_COMPLETED || state == STRAPPY_RUN_FAILED ||
                state == STRAPPY_RUN_INTERRUPTED ? state : 0;
        }
        { struct timespec delay = {0,250000000}; nanosleep(&delay,NULL); }
    }
}

int strappy_store_client_observe(long long session, uint64_t token, unsigned int mode)
{
    unsigned char payload[20];
    unsigned int status=STRAPPY_IPC_NOT_READY;
    strappy_ipc_put32(payload,(uint32_t)((uint64_t)session>>32));
    strappy_ipc_put32(payload+4,(uint32_t)session);
    strappy_ipc_put32(payload+8,(uint32_t)(token>>32));
    strappy_ipc_put32(payload+12,(uint32_t)token);
    strappy_ipc_put32(payload+16,mode);
    return strappy_store_exchange(STRAPPY_IPC_OBSERVE_SESSION,payload,sizeof(payload),
        &status,NULL,1,500) && status==STRAPPY_IPC_OK;
}
