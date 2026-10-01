#ifndef STRAPPY_IPC_H
#define STRAPPY_IPC_H

#include <stdint.h>
#include <sys/types.h>

#define STRAPPY_IPC_VERSION 1U
#define STRAPPY_IPC_HEADER 24U
#define STRAPPY_IPC_MAX_PAYLOAD 4096U
#define STRAPPY_IPC_MAX_CLIENTS 16

enum strappy_ipc_kind { STRAPPY_IPC_REQUEST = 1, STRAPPY_IPC_REPLY = 2,
                        STRAPPY_IPC_EVENT = 3 };
enum strappy_ipc_operation {
    STRAPPY_IPC_HELLO = 1, STRAPPY_IPC_SNAPSHOT = 2,
    STRAPPY_IPC_SUBSCRIBE = 3, STRAPPY_IPC_UNSUBSCRIBE = 4,
    STRAPPY_IPC_SUBMIT_PROMPT = 16, STRAPPY_IPC_CANCEL_RUN = 17,
    STRAPPY_IPC_SET_SESSION_OPTIONS = 18, STRAPPY_IPC_CREATE_SESSION = 19,
    STRAPPY_IPC_DELETE_SESSION = 20, STRAPPY_IPC_APPROVE_MODEL = 21,
    STRAPPY_IPC_APPROVE_DATABASE = 22, STRAPPY_IPC_START_SCAN = 23,
    STRAPPY_IPC_START_STUDY = 24,
    STRAPPY_IPC_SESSION_CHANGED = 32, STRAPPY_IPC_LEDGER_CHANGED = 33,
    STRAPPY_IPC_CATALOG_CHANGED = 34, STRAPPY_IPC_RUN_CHANGED = 35
};
enum strappy_ipc_status {
    STRAPPY_IPC_OK = 0, STRAPPY_IPC_INVALID = 1,
    STRAPPY_IPC_INCOMPATIBLE = 2, STRAPPY_IPC_HANDSHAKE_REQUIRED = 3,
    STRAPPY_IPC_DUPLICATE = 4, STRAPPY_IPC_NOT_READY = 5,
    STRAPPY_IPC_UNKNOWN_OPERATION = 6
};

typedef struct strappy_ipc_message {
    uint32_t kind, operation, request_id, length, status;
    unsigned char payload[STRAPPY_IPC_MAX_PAYLOAD];
} strappy_ipc_message;

typedef struct strappy_ipc_server strappy_ipc_server;

void strappy_ipc_put32(unsigned char *bytes, uint32_t value);
uint32_t strappy_ipc_get32(const unsigned char *bytes);
void strappy_ipc_encode(unsigned char *header, const strappy_ipc_message *message);
/* -2 is an incompatible version, -1 malformed framing. */
int strappy_ipc_decode(const unsigned char *header, strappy_ipc_message *message);

/* Directory must be absolute, owned by euid, mode 0700, and not a symlink.
 * The server owns service.sock and service.lock inside it. */
strappy_ipc_server *strappy_ipc_server_open(const char *directory);
/* Single event-loop thread only. Wait at most timeout_ms (0..1000).
 * wake_fd is the daemon's signal pipe, or -1. */
int strappy_ipc_server_step(strappy_ipc_server *server, int wake_fd, int timeout_ms);
/* Thread-safe worker boundary: call ONLY AFTER the state transaction commits.
 * Coalesced invalidations cover an entire topic, never individual ledger rows.
 * No callbacks, provider work, or database operations run on the IPC loop. */
int strappy_ipc_server_committed(strappy_ipc_server *server, uint32_t event);
/* Stop/join workers before close. */
void strappy_ipc_server_close(strappy_ipc_server *server);

/* Blocking client primitives for a UI-owned background thread; each complete
 * send/read has a bounded timeout. Never use these on the UI thread.
 * expected_uid authenticates the server (normally the client's own euid).
 * Close/reconnect after ANY I/O failure, then hello + subscribe/snapshot. */
int strappy_ipc_connect(const char *path, uid_t expected_uid, int timeout_ms);
int strappy_ipc_send(int fd, const strappy_ipc_message *message, int timeout_ms);
int strappy_ipc_receive(int fd, strappy_ipc_message *message, int timeout_ms);

#endif
