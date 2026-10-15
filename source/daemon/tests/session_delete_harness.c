/* Run only against a new disposable fixture path. No schema migration. */
#include "strappy_db_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void sql(sqlite3 *db, const char *text)
{
  assert(sqlite3_exec(db, text, NULL, NULL, NULL) == SQLITE_OK);
}

static int scalar(sqlite3 *db, const char *text)
{
  sqlite3_stmt *stmt = NULL;
  int value;
  assert(sqlite3_prepare_v2(db, text, -1, &stmt, NULL) == SQLITE_OK);
  assert(sqlite3_step(stmt) == SQLITE_ROW);
  value = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  return value;
}

int main(int argc, char **argv)
{
  sqlite3 *db = NULL;
  char *error = NULL;
  int schema_version;
  assert(argc == 2 && access(argv[1], F_OK) != 0);
  assert(strappy_db_initialize(argv[1], &error));
  assert(sqlite3_open(argv[1], &db) == SQLITE_OK);
  sql(db, "INSERT INTO sessions(id,created_at_ms,updated_at_ms) "
    "VALUES(1,1,1),(2,1,1);"
    "INSERT INTO prompt_runs(session_id,request_key,prompt,state,"
    "created_at_ms,updated_at_ms) VALUES"
    "(1,'keep','Other conversation',3,1,1),"
    "(2,'complete','Completed prompt',3,1,1),"
    "(2,'failed','Failed prompt',4,1,1),"
    "(2,'cancelled','Cancelled prompt',5,1,1),"
    "(2,'interrupted','Interrupted prompt',6,1,1);");
  schema_version = scalar(db, "PRAGMA schema_version");
  assert(scalar(db, "SELECT count(*) FROM pragma_foreign_key_list('prompt_runs')") == 0);
  /* Failure after run deletion must restore both the session and every run. */
  sql(db, "CREATE TRIGGER reject_session_delete BEFORE DELETE ON sessions "
    "BEGIN SELECT RAISE(ABORT,'fixture rejection'); END;");
  assert(!strappy_db_delete_session(argv[1], 2, &error));
  assert(error != NULL); free(error); error = NULL;
  assert(scalar(db, "SELECT count(*) FROM sessions") == 2);
  assert(scalar(db, "SELECT count(*) FROM prompt_runs") == 5);
  sql(db, "DROP TRIGGER reject_session_delete;");
  schema_version += 2;
  assert(strappy_db_delete_session(argv[1], 2, &error));
  assert(scalar(db, "SELECT count(*) FROM prompt_runs WHERE session_id=2") == 0);
  assert(scalar(db, "SELECT count(*) FROM sessions WHERE id=2") == 0);
  assert(scalar(db, "SELECT count(*) FROM prompt_runs WHERE session_id=1") == 1);
  assert(!strappy_db_delete_session(argv[1], 2, &error));
  free(error); error = NULL;
  sql(db, "INSERT INTO sessions(created_at_ms,updated_at_ms) VALUES(1,1);");
  assert(sqlite3_last_insert_rowid(db) == 2);
  assert(scalar(db, "SELECT count(*) FROM prompt_runs WHERE session_id=2") == 0);
  assert(scalar(db, "PRAGMA user_version") == 1);
  assert(scalar(db, "PRAGMA schema_version") == schema_version);
  sqlite3_close(db);
  strappy_db_shutdown_connection();
  printf("PASS session deletion: SQLite %s; runs removed, rollback preserves "
    "rows, other session preserved, reused ID clean, schema unchanged\n",
    sqlite3_libversion());
  return 0;
}
