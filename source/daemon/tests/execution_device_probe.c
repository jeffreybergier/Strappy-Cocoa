/* Native device validation client. All fixture writes are explicit and target
 * a fresh store; source application history and credentials are never copied. */
#include "strappy_db_internal.h"
#include "strappy_ipc.h"
#include "strappy_run_execution.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int seed(const char *source, const char *target, const char *directory)
{
    sqlite3 *db = NULL; sqlite3_stmt *q = NULL;
    char *uri; long long session = 0; int ok;
    if (access(target, F_OK) == 0 || !strappy_db_initialize(target, NULL)) return 1;
    if (sqlite3_open_v2(target, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_URI, NULL) != SQLITE_OK) return 1;
    uri = sqlite3_mprintf("file:%s?mode=ro", source);
    ok = sqlite3_prepare_v2(db, "ATTACH DATABASE ? AS source", -1, &q, NULL) == SQLITE_OK;
    if (ok) { sqlite3_bind_text(q, 1, uri, -1, SQLITE_TRANSIENT); ok = sqlite3_step(q) == SQLITE_DONE; }
    sqlite3_free(uri); sqlite3_finalize(q);
    if (ok) ok = sqlite3_exec(db,
        "BEGIN;"
        "INSERT OR REPLACE INTO provider_accounts SELECT * FROM source.provider_accounts;"
        "INSERT OR REPLACE INTO models SELECT * FROM source.models;"
        "INSERT OR REPLACE INTO model_capabilities SELECT * FROM source.model_capabilities;"
        "INSERT OR REPLACE INTO model_preferences SELECT * FROM source.model_preferences;"
        "INSERT OR REPLACE INTO model_hosted_tools SELECT * FROM source.model_hosted_tools;"
        "INSERT OR REPLACE INTO model_reasoning_overrides SELECT * FROM source.model_reasoning_overrides;"
        "INSERT OR REPLACE INTO app_preferences SELECT * FROM source.app_preferences;"
        "UPDATE default_session_options SET assistant_set_id='world_knowledge',round_limit=3,web_provider='none',bash_enabled=0,answer_quality_enabled=0;"
        "COMMIT;", NULL, NULL, NULL) == SQLITE_OK;
    sqlite3_close(db);
    if (ok) ok = strappy_db_create_session_with_working_directory(target, directory, &session, NULL);
    printf("fixture seed: %s session=%lld\n", ok ? "passed" : "failed", session);
    return ok ? 0 : 1;
}
static int command(const char *socket, const char *session_text, const char *key, const char *prompt)
{
    strappy_ipc_message m;
    uint64_t session = (uint64_t)strtoll(session_text, NULL, 10);
    int fd = strappy_ipc_connect(socket, geteuid(), 3000), ok;
    if (fd < 0 || strlen(key) != 64 || strlen(prompt) > 4024) return 1;
    memset(&m, 0, sizeof(m)); m.kind = STRAPPY_IPC_REQUEST; m.operation = STRAPPY_IPC_HELLO;
    m.request_id = 1; m.length = 8;
    strappy_ipc_put32(m.payload, STRAPPY_IPC_VERSION); strappy_ipc_put32(m.payload+4, STRAPPY_IPC_VERSION);
    ok = strappy_ipc_send(fd, &m, 3000) == 0 && strappy_ipc_receive(fd, &m, 3000) == 0 && m.status == 0;
    if (ok) {
        memset(&m, 0, sizeof(m)); m.kind = STRAPPY_IPC_REQUEST; m.operation = STRAPPY_IPC_SUBMIT_PROMPT;
        m.request_id = 2; m.length = (uint32_t)strlen(prompt) + 72;
        strappy_ipc_put32(m.payload, (uint32_t)(session >> 32)); strappy_ipc_put32(m.payload+4, (uint32_t)session);
        memcpy(m.payload+8, key, 64); memcpy(m.payload+72, prompt, strlen(prompt));
        ok = strappy_ipc_send(fd, &m, 3000) == 0 && strappy_ipc_receive(fd, &m, 10000) == 0 && m.status == 0;
        if (ok) printf("accepted run=%lu state=%lu\n", (unsigned long)strappy_ipc_get32(m.payload+4), (unsigned long)strappy_ipc_get32(m.payload+8));
        else printf("submission failed status=%lu\n", (unsigned long)m.status);
    }
    close(fd); return ok ? 0 : 1;
}
int main(int argc, char **argv)
{
    if (argc == 5 && strcmp(argv[1], "seed") == 0) return seed(argv[2], argv[3], argv[4]);
    if (argc == 6 && strcmp(argv[1], "submit") == 0) return command(argv[2], argv[3], argv[4], argv[5]);
    if (argc == 4 && strcmp(argv[1], "history") == 0) {
        strappy_session_message_record_list list;
        size_t i; int matched = 0;
        strappy_session_message_record_list_init(&list);
        if (!strappy_db_list_response_timeline(argv[2], strtoll(argv[3], NULL, 10), &list, NULL)) return 1;
        for (i = 0; i < list.count; ++i)
            if (list.records[i].role && strcmp(list.records[i].role, "assistant") == 0 &&
                list.records[i].content && strstr(list.records[i].content, "DAEMON_STEP3_OK")) matched = 1;
        printf("history: rows=%lu expected_assistant_answer=%d\n", (unsigned long)list.count, matched);
        strappy_session_message_record_list_destroy(&list); return matched ? 0 : 1;
    }
    if (argc == 4 && strcmp(argv[1], "state") == 0) {
        strappy_run_record run;
        if (!strappy_run_latest(argv[2], strtoll(argv[3], NULL, 10), &run)) return 1;
        printf("run=%lld state=%d cancel=%d\n", run.id, run.state, run.cancel_requested);
        strappy_run_destroy(&run); return 0;
    }
    return 1;
}
