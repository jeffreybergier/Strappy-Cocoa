#include "strappy_study.h"

#include "strappy_core.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int strappy_study_string_has_non_whitespace(const char *value)
{
  const unsigned char *cursor;

  if (value == NULL) {
    return 0;
  }
  cursor = (const unsigned char *)value;
  while (*cursor != '\0') {
    if ((*cursor != (unsigned char)' ') &&
        (*cursor != (unsigned char)'\t') &&
        (*cursor != (unsigned char)'\r') &&
        (*cursor != (unsigned char)'\n') &&
        (*cursor != (unsigned char)'\f') &&
        (*cursor != (unsigned char)'\v')) {
      return 1;
    }
    cursor++;
  }
  return 0;
}

static int strappy_study_record_is_available(
  const strappy_discovered_database_record *record)
{
  return (record != NULL) && record->is_valid_sqlite &&
    (record->assistant_database_id != NULL) &&
    (record->assistant_database_id[0] != '\0') &&
    (record->user_decision != NULL) &&
    (strcmp(record->user_decision, "allowed") == 0);
}

static int strappy_study_open_database(const char *db_path,
                                       sqlite3 **db_out,
                                       char **error_out)
{
  sqlite3 *db;
  int rc;

  if (db_out != NULL) {
    *db_out = NULL;
  }
  if ((db_path == NULL) || (db_path[0] == '\0') || (db_out == NULL)) {
    strappy_set_error(error_out, "Database Study path is not configured.");
    return 0;
  }
  if (!strappy_db_initialize(db_path, error_out)) {
    return 0;
  }
  db = NULL;
  rc = sqlite3_open_v2(db_path,
                       &db,
                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                       NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(
      error_out,
      "Could not open Database Study storage: %s",
      (db != NULL) ? sqlite3_errmsg(db) : "unknown SQLite error");
    sqlite3_close(db);
    return 0;
  }
  rc = sqlite3_exec(db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not configure Database Study storage: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }
  *db_out = db;
  return 1;
}

void strappy_study_database_id_list_init(
  strappy_study_database_id_list *list)
{
  if (list == NULL) {
    return;
  }
  list->database_ids = NULL;
  list->count = 0U;
}

void strappy_study_database_id_list_destroy(
  strappy_study_database_id_list *list)
{
  size_t index;

  if (list == NULL) {
    return;
  }
  for (index = 0U; index < list->count; index++) {
    free(list->database_ids[index]);
  }
  free(list->database_ids);
  strappy_study_database_id_list_init(list);
}

void strappy_study_batch_init(strappy_study_batch *batch)
{
  if (batch == NULL) {
    return;
  }
  strappy_study_database_id_list_init(&batch->database_ids);
  strappy_study_database_id_list_init(&batch->pending_database_ids);
  batch->prompt = NULL;
}

void strappy_study_batch_destroy(strappy_study_batch *batch)
{
  if (batch == NULL) {
    return;
  }
  strappy_study_database_id_list_destroy(&batch->database_ids);
  strappy_study_database_id_list_destroy(&batch->pending_database_ids);
  free(batch->prompt);
  strappy_study_batch_init(batch);
}

static void strappy_study_database_status_record_init(
  strappy_study_database_status_record *record)
{
  if (record == NULL) {
    return;
  }
  record->database_id = NULL;
  record->path = NULL;
  record->app_group_key = NULL;
  record->app_name = NULL;
  record->app_bundle_id = NULL;
  record->description = NULL;
  record->context = NULL;
  record->studied = 0;
  record->studied_at_ms = 0LL;
}

static void strappy_study_database_status_record_destroy(
  strappy_study_database_status_record *record)
{
  if (record == NULL) {
    return;
  }
  free(record->database_id);
  free(record->path);
  free(record->app_group_key);
  free(record->app_name);
  free(record->app_bundle_id);
  free(record->description);
  free(record->context);
  strappy_study_database_status_record_init(record);
}

void strappy_study_database_status_record_list_init(
  strappy_study_database_status_record_list *list)
{
  if (list == NULL) {
    return;
  }
  list->records = NULL;
  list->count = 0U;
}

void strappy_study_database_status_record_list_destroy(
  strappy_study_database_status_record_list *list)
{
  size_t index;

  if (list == NULL) {
    return;
  }
  for (index = 0U; index < list->count; index++) {
    strappy_study_database_status_record_destroy(&list->records[index]);
  }
  free(list->records);
  strappy_study_database_status_record_list_init(list);
}

int strappy_study_key_is_valid(const char *key)
{
  return (key != NULL) &&
    ((strcmp(key, STRAPPY_STUDY_KEY_DESCRIPTION) == 0) ||
     (strcmp(key, STRAPPY_STUDY_KEY_CONTEXT) == 0));
}

static int strappy_study_copy_fresh_value_with_updated_at(
  sqlite3 *db,
  const strappy_discovered_database_record *record,
  const char *key,
  char **value_out,
  long long *updated_at_ms_out,
  char **error_out)
{
  static const char *sql =
    "SELECT content, updated_at_ms FROM database_hints "
    "WHERE database_id = ?1 AND kind = ?2 "
    "AND observed_modified_at_s = ?3 "
    "ORDER BY updated_at_ms DESC, id DESC LIMIT 1;";
  sqlite3_stmt *stmt;
  const unsigned char *text;
  int rc;

  if (value_out != NULL) {
    *value_out = NULL;
  }
  if (updated_at_ms_out != NULL) {
    *updated_at_ms_out = 0LL;
  }
  if ((db == NULL) || (record == NULL) ||
      !strappy_study_key_is_valid(key) || (value_out == NULL)) {
    strappy_set_error(error_out, "Database Study value request is incomplete.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt,
                          1,
                          (sqlite3_int64)record->catalog_id) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 2, key, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt,
                          3,
                          (sqlite3_int64)record->modified_at) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare Database Study value: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    text = sqlite3_column_text(stmt, 0);
    if (text == NULL) {
      strappy_set_error(error_out, "Stored Database Study value is empty.");
      sqlite3_finalize(stmt);
      return 0;
    }
    *value_out = strappy_string_duplicate((const char *)text);
    if (*value_out == NULL) {
      strappy_set_error(error_out, "Could not allocate Database Study value.");
      sqlite3_finalize(stmt);
      return 0;
    }
    if (updated_at_ms_out != NULL) {
      *updated_at_ms_out = (long long)sqlite3_column_int64(stmt, 1);
    }
  } else if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read Database Study value: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  return 1;
}

int strappy_study_copy_fresh_value(
  sqlite3 *db,
  const strappy_discovered_database_record *record,
  const char *key,
  char **value_out,
  char **error_out)
{
  return strappy_study_copy_fresh_value_with_updated_at(db,
                                                         record,
                                                         key,
                                                         value_out,
                                                         NULL,
                                                         error_out);
}

static int strappy_study_add_nullable_string(cJSON *object,
                                             const char *name,
                                             const char *value)
{
  cJSON *item;

  item = (value != NULL) ? cJSON_CreateString(value) : cJSON_CreateNull();
  if (item == NULL) {
    return 0;
  }
  if (!cJSON_AddItemToObject(object, name, item)) {
    cJSON_Delete(item);
    return 0;
  }
  return 1;
}

static int strappy_study_upsert_value(
  sqlite3 *db,
  const strappy_discovered_database_record *record,
  const char *key,
  const char *value,
  long long source_item_id,
  sqlite3_int64 now_ms)
{
  static const char *select_sql =
    "SELECT id FROM database_hints WHERE database_id = ?1 AND kind = ?2 "
    "ORDER BY id LIMIT 1;";
  static const char *update_sql =
    "UPDATE database_hints SET content = ?1, observed_modified_at_s = ?2, "
    "source_item_id = ?3, updated_at_ms = ?4 WHERE id = ?5;";
  static const char *insert_sql =
    "INSERT INTO database_hints "
    "(database_id, kind, content, observed_modified_at_s, source_item_id, "
    "created_at_ms, updated_at_ms) "
    "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?6);";
  static const char *delete_duplicates_sql =
    "DELETE FROM database_hints WHERE database_id = ?1 AND kind = ?2 "
    "AND id <> ?3;";
  sqlite3_stmt *stmt;
  sqlite3_int64 row_id;
  int rc;
  int ok;

  if ((db == NULL) || !strappy_study_record_is_available(record) ||
      !strappy_study_key_is_valid(key) ||
      !strappy_study_string_has_non_whitespace(value)) {
    return 0;
  }

  row_id = 0;
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, select_sql, -1, &stmt, NULL);
  ok = (rc == SQLITE_OK) &&
    (sqlite3_bind_int64(stmt,
                        1,
                        (sqlite3_int64)record->catalog_id) == SQLITE_OK) &&
    (sqlite3_bind_text(stmt, 2, key, -1, SQLITE_TRANSIENT) == SQLITE_OK);
  if (ok) {
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
      row_id = sqlite3_column_int64(stmt, 0);
    } else if (rc != SQLITE_DONE) {
      ok = 0;
    }
  }
  sqlite3_finalize(stmt);

  stmt = NULL;
  if (ok && (row_id > 0)) {
    rc = sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
    ok = (rc == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 1, value, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
      (sqlite3_bind_int64(stmt,
                          2,
                          (sqlite3_int64)record->modified_at) == SQLITE_OK) &&
      ((source_item_id > 0LL) ?
         (sqlite3_bind_int64(stmt,
                             3,
                             (sqlite3_int64)source_item_id) == SQLITE_OK) :
         (sqlite3_bind_null(stmt, 3) == SQLITE_OK)) &&
      (sqlite3_bind_int64(stmt, 4, now_ms) == SQLITE_OK) &&
      (sqlite3_bind_int64(stmt, 5, row_id) == SQLITE_OK) &&
      (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
  } else if (ok) {
    rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
    ok = (rc == SQLITE_OK) &&
      (sqlite3_bind_int64(stmt,
                          1,
                          (sqlite3_int64)record->catalog_id) == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 2, key, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 3, value, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
      (sqlite3_bind_int64(stmt,
                          4,
                          (sqlite3_int64)record->modified_at) == SQLITE_OK) &&
      ((source_item_id > 0LL) ?
         (sqlite3_bind_int64(stmt,
                             5,
                             (sqlite3_int64)source_item_id) == SQLITE_OK) :
         (sqlite3_bind_null(stmt, 5) == SQLITE_OK)) &&
      (sqlite3_bind_int64(stmt, 6, now_ms) == SQLITE_OK) &&
      (sqlite3_step(stmt) == SQLITE_DONE);
    if (ok) {
      row_id = sqlite3_last_insert_rowid(db);
    }
    sqlite3_finalize(stmt);
  }

  stmt = NULL;
  if (ok) {
    rc = sqlite3_prepare_v2(db, delete_duplicates_sql, -1, &stmt, NULL);
    ok = (rc == SQLITE_OK) &&
      (sqlite3_bind_int64(stmt,
                          1,
                          (sqlite3_int64)record->catalog_id) == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 2, key, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
      (sqlite3_bind_int64(stmt, 3, row_id) == SQLITE_OK) &&
      (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
  }

  return ok;
}

char *strappy_study_save_values(
  sqlite3 *db,
  const strappy_discovered_database_record *record,
  const char *description,
  const char *context,
  long long source_item_id,
  char **error_out)
{
  sqlite3_int64 now_ms;
  char *json;
  int ok;

  if ((db == NULL) || !strappy_study_record_is_available(record)) {
    strappy_set_error(error_out, "database_study request is incomplete.");
    return NULL;
  }
  if (!strappy_study_string_has_non_whitespace(description)) {
    strappy_set_error(error_out,
                      "database_study description must not be blank.");
    return NULL;
  }
  if (!strappy_study_string_has_non_whitespace(context)) {
    strappy_set_error(error_out, "database_study context must not be blank.");
    return NULL;
  }

  json = strappy_string_duplicate("{}");
  if (json == NULL) {
    strappy_set_error(error_out,
                      "Could not serialize database_study acknowledgement.");
    return NULL;
  }

  if (sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not begin database_study: %s",
                                sqlite3_errmsg(db));
    free(json);
    return NULL;
  }

  now_ms = (sqlite3_int64)time(NULL) * 1000;
  ok = strappy_study_upsert_value(db,
                                  record,
                                  STRAPPY_STUDY_KEY_DESCRIPTION,
                                  description,
                                  source_item_id,
                                  now_ms) &&
    strappy_study_upsert_value(db,
                               record,
                               STRAPPY_STUDY_KEY_CONTEXT,
                               context,
                               source_item_id,
                               now_ms);
  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not save database_study: %s",
                                sqlite3_errmsg(db));
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    free(json);
    return NULL;
  }
  if (sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not commit database_study: %s",
                                sqlite3_errmsg(db));
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    free(json);
    return NULL;
  }
  return json;
}

char *strappy_study_status_json(const char *db_path, char **error_out)
{
  strappy_discovered_database_record_list discovered;
  sqlite3 *db;
  cJSON *root;
  cJSON *databases;
  char *json;
  size_t index;

  strappy_discovered_database_record_list_init(&discovered);
  if (!strappy_db_list_discovered_databases(db_path,
                                             &discovered,
                                             error_out)) {
    return NULL;
  }
  if (!strappy_study_open_database(db_path, &db, error_out)) {
    strappy_discovered_database_record_list_destroy(&discovered);
    return NULL;
  }
  root = cJSON_CreateObject();
  databases = cJSON_CreateArray();
  if ((root == NULL) || (databases == NULL)) {
    cJSON_Delete(root);
    cJSON_Delete(databases);
    sqlite3_close(db);
    strappy_discovered_database_record_list_destroy(&discovered);
    strappy_set_error(error_out, "Could not allocate Database Study JSON.");
    return NULL;
  }

  for (index = 0U; index < discovered.count; index++) {
    const strappy_discovered_database_record *record;
    char *description;
    char *context;

    record = &discovered.records[index];
    if (!strappy_study_record_is_available(record)) {
      continue;
    }
    description = NULL;
    context = NULL;
    if (!strappy_study_copy_fresh_value(db,
                                         record,
                                         STRAPPY_STUDY_KEY_DESCRIPTION,
                                         &description,
                                         error_out) ||
        !strappy_study_copy_fresh_value(db,
                                         record,
                                         STRAPPY_STUDY_KEY_CONTEXT,
                                         &context,
                                         error_out)) {
      free(description);
      free(context);
      cJSON_Delete(root);
      cJSON_Delete(databases);
      sqlite3_close(db);
      strappy_discovered_database_record_list_destroy(&discovered);
      return NULL;
    }
    {
      cJSON *entry;

      entry = cJSON_CreateObject();
      if ((entry == NULL) ||
        (cJSON_AddStringToObject(entry,
                                 "database_id",
                                 record->assistant_database_id) == NULL) ||
        !strappy_study_add_nullable_string(
          entry,
          STRAPPY_STUDY_KEY_DESCRIPTION,
          description) ||
        !strappy_study_add_nullable_string(
          entry,
          STRAPPY_STUDY_KEY_CONTEXT,
          context) ||
        !cJSON_AddItemToArray(databases, entry)) {
        free(description);
        free(context);
        cJSON_Delete(entry);
        cJSON_Delete(root);
        cJSON_Delete(databases);
        sqlite3_close(db);
        strappy_discovered_database_record_list_destroy(&discovered);
        strappy_set_error(error_out, "Could not build Database Study JSON.");
        return NULL;
      }
    }
    free(description);
    free(context);
  }
  sqlite3_close(db);
  strappy_discovered_database_record_list_destroy(&discovered);

  if (!cJSON_AddItemToObject(root, "databases", databases)) {
    cJSON_Delete(root);
    cJSON_Delete(databases);
    strappy_set_error(error_out, "Could not build Database Study JSON.");
    return NULL;
  }
  json = cJSON_Print(root);
  cJSON_Delete(root);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize Database Study JSON.");
  }
  return json;
}

int strappy_study_delete_database_values(const char *db_path,
                                         const char *database_id,
                                         char **error_out)
{
  static const char *delete_sql =
    "DELETE FROM database_hints "
    "WHERE database_id = "
      "(SELECT id FROM databases WHERE assistant_database_id = ?1) "
    "AND kind IN (?2, ?3);";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (!strappy_study_string_has_non_whitespace(database_id)) {
    strappy_set_error(error_out, "Database Study database ID is missing.");
    return 0;
  }
  if (!strappy_study_open_database(db_path, &db, error_out)) {
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, delete_sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt,
                           1,
                           database_id,
                           -1,
                           SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt,
                           2,
                           STRAPPY_STUDY_KEY_DESCRIPTION,
                           -1,
                           SQLITE_STATIC);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt,
                           3,
                           STRAPPY_STUDY_KEY_CONTEXT,
                           -1,
                           SQLITE_STATIC);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not delete Database Study values: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}

int strappy_study_reset(const char *db_path, char **error_out)
{
  sqlite3 *db;
  int rc;

  if (!strappy_study_open_database(db_path, &db, error_out)) {
    return 0;
  }
  rc = sqlite3_exec(db, "DELETE FROM database_hints;", NULL, NULL, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not reset Database Study values: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }
  sqlite3_close(db);
  return 1;
}

static int strappy_study_list_append(strappy_study_database_id_list *list,
                                     const char *database_id,
                                     char **error_out)
{
  char **values;

  if ((list == NULL) || (database_id == NULL)) {
    strappy_set_error(error_out, "Database Study target is incomplete.");
    return 0;
  }
  if (list->count >= (((size_t)-1) / sizeof(char *))) {
    strappy_set_error(error_out, "Database Study target list is too large.");
    return 0;
  }
  values = (char **)realloc(list->database_ids,
                            (list->count + 1U) * sizeof(char *));
  if (values == NULL) {
    strappy_set_error(error_out, "Could not allocate Database Study targets.");
    return 0;
  }
  list->database_ids = values;
  list->database_ids[list->count] = strappy_string_duplicate(database_id);
  if (list->database_ids[list->count] == NULL) {
    strappy_set_error(error_out, "Could not allocate Database Study target.");
    return 0;
  }
  list->count++;
  return 1;
}

static int strappy_study_status_list_append(
  strappy_study_database_status_record_list *list,
  const strappy_discovered_database_record *source,
  const char *description,
  const char *context,
  int studied,
  long long studied_at_ms,
  char **error_out)
{
  strappy_study_database_status_record *record;
  strappy_study_database_status_record *records;

  if ((list == NULL) || (source == NULL)) {
    strappy_set_error(error_out, "Database Study status row is incomplete.");
    return 0;
  }
  if (list->count >=
      (((size_t)-1) / sizeof(strappy_study_database_status_record))) {
    strappy_set_error(error_out, "Database Study status list is too large.");
    return 0;
  }

  records = (strappy_study_database_status_record *)realloc(
    list->records,
    (list->count + 1U) * sizeof(strappy_study_database_status_record));
  if (records == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate Database Study status rows.");
    return 0;
  }
  list->records = records;
  record = &list->records[list->count];
  strappy_study_database_status_record_init(record);
  record->database_id = strappy_string_duplicate(
    (source->assistant_database_id != NULL) ?
      source->assistant_database_id : "");
  record->path = strappy_string_duplicate(
    (source->path != NULL) ? source->path : "");
  record->app_group_key = strappy_string_duplicate(
    (source->app_group_key != NULL) ? source->app_group_key : "");
  record->app_name = strappy_string_duplicate(
    (source->app_name != NULL) ? source->app_name : "");
  record->app_bundle_id = strappy_string_duplicate(
    (source->app_bundle_id != NULL) ? source->app_bundle_id : "");
  record->description = (description != NULL) ?
    strappy_string_duplicate(description) : NULL;
  record->context = (context != NULL) ?
    strappy_string_duplicate(context) : NULL;
  if ((record->database_id == NULL) || (record->path == NULL) ||
      (record->app_group_key == NULL) || (record->app_name == NULL) ||
      (record->app_bundle_id == NULL) ||
      ((description != NULL) && (record->description == NULL)) ||
      ((context != NULL) && (record->context == NULL))) {
    strappy_study_database_status_record_destroy(record);
    strappy_set_error(error_out,
                      "Could not allocate a Database Study status row.");
    return 0;
  }
  record->studied = studied ? 1 : 0;
  record->studied_at_ms = studied ? studied_at_ms : 0LL;
  list->count++;
  return 1;
}

static int strappy_study_collect_progress(
  const char *db_path,
  strappy_study_database_id_list *pending_list,
  strappy_study_database_status_record_list *status_list,
  size_t *studied_count_out,
  size_t *approved_count_out,
  char **error_out)
{
  strappy_discovered_database_record_list discovered;
  sqlite3 *db;
  size_t approved_count;
  size_t index;
  size_t studied_count;

  if ((pending_list == NULL) && (status_list == NULL) &&
      ((studied_count_out == NULL) || (approved_count_out == NULL))) {
    strappy_set_error(error_out, "Database Study progress has no output.");
    return 0;
  }
  if (pending_list != NULL) {
    strappy_study_database_id_list_init(pending_list);
  }
  if (status_list != NULL) {
    strappy_study_database_status_record_list_init(status_list);
  }
  if (studied_count_out != NULL) {
    *studied_count_out = 0U;
  }
  if (approved_count_out != NULL) {
    *approved_count_out = 0U;
  }
  strappy_discovered_database_record_list_init(&discovered);
  if (!strappy_db_list_discovered_databases(db_path, &discovered, error_out)) {
    return 0;
  }
  if (!strappy_study_open_database(db_path, &db, error_out)) {
    strappy_discovered_database_record_list_destroy(&discovered);
    return 0;
  }

  approved_count = 0U;
  studied_count = 0U;
  for (index = 0U; index < discovered.count; index++) {
    const strappy_discovered_database_record *record;
    char *description;
    char *context;
    long long context_updated_at_ms;
    long long description_updated_at_ms;
    long long studied_at_ms;
    int studied;
    int ok;

    record = &discovered.records[index];
    if (!strappy_study_record_is_available(record)) {
      continue;
    }
    approved_count++;
    description = NULL;
    context = NULL;
    description_updated_at_ms = 0LL;
    context_updated_at_ms = 0LL;
    ok = strappy_study_copy_fresh_value_with_updated_at(
           db,
           record,
           STRAPPY_STUDY_KEY_DESCRIPTION,
           &description,
           &description_updated_at_ms,
           error_out) &&
      strappy_study_copy_fresh_value_with_updated_at(
        db,
        record,
        STRAPPY_STUDY_KEY_CONTEXT,
        &context,
        &context_updated_at_ms,
        error_out);
    if (!ok) {
      free(description);
      free(context);
      sqlite3_close(db);
      strappy_discovered_database_record_list_destroy(&discovered);
      if (pending_list != NULL) {
        strappy_study_database_id_list_destroy(pending_list);
      }
      if (status_list != NULL) {
        strappy_study_database_status_record_list_destroy(status_list);
      }
      return 0;
    }
    studied = ((description != NULL) && (context != NULL)) ? 1 : 0;
    studied_at_ms = (description_updated_at_ms > context_updated_at_ms) ?
      description_updated_at_ms : context_updated_at_ms;
    if ((status_list != NULL) &&
        !strappy_study_status_list_append(status_list,
                                          record,
                                          description,
                                          context,
                                          studied,
                                          studied_at_ms,
                                          error_out)) {
      free(description);
      free(context);
      sqlite3_close(db);
      strappy_discovered_database_record_list_destroy(&discovered);
      if (pending_list != NULL) {
        strappy_study_database_id_list_destroy(pending_list);
      }
      strappy_study_database_status_record_list_destroy(status_list);
      return 0;
    }
    if (studied) {
      studied_count++;
    } else if (pending_list != NULL) {
      if (!strappy_study_list_append(pending_list,
                                     record->assistant_database_id,
                                     error_out)) {
        free(description);
        free(context);
        sqlite3_close(db);
        strappy_discovered_database_record_list_destroy(&discovered);
        strappy_study_database_id_list_destroy(pending_list);
        if (status_list != NULL) {
          strappy_study_database_status_record_list_destroy(status_list);
        }
        return 0;
      }
    }
    free(description);
    free(context);
  }

  sqlite3_close(db);
  strappy_discovered_database_record_list_destroy(&discovered);
  if (studied_count_out != NULL) {
    *studied_count_out = studied_count;
  }
  if (approved_count_out != NULL) {
    *approved_count_out = approved_count;
  }
  return 1;
}

int strappy_study_progress(const char *db_path,
                           size_t *studied_count_out,
                           size_t *approved_count_out,
                           char **error_out)
{
  if ((studied_count_out == NULL) || (approved_count_out == NULL)) {
    strappy_set_error(error_out, "Database Study progress has no output.");
    return 0;
  }
  return strappy_study_collect_progress(db_path,
                                        NULL,
                                        NULL,
                                        studied_count_out,
                                        approved_count_out,
                                        error_out);
}

int strappy_study_list_database_status_records(
  const char *db_path,
  strappy_study_database_status_record_list *list,
  char **error_out)
{
  if (list == NULL) {
    strappy_set_error(error_out, "Database Study status list has no output.");
    return 0;
  }
  return strappy_study_collect_progress(db_path,
                                        NULL,
                                        list,
                                        NULL,
                                        NULL,
                                        error_out);
}

int strappy_study_list_unstudied_database_ids(
  const char *db_path,
  strappy_study_database_id_list *list,
  char **error_out)
{
  if (list == NULL) {
    strappy_set_error(error_out, "Database Study target list has no output.");
    return 0;
  }
  return strappy_study_collect_progress(db_path,
                                        list,
                                        NULL,
                                        NULL,
                                        NULL,
                                        error_out);
}

static int strappy_study_status_records_share_app(
  const strappy_study_database_status_record_list *list,
  size_t left_index,
  size_t right_index)
{
  const char *left_key;
  const char *right_key;

  if ((list == NULL) || (left_index >= list->count) ||
      (right_index >= list->count)) {
    return 0;
  }
  if (left_index == right_index) {
    return 1;
  }
  left_key = list->records[left_index].app_group_key;
  right_key = list->records[right_index].app_group_key;
  return (left_key != NULL) && (left_key[0] != '\0') &&
    (right_key != NULL) && (right_key[0] != '\0') &&
    (strcmp(left_key, right_key) == 0);
}

static int strappy_study_status_group_was_seen(
  const strappy_study_database_status_record_list *list,
  size_t index)
{
  size_t previous_index;

  for (previous_index = 0U; previous_index < index; previous_index++) {
    if (strappy_study_status_records_share_app(list,
                                               previous_index,
                                               index)) {
      return 1;
    }
  }
  return 0;
}

static int strappy_study_append_selected_group(
  const strappy_study_database_status_record_list *status,
  size_t group_index,
  unsigned char *selected,
  strappy_study_batch *batch,
  char **error_out)
{
  size_t index;

  if ((status == NULL) || (group_index >= status->count) ||
      (selected == NULL) || (batch == NULL)) {
    strappy_set_error(error_out,
                      "Database Study application group is incomplete.");
    return 0;
  }

  for (index = 0U; index < status->count; index++) {
    const strappy_study_database_status_record *record;

    if (!strappy_study_status_records_share_app(status,
                                                 group_index,
                                                 index)) {
      continue;
    }
    record = &status->records[index];
    if ((record->database_id == NULL) || (record->database_id[0] == '\0') ||
        !strappy_study_list_append(&batch->database_ids,
                                   record->database_id,
                                   error_out) ||
        (!record->studied &&
         !strappy_study_list_append(&batch->pending_database_ids,
                                    record->database_id,
                                    error_out))) {
      if ((error_out == NULL) || (*error_out == NULL)) {
        strappy_set_error(error_out,
                          "Database Study application group has no valid ID.");
      }
      return 0;
    }
    selected[index] = 1U;
  }
  return 1;
}

static char *strappy_study_selected_batch_prompt(
  const strappy_study_database_status_record_list *status,
  const unsigned char *selected,
  char **error_out)
{
  static const char *instruction =
    "Complete the Database Study workflow for every database_id. "
    "Databases within one application group belong to the same app and may "
    "contain tightly coupled data.";
  cJSON *root;
  cJSON *groups;
  char *prompt;
  size_t index;

  if ((status == NULL) || (selected == NULL)) {
    strappy_set_error(error_out, "Database Study batch prompt is incomplete.");
    return NULL;
  }

  root = cJSON_CreateObject();
  groups = cJSON_CreateArray();
  if ((root == NULL) || (groups == NULL) ||
      (cJSON_AddStringToObject(root,
                               "instruction",
                               instruction) == NULL)) {
    cJSON_Delete(root);
    cJSON_Delete(groups);
    strappy_set_error(error_out,
                      "Could not allocate Database Study batch prompt.");
    return NULL;
  }

  for (index = 0U; index < status->count; index++) {
    const strappy_study_database_status_record *record;
    cJSON *group;
    cJSON *database_ids;
    size_t member_index;
    size_t previous_index;
    int already_added;

    if (!selected[index]) {
      continue;
    }
    already_added = 0;
    for (previous_index = 0U;
         previous_index < index;
         previous_index++) {
      if (selected[previous_index] &&
          strappy_study_status_records_share_app(status,
                                                  previous_index,
                                                  index)) {
        already_added = 1;
        break;
      }
    }
    if (already_added) {
      continue;
    }

    record = &status->records[index];
    group = cJSON_CreateObject();
    database_ids = cJSON_CreateArray();
    if ((group == NULL) || (database_ids == NULL) ||
        !strappy_study_add_nullable_string(
          group,
          "app_name",
          ((record->app_name != NULL) && (record->app_name[0] != '\0')) ?
            record->app_name : NULL) ||
        !strappy_study_add_nullable_string(
          group,
          "app_bundle_id",
          ((record->app_bundle_id != NULL) &&
           (record->app_bundle_id[0] != '\0')) ?
            record->app_bundle_id : NULL)) {
      cJSON_Delete(group);
      cJSON_Delete(database_ids);
      cJSON_Delete(root);
      cJSON_Delete(groups);
      strappy_set_error(error_out,
                        "Could not build Database Study application group.");
      return NULL;
    }

    for (member_index = 0U;
         member_index < status->count;
         member_index++) {
      cJSON *database_id;

      if (!selected[member_index] ||
          !strappy_study_status_records_share_app(status,
                                                   index,
                                                   member_index)) {
        continue;
      }
      database_id = cJSON_CreateString(
        status->records[member_index].database_id);
      if ((database_id == NULL) ||
          !cJSON_AddItemToArray(database_ids, database_id)) {
        cJSON_Delete(database_id);
        cJSON_Delete(group);
        cJSON_Delete(database_ids);
        cJSON_Delete(root);
        cJSON_Delete(groups);
        strappy_set_error(error_out,
                          "Could not build Database Study database IDs.");
        return NULL;
      }
    }

    if (!cJSON_AddItemToObject(group, "database_ids", database_ids)) {
      cJSON_Delete(group);
      cJSON_Delete(database_ids);
      cJSON_Delete(root);
      cJSON_Delete(groups);
      strappy_set_error(error_out,
                        "Could not build Database Study application group.");
      return NULL;
    }
    database_ids = NULL;
    if (!cJSON_AddItemToArray(groups, group)) {
      cJSON_Delete(group);
      cJSON_Delete(root);
      cJSON_Delete(groups);
      strappy_set_error(error_out,
                        "Could not build Database Study application groups.");
      return NULL;
    }
  }

  if (!cJSON_AddItemToObject(root, "application_groups", groups)) {
    cJSON_Delete(root);
    cJSON_Delete(groups);
    strappy_set_error(error_out,
                      "Could not build Database Study batch prompt.");
    return NULL;
  }
  groups = NULL;
  prompt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (prompt == NULL) {
    strappy_set_error(error_out,
                      "Could not serialize Database Study batch prompt.");
  }
  return prompt;
}

int strappy_study_next_batch(const char *db_path,
                             strappy_study_batch *batch,
                             char **error_out)
{
  strappy_study_database_status_record_list status;
  unsigned char *selected;
  size_t index;
  int has_pending;
  int ok;

  if (batch == NULL) {
    strappy_set_error(error_out, "Database Study batch has no output.");
    return 0;
  }
  strappy_study_batch_init(batch);
  strappy_study_database_status_record_list_init(&status);
  if (!strappy_study_list_database_status_records(db_path,
                                                   &status,
                                                   error_out)) {
    return 0;
  }
  if (status.count == 0U) {
    strappy_study_database_status_record_list_destroy(&status);
    return 1;
  }

  selected = (unsigned char *)calloc(status.count, sizeof(unsigned char));
  if (selected == NULL) {
    strappy_study_database_status_record_list_destroy(&status);
    strappy_set_error(error_out,
                      "Could not allocate Database Study batch selection.");
    return 0;
  }

  has_pending = 0;
  ok = 1;
  for (index = 0U; ok && (index < status.count); index++) {
    size_t group_count;
    size_t member_index;
    size_t pending_count;
    int should_select;

    if (strappy_study_status_group_was_seen(&status, index)) {
      continue;
    }
    group_count = 0U;
    pending_count = 0U;
    for (member_index = 0U;
         member_index < status.count;
         member_index++) {
      if (!strappy_study_status_records_share_app(&status,
                                                   index,
                                                   member_index)) {
        continue;
      }
      group_count++;
      if (!status.records[member_index].studied) {
        pending_count++;
      }
    }
    if (pending_count == 0U) {
      continue;
    }
    has_pending = 1;
    should_select = (batch->database_ids.count == 0U) ? 1 : 0;
    if (!should_select && (group_count < 3U) &&
        (batch->database_ids.count < 3U) &&
        (group_count <= (3U - batch->database_ids.count))) {
      should_select = 1;
    }
    if (!should_select) {
      continue;
    }
    ok = strappy_study_append_selected_group(&status,
                                              index,
                                              selected,
                                              batch,
                                              error_out);
    if (ok && ((group_count >= 3U) ||
               (batch->database_ids.count == 3U))) {
      break;
    }
  }

  if (ok && has_pending && (batch->pending_database_ids.count == 0U)) {
    strappy_set_error(error_out,
                      "Could not select the next Database Study batch.");
    ok = 0;
  }
  if (ok && (batch->database_ids.count > 0U)) {
    batch->prompt = strappy_study_selected_batch_prompt(&status,
                                                        selected,
                                                        error_out);
    ok = (batch->prompt != NULL) ? 1 : 0;
  }

  free(selected);
  strappy_study_database_status_record_list_destroy(&status);
  if (!ok) {
    strappy_study_batch_destroy(batch);
  }
  return ok;
}
