#ifndef STRAPPY_IPC_H
#define STRAPPY_IPC_H

#include <stdint.h>
#include <sys/types.h>

#define STRAPPY_IPC_VERSION 6U
#define STRAPPY_IPC_STORE_ABI 2U
#define STRAPPY_IPC_HEADER 24U
/* Ordinary control requests, every reply, and every event stay small. */
#define STRAPPY_IPC_MAX_PAYLOAD 4096U
#define STRAPPY_IPC_MAX_PROMPT_BYTES (1024U * 1024U)
#define STRAPPY_IPC_MAX_OPTIONS_BYTES 4096U
#define STRAPPY_IPC_PROMPT_PREFIX 72U
#define STRAPPY_IPC_OPTIONS_PREFIX 76U
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
    STRAPPY_IPC_MUTATE = 25, STRAPPY_IPC_WORK_STATUS = 26,
    STRAPPY_IPC_REFRESH_CATALOG = 27,
    STRAPPY_IPC_SUBMIT_WITH_OPTIONS = 28,
    STRAPPY_IPC_REFRESH_CREDENTIALS = 29,
    STRAPPY_IPC_AUTH = 30,
    STRAPPY_IPC_SESSION_CHANGED = 32, STRAPPY_IPC_LEDGER_CHANGED = 33,
    STRAPPY_IPC_CATALOG_CHANGED = 34, STRAPPY_IPC_RUN_CHANGED = 35
};
enum strappy_ipc_status {
    STRAPPY_IPC_OK = 0, STRAPPY_IPC_INVALID = 1,
    STRAPPY_IPC_INCOMPATIBLE = 2, STRAPPY_IPC_HANDSHAKE_REQUIRED = 3,
    STRAPPY_IPC_DUPLICATE = 4, STRAPPY_IPC_NOT_READY = 5,
    STRAPPY_IPC_UNKNOWN_OPERATION = 6, STRAPPY_IPC_BUSY = 7,
    STRAPPY_IPC_CONFLICT = 8, STRAPPY_IPC_STORAGE_ERROR = 9
};

typedef struct strappy_ipc_message {
    uint32_t kind, operation, request_id, length, status;
    unsigned char payload[STRAPPY_IPC_MAX_PAYLOAD];
    /* Borrowed storage for large outgoing requests and server handler input.
     * Valid until send/handler returns; never used for replies or events. */
    const unsigned char *external_payload;
} strappy_ipc_message;

typedef struct strappy_ipc_server strappy_ipc_server;

uint32_t strappy_ipc_payload_limit(uint32_t kind, uint32_t operation);
const unsigned char *strappy_ipc_payload(const strappy_ipc_message *message);

void strappy_ipc_put32(unsigned char *bytes, uint32_t value);
uint32_t strappy_ipc_get32(const unsigned char *bytes);
void strappy_ipc_encode(unsigned char *header, const strappy_ipc_message *message);
/* -2 is an incompatible version, -1 malformed framing. */
int strappy_ipc_decode(const unsigned char *header, strappy_ipc_message *message);

/* Directory must be absolute, owned by euid, mode 0700, and not a symlink.
 * The server owns service.sock and service.lock inside it. */
strappy_ipc_server *strappy_ipc_server_open(const char *directory);
/* Before stepping the event loop, publish only after schema and seed
 * transactions have committed. Not a worker-thread API. */
/* Handler runs on a bounded command worker, never the socket loop. */
void strappy_ipc_server_handler(strappy_ipc_server *server,
    void (*handler)(const strappy_ipc_message *, strappy_ipc_message *, void *),
    void *context);
void strappy_ipc_server_store_ready(strappy_ipc_server *server);
/* Single event-loop thread only. Wait at most timeout_ms (0..1000).
 * wake_fd is the daemon's signal pipe, or -1. */
/* timeout_ms=-1 sleeps until I/O, worker completion, signal or peer deadline. */
int strappy_ipc_server_step(strappy_ipc_server *server, int wake_fd, int timeout_ms);
/* Thread-safe worker boundary: call ONLY AFTER the state transaction commits.
 * Coalesced invalidations cover an entire topic, never individual ledger rows.
 * No callbacks, provider work, or database operations run on the IPC loop. */
int strappy_ipc_server_committed(strappy_ipc_server *server, uint32_t event);
/* Stop/join workers before close. */
void strappy_ipc_server_drain(strappy_ipc_server *server);
void strappy_ipc_server_close(strappy_ipc_server *server);

/* Blocking caller-thread primitives; each complete send/read has a bounded
 * timeout. Short UI control calls intentionally wait on the GUI main thread;
 * prompt, monitor, and network-authentication work uses independent threads.
 * expected_uid authenticates the server (normally the client's own euid).
 * Close/reconnect after ANY I/O failure, then hello + subscribe/snapshot. */
int strappy_ipc_connect(const char *path, uid_t expected_uid, int timeout_ms);
int strappy_ipc_send(int fd, const strappy_ipc_message *message, int timeout_ms);
/* Receives bounded control messages/replies only; oversized input fails before
 * reading the body. The server owns its separate incremental request reader. */
int strappy_ipc_receive(int fd, strappy_ipc_message *message, int timeout_ms);

#endif
