/* Disposable fixtures only. --legacy leaves main's inline UNIQUE shape for
 * separate UI-reader and daemon-readiness probes. No product migration here. */
#include "strappy_db_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void sql(sqlite3 *db, const char *text)
{ assert(sqlite3_exec(db,text,NULL,NULL,NULL)==SQLITE_OK); }
static char *schema(sqlite3 *db)
{
    sqlite3_stmt *stmt=NULL;
    char *value;
    assert(sqlite3_prepare_v2(db,"SELECT group_concat(sql,';') FROM (SELECT sql FROM sqlite_master WHERE sql IS NOT NULL ORDER BY type,name)",-1,&stmt,NULL)==SQLITE_OK);
    assert(sqlite3_step(stmt)==SQLITE_ROW);
    value=sqlite3_mprintf("%s",sqlite3_column_text(stmt,0));
    assert(value); sqlite3_finalize(stmt); return value;
}
static void rejected(const char *path, sqlite3 *db)
{
    char *error=NULL, *before=schema(db), *after;
    sqlite3_stmt *stmt=NULL;
    assert(!strappy_db_check_reader(path,&error));
    assert(error && strstr(error,"incompatible UNIQUE") && strstr(error,"No migration"));
    free(error); error=NULL;
    assert(!strappy_db_initialize(path,&error));
    assert(error && strstr(error,"incompatible UNIQUE")); free(error);
    after=schema(db); assert(!strcmp(before,after)); sqlite3_free(before); sqlite3_free(after);
    assert(sqlite3_prepare_v2(db,"PRAGMA user_version",-1,&stmt,NULL)==SQLITE_OK);
    assert(sqlite3_step(stmt)==SQLITE_ROW && sqlite3_column_int(stmt,0)==1); sqlite3_finalize(stmt);
    assert(sqlite3_prepare_v2(db,"SELECT count(*) FROM function_calls WHERE item_id=1 AND provider_call_id='preserve-me' AND tool_name='fixture'",-1,&stmt,NULL)==SQLITE_OK);
    assert(sqlite3_step(stmt)==SQLITE_ROW && sqlite3_column_int(stmt,0)==1); sqlite3_finalize(stmt);
}
int main(int argc, char **argv)
{
    sqlite3 *db=NULL;
    char *error=NULL;
    int legacy=argc==3 && !strcmp(argv[2],"--legacy");
    assert(argc==2 || legacy);
    assert(strappy_db_initialize(argv[1],&error));
    assert(strappy_db_check_reader(argv[1],&error));
    assert(sqlite3_open(argv[1],&db)==SQLITE_OK);
    sql(db,"INSERT INTO function_calls VALUES(1,'preserve-me','fixture',NULL)");
    sql(db,"CREATE UNIQUE INDEX \"quoted ' provider\" ON function_calls(provider_call_id)");
    rejected(argv[1],db);
    sql(db,"DROP INDEX \"quoted ' provider\"");
    /* Equivalent composite/partial restrictions must not evade detection. */
    sql(db,"CREATE UNIQUE INDEX composite_call ON function_calls(provider_call_id,tool_name)");
    rejected(argv[1],db); sql(db,"DROP INDEX composite_call");
    sql(db,"CREATE UNIQUE INDEX partial_call ON function_calls(provider_call_id) WHERE tool_name='fixture'");
    rejected(argv[1],db); sql(db,"DROP INDEX partial_call");
    sql(db,"CREATE UNIQUE INDEX redundant_call ON function_calls(provider_call_id,item_id)");
    assert(strappy_db_check_reader(argv[1],&error));
    sql(db,"INSERT INTO function_calls VALUES(2,'preserve-me','fixture',NULL)");
    sql(db,"DELETE FROM function_calls WHERE item_id=2; DROP INDEX redundant_call");
    /* Exact main table definition; retain the synthetic row across replacement. */
    sql(db,"CREATE TEMP TABLE saved_calls AS SELECT * FROM function_calls; DROP TABLE function_calls;"
      "CREATE TABLE function_calls (item_id INTEGER PRIMARY KEY, provider_call_id TEXT NOT NULL UNIQUE,"
      "tool_name TEXT NOT NULL,tool_namespace TEXT,FOREIGN KEY(item_id) REFERENCES conversation_items(id) ON DELETE CASCADE);"
      "INSERT INTO function_calls SELECT * FROM saved_calls; DROP TABLE saved_calls;");
    rejected(argv[1],db);
    sql(db,"PRAGMA wal_checkpoint(TRUNCATE)");
    sqlite3_close(db);
    printf("schema_harness: SQLite %s; fresh schema, inline/named/composite/partial constraints, quoted names, redundant indexes, reader/writer rejection and data preservation passed\n",sqlite3_libversion());
    return 0;
}
