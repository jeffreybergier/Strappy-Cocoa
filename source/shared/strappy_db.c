#include "strappy_db.h"

#include "strappy_core.h"

#include <sqlite3.h>
#include <stdlib.h>

static int strappy_db_open(const char *db_path,
                           sqlite3 **db_out,
                           char **error_out)
{
  sqlite3 *db;
  int flags;
  int rc;

  if (db_out == NULL) {
    strappy_set_error(error_out, "strappy_db_open received no output.");
    return 0;
  }
  *db_out = NULL;

  if ((db_path == NULL) || (db_path[0] == '\0')) {
    strappy_set_error(error_out, "Session database path is not configured.");
    return 0;
  }

  db = NULL;
  flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
  rc = sqlite3_open_v2(db_path, &db, flags, NULL);
  if (rc != SQLITE_OK) {
    const char *message = "unknown sqlite error";
    if (db != NULL) {
      message = sqlite3_errmsg(db);
    }
    strappy_set_formatted_error(error_out,
                                "Could not open session database: %s",
                                message);
    if (db != NULL) {
      sqlite3_close(db);
    }
    return 0;
  }

  sqlite3_busy_timeout(db, 5000);
  *db_out = db;
  return 1;
}

static int strappy_db_ensure_schema(sqlite3 *db, char **error_out)
{
  static const char *sql =
    "CREATE TABLE IF NOT EXISTS sessions ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "prompt TEXT NOT NULL,"
    "response TEXT NOT NULL,"
    "model TEXT,"
    "http_status INTEGER NOT NULL DEFAULT 0,"
    "created_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now'))"
    ");";
  char *sqlite_error;
  int rc;

  sqlite_error = NULL;
  rc = sqlite3_exec(db, sql, NULL, NULL, &sqlite_error);
  if (rc != SQLITE_OK) {
    if (sqlite_error != NULL) {
      strappy_set_formatted_error(error_out,
                                  "Could not create session schema: %s",
                                  sqlite_error);
      sqlite3_free(sqlite_error);
    } else {
      strappy_set_formatted_error(error_out,
                                  "Could not create session schema: sqlite %d",
                                  rc);
    }
    return 0;
  }

  return 1;
}

int strappy_db_initialize(const char *db_path, char **error_out)
{
  sqlite3 *db;
  int ok;

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  ok = strappy_db_ensure_schema(db, error_out);
  sqlite3_close(db);
  return ok;
}

int strappy_db_save_exchange(const char *db_path,
                             const char *prompt,
                             const char *response,
                             const char *model,
                             long http_status,
                             char **error_out)
{
  static const char *sql =
    "INSERT INTO sessions "
    "(prompt, response, model, http_status) "
    "VALUES (?, ?, ?, ?);";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;
  int ok;

  if ((prompt == NULL) || (response == NULL)) {
    strappy_set_error(error_out, "Session exchange is incomplete.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session insert: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  ok = 1;
  if (sqlite3_bind_text(stmt, 1, prompt, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_text(stmt, 2, response, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (model != NULL) &&
      (sqlite3_bind_text(stmt, 3, model, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (model == NULL) && (sqlite3_bind_null(stmt, 3) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)http_status) != SQLITE_OK)) {
    ok = 0;
  }

  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session insert: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save session exchange: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}
