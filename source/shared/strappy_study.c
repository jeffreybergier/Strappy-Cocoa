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

char *strappy_study_save_value(
  sqlite3 *db,
  const strappy_discovered_database_record *record,
  const char *key,
  const char *value,
  long long source_item_id,
  char **error_out)
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
  sqlite3_int64 now_ms;
  int rc;
  int ok;
  char *json;
  cJSON *result;

  if ((db == NULL) || !strappy_study_record_is_available(record) ||
      !strappy_study_key_is_valid(key) ||
      !strappy_study_string_has_non_whitespace(value)) {
    strappy_set_error(error_out, "database_study request is incomplete.");
    return NULL;
  }

  now_ms = (sqlite3_int64)time(NULL) * 1000;
  row_id = 0;
  if (sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not begin database_study: %s",
                                sqlite3_errmsg(db));
    return NULL;
  }

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

  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not save database_study: %s",
                                sqlite3_errmsg(db));
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    return NULL;
  }
  if (sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not commit database_study: %s",
                                sqlite3_errmsg(db));
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    return NULL;
  }

  result = cJSON_CreateObject();
  json = (result != NULL) ? cJSON_PrintUnformatted(result) : NULL;
  cJSON_Delete(result);
  if (json == NULL) {
    strappy_set_error(error_out,
                      "Could not serialize database_study acknowledgement.");
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

char *strappy_study_batch_prompt(const char * const *database_ids,
                                 size_t database_id_count,
                                 char **error_out)
{
  static const char *prefix =
    "Study exactly these database_ids in this isolated batch: ";
  static const char *suffix =
    ". For each database_id, call database_study exactly twice: once with "
    "key description and once with key context. Complete both writes for "
    "every database_id.";
  cJSON *array;
  char *array_json;
  char *prompt;
  size_t index;
  size_t length;

  if ((database_ids == NULL) || (database_id_count == 0U) ||
      (database_id_count > 5U)) {
    strappy_set_error(error_out,
                      "Database Study batch must contain 1 to 5 IDs.");
    return NULL;
  }
  array = cJSON_CreateArray();
  if (array == NULL) {
    strappy_set_error(error_out, "Could not allocate Database Study batch.");
    return NULL;
  }
  for (index = 0U; index < database_id_count; index++) {
    cJSON *item;

    if ((database_ids[index] == NULL) || (database_ids[index][0] == '\0')) {
      cJSON_Delete(array);
      strappy_set_error(error_out, "Database Study batch contains an empty ID.");
      return NULL;
    }
    item = cJSON_CreateString(database_ids[index]);
    if ((item == NULL) || !cJSON_AddItemToArray(array, item)) {
      cJSON_Delete(item);
      cJSON_Delete(array);
      strappy_set_error(error_out, "Could not build Database Study batch.");
      return NULL;
    }
  }
  array_json = cJSON_PrintUnformatted(array);
  cJSON_Delete(array);
  if (array_json == NULL) {
    strappy_set_error(error_out, "Could not serialize Database Study batch.");
    return NULL;
  }
  length = strlen(prefix) + strlen(array_json) + strlen(suffix);
  prompt = (char *)malloc(length + 1U);
  if (prompt == NULL) {
    free(array_json);
    strappy_set_error(error_out, "Could not allocate Database Study prompt.");
    return NULL;
  }
  snprintf(prompt, length + 1U, "%s%s%s", prefix, array_json, suffix);
  free(array_json);
  return prompt;
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
  if ((record->database_id == NULL) || (record->path == NULL) ||
      (record->app_group_key == NULL) || (record->app_name == NULL) ||
      (record->app_bundle_id == NULL)) {
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
