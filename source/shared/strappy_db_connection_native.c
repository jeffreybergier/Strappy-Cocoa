#include "strappy_db_internal.h"

#include "strappy_core.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/* POSIX connection identity, concurrency, WAL, and shutdown policy. */

static pthread_mutex_t strappy_db_connection_mutex =
  PTHREAD_MUTEX_INITIALIZER;
static sqlite3 *strappy_db_connection = NULL;
static char *strappy_db_connection_path = NULL;
static dev_t strappy_db_connection_device = (dev_t)0;
static ino_t strappy_db_connection_inode = (ino_t)0;
static int strappy_db_connection_has_identity = 0;
static int strappy_db_connection_schema_ready = 0;
static int strappy_db_connection_shutdown_registered = 0;

static int strappy_db_enable_write_ahead_log(sqlite3 *db, char **error_out)
{
  sqlite3_stmt *stmt;
  const unsigned char *journal_mode;
  int rc;
  int finalize_rc;
  int ok;

  if (db == NULL) {
    strappy_set_error(error_out, "Database is not open.");
    return 0;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, "PRAGMA journal_mode = WAL;", -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not enable database write-ahead log: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  ok = 1;
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    journal_mode = sqlite3_column_text(stmt, 0);
    if ((journal_mode == NULL) ||
        (strcmp((const char *)journal_mode, "wal") != 0)) {
      strappy_set_formatted_error(error_out,
                                  "Could not enable database write-ahead log: "
                                  "journal mode is %s",
                                  (journal_mode != NULL) ?
                                    (const char *)journal_mode : "unknown");
      ok = 0;
    }
  } else {
    strappy_set_formatted_error(error_out,
                                "Could not enable database write-ahead log: %s",
                                sqlite3_errmsg(db));
    ok = 0;
  }
  finalize_rc = sqlite3_finalize(stmt);
  if (ok && (finalize_rc != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not enable database write-ahead log: %s",
                                sqlite3_errmsg(db));
    ok = 0;
  }
  return ok;
}

static void strappy_db_close_connection_locked(void)
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
  sqlite3_exec(strappy_db_connection,
               "PRAGMA wal_checkpoint(PASSIVE);",
               NULL,
               NULL,
               NULL);
  sqlite3_close(strappy_db_connection);
  strappy_db_connection = NULL;
  free(strappy_db_connection_path);
  strappy_db_connection_path = NULL;
  strappy_db_connection_device = (dev_t)0;
  strappy_db_connection_inode = (ino_t)0;
  strappy_db_connection_has_identity = 0;
  strappy_db_connection_schema_ready = 0;
}

static void strappy_db_shutdown_connection(void)
{
  if (pthread_mutex_lock(&strappy_db_connection_mutex) != 0) {
    return;
  }
  strappy_db_close_connection_locked();
  pthread_mutex_unlock(&strappy_db_connection_mutex);
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
  pthread_mutex_unlock(&strappy_db_connection_mutex);
}

int strappy_db_open(const char *db_path, sqlite3 **db_out, char **error_out)
{
  struct stat path_stat;
  sqlite3 *db;
  char *path_copy;
  int flags;
  int path_has_identity;
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
  if (pthread_mutex_lock(&strappy_db_connection_mutex) != 0) {
    strappy_set_error(error_out, "Could not lock the session database.");
    return 0;
  }
  path_has_identity = (stat(db_path, &path_stat) == 0) ? 1 : 0;
  if ((strappy_db_connection != NULL) &&
      (strappy_db_connection_path != NULL) &&
      (strcmp(strappy_db_connection_path, db_path) == 0) &&
      (((path_has_identity && strappy_db_connection_has_identity) &&
        (path_stat.st_dev == strappy_db_connection_device) &&
        (path_stat.st_ino == strappy_db_connection_inode)) ||
       (!path_has_identity && !strappy_db_connection_has_identity &&
        (db_path[0] == ':')))) {
    *db_out = strappy_db_connection;
    return 1;
  }
  strappy_db_close_connection_locked();
  db = NULL;
  flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
  rc = sqlite3_open_v2(db_path, &db, flags, NULL);
  if (rc != SQLITE_OK) {
    const char *message = (db != NULL) ? sqlite3_errmsg(db) :
      "unknown sqlite error";
    strappy_set_formatted_error(error_out,
                                "Could not open session database: %s",
                                message);
    if (db != NULL) {
      sqlite3_close(db);
    }
    pthread_mutex_unlock(&strappy_db_connection_mutex);
    return 0;
  }
  sqlite3_busy_timeout(db, 5000);
  if (!strappy_db_enable_write_ahead_log(db, error_out)) {
    sqlite3_close(db);
    pthread_mutex_unlock(&strappy_db_connection_mutex);
    return 0;
  }
  path_copy = strappy_string_duplicate(db_path);
  if (path_copy == NULL) {
    strappy_set_error(error_out, "Could not remember session database path.");
    sqlite3_close(db);
    pthread_mutex_unlock(&strappy_db_connection_mutex);
    return 0;
  }
  strappy_db_connection = db;
  strappy_db_connection_path = path_copy;
  strappy_db_connection_schema_ready = 0;
  if (stat(db_path, &path_stat) == 0) {
    strappy_db_connection_device = path_stat.st_dev;
    strappy_db_connection_inode = path_stat.st_ino;
    strappy_db_connection_has_identity = 1;
  }
  if (!strappy_db_connection_shutdown_registered &&
      (atexit(strappy_db_shutdown_connection) == 0)) {
    strappy_db_connection_shutdown_registered = 1;
  }
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
