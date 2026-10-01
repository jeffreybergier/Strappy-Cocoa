/* Provider-free native Step 4 probe. Use ONLY with a fresh isolated daemon store.
 * The fixture directory must contain scan/fixture.sqlite and work/. */
#include "../shared/strappy_ipc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *socket_path;
static int command(unsigned int operation, const char *json, char *result, size_t capacity)
{
    strappy_ipc_message m;
    int fd = strappy_ipc_connect(socket_path,geteuid(),3000), ok;
    if (fd < 0) return 0;
    memset(&m,0,sizeof(m)); m.kind = STRAPPY_IPC_REQUEST; m.operation = STRAPPY_IPC_HELLO;
    m.request_id = 1; m.length = 8;
    strappy_ipc_put32(m.payload,STRAPPY_IPC_VERSION); strappy_ipc_put32(m.payload+4,STRAPPY_IPC_VERSION);
    ok = strappy_ipc_send(fd,&m,3000) == 0 && strappy_ipc_receive(fd,&m,3000) == 0 && m.status == 0;
    if (ok) {
        memset(&m,0,sizeof(m)); m.kind = STRAPPY_IPC_REQUEST; m.operation = operation; m.request_id = 2;
        m.length = (uint32_t)strlen(json); memcpy(m.payload,json,m.length);
        ok = strappy_ipc_send(fd,&m,3000) == 0 && strappy_ipc_receive(fd,&m,10000) == 0 &&
            m.status == 0 && m.kind == STRAPPY_IPC_REPLY && m.operation == operation && m.request_id == 2;
        if (ok && result) {
            ok = m.length < capacity;
            if (ok) { memcpy(result,m.payload,m.length); result[m.length] = 0; }
        }
    }
    close(fd); return ok;
}
#define CHECK(expression) do { if (!(expression)) { fprintf(stderr,"Step 4 probe failed at line %d\n",__LINE__); return 1; } } while (0)
int main(int argc, char **argv)
{
    char account[256], model[256], session[64], json[4096], state[32];
    int count;
    if (argc != 3 || argv[2][0] != '/' || strchr(argv[2],'"') || strchr(argv[2],'\\') || strlen(argv[2]) > 512) return 2;
    socket_path = argv[1];
    CHECK(command(25,"{\"action\":\"create_account\",\"id\":\"other\",\"name\":\"Step 4 native fixture\"}",account,sizeof(account)));
    CHECK(command(25,"{\"action\":\"create_model\",\"id\":\"other\",\"wire\":\"step4-fixture\",\"name\":\"Native fixture\",\"context\":\"8192\",\"output\":\"4096\",\"reasoning\":\"0\",\"image\":\"0\",\"functions\":\"1\"}",model,sizeof(model)));
    snprintf(json,sizeof(json),"{\"action\":\"approve_model\",\"id\":\"%s\",\"value\":\"1\"}",model);
    CHECK(command(21,json,NULL,0));
    snprintf(json,sizeof(json),"{\"action\":\"defaults\",\"model\":\"%s\",\"account\":\"%s\",\"assistant\":\"world_knowledge\",\"directory\":\"%s/work\",\"fallback\":\"%s/work\",\"mask\":\"1023\",\"rounds\":\"3\",\"web_provider\":\"none\",\"web\":\"0\",\"bash\":\"0\",\"one_tool\":\"0\",\"quality\":\"0\"}",model,account,argv[2],argv[2]);
    CHECK(command(25,json,NULL,0));
    snprintf(json,sizeof(json),"{\"action\":\"create_session\",\"directory\":\"%s/work\"}",argv[2]);
    CHECK(command(19,json,session,sizeof(session)) && atoll(session) > 0);
    snprintf(json,sizeof(json),"{\"path\":\"%s/scan\",\"quick\":\"0\"}",argv[2]);
    CHECK(command(23,json,NULL,0));
    for (count = 0; count < 60; ++count) {
        CHECK(command(26,"",state,sizeof(state)));
        if (strappy_ipc_get32((unsigned char *)state) == 0) break;
        sleep(1);
    }
    CHECK(count < 60 && strappy_ipc_get32((unsigned char *)state+4) == 23 &&
        strappy_ipc_get32((unsigned char *)state+8) == 3);
    CHECK(command(22,"{\"action\":\"approve_database\",\"id\":\"1\",\"value\":\"1\"}",NULL,0));
    CHECK(command(25,"{\"action\":\"hide_database\",\"id\":\"1\",\"value\":\"1\"}",NULL,0));
    CHECK(command(25,"{\"action\":\"hide_database\",\"id\":\"1\",\"value\":\"0\"}",NULL,0));
    snprintf(json,sizeof(json),"{\"action\":\"delete_session\",\"session\":\"%s\"}",session);
    CHECK(command(20,json,NULL,0));
    CHECK(command(25,"{\"action\":\"archive_model\",\"id\":\"other\",\"wire\":\"step4-fixture\"}",NULL,0));
    snprintf(json,sizeof(json),"{\"action\":\"archive_account\",\"id\":\"%s\"}",account);
    CHECK(command(25,json,NULL,0));
    puts("Step 4 native probe passed: accounts, models, defaults, session create/delete, daemon scan, approvals, visibility, cross-connection status.");
    return 0;
}
