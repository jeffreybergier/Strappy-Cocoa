/* Native offline acceptance test. Run ONLY against a fresh fixture daemon.
 * Uses the production IPC client, read-only SQLite and a loopback provider.
 * Never reads the user's store or configures a provider credential. */
#include "strappy_ipc.h"
#include <cJSON.h>
#include <sqlite3.h>
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static const char *socket_path, *database_path;
static char *prompt;
static int listener, requests;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static void transfer(int fd, void *buffer, size_t size, int writing)
{
    unsigned char *bytes = buffer;
    while (size) {
        struct pollfd item;
        ssize_t n;
        item.fd=fd; item.events=writing ? POLLOUT : POLLIN; item.revents=0;
        if (poll(&item,1,30000) < 0 && errno == EINTR) continue;
        assert(item.revents & item.events);
        n = writing ? write(fd,bytes,size) : read(fd,bytes,size);
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        assert(n > 0); bytes += n; size -= (size_t)n;
    }
}
static int contains_prompt(const cJSON *node)
{
    const cJSON *child;
    if (cJSON_IsString(node) && !strcmp(node->valuestring,prompt)) return 1;
    for (child = node->child; child; child = child->next)
        if (contains_prompt(child)) return 1;
    return 0;
}
static void *provider(void *unused)
{
    int round;
    (void)unused;
    for (round = 0; round < 2; ++round) {
        int fd = accept(listener,NULL,NULL);
        char header[16384], response[1024], framing[256], *body, *length;
        size_t used = 0, size;
        cJSON *json;
        assert(fd >= 0);
        while (used < sizeof(header)-1) {
            transfer(fd,header+used,1,0); header[++used] = 0;
            if (used >= 4 && !strcmp(header+used-4,"\r\n\r\n")) break;
        }
        assert(used < sizeof(header)-1);
        /* Fixture accounts have no bearer credential; never accept a secret. */
        assert(!strstr(header,"Authorization:") && !strstr(header,"authorization:"));
        length = strstr(header,"Content-Length:");
        if (!length) length = strstr(header,"content-length:");
        assert(length); size = (size_t)strtoul(length+15,NULL,10);
        assert(size > STRAPPY_IPC_MAX_PROMPT_BYTES && size < 8U*1024U*1024U);
        if (strstr(header,"100-continue")) {
            char interim[] = "HTTP/1.1 100 Continue\r\n\r\n";
            transfer(fd,interim,sizeof(interim)-1,1);
        }
        body = malloc(size+1); assert(body);
        transfer(fd,body,size,0); body[size] = 0;
        json = cJSON_Parse(body); assert(json && contains_prompt(json));
        cJSON_Delete(json); free(body);
        pthread_mutex_lock(&mutex); ++requests; pthread_mutex_unlock(&mutex);
        snprintf(response,sizeof(response),"{\"id\":\"native-large-%d\",\"object\":\"response\",\"status\":\"completed\",\"output\":[{\"type\":\"message\",\"id\":\"message-%d\",\"role\":\"assistant\",\"status\":\"completed\",\"content\":[{\"type\":\"output_text\",\"text\":\"Native large prompt passed.\",\"annotations\":[]}]}]}",round,round);
        snprintf(framing,sizeof(framing),"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n",(unsigned long)strlen(response));
        transfer(fd,framing,strlen(framing),1); transfer(fd,response,strlen(response),1);
        close(fd);
    }
    return NULL;
}
static int connect_service(void)
{
    strappy_ipc_message m;
    int fd = strappy_ipc_connect(socket_path,geteuid(),3000);
    assert(fd >= 0);
    memset(&m,0,sizeof(m)); m.kind = STRAPPY_IPC_REQUEST; m.operation = STRAPPY_IPC_HELLO;
    m.request_id = 1; m.length = 8;
    strappy_ipc_put32(m.payload,STRAPPY_IPC_VERSION); strappy_ipc_put32(m.payload+4,STRAPPY_IPC_VERSION);
    assert(!strappy_ipc_send(fd,&m,3000) && !strappy_ipc_receive(fd,&m,3000));
    assert(m.status == STRAPPY_IPC_OK && m.length == 36 && strappy_ipc_get32(m.payload+24) == 7);
    return fd;
}
static unsigned int exchange(int fd, unsigned int operation, unsigned int ident,
    const unsigned char *payload, unsigned int length, strappy_ipc_message *reply)
{
    strappy_ipc_message m;
    memset(&m,0,sizeof(m)); m.kind = STRAPPY_IPC_REQUEST; m.operation = operation;
    m.request_id = ident; m.length = length; m.external_payload = payload;
    assert(!strappy_ipc_send(fd,&m,30000));
    assert(!strappy_ipc_receive(fd,reply,30000));
    assert(reply->kind == STRAPPY_IPC_REPLY && reply->operation == operation && reply->request_id == ident);
    return reply->status;
}
static char *command(const char *text)
{
    static unsigned int key_number;
    strappy_ipc_message reply;
    int fd = connect_service(), credential, i;
    char *result, *json=NULL, key[65];
    cJSON *command=cJSON_Parse(text), *snapshot;
    const char *action=cJSON_GetObjectItemCaseSensitive(command,"action")->valuestring;
    credential=!strcmp(action,"update_account") || !strcmp(action,"archive_account");
    if (credential) {
        snprintf(key,sizeof(key),"%064x",++key_number);
        cJSON_AddStringToObject(command,"credential_action",action);
        cJSON_ReplaceItemInObjectCaseSensitive(command,"action",cJSON_CreateString("credential_start"));
        cJSON_AddStringToObject(command,"key",key); json=cJSON_PrintUnformatted(command);
        text=json;
    }
    assert(exchange(fd,credential ? STRAPPY_IPC_AUTH : STRAPPY_IPC_MUTATE,2,
        (const unsigned char *)text,(unsigned int)strlen(text),&reply)==STRAPPY_IPC_OK);
    free(json); json=NULL;
    if (credential) {
        cJSON_ReplaceItemInObjectCaseSensitive(command,"action",cJSON_CreateString("auth_status"));
        json=cJSON_PrintUnformatted(command);
        for (i=0;i<300;++i) {
            assert(reply.length<sizeof(reply.payload)); reply.payload[reply.length]=0;
            snapshot=cJSON_Parse((char *)reply.payload); assert(snapshot);
            action=cJSON_GetObjectItemCaseSensitive(snapshot,"state")->valuestring;
            if (!strcmp(action,"completed")) { cJSON_Delete(snapshot); break; }
            assert(!strcmp(action,"running") || !strcmp(action,"queued")); cJSON_Delete(snapshot);
            usleep(100000);
            assert(exchange(fd,STRAPPY_IPC_AUTH,(unsigned int)i+3,(unsigned char *)json,(unsigned int)strlen(json),&reply)==STRAPPY_IPC_OK);
        }
        assert(i<300); free(json);
    }
    cJSON_Delete(command);
    close(fd); result = malloc((size_t)reply.length+1); assert(result);
    memcpy(result,reply.payload,reply.length); result[reply.length] = 0; return result;
}
static int state(sqlite3 *db, const char *key, int expected_rounds)
{
    sqlite3_stmt *q = NULL;
    int value = 0, rc;
    assert(sqlite3_prepare_v2(db,"SELECT r.state,r.prompt,s.round_limit FROM prompt_runs r JOIN session_settings s ON s.session_id=r.session_id WHERE r.request_key=?",-1,&q,NULL) == SQLITE_OK);
    sqlite3_bind_text(q,1,key,-1,SQLITE_STATIC); rc = sqlite3_step(q);
    if (rc == SQLITE_ROW) {
        value = sqlite3_column_int(q,0);
        assert(sqlite3_column_bytes(q,1) == (int)STRAPPY_IPC_MAX_PROMPT_BYTES);
        assert(!memcmp(sqlite3_column_text(q,1),prompt,STRAPPY_IPC_MAX_PROMPT_BYTES));
        assert(sqlite3_column_int(q,2) == expected_rounds);
    } else assert(rc == SQLITE_DONE);
    sqlite3_finalize(q); return value;
}
static void wait_completed(sqlite3 *db, const char *key)
{
    int i, value = 0;
    for (i=0; i<600; ++i) {
        value = state(db,key,7);
        if (value >= 3) break;
        usleep(100000);
    }
    printf("native run terminal state=%d\n",value); fflush(stdout);
    assert(value == 3);
}
int main(int argc, char **argv)
{
    struct sockaddr_in address;
    socklen_t address_size = sizeof(address);
    pthread_t thread;
    sqlite3 *db = NULL;
    sqlite3_stmt *q = NULL;
    char json[4096], key[65], *account, *model, *session;
    unsigned char *payload;
    unsigned int length = STRAPPY_IPC_OPTIONS_PREFIX + STRAPPY_IPC_MAX_OPTIONS_BYTES + STRAPPY_IPC_MAX_PROMPT_BYTES;
    unsigned int i;
    int fd, count;
    strappy_ipc_message m, reply;
    assert(argc == 4 && !strchr(argv[3],'"') && !strchr(argv[3],'\\'));
    socket_path = argv[1]; database_path = argv[2];
    signal(SIGPIPE,SIG_IGN); alarm(180);
    assert(sqlite3_open_v2(database_path,&db,SQLITE_OPEN_READONLY,NULL) == SQLITE_OK);
    sqlite3_busy_timeout(db,3000);
    assert(sqlite3_prepare_v2(db,"SELECT count(*) FROM provider_accounts",-1,&q,NULL) == SQLITE_OK);
    assert(sqlite3_step(q) == SQLITE_ROW && sqlite3_column_int(q,0) == 0); sqlite3_finalize(q);
    prompt = malloc(STRAPPY_IPC_MAX_PROMPT_BYTES+1); payload = malloc((size_t)length+1);
    assert(prompt && payload);
    for (i=0; i<STRAPPY_IPC_MAX_PROMPT_BYTES; i+=2) { prompt[i]=(char)0xc3; prompt[i+1]=(char)0xa9; }
    prompt[STRAPPY_IPC_MAX_PROMPT_BYTES] = 0;
    listener = socket(AF_INET,SOCK_STREAM,0); assert(listener >= 0);
    memset(&address,0,sizeof(address)); address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(!bind(listener,(struct sockaddr *)&address,sizeof(address)) && !listen(listener,2));
    assert(!getsockname(listener,(struct sockaddr *)&address,&address_size));
    assert(!pthread_create(&thread,NULL,provider,NULL));
    account = command("{\"action\":\"create_account\",\"id\":\"other\",\"name\":\"Native large prompt fixture\"}");
    snprintf(json,sizeof(json),"{\"action\":\"update_account\",\"id\":\"%s\",\"name\":\"Native fixture\",\"endpoint\":\"http://127.0.0.1:%u/responses\",\"output\":\"4096\"}",account,(unsigned int)ntohs(address.sin_port)); free(command(json));
    model = command("{\"action\":\"create_model\",\"id\":\"other\",\"wire\":\"native-large\",\"name\":\"Native large fixture\",\"context\":\"2097152\",\"output\":\"4096\",\"reasoning\":\"0\",\"image\":\"0\",\"functions\":\"1\"}");
    snprintf(json,sizeof(json),"{\"action\":\"approve_model\",\"id\":\"%s\",\"value\":\"1\"}",model); free(command(json));
    snprintf(json,sizeof(json),"{\"action\":\"defaults\",\"model\":\"%s\",\"account\":\"%s\",\"assistant\":\"world_knowledge\",\"directory\":\"%s\",\"fallback\":\"%s\",\"mask\":\"1023\",\"rounds\":\"3\",\"web_provider\":\"none\",\"web\":\"0\",\"bash\":\"0\",\"one_tool\":\"0\",\"quality\":\"0\"}",model,account,argv[3],argv[3]); free(command(json)); free(model);
    snprintf(json,sizeof(json),"{\"action\":\"create_session\",\"directory\":\"%s\"}",argv[3]); session = command(json);
    memset(key,'a',64); key[64]=0;
    strappy_ipc_put32(payload,0); strappy_ipc_put32(payload+4,(uint32_t)strtoul(session,NULL,10)); memcpy(payload+8,key,64);
    snprintf(json,sizeof(json),"{\"action\":\"options\",\"session\":\"%s\",\"mask\":\"256\",\"rounds\":\"7\",\"web\":\"0\",\"bash\":\"0\",\"one_tool\":\"0\",\"quality\":\"0\",\"web_provider\":\"none\"}",session);
    strappy_ipc_put32(payload+72,4096); memset(payload+76,' ',4096); memcpy(payload+76,json,strlen(json));
    memcpy(payload+76+4096,prompt,STRAPPY_IPC_MAX_PROMPT_BYTES);
    /* Leave a full-size transfer one byte short. Healthy clients still work. */
    fd = connect_service(); memset(&m,0,sizeof(m)); m.kind=1; m.operation=28; m.request_id=2; m.length=length;
    { unsigned char header[24]; strappy_ipc_encode(header,&m); transfer(fd,header,sizeof(header),1); }
    transfer(fd,payload,length-1,1);
    { int healthy=connect_service(); assert(exchange(healthy,26,2,NULL,0,&reply)==0); close(healthy); }
    assert(state(db,key,3)==0); close(fd);
    assert(sqlite3_prepare_v2(db,"SELECT round_limit FROM session_settings",-1,&q,NULL)==SQLITE_OK);
    assert(sqlite3_step(q)==SQLITE_ROW && sqlite3_column_int(q,0)==3); sqlite3_finalize(q);
    /* Separate text/options limits, malformed UTF-8, and oversized headers. */
    fd=connect_service(); strappy_ipc_put32(payload+72,4097);
    assert(exchange(fd,28,2,payload,length,&reply)==STRAPPY_IPC_INVALID);
    strappy_ipc_put32(payload+72,0); memcpy(payload+76,prompt,STRAPPY_IPC_MAX_PROMPT_BYTES); payload[76+STRAPPY_IPC_MAX_PROMPT_BYTES]='x';
    assert(exchange(fd,28,3,payload,77+STRAPPY_IPC_MAX_PROMPT_BYTES,&reply)==STRAPPY_IPC_INVALID);
    payload[75+STRAPPY_IPC_MAX_PROMPT_BYTES]=0xff;
    assert(exchange(fd,28,4,payload,76+STRAPPY_IPC_MAX_PROMPT_BYTES,&reply)==STRAPPY_IPC_INVALID);
    close(fd); assert(state(db,key,3)==0);
    fd=connect_service(); m.operation=25; m.length=4097;
    { unsigned char header[24], byte; strappy_ipc_encode(header,&m); transfer(fd,header,sizeof(header),1); struct pollfd item; item.fd=fd; item.events=POLLIN; item.revents=0; assert(poll(&item,1,3000)>0); assert(read(fd,&byte,1)==0); } close(fd);
    strappy_ipc_put32(payload+72,4096); memset(payload+76,' ',4096); memcpy(payload+76,json,strlen(json)); memcpy(payload+4172,prompt,STRAPPY_IPC_MAX_PROMPT_BYTES);
    /* Lose the acknowledgment, then retry the same maximum-size request. */
    fd=connect_service(); m.operation=28; m.length=length; m.external_payload=payload;
    assert(!strappy_ipc_send(fd,&m,30000)); close(fd); wait_completed(db,key);
    fd=connect_service(); assert(exchange(fd,28,2,payload,length,&reply)==0); close(fd);
    pthread_mutex_lock(&mutex); count=requests; pthread_mutex_unlock(&mutex); assert(count==1);
    /* Plain submissions also support 1 MiB, and the connection remains usable. */
    memset(key,'b',64); memcpy(payload+8,key,64); memcpy(payload+72,prompt,STRAPPY_IPC_MAX_PROMPT_BYTES);
    fd=connect_service(); assert(exchange(fd,16,2,payload,72+STRAPPY_IPC_MAX_PROMPT_BYTES,&reply)==0);
    assert(exchange(fd,26,3,NULL,0,&reply)==0); close(fd); wait_completed(db,key);
    pthread_join(thread,NULL); assert(requests==2);
    assert(sqlite3_prepare_v2(db,"PRAGMA integrity_check",-1,&q,NULL)==SQLITE_OK);
    assert(sqlite3_step(q)==SQLITE_ROW && !strcmp((const char *)sqlite3_column_text(q,0),"ok")); sqlite3_finalize(q);
    printf("PASS: uid=%lu pointer_bits=%lu; 1 MiB UTF-8 + 4 KiB options, partial transfer atomicity, size/UTF-8 rejection, lost-ack dedup, plain submission, exact provider bytes, SQLite integrity; requests=%d\n",(unsigned long)geteuid(),(unsigned long)(sizeof(void *)*8),requests);
    snprintf(json,sizeof(json),"{\"action\":\"archive_account\",\"id\":\"%s\"}",account); free(command(json)); free(account);
    free(session); free(payload); free(prompt); sqlite3_close(db); close(listener); return 0;
}
