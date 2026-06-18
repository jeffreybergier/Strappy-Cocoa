#include "strappy_db.h"

#include "strappy_core.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void strappy_session_record_init(strappy_session_record *record)
{
  if (record == NULL) {
    return;
  }

  record->session_id = 0;
  record->prompt = NULL;
  record->response = NULL;
  record->model = NULL;
  record->created_at = NULL;
  record->http_status = 0L;
}

void strappy_session_record_destroy(strappy_session_record *record)
{
  if (record == NULL) {
    return;
  }

  free(record->prompt);
  free(record->response);
  free(record->model);
  free(record->created_at);
  strappy_session_record_init(record);
}

void strappy_session_record_list_init(strappy_session_record_list *list)
{
  if (list == NULL) {
    return;
  }

  list->records = NULL;
  list->count = 0U;
}

void strappy_session_record_list_destroy(strappy_session_record_list *list)
{
  size_t index;

  if (list == NULL) {
    return;
  }

  for (index = 0U; index < list->count; index++) {
    strappy_session_record_destroy(&list->records[index]);
  }
  free(list->records);
  strappy_session_record_list_init(list);
}

void strappy_session_message_record_init(strappy_session_message_record *record)
{
  if (record == NULL) {
    return;
  }

  record->message_id = 0;
  record->session_id = 0;
  record->role = NULL;
  record->content = NULL;
  record->model = NULL;
  record->metadata = NULL;
  record->created_at = NULL;
  record->http_status = 0L;
}

void strappy_session_message_record_destroy(strappy_session_message_record *record)
{
  if (record == NULL) {
    return;
  }

  free(record->role);
  free(record->content);
  free(record->model);
  free(record->metadata);
  free(record->created_at);
  strappy_session_message_record_init(record);
}

void strappy_session_message_record_list_init(strappy_session_message_record_list *list)
{
  if (list == NULL) {
    return;
  }

  list->records = NULL;
  list->count = 0U;
}

void strappy_session_message_record_list_destroy(strappy_session_message_record_list *list)
{
  size_t index;

  if (list == NULL) {
    return;
  }

  for (index = 0U; index < list->count; index++) {
    strappy_session_message_record_destroy(&list->records[index]);
  }
  free(list->records);
  strappy_session_message_record_list_init(list);
}

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

static int strappy_db_exec(sqlite3 *db,
                           const char *sql,
                           const char *error_prefix,
                           char **error_out)
{
  char *sqlite_error;
  int rc;

  sqlite_error = NULL;
  rc = sqlite3_exec(db, sql, NULL, NULL, &sqlite_error);
  if (rc != SQLITE_OK) {
    if (sqlite_error != NULL) {
      strappy_set_formatted_error(error_out,
                                  "%s: %s",
                                  error_prefix,
                                  sqlite_error);
      sqlite3_free(sqlite_error);
    } else {
      strappy_set_formatted_error(error_out,
                                  "%s: sqlite %d",
                                  error_prefix,
                                  rc);
    }
    return 0;
  }

  return 1;
}

static int strappy_db_table_has_column(sqlite3 *db,
                                       const char *table_name,
                                       const char *column_name,
                                       int *exists_out,
                                       char **error_out)
{
  sqlite3_stmt *stmt;
  char sql[256];
  int result;
  int rc;

  if (exists_out == NULL) {
    strappy_set_error(error_out, "Column check output is missing.");
    return 0;
  }
  *exists_out = 0;

  result = snprintf(sql, sizeof(sql), "PRAGMA table_info(%s);", table_name);
  if ((result < 0) || ((size_t)result >= sizeof(sql))) {
    strappy_set_error(error_out, "Column check SQL is too large.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare column check: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    const unsigned char *name;

    name = sqlite3_column_text(stmt, 1);
    if ((name != NULL) && (strcmp((const char *)name, column_name) == 0)) {
      *exists_out = 1;
      sqlite3_finalize(stmt);
      return 1;
    }
  }

  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read column check: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_ensure_column(sqlite3 *db,
                                    const char *table_name,
                                    const char *column_name,
                                    const char *column_type,
                                    char **error_out)
{
  char sql[256];
  int exists;
  int result;

  if (!strappy_db_table_has_column(db,
                                   table_name,
                                   column_name,
                                   &exists,
                                   error_out)) {
    return 0;
  }
  if (exists) {
    return 1;
  }

  result = snprintf(sql,
                    sizeof(sql),
                    "ALTER TABLE %s ADD COLUMN %s %s;",
                    table_name,
                    column_name,
                    column_type);
  if ((result < 0) || ((size_t)result >= sizeof(sql))) {
    strappy_set_error(error_out, "Column migration SQL is too large.");
    return 0;
  }

  return strappy_db_exec(db, sql, "Could not migrate session schema", error_out);
}

static int strappy_db_ensure_schema(sqlite3 *db, char **error_out)
{
  static const char *sessions_sql =
    "CREATE TABLE IF NOT EXISTS sessions ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "prompt TEXT NOT NULL,"
    "response TEXT NOT NULL,"
    "model TEXT,"
    "http_status INTEGER NOT NULL DEFAULT 0,"
    "created_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now'))"
    ");";
  static const char *messages_sql =
    "CREATE TABLE IF NOT EXISTS session_messages ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "session_id INTEGER NOT NULL,"
    "role TEXT NOT NULL,"
    "content TEXT NOT NULL,"
    "model TEXT,"
    "http_status INTEGER NOT NULL DEFAULT 0,"
    "metadata TEXT,"
    "created_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
    "FOREIGN KEY(session_id) REFERENCES sessions(id)"
    ");";
  static const char *messages_index_sql =
    "CREATE INDEX IF NOT EXISTS session_messages_session_id_id_idx "
    "ON session_messages(session_id, id);";
  static const char *backfill_user_sql =
    "INSERT INTO session_messages "
    "(session_id, role, content, model, http_status, metadata, created_at) "
    "SELECT s.id, 'user', s.prompt, NULL, 0, NULL, s.created_at "
    "FROM sessions s "
    "WHERE NOT EXISTS ("
    "SELECT 1 FROM session_messages m WHERE m.session_id = s.id"
    ");";
  static const char *backfill_assistant_sql =
    "INSERT INTO session_messages "
    "(session_id, role, content, model, http_status, metadata, created_at) "
    "SELECT s.id, 'assistant', s.response, s.model, s.http_status, NULL, s.created_at "
    "FROM sessions s "
    "WHERE NOT EXISTS ("
    "SELECT 1 FROM session_messages m "
    "WHERE m.session_id = s.id AND m.role = 'assistant'"
    ");";

  if (!strappy_db_exec(db,
                       sessions_sql,
                       "Could not create session schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       messages_sql,
                       "Could not create session message schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       messages_index_sql,
                       "Could not create session message index",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_column(db,
                                "session_messages",
                                "metadata",
                                "TEXT",
                                error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       backfill_user_sql,
                       "Could not backfill session user messages",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       backfill_assistant_sql,
                       "Could not backfill session assistant messages",
                       error_out)) {
    return 0;
  }

  return 1;
}

static char *strappy_db_column_string(sqlite3_stmt *stmt, int column)
{
  const unsigned char *value;

  value = sqlite3_column_text(stmt, column);
  if (value == NULL) {
    return NULL;
  }

  return strappy_string_duplicate((const char *)value);
}

static int strappy_db_assign_record_from_statement(strappy_session_record *record,
                                                   sqlite3_stmt *stmt,
                                                   char **error_out)
{
  char *prompt;
  char *response;
  char *model;
  char *created_at;

  if ((record == NULL) || (stmt == NULL)) {
    strappy_set_error(error_out, "Session row request is incomplete.");
    return 0;
  }

  strappy_session_record_destroy(record);
  record->session_id = (long long)sqlite3_column_int64(stmt, 0);
  record->http_status = (long)sqlite3_column_int64(stmt, 4);

  prompt = strappy_db_column_string(stmt, 1);
  response = strappy_db_column_string(stmt, 2);
  model = strappy_db_column_string(stmt, 3);
  created_at = strappy_db_column_string(stmt, 5);

  if ((prompt == NULL) || (response == NULL) || (created_at == NULL)) {
    free(prompt);
    free(response);
    free(model);
    free(created_at);
    strappy_set_error(error_out, "Could not allocate session row.");
    return 0;
  }

  record->prompt = prompt;
  record->response = response;
  record->model = model;
  record->created_at = created_at;
  return 1;
}

static int strappy_db_assign_message_from_statement(
  strappy_session_message_record *record,
  sqlite3_stmt *stmt,
  char **error_out)
{
  char *role;
  char *content;
  char *model;
  char *metadata;
  char *created_at;

  if ((record == NULL) || (stmt == NULL)) {
    strappy_set_error(error_out, "Session message row request is incomplete.");
    return 0;
  }

  strappy_session_message_record_destroy(record);
  record->message_id = (long long)sqlite3_column_int64(stmt, 0);
  record->session_id = (long long)sqlite3_column_int64(stmt, 1);
  record->http_status = (long)sqlite3_column_int64(stmt, 5);

  role = strappy_db_column_string(stmt, 2);
  content = strappy_db_column_string(stmt, 3);
  model = strappy_db_column_string(stmt, 4);
  metadata = strappy_db_column_string(stmt, 6);
  created_at = strappy_db_column_string(stmt, 7);

  if ((role == NULL) || (content == NULL) || (created_at == NULL)) {
    free(role);
    free(content);
    free(model);
    free(metadata);
    free(created_at);
    strappy_set_error(error_out, "Could not allocate session message row.");
    return 0;
  }

  record->role = role;
  record->content = content;
  record->model = model;
  record->metadata = metadata;
  record->created_at = created_at;
  return 1;
}

static int strappy_db_insert_message(sqlite3 *db,
                                     long long session_id,
                                     const char *role,
                                     const char *content,
                                     const char *model,
                                     long http_status,
                                     const char *metadata,
                                     char **error_out)
{
  static const char *sql =
    "INSERT INTO session_messages "
    "(session_id, role, content, model, http_status, metadata) "
    "VALUES (?, ?, ?, ?, ?, ?);";
  sqlite3_stmt *stmt;
  int rc;
  int ok;

  if ((session_id <= 0) || (role == NULL) || (role[0] == '\0') ||
      (content == NULL)) {
    strappy_set_error(error_out, "Session message is incomplete.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session message insert: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  ok = 1;
  if (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id) != SQLITE_OK) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt, 2, role, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt, 3, content, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (model != NULL) &&
      (sqlite3_bind_text(stmt, 4, model, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (model == NULL) && (sqlite3_bind_null(stmt, 4) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 5, (sqlite3_int64)http_status) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (metadata != NULL) &&
      (sqlite3_bind_text(stmt, 6, metadata, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (metadata == NULL) && (sqlite3_bind_null(stmt, 6) != SQLITE_OK)) {
    ok = 0;
  }

  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session message insert: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save session message: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_session_exists(sqlite3 *db,
                                     long long session_id,
                                     char **error_out)
{
  static const char *sql = "SELECT 1 FROM sessions WHERE id = ?;";
  sqlite3_stmt *stmt;
  int rc;

  if (session_id <= 0) {
    strappy_set_error(error_out, "Session id is not valid.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session lookup: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc == SQLITE_ROW) {
    return 1;
  }

  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "Session was not found.");
  } else {
    strappy_set_formatted_error(error_out,
                                "Could not read session lookup: %s",
                                sqlite3_errmsg(db));
  }
  return 0;
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
                             const char *metadata,
                             char **error_out)
{
  return strappy_db_save_exchange_with_id(db_path,
                                          prompt,
                                          response,
                                          model,
                                          http_status,
                                          metadata,
                                          NULL,
                                          error_out);
}

int strappy_db_save_exchange_with_id(const char *db_path,
                                     const char *prompt,
                                     const char *response,
                                     const char *model,
                                     long http_status,
                                     const char *metadata,
                                     long long *session_id_out,
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
  long long session_id;

  if (session_id_out != NULL) {
    *session_id_out = 0;
  }

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

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin session insert", error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session insert: %s",
                                sqlite3_errmsg(db));
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
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
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save session exchange: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    sqlite3_close(db);
    return 0;
  }

  session_id = (long long)sqlite3_last_insert_rowid(db);
  sqlite3_finalize(stmt);

  if (!strappy_db_insert_message(db,
                                 session_id,
                                 "user",
                                 prompt,
                                 NULL,
                                 0L,
                                 NULL,
                                 error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_insert_message(db,
                                 session_id,
                                 "assistant",
                                 response,
                                 model,
                                 http_status,
                                 metadata,
                                 error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "COMMIT;", "Could not commit session insert", error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (session_id_out != NULL) {
    *session_id_out = session_id;
  }

  sqlite3_close(db);
  return 1;
}

int strappy_db_list_sessions(const char *db_path,
                             strappy_session_record_list *list,
                             char **error_out)
{
  static const char *sql =
    "SELECT id, prompt, response, model, http_status, created_at "
    "FROM sessions "
    "ORDER BY id DESC;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (list == NULL) {
    strappy_set_error(error_out, "strappy_db_list_sessions received no output.");
    return 0;
  }
  strappy_session_record_list_init(list);

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
                                "Could not prepare session list: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    strappy_session_record *next_records;

    if (list->count >= (((size_t)-1) / sizeof(strappy_session_record))) {
      strappy_set_error(error_out, "Session list is too large.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_session_record_list_destroy(list);
      return 0;
    }

    next_records = (strappy_session_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(strappy_session_record));
    if (next_records == NULL) {
      strappy_set_error(error_out, "Could not allocate session list.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_session_record_list_destroy(list);
      return 0;
    }

    list->records = next_records;
    strappy_session_record_init(&list->records[list->count]);
    if (!strappy_db_assign_record_from_statement(&list->records[list->count],
                                                 stmt,
                                                 error_out)) {
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_session_record_list_destroy(list);
      return 0;
    }

    list->count++;
  }

  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read session list: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    strappy_session_record_list_destroy(list);
    return 0;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}

int strappy_db_load_session(const char *db_path,
                            long long session_id,
                            strappy_session_record *record,
                            char **error_out)
{
  static const char *sql =
    "SELECT id, prompt, response, model, http_status, created_at "
    "FROM sessions "
    "WHERE id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;
  int ok;

  if (record == NULL) {
    strappy_set_error(error_out, "strappy_db_load_session received no output.");
    return 0;
  }
  strappy_session_record_init(record);

  if (session_id <= 0) {
    strappy_set_error(error_out, "Session id is not valid.");
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
                                "Could not prepare session load: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session load: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "Session was not found.");
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }
  if (rc != SQLITE_ROW) {
    strappy_set_formatted_error(error_out,
                                "Could not read session: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  ok = strappy_db_assign_record_from_statement(record, stmt, error_out);
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  if (!ok) {
    strappy_session_record_destroy(record);
    return 0;
  }

  return 1;
}

int strappy_db_append_exchange_to_session(const char *db_path,
                                          long long session_id,
                                          const char *prompt,
                                          const char *response,
                                          const char *model,
                                          long http_status,
                                          const char *metadata,
                                          char **error_out)
{
  static const char *update_sql =
    "UPDATE sessions "
    "SET response = ?, model = ?, http_status = ? "
    "WHERE id = ?;";
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

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin session append", error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_insert_message(db,
                                 session_id,
                                 "user",
                                 prompt,
                                 NULL,
                                 0L,
                                 NULL,
                                 error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_insert_message(db,
                                 session_id,
                                 "assistant",
                                 response,
                                 model,
                                 http_status,
                                 metadata,
                                 error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session update: %s",
                                sqlite3_errmsg(db));
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    sqlite3_close(db);
    return 0;
  }

  ok = 1;
  if (sqlite3_bind_text(stmt, 1, response, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
    ok = 0;
  }
  if (ok && (model != NULL) &&
      (sqlite3_bind_text(stmt, 2, model, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (model == NULL) && (sqlite3_bind_null(stmt, 2) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)http_status) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)session_id) != SQLITE_OK)) {
    ok = 0;
  }

  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update session exchange: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_finalize(stmt);

  if (!strappy_db_exec(db, "COMMIT;", "Could not commit session append", error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_close(db);
  return 1;
}

int strappy_db_list_session_messages(const char *db_path,
                                     long long session_id,
                                     strappy_session_message_record_list *list,
                                     char **error_out)
{
  static const char *sql =
    "SELECT id, session_id, role, content, model, http_status, metadata, created_at "
    "FROM session_messages "
    "WHERE session_id = ? "
    "ORDER BY id ASC;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (list == NULL) {
    strappy_set_error(error_out, "strappy_db_list_session_messages received no output.");
    return 0;
  }
  strappy_session_message_record_list_init(list);

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session message list: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session message list: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    strappy_session_message_record *next_records;

    if (list->count >= (((size_t)-1) / sizeof(strappy_session_message_record))) {
      strappy_set_error(error_out, "Session message list is too large.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_session_message_record_list_destroy(list);
      return 0;
    }

    next_records = (strappy_session_message_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(strappy_session_message_record));
    if (next_records == NULL) {
      strappy_set_error(error_out, "Could not allocate session message list.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_session_message_record_list_destroy(list);
      return 0;
    }

    list->records = next_records;
    strappy_session_message_record_init(&list->records[list->count]);
    if (!strappy_db_assign_message_from_statement(&list->records[list->count],
                                                  stmt,
                                                  error_out)) {
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_session_message_record_list_destroy(list);
      return 0;
    }

    list->count++;
  }

  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read session message list: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    strappy_session_message_record_list_destroy(list);
    return 0;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}
