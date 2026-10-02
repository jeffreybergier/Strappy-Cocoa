#include "strappy_study_internal.h"

#include "strappy_core.h"
#include "strappy_db_internal.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int strappy_study_record_is_available(
  const strappy_discovered_database_record *record)
{
  return (record != NULL) && record->is_valid_sqlite &&
    (record->assistant_database_id != NULL) &&
    (record->assistant_database_id[0] != '\0') &&
    (record->user_decision != NULL) &&
    (strcmp(record->user_decision, "allowed") == 0);
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

int strappy_study_copy_fresh_value_with_updated_at(
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

int strappy_study_list_append(strappy_study_database_id_list *list,
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

int strappy_study_collect_progress(
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
  if (!strappy_db_open_reader(db_path, &db, error_out)) {
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
