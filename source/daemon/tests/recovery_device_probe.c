/* Provider-free native recovery fixture. Use ONLY with an isolated test store;
 * seed/check run while its daemon is stopped. bash mode is intentionally killed. */
#include "../strappy_bash.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *q = NULL;
    int ok = sqlite3_prepare_v2(db,sql,-1,&q,NULL) == SQLITE_OK &&
        sqlite3_step(q) == SQLITE_ROW && sqlite3_column_int(q,0) == 1;
    sqlite3_finalize(q); return ok;
}
int main(int argc, char **argv)
{
    sqlite3 *db = NULL;
    int ok;
    if (argc != 4 || argv[2][0] != '/' || argv[3][0] != '/') return 2;
    if (!strcmp(argv[1],"bash")) {
        int truncated = 0, cancelled = 0;
        char *error = NULL;
        char *output = strappy_bash_execute(argv[2],800,
            "{\"command\":\"echo $$ > tool.pid; sleep 60\"}",NULL,NULL,
            &truncated,&cancelled,&error);
        free(output); free(error); return 0;
    }
    if (sqlite3_open(argv[2],&db) != SQLITE_OK) return 1;
    sqlite3_exec(db,"PRAGMA foreign_keys=ON",NULL,NULL,NULL);
    if (!strcmp(argv[1],"seed")) {
        char *sql = sqlite3_mprintf(
            "BEGIN;"
            "INSERT INTO provider_accounts(id,provider_id,display_name,created_at_ms,updated_at_ms) VALUES('recovery','other','Fixture',1,1);"
            "INSERT INTO models(id,provider_id,wire_model_id,name,last_seen_at_ms) VALUES('other:recovery','other','recovery','Fixture',1);"
            "INSERT INTO sessions(id,created_at_ms,updated_at_ms) VALUES(800,1,1);"
            "INSERT INTO session_settings(session_id,bash_enabled,updated_at_ms) VALUES(800,1,1);"
            "INSERT INTO session_working_directories VALUES(800,%Q,1);"
            "INSERT INTO prompt_runs(session_id,request_key,prompt,state,created_at_ms,updated_at_ms) VALUES(800,'native-recovery','Do not replay',2,1,1);"
            "INSERT INTO turns(id,session_id,ordinal,prompt_group_key,created_at_ms) VALUES(800,800,0,'native',1);"
            "INSERT INTO model_requests(id,turn_id,round_index,request_kind,model_id,provider_account_id,created_at_ms,state) VALUES(800,800,0,'responses','other:recovery','recovery',1,'running');"
            "INSERT INTO http_attempts(id,request_id,provider_account_id,attempt_index,method,endpoint,started_at_ms,state) VALUES(800,800,'recovery',0,'POST','https://invalid.invalid',1,'running');"
            "INSERT INTO conversation_items(id,session_id,turn_id,sequence,kind,source_attempt_id,created_at_ms) VALUES(800,800,800,1,'function_call',800,1);"
            "INSERT INTO function_calls(item_id,provider_call_id,tool_name) VALUES(800,'native-tool','bash');"
            "INSERT INTO tool_executions(function_call_item_id,response_attempt_id,output_index,state,started_at_ms) VALUES(800,800,0,'running',1);"
            "INSERT INTO scan_roots(id,path) VALUES(800,'/native-recovery');"
            "INSERT INTO scan_runs(scan_root_id,state,started_at_ms) VALUES(800,'running',1);"
            "COMMIT;",argv[3]);
        ok = sql && sqlite3_exec(db,sql,NULL,NULL,NULL) == SQLITE_OK;
        sqlite3_free(sql);
    } else {
        ok = check(db,"SELECT state=6 AND retry_until_ms=0 FROM prompt_runs WHERE session_id=800") &&
            check(db,"SELECT state='error' FROM turns WHERE id=800") &&
            check(db,"SELECT state='error' FROM model_requests WHERE id=800") &&
            check(db,"SELECT state='transport_error' FROM http_attempts WHERE id=800") &&
            check(db,"SELECT state='error' AND error_code='service_interrupted' FROM tool_executions WHERE function_call_item_id=800") &&
            check(db,"SELECT state='error' FROM scan_runs WHERE scan_root_id=800") &&
            check(db,"SELECT execution_state='error' AND error_code='service_interrupted' FROM function_outputs WHERE function_call_item_id=800") &&
            check(db,"SELECT COUNT(*)=0 FROM pragma_foreign_key_check") &&
            check(db,"SELECT user_version=1 FROM pragma_user_version");
    }
    sqlite3_close(db);
    puts(ok ? "Native recovery fixture passed." : "Native recovery fixture failed.");
    return ok ? 0 : 1;
}
