#include "strappy_store_client.h"
#include "strappy_db.h"
#include "strappy_ipc.h"
#include <pthread.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

static char *strappy_command_socket;

typedef struct strappy_store_monitor {
    char *socket_path;
    char *database_path;
    void (*changed)(void);
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
            monitor->changed();
        }
        while (ready) {
            struct pollfd item;
            int result;
            item.fd = fd; item.events = POLLIN; item.revents = 0;
            result = poll(&item, 1, 1000);
            if (result == 0) continue;
            if (result < 0 || strappy_ipc_receive(fd, &message, 1000) != 0 ||
                message.kind != STRAPPY_IPC_EVENT) break;
            /* An instance/revision gap always causes a full refresh. Nothing
             * relies on reconstructing state solely from notifications. */
            monitor->changed();
        }
        strappy_db_set_service_ready(monitor->database_path, 0);
        if (ready) monitor->changed();
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
                               void (*changed)(void))
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
    if (pthread_create(&thread, NULL, strappy_store_monitor_run, monitor) != 0) goto fail;
    strappy_command_socket = monitor->socket_path;
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
int strappy_store_client_command(unsigned int operation, const unsigned char *payload,
    unsigned int length, unsigned int *status)
{
    int attempt;
    if (!strappy_command_socket || length > STRAPPY_IPC_MAX_PAYLOAD) return 0;
    for (attempt = 0; attempt < 3; ++attempt) {
        int fd = strappy_ipc_connect(strappy_command_socket, geteuid(), 2000);
        strappy_ipc_message m;
        int ok;
        if (fd < 0) continue;
        memset(&m, 0, sizeof(m)); m.kind = STRAPPY_IPC_REQUEST; m.operation = STRAPPY_IPC_HELLO;
        m.request_id = 1; m.length = 8;
        strappy_ipc_put32(m.payload, STRAPPY_IPC_VERSION); strappy_ipc_put32(m.payload + 4, STRAPPY_IPC_VERSION);
        ok = strappy_ipc_send(fd, &m, 2000) == 0 && strappy_ipc_receive(fd, &m, 2000) == 0 &&
            m.kind == STRAPPY_IPC_REPLY && m.operation == STRAPPY_IPC_HELLO && m.request_id == 1 &&
            m.status == STRAPPY_IPC_OK && m.length == 36 &&
            strappy_ipc_get32(m.payload + 32) == STRAPPY_IPC_STORE_ABI &&
            (strappy_ipc_get32(m.payload + 24) & 7U) == 7U;
        if (ok) {
            memset(&m, 0, sizeof(m)); m.kind = STRAPPY_IPC_REQUEST; m.operation = operation;
            m.request_id = 2; m.length = length; memcpy(m.payload, payload, length);
            ok = strappy_ipc_send(fd, &m, 2000) == 0 && strappy_ipc_receive(fd, &m, 10000) == 0 &&
                m.kind == STRAPPY_IPC_REPLY && m.operation == operation && m.request_id == 2;
        }
        close(fd);
        if (ok) { *status = m.status; return 1; }
    }
    return 0;
}
