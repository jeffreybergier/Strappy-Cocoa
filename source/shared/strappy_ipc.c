#include "strappy_ipc.h"
#include "XPFoundation/strappy_ipc_platform.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

uint32_t strappy_ipc_payload_limit(uint32_t kind, uint32_t operation)
{
    if (kind == STRAPPY_IPC_REQUEST) {
        if (operation == STRAPPY_IPC_SUBMIT_PROMPT)
            return STRAPPY_IPC_PROMPT_PREFIX + STRAPPY_IPC_MAX_PROMPT_BYTES;
        if (operation == STRAPPY_IPC_SUBMIT_WITH_OPTIONS)
            return STRAPPY_IPC_OPTIONS_PREFIX + STRAPPY_IPC_MAX_OPTIONS_BYTES + STRAPPY_IPC_MAX_PROMPT_BYTES;
    }
    return STRAPPY_IPC_MAX_PAYLOAD;
}
const unsigned char *strappy_ipc_payload(const strappy_ipc_message *message)
{
    return message->external_payload ? message->external_payload : message->payload;
}

void strappy_ipc_put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v;
}
uint32_t strappy_ipc_get32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
void strappy_ipc_encode(unsigned char *p, const strappy_ipc_message *m)
{
    memcpy(p, "STPY", 4);
    p[4] = 0; p[5] = STRAPPY_IPC_VERSION;
    p[6] = 0; p[7] = (unsigned char)m->kind;
    strappy_ipc_put32(p + 8, m->operation);
    strappy_ipc_put32(p + 12, m->request_id);
    strappy_ipc_put32(p + 16, m->length);
    strappy_ipc_put32(p + 20, m->status);
}
int strappy_ipc_decode(const unsigned char *p, strappy_ipc_message *m)
{
    m->external_payload = NULL;
    if (memcmp(p, "STPY", 4) != 0) return -1;
    m->kind = ((uint32_t)p[6] << 8) | p[7];
    m->operation = strappy_ipc_get32(p + 8);
    m->request_id = strappy_ipc_get32(p + 12);
    m->length = strappy_ipc_get32(p + 16);
    m->status = strappy_ipc_get32(p + 20);
    if (m->length > strappy_ipc_payload_limit(m->kind, m->operation) || m->kind < 1 || m->kind > 3)
        return -1;
    if (p[4] != 0 || p[5] != STRAPPY_IPC_VERSION) return -2;
    return 0;
}
static int strappy_ipc_wait(int fd, short events, long long deadline)
{
    struct pollfd item;
    int result;
    long long remaining;
    item.fd = fd; item.events = events;
    do {
        remaining = deadline - strappy_ipc_platform_milliseconds();
        if (remaining <= 0) return -1;
        /* Client deadlines never exceed thirty seconds. */
        if (remaining > 30000) remaining = 30000;
        result = poll(&item, 1, (int)remaining);
    } while (result < 0 && errno == EINTR);
    return result > 0 && (item.revents & events) != 0 ? 0 : -1;
}
static int strappy_ipc_transfer(int fd, unsigned char *data, size_t size,
                                int writing, long long deadline)
{
    while (size > 0) {
        ssize_t count;
        if (strappy_ipc_wait(fd, writing ? POLLOUT : POLLIN, deadline) != 0)
            return -1;
        count = writing ? strappy_ipc_platform_send(fd, data, size) : read(fd, data, size);
        if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (count <= 0) return -1;
        data += count; size -= (size_t)count;
    }
    return 0;
}
int strappy_ipc_connect(const char *path, uid_t expected_uid, int timeout_ms)
{
    struct sockaddr_un address;
    int fd, error = 0;
    socklen_t length = sizeof(error);
    if (!path || path[0] != '/' || strlen(path) >= sizeof(address.sun_path) ||
        timeout_ms <= 0 || timeout_ms > 30000) return -1;
    memset(&address, 0, sizeof(address)); address.sun_family = AF_UNIX;
    strcpy(address.sun_path, path);
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    if (fcntl(fd, F_SETFL, O_NONBLOCK) != 0 ||
        fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) goto fail;
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        if (errno != EINPROGRESS ||
            strappy_ipc_wait(fd, POLLOUT, strappy_ipc_platform_milliseconds() + timeout_ms) != 0 ||
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) != 0 || error != 0)
            goto fail;
    }
    if (!strappy_ipc_platform_peer_matches(fd, expected_uid)) goto fail;
    return fd;
fail:
    (void)close(fd); return -1;
}
int strappy_ipc_send(int fd, const strappy_ipc_message *message, int timeout_ms)
{
    unsigned char header[STRAPPY_IPC_HEADER];
    long long deadline = strappy_ipc_platform_milliseconds() + timeout_ms;
    if (!message || message->length > strappy_ipc_payload_limit(message->kind, message->operation) ||
        (message->length > STRAPPY_IPC_MAX_PAYLOAD && !message->external_payload) ||
        timeout_ms <= 0 || timeout_ms > 30000) return -1;
    strappy_ipc_encode(header, message);
    if (strappy_ipc_transfer(fd, header, sizeof(header), 1, deadline) != 0) return -1;
    return strappy_ipc_transfer(fd, (unsigned char *)strappy_ipc_payload(message),
                               message->length, 1, deadline);
}
int strappy_ipc_receive(int fd, strappy_ipc_message *message, int timeout_ms)
{
    unsigned char header[STRAPPY_IPC_HEADER];
    long long deadline = strappy_ipc_platform_milliseconds() + timeout_ms;
    if (!message || timeout_ms <= 0 || timeout_ms > 120000 ||
        strappy_ipc_transfer(fd, header, sizeof(header), 0, deadline) != 0 ||
        strappy_ipc_decode(header, message) != 0 ||
        message->length > STRAPPY_IPC_MAX_PAYLOAD) return -1;
    return strappy_ipc_transfer(fd, message->payload, message->length, 0, deadline);
}
