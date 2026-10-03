#include "../../shared/strappy_ipc.h"
#include "../../shared/XPFoundation/strappy_ipc_platform.h"
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
    int pair[2];
    unsigned char header[24];
    const unsigned char expected[24] = {
        'S','T','P','Y',0,8,0,1,0,0,0,16,1,2,3,4,0,0,0,3,0,0,0,0
    };
    strappy_ipc_message message, decoded;
    int64_t start;
    memset(&message, 0, sizeof(message)); message.kind = 1;
    message.operation = 16; message.request_id = 0x01020304; message.length = 3;
    memcpy(message.payload, "abc", 3);
    strappy_ipc_encode(header, &message);
    assert(memcmp(header, expected, 24) == 0);
    assert(strappy_ipc_decode(header, &decoded) == 0);
    assert(decoded.request_id == 0x01020304 && decoded.length == 3);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    assert(fcntl(pair[0], F_SETFL, O_NONBLOCK) == 0);
    assert(fcntl(pair[1], F_SETFL, O_NONBLOCK) == 0);
    assert(strappy_ipc_platform_peer_matches(pair[0], geteuid()));
    assert(!strappy_ipc_platform_peer_matches(pair[0], geteuid() + 1));
    assert(strappy_ipc_send(pair[0], &message, 100) == 0);
    assert(strappy_ipc_receive(pair[1], &decoded, 100) == 0);
    assert(decoded.length == 3 && memcmp(decoded.payload, "abc", 3) == 0);
    start = strappy_ipc_platform_milliseconds();
    assert(strappy_ipc_receive(pair[1], &decoded, 30) == -1);
    assert(strappy_ipc_platform_milliseconds() - start >= 20);
    assert(strappy_ipc_platform_milliseconds() - start < 1000);
    assert(strappy_ipc_send(pair[0], &message, 0) == -1);
    message.length = STRAPPY_IPC_MAX_PAYLOAD + 1;
    assert(strappy_ipc_send(pair[0], &message, 100) == -1);
    /* Oversized control/reply frames fail during header validation. A large
     * prompt header is valid, but the bounded client receive API must not copy
     * its body into the inline control buffer. */
    strappy_ipc_encode(header,&message);
    assert(strappy_ipc_decode(header,&decoded) == 0);
    assert(write(pair[0],header,sizeof(header)) == (ssize_t)sizeof(header));
    assert(strappy_ipc_receive(pair[1],&decoded,100) == -1);
    message.kind = STRAPPY_IPC_REPLY;
    strappy_ipc_encode(header,&message);
    assert(strappy_ipc_decode(header,&decoded) == -1);
    message.kind = STRAPPY_IPC_REQUEST; message.operation = STRAPPY_IPC_MUTATE;
    strappy_ipc_encode(header,&message);
    assert(strappy_ipc_decode(header,&decoded) == -1);
    message.operation = STRAPPY_IPC_SUBMIT_WITH_OPTIONS;
    message.length = STRAPPY_IPC_OPTIONS_PREFIX + STRAPPY_IPC_MAX_OPTIONS_BYTES + STRAPPY_IPC_MAX_PROMPT_BYTES;
    strappy_ipc_encode(header,&message);
    assert(strappy_ipc_decode(header,&decoded) == 0);
    ++message.length; strappy_ipc_encode(header,&message);
    assert(strappy_ipc_decode(header,&decoded) == -1);
    message.length = 0;
    (void)close(pair[1]);
    /* Library users must not need a process-wide SIGPIPE handler. */
    (void)signal(SIGPIPE, SIG_DFL);
    assert(strappy_ipc_send(pair[0], &message, 100) == -1);
    assert(strappy_ipc_receive(pair[0], &message, 100) == -1);
    (void)close(pair[0]);
    puts("ipc_client_harness: wire vector, credentials, round trip, deadlines, disconnect without SIGPIPE passed");
    return 0;
}
