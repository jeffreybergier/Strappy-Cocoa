#include "strappy_db_internal.h"

#include "strappy_core.h"

#include <stdlib.h>
#include <string.h>

/* Single-Worker policy: no POSIX identity, pthreads, or mandatory WAL. */

static sqlite3 *strappy_db_connection = NULL;
static char *strappy_db_connection_path = NULL;
static int strappy_db_connection_schema_ready = 0;
static int strappy_db_persistent_vfs_enabled = 0;

static void strappy_db_close_connection(void)
{
  sqlite3_stmt *stmt;

  if (strappy_db_connection == NULL) {
    return;
  }
  while ((stmt = sqlite3_next_stmt(strappy_db_connection, NULL)) != NULL) {
    sqlite3_finalize(stmt);
  }
  if (!sqlite3_get_autocommit(strappy_db_connection)) {
    sqlite3_exec(strappy_db_connection, "ROLLBACK;", NULL, NULL, NULL);
  }
  sqlite3_close(strappy_db_connection);
  strappy_db_connection = NULL;
  free(strappy_db_connection_path);
  strappy_db_connection_path = NULL;
  strappy_db_connection_schema_ready = 0;
}

void strappy_db_web_set_persistent_vfs_enabled(int enabled)
{
  int next_enabled;

  next_enabled = enabled ? 1 : 0;
  if (strappy_db_persistent_vfs_enabled != next_enabled) {
    strappy_db_close_connection();
    strappy_db_persistent_vfs_enabled = next_enabled;
  }
}

void strappy_db_release(sqlite3 *db)
{
  sqlite3_stmt *stmt;

  if (db != strappy_db_connection) {
    if (db != NULL) {
      sqlite3_close(db);
    }
    return;
  }
  while ((stmt = sqlite3_next_stmt(db, NULL)) != NULL) {
    sqlite3_finalize(stmt);
  }
  if (!sqlite3_get_autocommit(db)) {
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
  }
}

int strappy_db_open(const char *db_path, sqlite3 **db_out, char **error_out)
{
  const char *vfs_name;
  sqlite3 *db;
  char *path_copy;
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
  if ((strappy_db_connection != NULL) &&
      (strappy_db_connection_path != NULL) &&
      (strcmp(strappy_db_connection_path, db_path) == 0)) {
    *db_out = strappy_db_connection;
    return 1;
  }
  strappy_db_close_connection();
  db = NULL;
  flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
  vfs_name = ((db_path[0] != ':') && strappy_db_persistent_vfs_enabled) ?
    "opfs-sahpool" : NULL;
  rc = sqlite3_open_v2(db_path, &db, flags, vfs_name);
  if (rc != SQLITE_OK) {
    const char *message = (db != NULL) ? sqlite3_errmsg(db) :
      "unknown sqlite error";
    strappy_set_formatted_error(error_out,
                                "Could not open session database: %s",
                                message);
    if (db != NULL) {
      sqlite3_close(db);
    }
    return 0;
  }
  sqlite3_busy_timeout(db, 5000);
  path_copy = strappy_string_duplicate(db_path);
  if (path_copy == NULL) {
    strappy_set_error(error_out, "Could not remember session database path.");
    sqlite3_close(db);
    return 0;
  }
  strappy_db_connection = db;
  strappy_db_connection_path = path_copy;
  strappy_db_connection_schema_ready = 0;
  *db_out = db;
  return 1;
}

int strappy_db_connection_schema_is_ready(sqlite3 *db)
{
  return (db != NULL) && (db == strappy_db_connection) &&
    strappy_db_connection_schema_ready;
}

void strappy_db_connection_mark_schema_ready(sqlite3 *db)
{
  if ((db != NULL) && (db == strappy_db_connection)) {
    strappy_db_connection_schema_ready = 1;
  }
}
