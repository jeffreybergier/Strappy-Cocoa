#include "../shared/strappy_ipc.h"
#include "../shared/XPFoundation/strappy_ipc_platform.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define STRAPPY_IPC_QUEUE 8192U
#define STRAPPY_IPC_SCOPES 64
struct strappy_ipc_scope { uint32_t topic; long long session, request; };
struct strappy_ipc_peer {
    int fd, negotiated, subscribed, closing;
    uint32_t last_id;
    pthread_t worker;
    int working, finished, wake_fd;
    int worker_started, worker_stop, command_ready;
    pthread_cond_t command_changed;
    unsigned int refresh;
    pthread_mutex_t *mutex;
    void (*handler)(const strappy_ipc_message *, strappy_ipc_message *, void *);
    void *context;
    strappy_ipc_message command, response;
    int64_t progress;
    size_t received, needed, queued;
    unsigned char input[STRAPPY_IPC_HEADER + STRAPPY_IPC_MAX_PAYLOAD];
    unsigned char *large_payload;
    unsigned char output[STRAPPY_IPC_QUEUE];
};
struct strappy_ipc_server {
    int listener, lock, wake[2], bound;
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    unsigned char instance[16];
    pthread_mutex_t mutex;
    uint64_t revision;
    unsigned int pending;
    unsigned int scope_count;
    struct strappy_ipc_scope scopes[STRAPPY_IPC_SCOPES];
    int store_ready;
    void (*handler)(const strappy_ipc_message *, strappy_ipc_message *, void *);
    void *context;
    struct strappy_ipc_peer peers[STRAPPY_IPC_MAX_CLIENTS];
};
static int strappy_ipc_nonblocking(int fd)
{
    return fd >= 0 && fd < FD_SETSIZE &&
        fcntl(fd, F_SETFL, O_NONBLOCK) == 0 &&
        fcntl(fd, F_SETFD, FD_CLOEXEC) == 0 ? 0 : -1;
}
static void strappy_ipc_error(strappy_ipc_message *reply)
{
    static const char *const errors[] = { "", "invalid_request", "incompatible_version",
        "handshake_required", "duplicate_request", "execution_not_ready", "unknown_operation",
        "service_busy", "request_conflict", "storage_error" };
    if (reply->status >= sizeof(errors) / sizeof(errors[0])) reply->status = STRAPPY_IPC_STORAGE_ERROR;
    if (reply->status) {
        reply->length = (uint32_t)strlen(errors[reply->status]);
        memcpy(reply->payload, errors[reply->status], reply->length);
    }
}
static void *strappy_ipc_command(void *context)
{
    struct strappy_ipc_peer *p = context;
    for (;;) {
        pthread_mutex_lock(p->mutex);
        while (!p->command_ready && !p->worker_stop)
            pthread_cond_wait(&p->command_changed,p->mutex);
        if (!p->command_ready) { pthread_mutex_unlock(p->mutex); break; }
        p->command_ready = 0;
        pthread_mutex_unlock(p->mutex);
        p->handler(&p->command, &p->response, p->context);
        strappy_ipc_error(&p->response);
        pthread_mutex_lock(p->mutex);
        p->finished = 1;
        pthread_mutex_unlock(p->mutex);
        /* Rejected/read-only commands must also wake an idle event loop. */
        while (write(p->wake_fd, "c", 1) < 0 && errno == EINTR) {}
    }
    return NULL;
}
static void strappy_ipc_drop(struct strappy_ipc_peer *peer)
{
    if (peer->worker_started) {
        pthread_mutex_lock(peer->mutex); peer->worker_stop = 1;
        pthread_cond_signal(&peer->command_changed); pthread_mutex_unlock(peer->mutex);
        pthread_join(peer->worker,NULL); pthread_cond_destroy(&peer->command_changed);
    }
    free(peer->large_payload);
    if (peer->fd >= 0) (void)close(peer->fd);
    memset(peer, 0, sizeof(*peer)); peer->fd = -1;
}
static void strappy_ipc_queue(struct strappy_ipc_peer *peer,
                              const strappy_ipc_message *message)
{
    size_t size = STRAPPY_IPC_HEADER + message->length;
    if (peer->fd < 0) return;
    if (peer->working) {
        if (message->kind == STRAPPY_IPC_EVENT &&
            message->operation >= STRAPPY_IPC_SESSION_CHANGED && message->operation <= STRAPPY_IPC_RUN_CHANGED)
            peer->refresh |= 1U << (message->operation - STRAPPY_IPC_SESSION_CHANGED);
        return;
    }
    if (size > sizeof(peer->output) - peer->queued) {
        /* Disconnect instead of silently losing an invalidation. */
        strappy_ipc_drop(peer); return;
    }
    if (peer->queued == 0) peer->progress = strappy_ipc_platform_milliseconds();
    strappy_ipc_encode(peer->output + peer->queued, message);
    memcpy(peer->output + peer->queued + STRAPPY_IPC_HEADER,
           message->payload, message->length);
    peer->queued += size;
}
static void strappy_ipc_snapshot(strappy_ipc_server *s, strappy_ipc_message *m)
{
    uint64_t revision;
    (void)pthread_mutex_lock(&s->mutex); revision = s->revision;
    (void)pthread_mutex_unlock(&s->mutex);
    memcpy(m->payload, s->instance, 16);
    strappy_ipc_put32(m->payload + 16, (uint32_t)(revision >> 32));
    strappy_ipc_put32(m->payload + 20, (uint32_t)revision);
    /* bit 0 transport, bit 1 store; execution remains unavailable */
    strappy_ipc_put32(m->payload + 24, s->store_ready ? (s->handler ? 7U : 3U) : 1U);
    strappy_ipc_put32(m->payload + 28, STRAPPY_IPC_MAX_PAYLOAD);
    strappy_ipc_put32(m->payload + 32, STRAPPY_IPC_STORE_ABI);
    m->length = 36;
}
static void strappy_ipc_request(strappy_ipc_server *s, struct strappy_ipc_peer *p)
{
    strappy_ipc_message request, reply;
    int decoded = strappy_ipc_decode(p->input, &request);
    memset(&reply, 0, sizeof(reply));
    reply.kind = STRAPPY_IPC_REPLY;
    if (decoded == -1) { strappy_ipc_drop(p); return; }
    reply.operation = request.operation; reply.request_id = request.request_id;
    if (decoded == -2) {
        reply.status = STRAPPY_IPC_INCOMPATIBLE; p->closing = 1;
    } else if (request.kind != STRAPPY_IPC_REQUEST || request.status != 0 ||
               request.request_id == 0) {
        reply.status = STRAPPY_IPC_INVALID; p->closing = 1;
    } else if (request.request_id <= p->last_id) {
        reply.status = STRAPPY_IPC_DUPLICATE;
    } else {
        p->last_id = request.request_id;
        if (request.operation == STRAPPY_IPC_HELLO) {
            if (p->negotiated || request.length != 8) reply.status = STRAPPY_IPC_INVALID;
            else if (strappy_ipc_get32(p->input + STRAPPY_IPC_HEADER) > STRAPPY_IPC_VERSION ||
                     strappy_ipc_get32(p->input + STRAPPY_IPC_HEADER + 4) < STRAPPY_IPC_VERSION) {
                reply.status = STRAPPY_IPC_INCOMPATIBLE; p->closing = 1;
            } else {
                p->negotiated = 1; strappy_ipc_snapshot(s, &reply);
            }
        } else if (!p->negotiated) reply.status = STRAPPY_IPC_HANDSHAKE_REQUIRED;
        else if (request.operation >= STRAPPY_IPC_SNAPSHOT &&
                 request.operation <= STRAPPY_IPC_UNSUBSCRIBE) {
            if (request.length != 0) reply.status = STRAPPY_IPC_INVALID;
            else {
                if (request.operation == STRAPPY_IPC_SUBSCRIBE) p->subscribed = 1;
                if (request.operation == STRAPPY_IPC_UNSUBSCRIBE) p->subscribed = 0;
                strappy_ipc_snapshot(s, &reply);
            }
        } else if (request.operation >= STRAPPY_IPC_SUBMIT_PROMPT &&
                   request.operation <= STRAPPY_IPC_AUTH)
        {
            if (s->handler) {
                if (p->large_payload) request.external_payload = p->large_payload;
                else memcpy(request.payload, p->input + STRAPPY_IPC_HEADER, request.length);
                p->command = request; p->response = reply;
                p->handler = s->handler; p->context = s->context; p->mutex = &s->mutex;
                p->wake_fd = s->wake[1];
                if (!p->worker_started && pthread_cond_init(&p->command_changed,NULL) == 0) {
                    if (pthread_create(&p->worker,NULL,strappy_ipc_command,p) == 0) p->worker_started = 1;
                    else pthread_cond_destroy(&p->command_changed);
                }
                if (p->worker_started) {
                    pthread_mutex_lock(&s->mutex);
                    p->finished = 0; p->command_ready = 1; p->working = 1;
                    pthread_cond_signal(&p->command_changed);
                    pthread_mutex_unlock(&s->mutex);
                    p->received = 0; p->needed = STRAPPY_IPC_HEADER;
                    return;
                }
                reply.status = STRAPPY_IPC_BUSY;
            } else reply.status = STRAPPY_IPC_NOT_READY;
        }
        else reply.status = STRAPPY_IPC_UNKNOWN_OPERATION;
    }
    strappy_ipc_error(&reply);
    free(p->large_payload); p->large_payload = NULL;
    strappy_ipc_queue(p, &reply);
    p->received = 0; p->needed = STRAPPY_IPC_HEADER;
}
static void strappy_ipc_read(strappy_ipc_server *s, struct strappy_ipc_peer *p)
{
    unsigned char *destination = p->large_payload ?
        p->large_payload + (p->received - STRAPPY_IPC_HEADER) : p->input + p->received;
    ssize_t count = read(p->fd, destination, p->needed - p->received);
    if (count < 0 && (errno == EINTR || errno == EAGAIN)) return;
    if (count <= 0) { strappy_ipc_drop(p); return; }
    p->received += (size_t)count;
    p->progress = strappy_ipc_platform_milliseconds();
    if (p->received != p->needed) return;
    if (p->needed == STRAPPY_IPC_HEADER) {
        strappy_ipc_message m;
        int status = strappy_ipc_decode(p->input, &m);
        if (status == -1) { strappy_ipc_drop(p); return; }
        if (status == -2) { strappy_ipc_request(s, p); return; }
        if (m.length > STRAPPY_IPC_MAX_PAYLOAD) {
            /* Decode bounds the operation-specific length before allocation.
             * Never reserve a prompt buffer for an unauthenticated handshake. */
            if (!p->negotiated || m.kind != STRAPPY_IPC_REQUEST || m.status != 0 ||
                !m.request_id) { strappy_ipc_drop(p); return; }
            p->large_payload = malloc(m.length);
            if (!p->large_payload) { strappy_ipc_drop(p); return; }
        }
        p->needed += m.length;
        if (m.length != 0) return;
    }
    strappy_ipc_request(s, p);
}
strappy_ipc_server *strappy_ipc_server_open(const char *directory)
{
    strappy_ipc_server *s;
    struct sockaddr_un address;
    struct stat info;
    char lock_path[sizeof(address.sun_path) + 8];
    int i, random_fd;
    ssize_t count;
    if (!directory || directory[0] != '/' ||
        lstat(directory, &info) != 0 || !S_ISDIR(info.st_mode) ||
        info.st_uid != geteuid() || (info.st_mode & 0777) != 0700) return NULL;
    s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->listener = s->lock = s->wake[0] = s->wake[1] = -1;
    for (i = 0; i < STRAPPY_IPC_MAX_CLIENTS; ++i) s->peers[i].fd = -1;
    if (pthread_mutex_init(&s->mutex, NULL) != 0) { free(s); return NULL; }
    if (snprintf(s->path, sizeof(s->path), "%s/service.sock", directory) >= (int)sizeof(s->path) ||
        snprintf(lock_path, sizeof(lock_path), "%s/service.lock", directory) >= (int)sizeof(lock_path))
        goto fail;
    s->lock = open(lock_path, O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
    if (s->lock < 0 || fcntl(s->lock, F_SETFD, FD_CLOEXEC) != 0 ||
        fstat(s->lock, &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_uid != geteuid() || (info.st_mode & 0777) != 0600 ||
        flock(s->lock, LOCK_EX | LOCK_NB) != 0) goto fail;
    /* Only the lock owner can remove a socket left by an earlier crash. */
    if (lstat(s->path, &info) == 0) {
        if (!S_ISSOCK(info.st_mode) || info.st_uid != geteuid() || unlink(s->path) != 0)
            goto fail;
    } else if (errno != ENOENT) goto fail;
    random_fd = open("/dev/urandom", O_RDONLY);
    if (random_fd < 0) goto fail;
    do { count = read(random_fd, s->instance, sizeof(s->instance)); }
    while (count < 0 && errno == EINTR);
    (void)close(random_fd);
    if (count != sizeof(s->instance)) goto fail;
    if (pipe(s->wake) != 0 || strappy_ipc_nonblocking(s->wake[0]) != 0 ||
        strappy_ipc_nonblocking(s->wake[1]) != 0) goto fail;
    s->listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (strappy_ipc_nonblocking(s->listener) != 0) goto fail;
    memset(&address, 0, sizeof(address)); address.sun_family = AF_UNIX;
    strcpy(address.sun_path, s->path);
    if (bind(s->listener, (struct sockaddr *)&address, sizeof(address)) != 0) goto fail;
    s->bound = 1;
    if (chmod(s->path, 0600) != 0 || listen(s->listener, STRAPPY_IPC_MAX_CLIENTS) != 0)
        goto fail;
    return s;
fail:
    strappy_ipc_server_close(s); return NULL;
}
int strappy_ipc_server_committed_scope(strappy_ipc_server *s, uint32_t event,
    long long session, long long request)
{
    char byte = 1;
    unsigned int i, bit;
    if (!s || event < STRAPPY_IPC_SESSION_CHANGED || event > STRAPPY_IPC_RUN_CHANGED ||
        session<0 || request<0) return -1;
    bit=1U << (event-STRAPPY_IPC_SESSION_CHANGED);
    pthread_mutex_lock(&s->mutex);
    if (s->revision == UINT64_MAX) { pthread_mutex_unlock(&s->mutex); return -1; }
    ++s->revision;
    if (!session) s->pending |= bit;
    else if (!(s->pending & bit)) {
        for (i=0; i<s->scope_count; ++i)
            if (s->scopes[i].topic==event && s->scopes[i].session==session &&
                s->scopes[i].request==request) break;
        if (i==s->scope_count) {
            if (i==STRAPPY_IPC_SCOPES) {
                /* Explicit loss of detail, never a silently dropped update. */
                for (i=0; i<s->scope_count; ++i)
                    s->pending |= 1U << (s->scopes[i].topic-STRAPPY_IPC_SESSION_CHANGED);
                s->pending |= bit; s->scope_count=0;
            } else {
                s->scopes[i].topic=event; s->scopes[i].session=session;
                s->scopes[i].request=request; ++s->scope_count;
            }
        }
    }
    pthread_mutex_unlock(&s->mutex);
    while (write(s->wake[1], &byte, 1) < 0 && errno == EINTR) {}
    return 0;
}
int strappy_ipc_server_committed(strappy_ipc_server *s, uint32_t event)
{ return strappy_ipc_server_committed_scope(s,event,0,0); }
int strappy_ipc_server_step(strappy_ipc_server *s, int wake_fd, int timeout_ms)
{
    fd_set readers, writers;
    struct timeval timeout;
    int wait_ms = timeout_ms;
    int maximum, i, ready;
    unsigned int pending, count, index;
    struct strappy_ipc_scope scopes[STRAPPY_IPC_SCOPES];
    char bytes[128];
    if (!s || timeout_ms < -1 || wake_fd >= FD_SETSIZE) return -1;
    FD_ZERO(&readers); FD_ZERO(&writers);
    FD_SET(s->listener, &readers); FD_SET(s->wake[0], &readers);
    maximum = s->listener > s->wake[0] ? s->listener : s->wake[0];
    if (wake_fd >= 0) { FD_SET(wake_fd, &readers); if (wake_fd > maximum) maximum = wake_fd; }
    for (i = 0; i < STRAPPY_IPC_MAX_CLIENTS; ++i) {
        struct strappy_ipc_peer *p = &s->peers[i];
        if (p->fd < 0) continue;
        if (p->working) {
            int finished;
            pthread_mutex_lock(&s->mutex); finished = p->finished;
            pthread_mutex_unlock(&s->mutex);
            if (!finished) continue;
            p->working = 0;
            free(p->large_payload); p->large_payload = NULL; p->command.external_payload = NULL;
            strappy_ipc_queue(p, &p->response);
            if (p->refresh && p->subscribed) {
                unsigned int topic;
                for (topic = 0; topic < 4; ++topic) if (p->refresh & (1U << topic)) {
                    strappy_ipc_message event;
                    memset(&event, 0, sizeof(event)); event.kind = STRAPPY_IPC_EVENT;
                    event.operation = STRAPPY_IPC_SESSION_CHANGED + topic;
                    strappy_ipc_snapshot(s, &event); strappy_ipc_queue(p, &event);
                }
                p->refresh = 0;
            }
        }
        if ((!p->negotiated || p->received || p->queued) &&
            strappy_ipc_platform_milliseconds() - p->progress >= 30000) {
            strappy_ipc_drop(p); continue;
        }
        if (!p->negotiated || p->received || p->queued) {
            int remaining = (int)(30000 - (strappy_ipc_platform_milliseconds() - p->progress));
            if (remaining < 0) remaining = 0;
            if (wait_ms < 0 || remaining < wait_ms) wait_ms = remaining;
        }
        if (!p->closing) FD_SET(p->fd, &readers);
        if (p->queued) FD_SET(p->fd, &writers);
        if (p->fd > maximum) maximum = p->fd;
    }
    timeout.tv_sec = wait_ms < 0 ? 0 : wait_ms / 1000; timeout.tv_usec = (wait_ms < 0 ? 0 : wait_ms % 1000) * 1000;
    ready = select(maximum + 1, &readers, &writers, NULL, wait_ms < 0 ? NULL : &timeout);
    if (ready < 0) return errno == EINTR ? 0 : -1;
    /* Process only descriptors in this select's set before accepting new ones. */
    for (i = 0; i < STRAPPY_IPC_MAX_CLIENTS; ++i) {
        struct strappy_ipc_peer *p = &s->peers[i];
        if (p->fd < 0) continue;
        if (p->working) {
            int finished;
            pthread_mutex_lock(&s->mutex); finished = p->finished;
            pthread_mutex_unlock(&s->mutex);
            if (!finished) continue;
            p->working = 0;
            free(p->large_payload); p->large_payload = NULL; p->command.external_payload = NULL;
            strappy_ipc_queue(p, &p->response);
            if (p->refresh && p->subscribed) {
                unsigned int topic;
                for (topic = 0; topic < 4; ++topic) if (p->refresh & (1U << topic)) {
                    strappy_ipc_message event;
                    memset(&event, 0, sizeof(event)); event.kind = STRAPPY_IPC_EVENT;
                    event.operation = STRAPPY_IPC_SESSION_CHANGED + topic;
                    strappy_ipc_snapshot(s, &event); strappy_ipc_queue(p, &event);
                }
                p->refresh = 0;
            }
        }
        if (FD_ISSET(p->fd, &writers)) {
            ssize_t count = strappy_ipc_platform_send(p->fd, p->output, p->queued);
            if (count > 0) {
                p->progress = strappy_ipc_platform_milliseconds();
                p->queued -= (size_t)count;
                memmove(p->output, p->output + count, p->queued);
            } else if (count == 0 || (errno != EINTR && errno != EAGAIN)) strappy_ipc_drop(p);
        }
        if (p->fd >= 0 && p->closing && !p->queued) strappy_ipc_drop(p);
        if (p->fd >= 0 && !p->closing && FD_ISSET(p->fd, &readers)) strappy_ipc_read(s, p);
    }
    if (FD_ISSET(s->listener, &readers)) {
        int fd = accept(s->listener, NULL, NULL);
        if (fd >= 0) {
            for (i = 0; i < STRAPPY_IPC_MAX_CLIENTS; ++i) if (s->peers[i].fd < 0) break;
            if (i == STRAPPY_IPC_MAX_CLIENTS || strappy_ipc_nonblocking(fd) != 0 ||
                !strappy_ipc_platform_peer_matches(fd, geteuid())) (void)close(fd);
            else {
                s->peers[i].fd = fd; s->peers[i].needed = STRAPPY_IPC_HEADER;
                s->peers[i].progress = strappy_ipc_platform_milliseconds();
            }
        }
    }
    /* Bounded drain: a busy producer must not starve client I/O. */
    if (FD_ISSET(s->wake[0], &readers)) (void)read(s->wake[0], bytes, sizeof(bytes));
    (void)pthread_mutex_lock(&s->mutex); pending = s->pending; s->pending = 0;
    count=s->scope_count; memcpy(scopes,s->scopes,count*sizeof(*scopes)); s->scope_count=0;
    (void)pthread_mutex_unlock(&s->mutex);
    for (i = 0; i < 4; ++i) if (pending & (1U << i)) {
        strappy_ipc_message event;
        int j;
        memset(&event, 0, sizeof(event)); event.kind = STRAPPY_IPC_EVENT;
        event.operation = STRAPPY_IPC_SESSION_CHANGED + (uint32_t)i;
        strappy_ipc_snapshot(s, &event);
        for (j = 0; j < STRAPPY_IPC_MAX_CLIENTS; ++j)
            if (s->peers[j].subscribed) strappy_ipc_queue(&s->peers[j], &event);
    }
    for (index=0; index<count; ++index) {
        struct strappy_ipc_scope *scope=&scopes[index];
        strappy_ipc_message event;
        int j;
        if (pending & (1U << (scope->topic-STRAPPY_IPC_SESSION_CHANGED))) continue;
        memset(&event,0,sizeof(event)); event.kind=STRAPPY_IPC_EVENT; event.operation=scope->topic;
        strappy_ipc_snapshot(s,&event); event.length=52;
        strappy_ipc_put32(event.payload+36,(uint32_t)((uint64_t)scope->session>>32));
        strappy_ipc_put32(event.payload+40,(uint32_t)scope->session);
        strappy_ipc_put32(event.payload+44,(uint32_t)((uint64_t)scope->request>>32));
        strappy_ipc_put32(event.payload+48,(uint32_t)scope->request);
        for (j=0; j<STRAPPY_IPC_MAX_CLIENTS; ++j)
            if (s->peers[j].subscribed) strappy_ipc_queue(&s->peers[j],&event);
    }
    return 0;
}
void strappy_ipc_server_close(strappy_ipc_server *s)
{
    int i;
    if (!s) return;
    for (i = 0; i < STRAPPY_IPC_MAX_CLIENTS; ++i) strappy_ipc_drop(&s->peers[i]);
    if (s->listener >= 0) (void)close(s->listener);
    if (s->bound) (void)unlink(s->path);
    if (s->wake[0] >= 0) (void)close(s->wake[0]);
    if (s->wake[1] >= 0) (void)close(s->wake[1]);
    if (s->lock >= 0) (void)close(s->lock);
    (void)pthread_mutex_destroy(&s->mutex); free(s);
}

void strappy_ipc_server_store_ready(strappy_ipc_server *server)
{
    server->store_ready = 1;
}

void strappy_ipc_server_handler(strappy_ipc_server *s,
    void (*handler)(const strappy_ipc_message *, strappy_ipc_message *, void *), void *context)
{
    s->handler = handler; s->context = context;
}

void strappy_ipc_server_drain(strappy_ipc_server *s)
{
    int i;
    if (!s) return;
    for (i = 0; i < STRAPPY_IPC_MAX_CLIENTS; ++i) if (s->peers[i].worker_started) {
        struct strappy_ipc_peer *p = &s->peers[i];
        pthread_mutex_lock(&s->mutex); p->worker_stop = 1;
        pthread_cond_signal(&p->command_changed); pthread_mutex_unlock(&s->mutex);
        pthread_join(p->worker,NULL); pthread_cond_destroy(&p->command_changed);
        p->worker_started = p->working = 0;
    }
}
