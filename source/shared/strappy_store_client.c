#include "strappy_store_client.h"
#include "strappy_db.h"
#include "strappy_ipc.h"
#include <pthread.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>
#include <time.h>

static char *strappy_command_socket;
static pthread_mutex_t strappy_status_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned int strappy_active_job, strappy_last_job, strappy_job_result;
static int strappy_store_exchange(unsigned int operation, const unsigned char *payload,
    unsigned int length, unsigned int *status, char **response, int attempts, int timeout);
/* The process-lifetime control worker owns its socket. Callers wait on their
 * own completion, never on the GUI thread; insertion order is execution order. */
typedef struct strappy_control_request {
    unsigned int operation, length, status;
    const unsigned char *payload;
    char *response;
    int attempts, timeout, done, ok;
    pthread_cond_t completed;
    struct strappy_control_request *next;
} strappy_control_request;
static pthread_mutex_t strappy_control_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t strappy_control_changed = PTHREAD_COND_INITIALIZER;
static strappy_control_request *strappy_control_head, *strappy_control_tail;
static int strappy_control_started;
static void *strappy_control_run(void *unused);
static void strappy_store_refresh_work(void)
{
    unsigned int status = 0;
    char *response = NULL;
    unsigned int active = 0, last = 0, result = 0;
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
}
void strappy_store_client_work_status(unsigned int *active, unsigned int *last, unsigned int *result)
{
    pthread_mutex_lock(&strappy_status_mutex);
    *active = strappy_active_job; *last = strappy_last_job; *result = strappy_job_result;
    pthread_mutex_unlock(&strappy_status_mutex);
}

typedef struct strappy_store_monitor {
    char *socket_path;
    char *database_path;
    void (*changed)(unsigned int topics);
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
            unsigned int status;
            /* Establish/recover the control connection before exposing readiness. */
            (void)strappy_store_exchange(STRAPPY_IPC_SNAPSHOT,NULL,0,&status,NULL,2,2000);
            strappy_db_set_service_ready(monitor->database_path, 1);
            strappy_store_refresh_work();
            monitor->changed(STRAPPY_STORE_ALL);
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
                message.operation > STRAPPY_IPC_RUN_CHANGED || message.length != 36) break;
            if (message.operation == STRAPPY_IPC_RUN_CHANGED ||
                message.operation == STRAPPY_IPC_CATALOG_CHANGED) strappy_store_refresh_work();
            monitor->changed(1U << (message.operation - STRAPPY_IPC_SESSION_CHANGED));
        }
        strappy_db_set_service_ready(monitor->database_path, 0);
        if (ready) { strappy_store_refresh_work(); monitor->changed(STRAPPY_STORE_ALL); }
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
                               void (*changed)(unsigned int topics))
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
    strappy_command_socket = monitor->socket_path;
    if (pthread_create(&thread, NULL, strappy_store_monitor_run, monitor) != 0) { strappy_command_socket = NULL; goto fail; }
    pthread_detach(thread);
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
    if (!strappy_command_socket || length > STRAPPY_IPC_MAX_PAYLOAD) return 0;
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
            m.request_id = identifier; m.length = length; if (length) memcpy(m.payload, payload, length);
            ok = strappy_ipc_send(fd, &m, 2000) == 0 && strappy_ipc_receive(fd, &m, timeout) == 0 &&
                m.kind == STRAPPY_IPC_REPLY && m.operation == operation && m.request_id == identifier;
        }
        if (!ok) { close(fd); *connection = -1; }
        if (ok) {
            *status = m.status;
            if (response) {
                if (operation == STRAPPY_IPC_WORK_STATUS && m.length != 12) return 0;
                *response = calloc((size_t)m.length+1,1);
                if (!*response) return 0;
                memcpy(*response,m.payload,m.length);
            }
            return 1;
        }
    }
    return 0;
}

static void *strappy_control_run(void *unused)
{
    int fd = -1;
    uint32_t sequence = 0;
    (void)unused;
    for (;;) {
        strappy_control_request *request;
        pthread_mutex_lock(&strappy_control_mutex);
        while (!strappy_control_head) pthread_cond_wait(&strappy_control_changed, &strappy_control_mutex);
        request = strappy_control_head;
        strappy_control_head = request->next;
        if (!strappy_control_head) strappy_control_tail = NULL;
        pthread_mutex_unlock(&strappy_control_mutex);
        request->ok = strappy_store_exchange_once(request->operation, request->payload,
            request->length, &request->status, &request->response, request->attempts,
            request->timeout, &fd, &sequence);
        pthread_mutex_lock(&strappy_control_mutex);
        request->done = 1;
        pthread_cond_signal(&request->completed);
        pthread_mutex_unlock(&strappy_control_mutex);
    }
    return NULL;
}
static int strappy_store_exchange(unsigned int operation, const unsigned char *payload,
    unsigned int length, unsigned int *status, char **response, int attempts, int timeout)
{
    strappy_control_request *request;
    int ok;
    /* Monitor status must not wait behind control work. It owns a second,
     * persistent command socket in addition to its subscription. */
    if (operation == STRAPPY_IPC_WORK_STATUS) {
        static int monitor_fd = -1;
        static uint32_t monitor_sequence;
        static pthread_mutex_t monitor_mutex = PTHREAD_MUTEX_INITIALIZER;
        int ok;
        pthread_mutex_lock(&monitor_mutex);
        ok = strappy_store_exchange_once(operation,payload,length,status,response,
            attempts,timeout,&monitor_fd,&monitor_sequence);
        pthread_mutex_unlock(&monitor_mutex);
        return ok;
    }
    request = calloc(1,sizeof(*request));
    if (!request) return 0;
    request->operation = operation; request->payload = payload; request->length = length;
    request->attempts = attempts; request->timeout = timeout;
    if (pthread_cond_init(&request->completed,NULL) != 0) { free(request); return 0; }
    pthread_mutex_lock(&strappy_control_mutex);
    if (!strappy_control_started) {
        pthread_t worker;
        if (pthread_create(&worker,NULL,strappy_control_run,NULL) != 0) {
            pthread_mutex_unlock(&strappy_control_mutex);
            pthread_cond_destroy(&request->completed); free(request); return 0;
        }
        pthread_detach(worker); strappy_control_started = 1;
    }
    if (strappy_control_tail) strappy_control_tail->next = request;
    else strappy_control_head = request;
    strappy_control_tail = request;
    pthread_cond_signal(&strappy_control_changed);
    while (!request->done) pthread_cond_wait(&request->completed,&strappy_control_mutex);
    pthread_mutex_unlock(&strappy_control_mutex);
    pthread_cond_destroy(&request->completed);
    *status = request->status;
    if (response) *response = request->response; else free(request->response);
    ok = request->ok; free(request); return ok;
}

int strappy_store_client_command(unsigned int operation, const unsigned char *payload,
    unsigned int length, unsigned int *status)
{
    return strappy_store_exchange(operation,payload,length,status,NULL,3,10000);
}
char *strappy_store_client_mutation(unsigned int operation, const cJSON *command, unsigned int *status)
{
    char *json = cJSON_PrintUnformatted(command), *result = NULL;
    *status = STRAPPY_IPC_INVALID;
    if (json) {
        const cJSON *action = cJSON_GetObjectItemCaseSensitive(command,"action");
        if (operation == STRAPPY_IPC_MUTATE && cJSON_IsString(action) && !strcmp(action->valuestring,"refresh_oauth")) {
            /* Explicit credential refresh may wait on the network. It has an
             * independent worker/connection and cannot occupy the control FIFO. */
            int fd = -1;
            uint32_t sequence = 0;
            if (!strappy_store_exchange_once(STRAPPY_IPC_REFRESH_CREDENTIALS,
                (unsigned char *)json,(unsigned int)strlen(json),status,&result,1,120000,&fd,&sequence))
                *status = STRAPPY_IPC_NOT_READY;
            if (fd >= 0) close(fd);
        } else if (!strappy_store_exchange(operation,(unsigned char *)json,(unsigned int)strlen(json),status,&result,1,
            cJSON_GetObjectItemCaseSensitive(command,"identity") ? 120000 : 2000))
            *status = STRAPPY_IPC_NOT_READY;
        memset(json,0,strlen(json)); free(json);
    }
    if (*status != STRAPPY_IPC_OK) { free(result); result = NULL; }
    return result;
}
