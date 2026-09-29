#include "strappy_db_internal.h"
#include "strappy_core.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* Derived semantic state, maintained in the same transaction as the ledger.
 * Sequence, not MAX(timestamp), defines the most recent conversation item. */
int strappy_db_ensure_session_activity(sqlite3 *db, char **error_out)
{
  sqlite3_stmt *stmt = NULL;
  int rc;
  int exists;
  static const char *schema =
    "CREATE TABLE session_activity (session_id INTEGER PRIMARY KEY,"
      "last_activity_at_ms INTEGER NOT NULL,"
      "FOREIGN KEY(session_id) REFERENCES sessions(id) ON DELETE CASCADE);"
    "CREATE INDEX session_activity_order_idx ON session_activity"
      "(last_activity_at_ms DESC,session_id DESC);"
    "INSERT INTO session_activity SELECT s.id,COALESCE("
      "(SELECT i.created_at_ms FROM conversation_items i WHERE i.session_id=s.id "
       "ORDER BY i.sequence DESC LIMIT 1),s.created_at_ms) FROM sessions s;"
    "CREATE TRIGGER session_activity_insert AFTER INSERT ON sessions BEGIN "
      "INSERT INTO session_activity VALUES(NEW.id,NEW.created_at_ms); END;"
    "CREATE TRIGGER session_activity_created AFTER UPDATE OF created_at_ms ON sessions BEGIN "
      "UPDATE session_activity SET last_activity_at_ms=COALESCE("
        "(SELECT created_at_ms FROM conversation_items WHERE session_id=NEW.id "
          "ORDER BY sequence DESC LIMIT 1),NEW.created_at_ms) WHERE session_id=NEW.id; END;"
    "CREATE TRIGGER session_activity_item_insert AFTER INSERT ON conversation_items BEGIN "
      "UPDATE session_activity SET last_activity_at_ms=COALESCE("
        "(SELECT created_at_ms FROM conversation_items WHERE session_id=NEW.session_id "
          "ORDER BY sequence DESC LIMIT 1),"
        "(SELECT created_at_ms FROM sessions WHERE id=NEW.session_id)) "
        "WHERE session_id=NEW.session_id AND EXISTS(SELECT 1 FROM sessions WHERE id=NEW.session_id); "
    "END;"
    "CREATE TRIGGER session_activity_item_delete AFTER DELETE ON conversation_items BEGIN "
      "UPDATE session_activity SET last_activity_at_ms=COALESCE("
        "(SELECT created_at_ms FROM conversation_items WHERE session_id=OLD.session_id "
          "ORDER BY sequence DESC LIMIT 1),"
        "(SELECT created_at_ms FROM sessions WHERE id=OLD.session_id)) "
        "WHERE session_id=OLD.session_id AND EXISTS(SELECT 1 FROM sessions WHERE id=OLD.session_id); "
    "END;"
    "CREATE TRIGGER session_activity_item_update AFTER UPDATE OF session_id,sequence,created_at_ms ON conversation_items BEGIN "
      "UPDATE session_activity SET last_activity_at_ms=COALESCE("
        "(SELECT created_at_ms FROM conversation_items WHERE session_id=OLD.session_id "
          "ORDER BY sequence DESC LIMIT 1),"
        "(SELECT created_at_ms FROM sessions WHERE id=OLD.session_id)) "
        "WHERE session_id=OLD.session_id AND EXISTS(SELECT 1 FROM sessions WHERE id=OLD.session_id); "
      "UPDATE session_activity SET last_activity_at_ms=COALESCE("
        "(SELECT created_at_ms FROM conversation_items WHERE session_id=NEW.session_id "
          "ORDER BY sequence DESC LIMIT 1),"
        "(SELECT created_at_ms FROM sessions WHERE id=NEW.session_id)) "
        "WHERE session_id=NEW.session_id AND EXISTS(SELECT 1 FROM sessions WHERE id=NEW.session_id); "
    "END;";

  /* Creation and initial population are atomic. Reopening an initialized
   * database never scans the existing sessions or replays the backfill. */
  if (!strappy_db_exec(db, "SAVEPOINT session_activity_init;",
                       "Could not begin activity initialization", error_out)) return 0;
  rc = sqlite3_prepare_v2(db,
    "SELECT 1 FROM sqlite_master WHERE type='table' AND name='session_activity';",
    -1, &stmt, NULL);
  if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
  exists = (rc == SQLITE_ROW);
  sqlite3_finalize(stmt);
  if ((rc != SQLITE_ROW && rc != SQLITE_DONE) ||
      (!exists && !strappy_db_exec(db, schema, "Could not create session activity", error_out))) {
    if (error_out != NULL && *error_out == NULL)
      strappy_set_formatted_error(error_out, "Could not inspect session activity: %s", sqlite3_errmsg(db));
    sqlite3_exec(db, "ROLLBACK TO session_activity_init; RELEASE session_activity_init;", NULL, NULL, NULL);
    return 0;
  }
  return strappy_db_exec(db, "RELEASE session_activity_init;",
                         "Could not finish activity initialization", error_out);
}

struct strappy_session_reader {
  sqlite3 *db;
  sqlite3_stmt *page;
  sqlite3_stmt *ties;
  sqlite3_stmt *earlier;
  sqlite3_stmt *since;
  sqlite3_stmt *identity;
  sqlite3_stmt *rank;
  size_t count;
  size_t next_offset;
  long long last_activity;
  long long last_id;
  unsigned long long steps;
};

#define STRAPPY_SIDEBAR_SELECT \
  "SELECT p.session_id,p.last_activity_at_ms,s.name,COALESCE(m.name," \
    "s.model_id," STRAPPY_DB_DEFAULT_MODEL_SQL ",'') FROM (" \
    "SELECT session_id,last_activity_at_ms FROM session_activity "
#define STRAPPY_SIDEBAR_ORDER \
    "ORDER BY last_activity_at_ms DESC,session_id DESC LIMIT ?3 "
#define STRAPPY_SIDEBAR_JOIN \
  ") p JOIN sessions s ON s.id=p.session_id LEFT JOIN models m ON m.id=" \
    "COALESCE(s.model_id," STRAPPY_DB_DEFAULT_MODEL_SQL ") " \
  "ORDER BY p.last_activity_at_ms DESC,p.session_id DESC;"

static int strappy_sidebar_error(strappy_session_reader *reader, char **error_out)
{
  strappy_set_formatted_error(error_out, "Could not read session sidebar: %s",
                              sqlite3_errmsg(reader->db));
  return 0;
}

static void strappy_sidebar_reset(strappy_session_reader *reader, sqlite3_stmt *stmt)
{
  reader->steps += (unsigned long long)sqlite3_stmt_status(stmt, SQLITE_STMTSTATUS_VM_STEP, 1);
  sqlite3_reset(stmt);
  sqlite3_clear_bindings(stmt);
}

void strappy_db_sidebar_close(strappy_session_reader *reader)
{
  if (reader == NULL) return;
  sqlite3_finalize(reader->page);
  sqlite3_finalize(reader->ties);
  sqlite3_finalize(reader->earlier);
  sqlite3_finalize(reader->since);
  sqlite3_finalize(reader->identity);
  sqlite3_finalize(reader->rank);
  if (reader->db != NULL) {
    sqlite3_exec(reader->db, "ROLLBACK;", NULL, NULL, NULL);
    sqlite3_close(reader->db);
  }
  free(reader);
}

int strappy_db_sidebar_open(const char *path, strappy_session_reader **out,
                            char **error_out)
{
  strappy_session_reader *reader;
  sqlite3_stmt *count_stmt = NULL;
  sqlite3_int64 count = 0;
  int rc;
  if (out == NULL) {
    strappy_set_error(error_out, "Missing session sidebar output.");
    return 0;
  }
  *out = NULL;
  if (!strappy_db_initialize(path, error_out)) return 0;
  reader = (strappy_session_reader *)calloc(1U, sizeof(*reader));
  if (reader == NULL) {
    strappy_set_error(error_out, "Could not allocate session sidebar.");
    return 0;
  }
  reader->next_offset = (size_t)-1;
  rc = sqlite3_open_v2(path, &reader->db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, NULL);
  if (rc == SQLITE_OK) {
    sqlite3_busy_timeout(reader->db, 5000);
    rc = sqlite3_exec(reader->db, "BEGIN;", NULL, NULL, NULL);
  }
  if (rc == SQLITE_OK)
    rc = sqlite3_prepare_v2(reader->db, "SELECT count(*) FROM session_activity;", -1, &count_stmt, NULL);
  if (rc == SQLITE_OK) rc = sqlite3_step(count_stmt);
  if (rc == SQLITE_ROW) count = sqlite3_column_int64(count_stmt, 0);
  if (count_stmt != NULL) strappy_sidebar_reset(reader, count_stmt);
  sqlite3_finalize(count_stmt);
  if (rc != SQLITE_ROW || count < 0 || (unsigned long long)count > (unsigned long long)LONG_MAX - 5ULL) {
    if (rc == SQLITE_ROW) strappy_set_error(error_out, "Session list exceeds table capacity.");
    else strappy_sidebar_error(reader, error_out);
    strappy_db_sidebar_close(reader);
    return 0;
  }
  reader->count = (size_t)count;
#define STRAPPY_PREPARE(member, sql) \
  if (sqlite3_prepare_v2(reader->db, sql, -1, &reader->member, NULL) != SQLITE_OK) { \
    strappy_sidebar_error(reader, error_out); \
    strappy_db_sidebar_close(reader); \
    return 0; \
  }
  STRAPPY_PREPARE(page, STRAPPY_SIDEBAR_SELECT STRAPPY_SIDEBAR_ORDER "OFFSET ?4" STRAPPY_SIDEBAR_JOIN)
  STRAPPY_PREPARE(ties, STRAPPY_SIDEBAR_SELECT
    "WHERE last_activity_at_ms=?1 AND session_id<?2 " STRAPPY_SIDEBAR_ORDER STRAPPY_SIDEBAR_JOIN)
  STRAPPY_PREPARE(earlier, STRAPPY_SIDEBAR_SELECT
    "WHERE last_activity_at_ms<?1 " STRAPPY_SIDEBAR_ORDER STRAPPY_SIDEBAR_JOIN)
  STRAPPY_PREPARE(since,
    "SELECT count(*) FROM session_activity WHERE last_activity_at_ms>=?1;")
  STRAPPY_PREPARE(identity,
    "SELECT last_activity_at_ms FROM session_activity WHERE session_id=?1;")
  STRAPPY_PREPARE(rank,
    "SELECT (SELECT count(*) FROM session_activity WHERE last_activity_at_ms>?1) + "
    "(SELECT count(*) FROM session_activity WHERE last_activity_at_ms=?1 AND session_id>?2);")
#undef STRAPPY_PREPARE
  *out = reader;
  return 1;
}

size_t strappy_db_sidebar_count(const strappy_session_reader *reader)
{
  return reader != NULL ? reader->count : 0;
}

unsigned long long strappy_db_sidebar_steps(const strappy_session_reader *reader)
{
  return reader != NULL ? reader->steps : 0;
}

int strappy_db_sidebar_count_since(strappy_session_reader *reader, long long timestamp,
                                   size_t *count, char **error_out)
{
  int rc;
  if (reader == NULL || count == NULL) {
    strappy_set_error(error_out, "Missing sidebar count input.");
    return 0;
  }
  *count = 0;
  rc = sqlite3_bind_int64(reader->since, 1, (sqlite3_int64)timestamp);
  if (rc == SQLITE_OK) rc = sqlite3_step(reader->since);
  if (rc == SQLITE_ROW) *count = (size_t)sqlite3_column_int64(reader->since, 0);
  else strappy_sidebar_error(reader, error_out);
  strappy_sidebar_reset(reader, reader->since);
  return rc == SQLITE_ROW;
}

int strappy_db_sidebar_index(strappy_session_reader *reader, long long session_id,
                             size_t *index, char **error_out)
{
  long long timestamp = 0;
  int rc;
  if (reader == NULL || index == NULL) {
    strappy_set_error(error_out, "Missing sidebar identity input.");
    return 0;
  }
  *index = (size_t)-1;
  rc = sqlite3_bind_int64(reader->identity, 1, (sqlite3_int64)session_id);
  if (rc == SQLITE_OK) rc = sqlite3_step(reader->identity);
  if (rc == SQLITE_ROW) timestamp = (long long)sqlite3_column_int64(reader->identity, 0);
  else if (rc != SQLITE_DONE) strappy_sidebar_error(reader, error_out);
  strappy_sidebar_reset(reader, reader->identity);
  if (rc == SQLITE_DONE) return 1;
  if (rc != SQLITE_ROW) return 0;
  rc = sqlite3_bind_int64(reader->rank, 1, (sqlite3_int64)timestamp);
  if (rc == SQLITE_OK) rc = sqlite3_bind_int64(reader->rank, 2, (sqlite3_int64)session_id);
  if (rc == SQLITE_OK) rc = sqlite3_step(reader->rank);
  if (rc == SQLITE_ROW) *index = (size_t)sqlite3_column_int64(reader->rank, 0);
  else strappy_sidebar_error(reader, error_out);
  strappy_sidebar_reset(reader, reader->rank);
  return rc == SQLITE_ROW;
}

void strappy_db_sidebar_page_destroy(strappy_sidebar_page *page)
{
  size_t index;
  if (page == NULL) return;
  for (index = 0; index < page->count; index++) {
    free(page->records[index].name);
    free(page->records[index].model_name);
  }
  memset(page, 0, sizeof(*page));
}

static int strappy_sidebar_read_statement(strappy_session_reader *reader,
                                          sqlite3_stmt *stmt,
                                          strappy_sidebar_page *page,
                                          char **error_out)
{
  int rc;
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    strappy_sidebar_record *record;
    if (page->count >= STRAPPY_SIDEBAR_PAGE_SIZE) {
      strappy_set_error(error_out, "Sidebar page exceeded its bound.");
      strappy_sidebar_reset(reader, stmt);
      return 0;
    }
    record = &page->records[page->count++];
    record->session_id = (long long)sqlite3_column_int64(stmt, 0);
    record->last_activity_at_ms = (long long)sqlite3_column_int64(stmt, 1);
    record->name = strappy_db_column_string(stmt, 2);
    record->model_name = strappy_db_column_string(stmt, 3);
    if (record->name == NULL || record->model_name == NULL) {
      strappy_set_error(error_out, "Could not allocate sidebar row.");
      strappy_sidebar_reset(reader, stmt);
      return 0;
    }
  }
  if (rc != SQLITE_DONE) strappy_sidebar_error(reader, error_out);
  strappy_sidebar_reset(reader, stmt);
  return rc == SQLITE_DONE;
}

int strappy_db_sidebar_read(strappy_session_reader *reader, size_t offset,
                            strappy_sidebar_page *page, char **error_out)
{
  int ok;
  sqlite3_stmt *stmt;
  if (reader == NULL || page == NULL) {
    strappy_set_error(error_out, "Missing sidebar page input.");
    return 0;
  }
  memset(page, 0, sizeof(*page));
  if (offset >= reader->count) return 1;
  if (offset == reader->next_offset) {
    stmt = reader->ties;
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)reader->last_activity);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)reader->last_id);
    sqlite3_bind_int(stmt, 3, (int)STRAPPY_SIDEBAR_PAGE_SIZE);
    ok = strappy_sidebar_read_statement(reader, stmt, page, error_out);
    if (ok && page->count < STRAPPY_SIDEBAR_PAGE_SIZE) {
      stmt = reader->earlier;
      sqlite3_bind_int64(stmt, 1, (sqlite3_int64)reader->last_activity);
      sqlite3_bind_int(stmt, 3, (int)(STRAPPY_SIDEBAR_PAGE_SIZE - page->count));
      ok = strappy_sidebar_read_statement(reader, stmt, page, error_out);
    }
  } else {
    stmt = reader->page;
    sqlite3_bind_int(stmt, 3, (int)STRAPPY_SIDEBAR_PAGE_SIZE);
    sqlite3_bind_int64(stmt, 4, (sqlite3_int64)offset);
    ok = strappy_sidebar_read_statement(reader, stmt, page, error_out);
  }
  if (!ok) {
    reader->next_offset = (size_t)-1;
    strappy_db_sidebar_page_destroy(page);
    return 0;
  }
  reader->next_offset = offset + page->count;
  if (page->count > 0U) {
    const strappy_sidebar_record *last = &page->records[page->count - 1U];
    reader->last_activity = last->last_activity_at_ms;
    reader->last_id = last->session_id;
  }
  return 1;
}
