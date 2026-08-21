#include "strappy_db_internal.h"

#include "strappy_core.h"

#include <cJSON.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Database discovery, model catalog, and model-selection persistence. */

void strappy_discovered_database_record_init(strappy_discovered_database_record *record)
{
  if (record == NULL) {
    return;
  }

  record->catalog_id = 0;
  record->assistant_database_id = NULL;
  record->path = NULL;
  record->size = 0;
  record->modified_at = 0;
  record->device = 0U;
  record->inode = 0U;
  record->is_valid_sqlite = 0;
  record->validation_error = NULL;
  record->scan_status = NULL;
  record->user_decision = NULL;
  record->scan_root = NULL;
  record->app_group_key = NULL;
  record->app_name = NULL;
  record->app_bundle_id = NULL;
  record->app_container_path = NULL;
  record->app_bundle_path = NULL;
  record->app_source = NULL;
  record->origin_kind = NULL;
  record->location_tail = NULL;
  record->hidden = 0;
  record->auto_hidden = 0;
  record->has_hidden_override = 0;
  record->hidden_override = 0;
  record->hidden_reason = NULL;
  record->first_seen_at = NULL;
  record->last_seen_at = NULL;
  record->last_scanned_at = NULL;
}

void strappy_discovered_database_record_destroy(strappy_discovered_database_record *record)
{
  if (record == NULL) {
    return;
  }

  free(record->assistant_database_id);
  free(record->path);
  free(record->validation_error);
  free(record->scan_status);
  free(record->user_decision);
  free(record->scan_root);
  free(record->app_group_key);
  free(record->app_name);
  free(record->app_bundle_id);
  free(record->app_container_path);
  free(record->app_bundle_path);
  free(record->app_source);
  free(record->origin_kind);
  free(record->location_tail);
  free(record->hidden_reason);
  free(record->first_seen_at);
  free(record->last_seen_at);
  free(record->last_scanned_at);
  strappy_discovered_database_record_init(record);
}

void strappy_discovered_database_record_list_init(
  strappy_discovered_database_record_list *list)
{
  if (list == NULL) {
    return;
  }

  list->records = NULL;
  list->count = 0U;
}

void strappy_discovered_database_record_list_destroy(
  strappy_discovered_database_record_list *list)
{
  size_t index;

  if (list == NULL) {
    return;
  }

  for (index = 0U; index < list->count; index++) {
    strappy_discovered_database_record_destroy(&list->records[index]);
  }
  free(list->records);
  strappy_discovered_database_record_list_init(list);
}

void strappy_database_display_name_record_list_init(
  strappy_database_display_name_record_list *list)
{
  if (list == NULL) {
    return;
  }
  list->records = NULL;
  list->count = 0U;
}

void strappy_database_display_name_record_list_destroy(
  strappy_database_display_name_record_list *list)
{
  size_t index;

  if (list == NULL) {
    return;
  }
  for (index = 0U; index < list->count; index++) {
    free(list->records[index].assistant_database_id);
    free(list->records[index].filename);
  }
  free(list->records);
  strappy_database_display_name_record_list_init(list);
}

void strappy_model_record_init(strappy_model_record *record)
{
  if (record == NULL) {
    return;
  }

  record->model_id = NULL;
  record->provider_account_id = NULL;
  record->provider_id = NULL;
  record->provider_account_name = NULL;
  record->wire_model_id = NULL;
  record->billing_kind = NULL;
  record->reasoning_enabled = 1;
  record->local_functions_enabled = 1;
  record->image_input_enabled = 0;
  record->hosted_tools_enabled = 1;
  record->canonical_slug = NULL;
  record->hugging_face_id = NULL;
  record->name = NULL;
  record->description = NULL;
  record->context_length = 0;
  record->created = 0;
  record->architecture_modality = NULL;
  record->architecture_tokenizer = NULL;
  record->architecture_instruct_type = NULL;
  record->pricing_prompt = NULL;
  record->pricing_completion = NULL;
  record->pricing_request = NULL;
  record->pricing_image = NULL;
  record->pricing_audio = NULL;
  record->pricing_web_search = NULL;
  record->pricing_internal_reasoning = NULL;
  record->pricing_input_cache_read = NULL;
  record->pricing_input_cache_write = NULL;
  record->top_provider_context_length = 0;
  record->top_provider_max_completion_tokens = 0;
  record->top_provider_is_moderated = 0;
  record->knowledge_cutoff = NULL;
  record->expiration_date = NULL;
  record->links_details = NULL;
  record->links_json = NULL;
  record->reasoning_json = NULL;
  record->benchmarks_json = NULL;
  record->default_parameters_json = NULL;
  record->per_request_limits_json = NULL;
  record->raw_json = NULL;
  record->fetched_at = NULL;
  record->selected = 0;
  record->allowed = 0;
}

void strappy_model_record_destroy(strappy_model_record *record)
{
  if (record == NULL) {
    return;
  }

  free(record->model_id);
  free(record->provider_account_id);
  free(record->provider_id);
  free(record->provider_account_name);
  free(record->wire_model_id);
  free(record->billing_kind);
  free(record->canonical_slug);
  free(record->hugging_face_id);
  free(record->name);
  free(record->description);
  free(record->architecture_modality);
  free(record->architecture_tokenizer);
  free(record->architecture_instruct_type);
  free(record->pricing_prompt);
  free(record->pricing_completion);
  free(record->pricing_request);
  free(record->pricing_image);
  free(record->pricing_audio);
  free(record->pricing_web_search);
  free(record->pricing_internal_reasoning);
  free(record->pricing_input_cache_read);
  free(record->pricing_input_cache_write);
  free(record->knowledge_cutoff);
  free(record->expiration_date);
  free(record->links_details);
  free(record->links_json);
  free(record->reasoning_json);
  free(record->benchmarks_json);
  free(record->default_parameters_json);
  free(record->per_request_limits_json);
  free(record->raw_json);
  free(record->fetched_at);
  strappy_model_record_init(record);
}

void strappy_model_record_list_init(strappy_model_record_list *list)
{
  if (list == NULL) {
    return;
  }

  list->records = NULL;
  list->count = 0U;
}

void strappy_model_record_list_destroy(strappy_model_record_list *list)
{
  size_t index;

  if (list == NULL) {
    return;
  }

  for (index = 0U; index < list->count; index++) {
    strappy_model_record_destroy(&list->records[index]);
  }
  free(list->records);
  strappy_model_record_list_init(list);
}

void strappy_openrouter_model_record_init(strappy_openrouter_model_record *record)
{
  strappy_model_record_init(record);
}

void strappy_openrouter_model_record_destroy(strappy_openrouter_model_record *record)
{
  strappy_model_record_destroy(record);
}

void strappy_openrouter_model_record_list_init(
  strappy_openrouter_model_record_list *list)
{
  strappy_model_record_list_init(list);
}

void strappy_openrouter_model_record_list_destroy(
  strappy_openrouter_model_record_list *list)
{
  strappy_model_record_list_destroy(list);
}

void strappy_model_route_record_init(strappy_model_route_record *record)
{
  if (record == NULL) {
    return;
  }
  record->model_id = NULL;
  record->provider_account_id = NULL;
  record->provider_id = NULL;
  record->responses_endpoint = NULL;
  record->wire_model_id = NULL;
  record->billing_kind = NULL;
  record->reasoning_enabled = 1;
  record->local_functions_enabled = 1;
  record->hosted_tools_enabled = 1;
}

void strappy_model_route_record_destroy(strappy_model_route_record *record)
{
  if (record == NULL) {
    return;
  }
  free(record->model_id);
  free(record->provider_account_id);
  free(record->provider_id);
  free(record->responses_endpoint);
  free(record->wire_model_id);
  free(record->billing_kind);
  strappy_model_route_record_init(record);
}

void strappy_provider_account_record_init(
  strappy_provider_account_record *record)
{
  if (record != NULL) {
    memset(record, 0, sizeof(*record));
  }
}

void strappy_provider_account_record_destroy(
  strappy_provider_account_record *record)
{
  if (record == NULL) {
    return;
  }
  free(record->account_id);
  free(record->provider_id);
  free(record->display_name);
  free(record->lifecycle_state);
  free(record->responses_endpoint);
  strappy_provider_account_record_init(record);
}

void strappy_provider_account_record_list_init(
  strappy_provider_account_record_list *list)
{
  if (list != NULL) {
    list->records = NULL;
    list->count = 0U;
  }
}

void strappy_provider_account_record_list_destroy(
  strappy_provider_account_record_list *list)
{
  size_t index;
  if (list == NULL) {
    return;
  }
  for (index = 0U; index < list->count; index++) {
    strappy_provider_account_record_destroy(&list->records[index]);
  }
  free(list->records);
  strappy_provider_account_record_list_init(list);
}

static int strappy_db_validate_account_metadata(
  const char *provider_id,
  const char *display_name,
  const char *responses_endpoint,
  int allow_incomplete_endpoint,
  const strappy_provider_definition **definition_out,
  char **error_out)
{
  const strappy_provider_definition *definition;
  char *resolved;
  size_t display_name_length;

  display_name_length = (display_name != NULL) ? strlen(display_name) : 0U;
  if ((display_name == NULL) || (display_name[0] == '\0') ||
      (display_name_length > 160U) ||
      !strappy_utf8_validate(display_name, display_name_length)) {
    strappy_set_error(error_out, "Provider account name is invalid.");
    return 0;
  }
  definition = strappy_provider_find(provider_id);
  if (definition == NULL) {
    strappy_set_error(error_out, "Provider account type is not registered.");
    return 0;
  }
  if (allow_incomplete_endpoint && definition->requires_endpoint_override &&
      ((responses_endpoint == NULL) || (responses_endpoint[0] == '\0'))) {
    if (definition_out != NULL) {
      *definition_out = definition;
    }
    return 1;
  }
  resolved = strappy_provider_definition_responses_endpoint(
    definition, responses_endpoint, error_out);
  if (resolved == NULL) {
    return 0;
  }
  free(resolved);
  if (definition_out != NULL) {
    *definition_out = definition;
  }
  return 1;
}

static int strappy_db_read_account(sqlite3_stmt *stmt,
                                   strappy_provider_account_record *record,
                                   char **error_out)
{
  strappy_provider_account_record_init(record);
  record->account_id = strappy_db_column_string(stmt, 0);
  record->provider_id = strappy_db_column_string(stmt, 1);
  record->display_name = strappy_db_column_string(stmt, 2);
  record->lifecycle_state = strappy_db_column_string(stmt, 3);
  record->responses_endpoint = strappy_db_column_string(stmt, 4);
  record->created_at_ms = (long long)sqlite3_column_int64(stmt, 5);
  record->updated_at_ms = (long long)sqlite3_column_int64(stmt, 6);
  record->has_last_used_at_ms = sqlite3_column_type(stmt, 7) != SQLITE_NULL;
  record->last_used_at_ms = record->has_last_used_at_ms ?
    (long long)sqlite3_column_int64(stmt, 7) : 0LL;
  if ((record->account_id == NULL) || (record->provider_id == NULL) ||
      (record->display_name == NULL) || (record->lifecycle_state == NULL)) {
    strappy_provider_account_record_destroy(record);
    strappy_set_error(error_out, "Could not allocate provider account.");
    return 0;
  }
  return 1;
}

int strappy_db_create_provider_account(const char *db_path,
                                       const char *provider_id,
                                       const char *display_name,
                                       const char *responses_endpoint,
                                       char **account_id_out,
                                       char **error_out)
{
  static const char *sql =
    "INSERT INTO provider_accounts "
    "(id, provider_id, display_name, lifecycle_state, responses_endpoint, "
    "created_at_ms, updated_at_ms) VALUES "
    "('acct_' || lower(hex(randomblob(16))), ?, ?, 'active', ?, ?, ?);";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  long long now_ms;
  int rc;
  if (account_id_out == NULL) {
    strappy_set_error(error_out, "Provider account id output is missing.");
    return 0;
  }
  *account_id_out = NULL;
  if (!strappy_db_validate_account_metadata(provider_id, display_name,
                                             responses_endpoint, 1, NULL,
                                             error_out) ||
      !strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  stmt = NULL;
  now_ms = strappy_db_now_ms();
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if ((rc == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 1, provider_id, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 2, display_name, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
      ((responses_endpoint != NULL) ?
       (sqlite3_bind_text(stmt, 3, responses_endpoint, -1, SQLITE_TRANSIENT) == SQLITE_OK) :
       (sqlite3_bind_null(stmt, 3) == SQLITE_OK)) &&
      (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)now_ms) == SQLITE_OK) &&
      (sqlite3_bind_int64(stmt, 5, (sqlite3_int64)now_ms) == SQLITE_OK)) {
    rc = sqlite3_step(stmt);
  }
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out, "Could not create provider account: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  sqlite3_finalize(stmt);
  stmt = NULL;
  rc = sqlite3_prepare_v2(db,
    "SELECT id FROM provider_accounts WHERE rowid = last_insert_rowid();",
    -1, &stmt, NULL);
  if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) *account_id_out = strappy_db_column_string(stmt, 0);
  if (*account_id_out == NULL) {
    strappy_set_error(error_out, "Could not read created provider account id.");
    sqlite3_finalize(stmt); strappy_db_release(db); return 0;
  }
  sqlite3_finalize(stmt);
  if (!strappy_db_exec(db, STRAPPY_DB_INSERT_BUILTIN_DEFAULT_MODEL_SQL,
                       "Could not create account default model", error_out) ||
      !strappy_db_exec(db,
        "UPDATE app_preferences SET default_model_id=COALESCE(default_model_id,"
        STRAPPY_DB_DEFAULT_MODEL_SQL ", (SELECT id FROM models WHERE "
        "catalog_active=1 ORDER BY name,id LIMIT 1)), "
        "default_provider_account_id=COALESCE(default_provider_account_id,"
        "(SELECT a.id FROM provider_accounts a JOIN models m ON "
        "m.provider_id=a.provider_id WHERE m.id=COALESCE(default_model_id,"
        STRAPPY_DB_DEFAULT_MODEL_SQL ") AND a.lifecycle_state='active' "
        "ORDER BY a.last_used_at_ms DESC,a.created_at_ms,a.id LIMIT 1)) "
        "WHERE id=1;",
        "Could not initialize account defaults", error_out)) {
    strappy_db_release(db); free(*account_id_out); *account_id_out=NULL; return 0;
  }
  strappy_db_release(db);
  return 1;
}

int strappy_db_restore_provider_account(const char *db_path,
                                        const char *account_id,
                                        const char *provider_id,
                                        const char *display_name,
                                        const char *responses_endpoint,
                                        char **error_out)
{
  static const char *sql =
    "INSERT OR IGNORE INTO provider_accounts "
    "(id,provider_id,display_name,lifecycle_state,responses_endpoint,"
    "created_at_ms,updated_at_ms) VALUES (?, ?, ?, 'active', ?, ?, ?);";
  const strappy_provider_definition *definition;
  sqlite3 *db;
  sqlite3_stmt *stmt;
  long long now_ms;
  int rc;

  definition = strappy_provider_find(provider_id);
  if ((account_id == NULL) || (account_id[0] == '\0') ||
      (definition == NULL) ||
      !strappy_db_validate_account_metadata(provider_id, display_name,
                                             responses_endpoint, 1, NULL,
                                             error_out)) {
    if ((error_out != NULL) && (*error_out == NULL)) {
      strappy_set_error(error_out, "Restored provider account is invalid.");
    }
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) return 0;
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  now_ms = strappy_db_now_ms();
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt,1,account_id,-1,SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt,2,provider_id,-1,SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt,3,display_name,-1,SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) rc = responses_endpoint != NULL ?
    sqlite3_bind_text(stmt,4,responses_endpoint,-1,SQLITE_TRANSIENT) :
    sqlite3_bind_null(stmt,4);
  if (rc == SQLITE_OK) rc = sqlite3_bind_int64(stmt,5,(sqlite3_int64)now_ms);
  if (rc == SQLITE_OK) rc = sqlite3_bind_int64(stmt,6,(sqlite3_int64)now_ms);
  if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,"Could not restore provider account: %s",
                                sqlite3_errmsg(db));
    strappy_db_release(db);
    return 0;
  }
  if (!strappy_db_exec(db, STRAPPY_DB_INSERT_BUILTIN_DEFAULT_MODEL_SQL,
                       "Could not create restored account default model",
                       error_out) ||
      !strappy_db_exec(db,
        "UPDATE app_preferences SET default_model_id=COALESCE(default_model_id,"
        STRAPPY_DB_DEFAULT_MODEL_SQL ", (SELECT id FROM models WHERE "
        "catalog_active=1 ORDER BY name,id LIMIT 1)), "
        "default_provider_account_id=COALESCE(default_provider_account_id,"
        "(SELECT a.id FROM provider_accounts a JOIN models m ON "
        "m.provider_id=a.provider_id WHERE m.id=COALESCE(default_model_id,"
        STRAPPY_DB_DEFAULT_MODEL_SQL ") AND a.lifecycle_state='active' "
        "ORDER BY a.last_used_at_ms DESC,a.created_at_ms,a.id LIMIT 1)) "
        "WHERE id=1;",
        "Could not initialize restored account defaults", error_out)) {
    strappy_db_release(db);
    return 0;
  }
  strappy_db_release(db);
  return 1;
}

int strappy_db_list_provider_accounts(
  const char *db_path, const char *provider_id, int include_archived,
  strappy_provider_account_record_list *list, char **error_out)
{
  static const char *sql =
    "SELECT id, provider_id, display_name, lifecycle_state, "
    "responses_endpoint, created_at_ms, updated_at_ms, last_used_at_ms "
    "FROM provider_accounts WHERE (? IS NULL OR provider_id = ?) "
    "AND (? = 1 OR lifecycle_state = 'active') "
    "ORDER BY created_at_ms, id;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;
  if (list == NULL) {
    strappy_set_error(error_out, "Provider account list output is missing.");
    return 0;
  }
  strappy_provider_account_record_list_init(list);
  if (!strappy_db_open(db_path, &db, error_out)) return 0;
  if (!strappy_db_ensure_schema(db, error_out)) { strappy_db_release(db); return 0; }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    if (provider_id != NULL) {
      rc = sqlite3_bind_text(stmt, 1, provider_id, -1, SQLITE_TRANSIENT);
      if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 2, provider_id, -1, SQLITE_TRANSIENT);
    } else {
      rc = sqlite3_bind_null(stmt, 1);
      if (rc == SQLITE_OK) rc = sqlite3_bind_null(stmt, 2);
    }
  }
  if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 3, include_archived ? 1 : 0);
  if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
  while (rc == SQLITE_ROW) {
    strappy_provider_account_record *next = (strappy_provider_account_record *)
      realloc(list->records, (list->count + 1U) * sizeof(*list->records));
    if (next == NULL) { rc = SQLITE_NOMEM; break; }
    list->records = next;
    if (!strappy_db_read_account(stmt, &list->records[list->count], error_out)) {
      rc = SQLITE_NOMEM; break;
    }
    list->count++;
    rc = sqlite3_step(stmt);
  }
  if (rc != SQLITE_DONE) {
    if ((error_out == NULL) || (*error_out == NULL))
      strappy_set_formatted_error(error_out, "Could not list provider accounts: %s", sqlite3_errmsg(db));
    sqlite3_finalize(stmt); strappy_db_release(db);
    strappy_provider_account_record_list_destroy(list); return 0;
  }
  sqlite3_finalize(stmt); strappy_db_release(db); return 1;
}

int strappy_db_get_provider_account(const char *db_path, const char *account_id,
                                    strappy_provider_account_record *record,
                                    char **error_out)
{
  strappy_provider_account_record_list list;
  size_t index;
  if ((account_id == NULL) || (account_id[0] == '\0') || (record == NULL)) {
    strappy_set_error(error_out, "Provider account lookup is incomplete."); return 0;
  }
  if (!strappy_db_list_provider_accounts(db_path, NULL, 1, &list, error_out)) return 0;
  for (index = 0U; index < list.count; index++) {
    if (strcmp(list.records[index].account_id, account_id) == 0) {
      *record = list.records[index];
      strappy_provider_account_record_init(&list.records[index]);
      strappy_provider_account_record_list_destroy(&list); return 1;
    }
  }
  strappy_provider_account_record_list_destroy(&list);
  strappy_set_error(error_out, "Provider account was not found."); return 0;
}

int strappy_db_update_provider_account(const char *db_path, const char *account_id,
                                       const char *display_name,
                                       const char *responses_endpoint,
                                       char **error_out)
{
  sqlite3 *db; sqlite3_stmt *stmt; strappy_provider_account_record current; int rc; int changed;
  if (!strappy_db_get_provider_account(db_path, account_id, &current, error_out)) return 0;
  if (!strappy_db_validate_account_metadata(current.provider_id, display_name,
                                             responses_endpoint, 0, NULL,
                                             error_out)) {
    strappy_provider_account_record_destroy(&current); return 0;
  }
  strappy_provider_account_record_destroy(&current);
  if (!strappy_db_open(db_path, &db, error_out)) return 0;
  if (!strappy_db_ensure_schema(db, error_out)) { strappy_db_release(db); return 0; }
  stmt = NULL; rc = sqlite3_prepare_v2(db,
    "UPDATE provider_accounts SET display_name=?, responses_endpoint=?, updated_at_ms=? WHERE id=?;",
    -1, &stmt, NULL);
  if ((rc == SQLITE_OK) && (sqlite3_bind_text(stmt,1,display_name,-1,SQLITE_TRANSIENT)==SQLITE_OK) &&
      ((responses_endpoint != NULL) ? (sqlite3_bind_text(stmt,2,responses_endpoint,-1,SQLITE_TRANSIENT)==SQLITE_OK) : (sqlite3_bind_null(stmt,2)==SQLITE_OK)) &&
      (sqlite3_bind_int64(stmt,3,(sqlite3_int64)strappy_db_now_ms())==SQLITE_OK) &&
      (sqlite3_bind_text(stmt,4,account_id,-1,SQLITE_TRANSIENT)==SQLITE_OK)) rc=sqlite3_step(stmt);
  changed=(rc==SQLITE_DONE)?sqlite3_changes(db):0;
  if (changed != 1) strappy_set_error(error_out,"Provider account could not be updated.");
  sqlite3_finalize(stmt); strappy_db_release(db); return changed==1;
}

int strappy_db_archive_provider_account(const char *db_path, const char *account_id,
                                        char **error_out)
{
  sqlite3 *db; sqlite3_stmt *stmt; int rc; int changed;
  if (!strappy_db_open(db_path,&db,error_out)) return 0;
  if (!strappy_db_ensure_schema(db,error_out)) { strappy_db_release(db); return 0; }
  stmt=NULL; rc=sqlite3_prepare_v2(db,"UPDATE provider_accounts SET lifecycle_state='archived', updated_at_ms=? WHERE id=? AND lifecycle_state='active';",-1,&stmt,NULL);
  if ((rc==SQLITE_OK) && (sqlite3_bind_int64(stmt,1,(sqlite3_int64)strappy_db_now_ms())==SQLITE_OK) && (sqlite3_bind_text(stmt,2,account_id,-1,SQLITE_TRANSIENT)==SQLITE_OK)) rc=sqlite3_step(stmt);
  changed=(rc==SQLITE_DONE)?sqlite3_changes(db):0;
  sqlite3_finalize(stmt);
  if (changed==1) {
    stmt=NULL; rc=sqlite3_prepare_v2(db,
      "UPDATE app_preferences SET default_model_id=NULL,default_provider_account_id=NULL,updated_at_ms=? WHERE id=1 AND default_provider_account_id=?;",-1,&stmt,NULL);
    if ((rc==SQLITE_OK)&&(sqlite3_bind_int64(stmt,1,(sqlite3_int64)strappy_db_now_ms())==SQLITE_OK)&&
        (sqlite3_bind_text(stmt,2,account_id,-1,SQLITE_TRANSIENT)==SQLITE_OK)) rc=sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc!=SQLITE_DONE) changed=0;
  }
  if (changed!=1) strappy_set_error(error_out,"Active provider account was not found.");
  strappy_db_release(db); return changed==1;
}

int strappy_db_mark_provider_account_used(const char *db_path,
                                          const char *account_id,
                                          long long used_at_ms,
                                          char **error_out)
{
  sqlite3 *db; sqlite3_stmt *stmt; int rc; int changed;
  if (used_at_ms <= 0LL) used_at_ms=strappy_db_now_ms();
  if (!strappy_db_open(db_path,&db,error_out)) return 0;
  if (!strappy_db_ensure_schema(db,error_out)) { strappy_db_release(db); return 0; }
  stmt=NULL; rc=sqlite3_prepare_v2(db,
    "UPDATE provider_accounts SET last_used_at_ms=?,updated_at_ms=MAX(updated_at_ms,?) WHERE id=? AND lifecycle_state='active';",-1,&stmt,NULL);
  if ((rc==SQLITE_OK)&&(sqlite3_bind_int64(stmt,1,(sqlite3_int64)used_at_ms)==SQLITE_OK)&&
      (sqlite3_bind_int64(stmt,2,(sqlite3_int64)used_at_ms)==SQLITE_OK)&&
      (sqlite3_bind_text(stmt,3,account_id,-1,SQLITE_TRANSIENT)==SQLITE_OK)) rc=sqlite3_step(stmt);
  changed=(rc==SQLITE_DONE)?sqlite3_changes(db):0; sqlite3_finalize(stmt); strappy_db_release(db);
  if (changed!=1) { strappy_set_error(error_out,"Active provider account was not found."); return 0; }
  return 1;
}

int strappy_db_get_designated_provider_account(const char *db_path,
                                               const char *provider_id,
                                               char **account_id_out,
                                               char **error_out)
{
  strappy_provider_account_record_list list;
  if (account_id_out == NULL) { strappy_set_error(error_out,"Provider account output is missing."); return 0; }
  *account_id_out=NULL;
  if (!strappy_db_list_provider_accounts(db_path,provider_id,0,&list,error_out)) return 0;
  if (list.count == 0U) { strappy_provider_account_record_list_destroy(&list); strappy_set_error(error_out,"No active provider account is configured."); return 0; }
  *account_id_out=strappy_string_duplicate(list.records[0].account_id);
  strappy_provider_account_record_list_destroy(&list);
  if (*account_id_out == NULL) { strappy_set_error(error_out,"Could not allocate provider account id."); return 0; }
  return 1;
}

static char *strappy_db_like_pattern_for_search(const char *search_text,
                                                char **error_out)
{
  char *pattern;
  size_t index;
  size_t input_length;
  size_t output_index;
  size_t pattern_length;

  if ((search_text == NULL) || (search_text[0] == '\0')) {
    return NULL;
  }

  input_length = strlen(search_text);
  pattern_length = 2U;
  for (index = 0U; index < input_length; index++) {
    size_t character_length;

    character_length = ((search_text[index] == '%') ||
                        (search_text[index] == '_') ||
                        (search_text[index] == '\\')) ? 2U : 1U;
    if (pattern_length > (((size_t)-1) - character_length - 1U)) {
      strappy_set_error(error_out, "Model search text is too large.");
      return NULL;
    }
    pattern_length += character_length;
  }

  pattern = (char *)malloc(pattern_length + 1U);
  if (pattern == NULL) {
    strappy_set_error(error_out, "Could not allocate model search.");
    return NULL;
  }

  output_index = 0U;
  pattern[output_index++] = '%';
  for (index = 0U; index < input_length; index++) {
    if ((search_text[index] == '%') ||
        (search_text[index] == '_') ||
        (search_text[index] == '\\')) {
      pattern[output_index++] = '\\';
    }
    pattern[output_index++] = search_text[index];
  }
  pattern[output_index++] = '%';
  pattern[output_index] = '\0';
  return pattern;
}

static char *strappy_db_create_assistant_database_id(long long catalog_id)
{
  char buffer[64];
  int result;

  if (catalog_id <= 0) {
    return NULL;
  }

  result = snprintf(buffer, sizeof(buffer), "db_%lld", catalog_id);
  if ((result <= 0) || ((size_t)result >= sizeof(buffer))) {
    return NULL;
  }

  return strappy_string_duplicate(buffer);
}

static const char *strappy_db_origin_kind_for_input(
  const strappy_discovered_database_input *record)
{
  if ((record != NULL) &&
      (record->origin_kind != NULL) &&
      (record->origin_kind[0] != '\0')) {
    return record->origin_kind;
  }

  return "other";
}

static int strappy_db_is_valid_user_decision(const char *user_decision)
{
  if (user_decision == NULL) {
    return 0;
  }

  return ((strcmp(user_decision, "unknown") == 0) ||
          (strcmp(user_decision, "allowed") == 0) ||
          (strcmp(user_decision, "denied") == 0));
}

static int strappy_db_bind_optional_text(sqlite3 *db,
                                         sqlite3_stmt *stmt,
                                         int index,
                                         const char *value,
                                         const char *error_prefix,
                                         char **error_out)
{
  int rc;

  if ((value != NULL) && (value[0] != '\0')) {
    rc = sqlite3_bind_text(stmt, index, value, -1, SQLITE_TRANSIENT);
  } else {
    rc = sqlite3_bind_null(stmt, index);
  }

  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "%s: %s",
                                error_prefix,
                                sqlite3_errmsg(db));
    return 0;
  }

  return 1;
}

static int strappy_db_assign_discovered_database_from_statement(
  strappy_discovered_database_record *record,
  sqlite3_stmt *stmt,
  char **error_out)
{
  char *assistant_database_id;
  char *path;
  char *validation_error;
  char *scan_status;
  char *user_decision;
  char *scan_root;
  char *app_group_key;
  char *app_name;
  char *app_bundle_id;
  char *app_container_path;
  char *app_bundle_path;
  char *app_source;
  char *origin_kind;
  char *location_tail;
  char *hidden_reason;
  char *first_seen_at;
  char *last_seen_at;
  char *last_scanned_at;
  long long catalog_id;
  int hidden;
  int auto_hidden;
  int has_hidden_override;
  int hidden_override;

  if ((record == NULL) || (stmt == NULL)) {
    strappy_set_error(error_out, "Discovered database row request is incomplete.");
    return 0;
  }

  strappy_discovered_database_record_destroy(record);
  catalog_id = (long long)sqlite3_column_int64(stmt, 0);
  record->catalog_id = catalog_id;
  record->size = (long long)sqlite3_column_int64(stmt, 3);
  record->modified_at = (long long)sqlite3_column_int64(stmt, 4);
  record->device = (unsigned long long)sqlite3_column_int64(stmt, 5);
  record->inode = (unsigned long long)sqlite3_column_int64(stmt, 6);
  record->is_valid_sqlite = sqlite3_column_int(stmt, 7) ? 1 : 0;

  assistant_database_id = strappy_db_column_string(stmt, 1);
  if (assistant_database_id == NULL) {
    assistant_database_id = strappy_db_create_assistant_database_id(catalog_id);
  }
  path = strappy_db_column_string(stmt, 2);
  validation_error = strappy_db_column_string(stmt, 8);
  scan_status = strappy_db_column_string(stmt, 9);
  user_decision = strappy_db_column_string(stmt, 10);
  hidden = sqlite3_column_int(stmt, 11) ? 1 : 0;
  auto_hidden = sqlite3_column_int(stmt, 12) ? 1 : 0;
  has_hidden_override = (sqlite3_column_type(stmt, 13) != SQLITE_NULL) ? 1 : 0;
  hidden_override = has_hidden_override && sqlite3_column_int(stmt, 13) ? 1 : 0;
  hidden_reason = strappy_db_column_string(stmt, 14);
  scan_root = strappy_db_column_string(stmt, 15);
  app_group_key = strappy_db_column_string(stmt, 16);
  app_name = strappy_db_column_string(stmt, 17);
  app_bundle_id = strappy_db_column_string(stmt, 18);
  app_container_path = strappy_db_column_string(stmt, 19);
  app_bundle_path = strappy_db_column_string(stmt, 20);
  app_source = strappy_db_column_string(stmt, 21);
  origin_kind = strappy_db_column_string(stmt, 22);
  location_tail = strappy_db_column_string(stmt, 23);
  first_seen_at = strappy_db_column_string(stmt, 24);
  last_seen_at = strappy_db_column_string(stmt, 25);
  last_scanned_at = strappy_db_column_string(stmt, 26);

  if ((assistant_database_id == NULL) || (path == NULL) ||
      (scan_status == NULL) || (user_decision == NULL) ||
      (origin_kind == NULL) || (first_seen_at == NULL) || (last_seen_at == NULL) ||
      (last_scanned_at == NULL)) {
    free(assistant_database_id);
    free(path);
    free(validation_error);
    free(scan_status);
    free(user_decision);
    free(scan_root);
    free(app_group_key);
    free(app_name);
    free(app_bundle_id);
    free(app_container_path);
    free(app_bundle_path);
    free(app_source);
    free(origin_kind);
    free(location_tail);
    free(hidden_reason);
    free(first_seen_at);
    free(last_seen_at);
    free(last_scanned_at);
    strappy_set_error(error_out, "Could not allocate discovered database row.");
    return 0;
  }

  record->assistant_database_id = assistant_database_id;
  record->path = path;
  record->validation_error = validation_error;
  record->scan_status = scan_status;
  record->user_decision = user_decision;
  record->scan_root = scan_root;
  record->app_group_key = app_group_key;
  record->app_name = app_name;
  record->app_bundle_id = app_bundle_id;
  record->app_container_path = app_container_path;
  record->app_bundle_path = app_bundle_path;
  record->app_source = app_source;
  record->origin_kind = origin_kind;
  record->location_tail = location_tail;
  record->hidden = hidden;
  record->auto_hidden = auto_hidden;
  record->has_hidden_override = has_hidden_override;
  record->hidden_override = hidden_override;
  record->hidden_reason = hidden_reason;
  record->first_seen_at = first_seen_at;
  record->last_seen_at = last_seen_at;
  record->last_scanned_at = last_scanned_at;
  return 1;
}

static char *strappy_db_json_copy_value_text(cJSON *value)
{
  if ((value == NULL) || cJSON_IsNull(value)) {
    return NULL;
  }

  if (cJSON_IsString(value) && (value->valuestring != NULL)) {
    return strappy_string_duplicate(value->valuestring);
  }

  return cJSON_PrintUnformatted(value);
}

static char *strappy_db_json_copy_object_text(cJSON *object, const char *key)
{
  if ((object == NULL) || (key == NULL)) {
    return NULL;
  }

  return strappy_db_json_copy_value_text(cJSON_GetObjectItem(object, key));
}

static cJSON *strappy_db_json_object_child(cJSON *object, const char *key)
{
  cJSON *child;

  if ((object == NULL) || (key == NULL)) {
    return NULL;
  }

  child = cJSON_GetObjectItem(object, key);
  return cJSON_IsObject(child) ? child : NULL;
}

static char *strappy_db_json_copy_nested_text(cJSON *object,
                                              const char *parent_key,
                                              const char *child_key)
{
  return strappy_db_json_copy_object_text(
    strappy_db_json_object_child(object, parent_key),
    child_key);
}

static long long strappy_db_json_integer(cJSON *value)
{
  if (cJSON_IsNumber(value)) {
    return (long long)value->valuedouble;
  }

  if (cJSON_IsString(value) && (value->valuestring != NULL)) {
    return (long long)strtoll(value->valuestring, NULL, 10);
  }

  return 0;
}

static long long strappy_db_json_object_integer(cJSON *object, const char *key)
{
  if ((object == NULL) || (key == NULL)) {
    return 0;
  }

  return strappy_db_json_integer(cJSON_GetObjectItem(object, key));
}

static long long strappy_db_json_nested_integer(cJSON *object,
                                                const char *parent_key,
                                                const char *child_key)
{
  return strappy_db_json_object_integer(
    strappy_db_json_object_child(object, parent_key),
    child_key);
}

static int strappy_db_json_bool(cJSON *value)
{
  if (cJSON_IsTrue(value)) {
    return 1;
  }
  if (cJSON_IsFalse(value) || (value == NULL) || cJSON_IsNull(value)) {
    return 0;
  }
  if (cJSON_IsNumber(value) && (value->valuedouble != 0.0)) {
    return 1;
  }
  if (cJSON_IsString(value) && (value->valuestring != NULL)) {
    return ((strcmp(value->valuestring, "true") == 0) ||
            (strcmp(value->valuestring, "1") == 0)) ? 1 : 0;
  }
  return 0;
}

static int strappy_db_json_nested_bool(cJSON *object,
                                       const char *parent_key,
                                       const char *child_key)
{
  cJSON *parent;

  parent = strappy_db_json_object_child(object, parent_key);
  return strappy_db_json_bool(cJSON_GetObjectItem(parent, child_key));
}

static int strappy_db_bind_text_or_null(sqlite3 *db,
                                        sqlite3_stmt *stmt,
                                        int index,
                                        const char *value,
                                        const char *error_prefix,
                                        char **error_out)
{
  return strappy_db_bind_optional_text(db,
                                       stmt,
                                       index,
                                       value,
                                       error_prefix,
                                       error_out);
}

static int strappy_db_assign_model_from_statement(
  strappy_openrouter_model_record *record,
  sqlite3_stmt *stmt,
  char **error_out)
{
  if ((record == NULL) || (stmt == NULL)) {
    strappy_set_error(error_out, "Model row request is incomplete.");
    return 0;
  }

  strappy_openrouter_model_record_destroy(record);
  record->context_length = (long long)sqlite3_column_int64(stmt, 5);
  record->created = (long long)sqlite3_column_int64(stmt, 6);
  record->top_provider_context_length = (long long)sqlite3_column_int64(stmt, 19);
  record->top_provider_max_completion_tokens =
    (long long)sqlite3_column_int64(stmt, 20);
  record->top_provider_is_moderated = sqlite3_column_int(stmt, 21) ? 1 : 0;
  record->selected = sqlite3_column_int(stmt, 32) ? 1 : 0;
  record->allowed = sqlite3_column_int(stmt, 33) ? 1 : 0;

  record->model_id = strappy_db_column_string(stmt, 0);
  record->canonical_slug = strappy_db_column_string(stmt, 1);
  record->hugging_face_id = strappy_db_column_string(stmt, 2);
  record->name = strappy_db_column_string(stmt, 3);
  record->description = strappy_db_column_string(stmt, 4);
  record->architecture_modality = strappy_db_column_string(stmt, 7);
  record->architecture_tokenizer = strappy_db_column_string(stmt, 8);
  record->architecture_instruct_type = strappy_db_column_string(stmt, 9);
  record->pricing_prompt = strappy_db_column_string(stmt, 10);
  record->pricing_completion = strappy_db_column_string(stmt, 11);
  record->pricing_request = strappy_db_column_string(stmt, 12);
  record->pricing_image = strappy_db_column_string(stmt, 13);
  record->pricing_audio = strappy_db_column_string(stmt, 14);
  record->pricing_web_search = strappy_db_column_string(stmt, 15);
  record->pricing_internal_reasoning = strappy_db_column_string(stmt, 16);
  record->pricing_input_cache_read = strappy_db_column_string(stmt, 17);
  record->pricing_input_cache_write = strappy_db_column_string(stmt, 18);
  record->knowledge_cutoff = strappy_db_column_string(stmt, 22);
  record->expiration_date = strappy_db_column_string(stmt, 23);
  record->links_details = strappy_db_column_string(stmt, 24);
  record->links_json = strappy_db_column_string(stmt, 25);
  record->reasoning_json = strappy_db_column_string(stmt, 26);
  record->benchmarks_json = strappy_db_column_string(stmt, 27);
  record->default_parameters_json = strappy_db_column_string(stmt, 28);
  record->per_request_limits_json = strappy_db_column_string(stmt, 29);
  record->raw_json = strappy_db_column_string(stmt, 30);
  record->fetched_at = strappy_db_column_string(stmt, 31);
  record->provider_account_id = strappy_db_column_string(stmt, 34);
  record->provider_id = strappy_db_column_string(stmt, 35);
  record->provider_account_name = strappy_db_column_string(stmt, 36);
  record->wire_model_id = strappy_db_column_string(stmt, 37);
  record->billing_kind = strappy_db_column_string(stmt, 38);
  record->reasoning_enabled = sqlite3_column_int(stmt, 39) ? 1 : 0;
  record->local_functions_enabled = sqlite3_column_int(stmt, 40) ? 1 : 0;
  record->hosted_tools_enabled = sqlite3_column_int(stmt, 41) ? 1 : 0;
  record->image_input_enabled = sqlite3_column_int(stmt, 42) ? 1 : 0;

  if ((record->model_id == NULL) || (record->fetched_at == NULL) ||
      (record->provider_account_id == NULL) ||
      (record->provider_id == NULL) ||
      (record->provider_account_name == NULL) ||
      (record->wire_model_id == NULL) ||
      (record->billing_kind == NULL)) {
    strappy_openrouter_model_record_destroy(record);
    strappy_set_error(error_out, "Could not allocate model row.");
    return 0;
  }

  return 1;
}

static char *strappy_db_catalog_stable_key(
  const strappy_discovered_database_input *record)
{
  const char *prefix;
  const char *first;
  const char *second;
  size_t length;
  char *key;

  if ((record == NULL) || (record->path == NULL)) {
    return NULL;
  }
  if ((record->app_group_key != NULL) &&
      (record->app_group_key[0] != '\0') &&
      (record->location_tail != NULL) &&
      (record->location_tail[0] != '\0')) {
    prefix = "app:";
    first = record->app_group_key;
    second = record->location_tail;
  } else {
    prefix = "path:";
    first = record->path;
    second = "";
  }
  length = strlen(prefix) + strlen(first) + strlen(second) + 2U;
  key = (char *)malloc(length);
  if (key == NULL) {
    return NULL;
  }
  if (second[0] != '\0') {
    snprintf(key, length, "%s%s:%s", prefix, first, second);
  } else {
    snprintf(key, length, "%s%s", prefix, first);
  }
  return key;
}

static int strappy_db_catalog_application_id(
  sqlite3 *db,
  const strappy_discovered_database_input *record,
  long long now_ms,
  long long *application_id_out,
  char **error_out)
{
  static const char *insert_sql =
    "INSERT OR IGNORE INTO applications "
    "(stable_key, name, bundle_id, container_path, bundle_path, source) "
    "VALUES (?, ?, ?, ?, ?, ?);";
  static const char *update_sql =
    "UPDATE applications SET "
    "name = COALESCE(?, name), bundle_id = COALESCE(?, bundle_id), "
    "container_path = COALESCE(?, container_path), "
    "bundle_path = COALESCE(?, bundle_path), source = COALESCE(?, source) "
    "WHERE stable_key = ?;";
  static const char *select_sql =
    "SELECT id FROM applications WHERE stable_key = ?;";
  sqlite3_stmt *stmt;
  const char *stable_key;
  int rc;
  int ok;

  (void)now_ms;
  if (application_id_out == NULL) {
    strappy_set_error(error_out, "Application catalog output is missing.");
    return 0;
  }
  *application_id_out = 0LL;
  stable_key = ((record != NULL) && (record->app_group_key != NULL) &&
                (record->app_group_key[0] != '\0')) ?
    record->app_group_key : NULL;
  if (stable_key == NULL) {
    return 1;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare application insert: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  ok = (sqlite3_bind_text(stmt, 1, stable_key, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
       strappy_db_bind_optional_text(db, stmt, 2, record->app_name,
                                     "Could not bind application insert",
                                     error_out) &&
       strappy_db_bind_optional_text(db, stmt, 3, record->app_bundle_id,
                                     "Could not bind application insert",
                                     error_out) &&
       strappy_db_bind_optional_text(db, stmt, 4, record->app_container_path,
                                     "Could not bind application insert",
                                     error_out) &&
       strappy_db_bind_optional_text(db, stmt, 5, record->app_bundle_path,
                                     "Could not bind application insert",
                                     error_out) &&
       strappy_db_bind_optional_text(db, stmt, 6, record->app_source,
                                     "Could not bind application insert",
                                     error_out);
  if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
    if (ok) {
      strappy_set_formatted_error(error_out,
                                  "Could not save application: %s",
                                  sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare application update: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  ok = strappy_db_bind_optional_text(db, stmt, 1, record->app_name,
                                     "Could not bind application update",
                                     error_out) &&
       strappy_db_bind_optional_text(db, stmt, 2, record->app_bundle_id,
                                     "Could not bind application update",
                                     error_out) &&
       strappy_db_bind_optional_text(db, stmt, 3, record->app_container_path,
                                     "Could not bind application update",
                                     error_out) &&
       strappy_db_bind_optional_text(db, stmt, 4, record->app_bundle_path,
                                     "Could not bind application update",
                                     error_out) &&
       strappy_db_bind_optional_text(db, stmt, 5, record->app_source,
                                     "Could not bind application update",
                                     error_out) &&
       (sqlite3_bind_text(stmt, 6, stable_key, -1, SQLITE_TRANSIENT) == SQLITE_OK);
  if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
    if (ok) {
      strappy_set_formatted_error(error_out,
                                  "Could not update application: %s",
                                  sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, select_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, stable_key, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare application lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    strappy_set_formatted_error(error_out,
                                "Could not read application: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  *application_id_out = (long long)sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_catalog_scan_root_id(sqlite3 *db,
                                           const char *path,
                                           long long now_ms,
                                           long long *root_id_out,
                                           char **error_out)
{
  static const char *insert_sql =
    "INSERT OR IGNORE INTO scan_roots (path) VALUES (?);";
  static const char *select_sql =
    "SELECT id FROM scan_roots WHERE path = ?;";
  sqlite3_stmt *stmt;
  int rc;

  (void)now_ms;
  if (root_id_out == NULL) {
    strappy_set_error(error_out, "Scan-root output is missing.");
    return 0;
  }
  *root_id_out = 0LL;
  if ((path == NULL) || (path[0] == '\0')) {
    return 1;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not save scan root: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, select_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_ROW)) {
    strappy_set_formatted_error(error_out,
                                "Could not read scan root: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  *root_id_out = (long long)sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_catalog_save_one(sqlite3 *db,
                                       const strappy_discovered_database_input *record,
                                       long long scan_run_id,
                                       long long now_ms,
                                       char **error_out)
{
  static const char *find_path_sql =
    "SELECT database_id FROM database_locations WHERE path = ?;";
  static const char *find_stable_sql =
    "SELECT id FROM databases WHERE stable_key = ?;";
  static const char *insert_database_sql =
    "INSERT INTO databases "
    "(stable_key, application_id, display_name, origin_kind, location_tail, "
     "first_seen_at_ms, last_seen_at_ms) VALUES (?, ?, ?, ?, ?, ?, ?);";
  static const char *update_database_sql =
    "UPDATE databases SET application_id = ?, display_name = ?, "
    "origin_kind = ?, location_tail = ?, last_seen_at_ms = ? "
    "WHERE id = ?;";
  static const char *assistant_id_sql =
    "UPDATE databases SET assistant_database_id = ? "
    "WHERE id = ? AND assistant_database_id IS NULL;";
  static const char *permission_sql =
    "INSERT OR IGNORE INTO database_permissions "
    "(database_id, decision, hidden, auto_hidden, hidden_override, "
     "hidden_reason, updated_at_ms) "
    "VALUES (?, 'unknown', ?, ?, NULL, ?, ?);";
  static const char *update_permission_sql =
    "UPDATE database_permissions SET auto_hidden = ?, hidden_reason = ?, "
    "hidden = CASE WHEN hidden_override IS NULL THEN ? "
                   "ELSE hidden_override END "
    "WHERE database_id = ?;";
  static const char *insert_location_sql =
    "INSERT OR IGNORE INTO database_locations "
    "(database_id, path, scan_root_id, last_scan_run_id, device, inode, "
     "size_bytes, modified_at_s, validation_state, validation_error, active, "
     "first_seen_at_ms, last_seen_at_ms, last_scanned_at_ms) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1, ?, ?, ?);";
  static const char *update_location_sql =
    "UPDATE database_locations SET database_id = ?, scan_root_id = ?, "
    "last_scan_run_id = COALESCE(?, last_scan_run_id), device = ?, inode = ?, "
    "size_bytes = ?, "
    "modified_at_s = ?, validation_state = ?, validation_error = ?, active = 1, "
    "last_seen_at_ms = ?, last_scanned_at_ms = ? WHERE path = ?;";
  static const char *count_scan_record_sql =
    "UPDATE scan_runs SET candidate_count = candidate_count + 1, "
    "database_count = database_count + ? "
    "WHERE id = ? AND scan_root_id = ? AND state = 'running';";
  sqlite3_stmt *stmt;
  char *stable_key;
  char *assistant_id;
  const char *validation_state;
  long long application_id;
  long long scan_root_id;
  long long database_id;
  int rc;
  int ok;

  if ((record == NULL) || (record->path == NULL) || (record->path[0] == '\0')) {
    strappy_set_error(error_out, "Discovered database path is empty.");
    return 0;
  }
  stable_key = strappy_db_catalog_stable_key(record);
  if (stable_key == NULL) {
    strappy_set_error(error_out, "Could not allocate database stable key.");
    return 0;
  }
  if (!strappy_db_catalog_application_id(db,
                                         record,
                                         now_ms,
                                         &application_id,
                                         error_out) ||
      !strappy_db_catalog_scan_root_id(db,
                                      record->scan_root,
                                      now_ms,
                                      &scan_root_id,
                                      error_out)) {
    free(stable_key);
    return 0;
  }

  database_id = 0LL;
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, find_path_sql, -1, &stmt, NULL);
  if ((rc == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 1, record->path, -1, SQLITE_TRANSIENT) == SQLITE_OK)) {
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
      database_id = (long long)sqlite3_column_int64(stmt, 0);
    }
  }
  if ((rc != SQLITE_ROW) && (rc != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not find database location: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    free(stable_key);
    return 0;
  }
  sqlite3_finalize(stmt);
  if (database_id == 0LL) {
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, find_stable_sql, -1, &stmt, NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_text(stmt, 1, stable_key, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
      strappy_set_formatted_error(error_out,
                                  "Could not prepare database lookup: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      free(stable_key);
      return 0;
    }
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
      database_id = (long long)sqlite3_column_int64(stmt, 0);
    } else if (rc != SQLITE_DONE) {
      strappy_set_formatted_error(error_out,
                                  "Could not read database lookup: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      free(stable_key);
      return 0;
    }
    sqlite3_finalize(stmt);
  }

  if (database_id == 0LL) {
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, insert_database_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
      strappy_set_formatted_error(error_out,
                                  "Could not prepare database insert: %s",
                                  sqlite3_errmsg(db));
      free(stable_key);
      return 0;
    }
    ok = (sqlite3_bind_text(stmt, 1, stable_key, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
         ((application_id > 0LL) ?
            (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)application_id) == SQLITE_OK) :
            (sqlite3_bind_null(stmt, 2) == SQLITE_OK)) &&
         strappy_db_bind_optional_text(db, stmt, 3, record->app_name,
                                       "Could not bind database insert", error_out) &&
         (sqlite3_bind_text(stmt, 4,
                            strappy_db_origin_kind_for_input(record),
                            -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
         strappy_db_bind_optional_text(db, stmt, 5, record->location_tail,
                                       "Could not bind database insert", error_out) &&
         (sqlite3_bind_int64(stmt, 6, (sqlite3_int64)now_ms) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 7, (sqlite3_int64)now_ms) == SQLITE_OK);
    if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
      if (ok) {
        strappy_set_formatted_error(error_out,
                                    "Could not save database: %s",
                                    sqlite3_errmsg(db));
      }
      sqlite3_finalize(stmt);
      free(stable_key);
      return 0;
    }
    database_id = (long long)sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);
  } else {
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, update_database_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
      strappy_set_formatted_error(error_out,
                                  "Could not prepare database update: %s",
                                  sqlite3_errmsg(db));
      free(stable_key);
      return 0;
    }
    ok = ((application_id > 0LL) ?
            (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)application_id) == SQLITE_OK) :
            (sqlite3_bind_null(stmt, 1) == SQLITE_OK)) &&
         strappy_db_bind_optional_text(db, stmt, 2, record->app_name,
                                       "Could not bind database update", error_out) &&
         (sqlite3_bind_text(stmt, 3,
                            strappy_db_origin_kind_for_input(record),
                            -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
         strappy_db_bind_optional_text(db, stmt, 4, record->location_tail,
                                       "Could not bind database update", error_out) &&
         (sqlite3_bind_int64(stmt, 5, (sqlite3_int64)now_ms) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 6, (sqlite3_int64)database_id) == SQLITE_OK);
    if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
      if (ok) {
        strappy_set_formatted_error(error_out,
                                    "Could not update database: %s",
                                    sqlite3_errmsg(db));
      }
      sqlite3_finalize(stmt);
      free(stable_key);
      return 0;
    }
    sqlite3_finalize(stmt);
  }

  assistant_id = strappy_db_create_assistant_database_id(database_id);
  if (assistant_id == NULL) {
    free(stable_key);
    strappy_set_error(error_out, "Could not allocate assistant database id.");
    return 0;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, assistant_id_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, assistant_id, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)database_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not save assistant database id: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    free(assistant_id);
    free(stable_key);
    return 0;
  }
  sqlite3_finalize(stmt);
  free(assistant_id);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, permission_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)database_id) != SQLITE_OK) ||
      (sqlite3_bind_int(stmt, 2, record->hidden ? 1 : 0) != SQLITE_OK) ||
      (sqlite3_bind_int(stmt, 3, record->hidden ? 1 : 0) != SQLITE_OK) ||
      !strappy_db_bind_optional_text(db,
                                     stmt,
                                     4,
                                     record->hidden_reason,
                                     "Could not bind hidden reason",
                                     error_out) ||
      (sqlite3_bind_int64(stmt, 5, (sqlite3_int64)now_ms) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not save database permission: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    free(stable_key);
    return 0;
  }
  sqlite3_finalize(stmt);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, update_permission_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int(stmt, 1, record->hidden ? 1 : 0) != SQLITE_OK) ||
      !strappy_db_bind_optional_text(db,
                                     stmt,
                                     2,
                                     record->hidden_reason,
                                     "Could not bind hidden reason update",
                                     error_out) ||
      (sqlite3_bind_int(stmt, 3, record->hidden ? 1 : 0) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)database_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not update database hidden classification: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    free(stable_key);
    return 0;
  }
  sqlite3_finalize(stmt);

  validation_state = record->is_valid_sqlite ? "valid" :
    (((record->validation_error != NULL) &&
      (record->validation_error[0] != '\0')) ? "invalid" : "candidate");
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, insert_location_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare database location insert: %s",
                                sqlite3_errmsg(db));
    free(stable_key);
    return 0;
  }
  ok = (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)database_id) == SQLITE_OK) &&
       (sqlite3_bind_text(stmt, 2, record->path, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
       ((scan_root_id > 0LL) ?
          (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)scan_root_id) == SQLITE_OK) :
          (sqlite3_bind_null(stmt, 3) == SQLITE_OK)) &&
       ((scan_run_id > 0LL) ?
          (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)scan_run_id) == SQLITE_OK) :
          (sqlite3_bind_null(stmt, 4) == SQLITE_OK)) &&
       (sqlite3_bind_int64(stmt, 5, (sqlite3_int64)record->device) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 6, (sqlite3_int64)record->inode) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 7, (sqlite3_int64)record->size) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 8, (sqlite3_int64)record->modified_at) == SQLITE_OK) &&
       (sqlite3_bind_text(stmt, 9, validation_state, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
       strappy_db_bind_optional_text(db, stmt, 10, record->validation_error,
                                     "Could not bind database location insert",
                                     error_out) &&
       (sqlite3_bind_int64(stmt, 11, (sqlite3_int64)now_ms) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 12, (sqlite3_int64)now_ms) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 13, (sqlite3_int64)now_ms) == SQLITE_OK);
  if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
    if (ok) {
      strappy_set_formatted_error(error_out,
                                  "Could not save database location: %s",
                                  sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    free(stable_key);
    return 0;
  }
  sqlite3_finalize(stmt);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, update_location_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare database location update: %s",
                                sqlite3_errmsg(db));
    free(stable_key);
    return 0;
  }
  ok = (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)database_id) == SQLITE_OK) &&
       ((scan_root_id > 0LL) ?
          (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)scan_root_id) == SQLITE_OK) :
          (sqlite3_bind_null(stmt, 2) == SQLITE_OK)) &&
       ((scan_run_id > 0LL) ?
          (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)scan_run_id) == SQLITE_OK) :
          (sqlite3_bind_null(stmt, 3) == SQLITE_OK)) &&
       (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)record->device) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 5, (sqlite3_int64)record->inode) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 6, (sqlite3_int64)record->size) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 7, (sqlite3_int64)record->modified_at) == SQLITE_OK) &&
       (sqlite3_bind_text(stmt, 8, validation_state, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
       strappy_db_bind_optional_text(db, stmt, 9, record->validation_error,
                                     "Could not bind database location update",
                                     error_out) &&
       (sqlite3_bind_int64(stmt, 10, (sqlite3_int64)now_ms) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 11, (sqlite3_int64)now_ms) == SQLITE_OK) &&
       (sqlite3_bind_text(stmt, 12, record->path, -1, SQLITE_TRANSIENT) == SQLITE_OK);
  if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
    if (ok) {
      strappy_set_formatted_error(error_out,
                                  "Could not update database location: %s",
                                  sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    free(stable_key);
    return 0;
  }
  sqlite3_finalize(stmt);
  if (scan_run_id > 0LL) {
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, count_scan_record_sql, -1, &stmt, NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_int(stmt, 1, record->is_valid_sqlite ? 1 : 0) !=
         SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)scan_run_id) !=
         SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)scan_root_id) !=
         SQLITE_OK) ||
        (sqlite3_step(stmt) != SQLITE_DONE) ||
        (sqlite3_changes(db) != 1)) {
      strappy_set_formatted_error(error_out,
                                  "Could not count scan record: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      free(stable_key);
      return 0;
    }
    sqlite3_finalize(stmt);
  }
  free(stable_key);
  return 1;
}

static int strappy_db_catalog_begin_scan(sqlite3 *db,
                                         const char *scan_root,
                                         long long now_ms,
                                         int reset_existing_locations,
                                         long long *scan_run_id_out,
                                         char **error_out)
{
  static const char *cancel_running_sql =
    "UPDATE scan_runs SET state = 'cancelled', completed_at_ms = ?, "
    "error_message = COALESCE(error_message, 'Superseded by a newer scan.') "
    "WHERE scan_root_id = ? AND state = 'running';";
  static const char *insert_run_sql =
    "INSERT INTO scan_runs (scan_root_id, state, started_at_ms) "
    "VALUES (?, 'running', ?);";
  static const char *deactivate_sql =
    "UPDATE database_locations SET active = 0 WHERE scan_root_id = ?;";
  static const char *start_root_sql =
    "UPDATE scan_roots SET last_started_at_ms = ? WHERE id = ?;";
  sqlite3_stmt *stmt;
  long long root_id;
  int rc;

  if ((db == NULL) || (scan_root == NULL) || (scan_root[0] == '\0') ||
      (scan_run_id_out == NULL)) {
    strappy_set_error(error_out, "Database scan start is incomplete.");
    return 0;
  }
  *scan_run_id_out = 0LL;
  root_id = 0LL;
  if (!strappy_db_catalog_scan_root_id(db,
                                      scan_root,
                                      now_ms,
                                      &root_id,
                                      error_out)) {
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, cancel_running_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now_ms) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)root_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not cancel stale scan run: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, insert_run_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)root_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)now_ms) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not begin scan run: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  *scan_run_id_out = (long long)sqlite3_last_insert_rowid(db);
  sqlite3_finalize(stmt);

  if (reset_existing_locations) {
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, deactivate_sql, -1, &stmt, NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)root_id) != SQLITE_OK) ||
        (sqlite3_step(stmt) != SQLITE_DONE)) {
      strappy_set_formatted_error(error_out,
                                  "Could not reset scan locations: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return 0;
    }
    sqlite3_finalize(stmt);
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, start_root_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now_ms) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)root_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE) ||
      (sqlite3_changes(db) != 1)) {
    strappy_set_formatted_error(error_out,
                                "Could not start scan root: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_catalog_finish_scan(sqlite3 *db,
                                          long long scan_run_id,
                                          const char *state,
                                          const char *error_message,
                                          long long now_ms,
                                          int reconcile_unseen_locations,
                                          char **error_out)
{
  static const char *reconcile_locations_sql =
    "UPDATE database_locations SET active = 0 WHERE scan_root_id = "
    "(SELECT scan_root_id FROM scan_runs WHERE id = ? AND state = 'running') "
    "AND active = 1 AND "
    "(last_scan_run_id IS NULL OR last_scan_run_id <> ?);";
  static const char *finish_run_sql =
    "UPDATE scan_runs SET state = ?, completed_at_ms = ?, error_message = ? "
    "WHERE id = ? AND state = 'running';";
  static const char *finish_root_sql =
    "UPDATE scan_roots SET last_completed_at_ms = ? WHERE id = "
    "(SELECT scan_root_id FROM scan_runs WHERE id = ?);";
  sqlite3_stmt *stmt;
  int rc;

  if ((db == NULL) || (scan_run_id <= 0LL) || (state == NULL) ||
      ((strcmp(state, "completed") != 0) &&
       (strcmp(state, "error") != 0) &&
       (strcmp(state, "cancelled") != 0))) {
    strappy_set_error(error_out, "Database scan finish is incomplete.");
    return 0;
  }

  if (reconcile_unseen_locations && (strcmp(state, "completed") == 0)) {
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, reconcile_locations_sql, -1, &stmt, NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)scan_run_id) != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)scan_run_id) != SQLITE_OK) ||
        (sqlite3_step(stmt) != SQLITE_DONE)) {
      strappy_set_formatted_error(error_out,
                                  "Could not reconcile scan locations: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return 0;
    }
    sqlite3_finalize(stmt);
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, finish_run_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, state, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)now_ms) != SQLITE_OK) ||
      !strappy_db_bind_optional_text(db,
                                     stmt,
                                     3,
                                     error_message,
                                     "Could not bind scan error",
                                     error_out) ||
      (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)scan_run_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE) ||
      (sqlite3_changes(db) != 1)) {
    strappy_set_formatted_error(error_out,
                                "Could not finish scan run: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);

  if (strcmp(state, "completed") != 0) {
    return 1;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, finish_root_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now_ms) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)scan_run_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE) ||
      (sqlite3_changes(db) != 1)) {
    strappy_set_formatted_error(error_out,
                                "Could not finish scan root: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_catalog_save(sqlite3 *db,
                                   const strappy_discovered_database_input *records,
                                   size_t count,
                                   int replace_root,
                                   const char *scan_root,
                                   long long scan_run_id,
                                   char **error_out)
{
  long long now_ms;
  long long run_id;
  size_t index;
  int ok;

  now_ms = strappy_db_now_ms();
  run_id = scan_run_id;
  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin catalog write",
                       error_out)) {
    return 0;
  }
  ok = 1;
  if (replace_root) {
    ok = strappy_db_catalog_begin_scan(db,
                                       scan_root,
                                       now_ms,
                                       1,
                                       &run_id,
                                       error_out);
  }
  for (index = 0U; ok && (index < count); index++) {
    ok = strappy_db_catalog_save_one(db,
                                     &records[index],
                                     run_id,
                                     now_ms,
                                     error_out);
  }
  if (ok && replace_root) {
    ok = strappy_db_catalog_finish_scan(db,
                                        run_id,
                                        "completed",
                                        NULL,
                                        now_ms,
                                        0,
                                        error_out);
  }
  if (ok) {
    ok = strappy_db_exec(
      db,
      "DELETE FROM applications WHERE NOT EXISTS ("
        "SELECT 1 FROM databases WHERE databases.application_id = "
        "applications.id);",
      "Could not remove unreferenced applications",
      error_out);
  }
  if (ok) {
    ok = strappy_db_exec(db, "COMMIT;", "Could not commit catalog write",
                         error_out);
  }
  if (!ok) {
    (void)strappy_db_exec(db, "ROLLBACK;", "Could not roll back catalog write",
                          NULL);
  }
  return ok;
}

static int strappy_db_semantic_save_discovered_databases(
  const char *db_path,
  const strappy_discovered_database_input *records,
  size_t count,
  int replace_root,
  const char *scan_root,
  long long scan_run_id,
  char **error_out)
{
  sqlite3 *db;
  int ok;

  if ((records == NULL) && (count > 0U)) {
    strappy_set_error(error_out, "Discovered database records are missing.");
    return 0;
  }
  if (replace_root && ((scan_root == NULL) || (scan_root[0] == '\0'))) {
    strappy_set_error(error_out, "Replacement scan root is empty.");
    return 0;
  }
  if (replace_root && (scan_run_id > 0LL)) {
    strappy_set_error(error_out,
                      "Replacement scan cannot reuse an existing scan run.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  ok = strappy_db_ensure_schema(db, error_out) &&
       strappy_db_catalog_save(db,
                               records,
                               count,
                               replace_root,
                               scan_root,
                               scan_run_id,
                               error_out);
  strappy_db_release(db);
  return ok;
}

static int strappy_db_semantic_update_database_permission(
  const char *db_path,
  long long database_id,
  const char *decision,
  int update_hidden,
  int hidden,
  char **error_out)
{
  static const char *decision_sql =
    "UPDATE database_permissions SET decision = ?, updated_at_ms = ? "
    "WHERE database_id = ? AND "
    "(? != 'allowed' OR EXISTS (SELECT 1 FROM database_locations l "
    "WHERE l.database_id = database_permissions.database_id "
    "AND l.active = 1 AND l.validation_state = 'valid'));";
  static const char *hidden_sql =
    "UPDATE database_permissions SET hidden = ?, hidden_override = ?, "
    "updated_at_ms = ? "
    "WHERE database_id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  const char *sql;
  long long now_ms;
  int rc;
  int ok;

  if (database_id <= 0LL) {
    strappy_set_error(error_out, "Discovered database id is not valid.");
    return 0;
  }
  if (!update_hidden && !strappy_db_is_valid_user_decision(decision)) {
    strappy_set_error(error_out, "Discovered database decision is not valid.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  sql = update_hidden ? hidden_sql : decision_sql;
  now_ms = strappy_db_now_ms();
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare database permission update: %s",
                                sqlite3_errmsg(db));
    strappy_db_release(db);
    return 0;
  }
  if (update_hidden) {
    ok = (sqlite3_bind_int(stmt, 1, hidden ? 1 : 0) == SQLITE_OK) &&
         (sqlite3_bind_int(stmt, 2, hidden ? 1 : 0) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)now_ms) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)database_id) == SQLITE_OK);
  } else {
    ok = (sqlite3_bind_text(stmt, 1, decision, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)now_ms) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)database_id) == SQLITE_OK) &&
         (sqlite3_bind_text(stmt, 4, decision, -1, SQLITE_TRANSIENT) == SQLITE_OK);
  }
  if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not update database permission: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  if (sqlite3_changes(db) < 1) {
    strappy_set_error(error_out,
                      update_hidden ? "Discovered database was not found." :
                      "Discovered database was not found or is not valid SQLite.");
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
}

static int strappy_db_begin_discovered_database_scan_with_policy(
  const char *db_path,
  const char *scan_root,
  int reset_existing_locations,
  long long *scan_run_id_out,
  char **error_out)
{
  sqlite3 *db;
  int ok;

  if (scan_run_id_out == NULL) {
    strappy_set_error(error_out, "Database scan run output is missing.");
    return 0;
  }
  *scan_run_id_out = 0LL;
  if ((scan_root == NULL) || (scan_root[0] == '\0')) {
    strappy_set_error(error_out, "Database scan root is empty.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out) ||
      !strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin database scan",
                       error_out)) {
    strappy_db_release(db);
    return 0;
  }
  ok = strappy_db_catalog_begin_scan(db,
                                     scan_root,
                                     strappy_db_now_ms(),
                                     reset_existing_locations,
                                     scan_run_id_out,
                                     error_out);
  if (ok) {
    ok = strappy_db_exec(db,
                         "COMMIT;",
                         "Could not commit database scan start",
                         error_out);
  }
  if (!ok) {
    (void)strappy_db_exec(db,
                          "ROLLBACK;",
                          "Could not roll back database scan start",
                          NULL);
    *scan_run_id_out = 0LL;
  }
  strappy_db_release(db);
  return ok;
}

int strappy_db_begin_discovered_database_scan(
  const char *db_path,
  const char *scan_root,
  long long *scan_run_id_out,
  char **error_out)
{
  return strappy_db_begin_discovered_database_scan_with_policy(
    db_path,
    scan_root,
    1,
    scan_run_id_out,
    error_out);
}

int strappy_db_begin_incremental_discovered_database_scan(
  const char *db_path,
  const char *scan_root,
  long long *scan_run_id_out,
  char **error_out)
{
  return strappy_db_begin_discovered_database_scan_with_policy(
    db_path,
    scan_root,
    0,
    scan_run_id_out,
    error_out);
}

static int strappy_db_finish_discovered_database_scan_with_policy(
  const char *db_path,
  long long scan_run_id,
  const char *state,
  const char *error_message,
  int reconcile_unseen_locations,
  char **error_out)
{
  sqlite3 *db;
  int ok;

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out) ||
      !strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin database scan finish",
                       error_out)) {
    strappy_db_release(db);
    return 0;
  }
  ok = strappy_db_catalog_finish_scan(db,
                                      scan_run_id,
                                      state,
                                      error_message,
                                      strappy_db_now_ms(),
                                      reconcile_unseen_locations,
                                      error_out);
  if (ok) {
    ok = strappy_db_exec(db,
                         "COMMIT;",
                         "Could not commit database scan finish",
                         error_out);
  }
  if (!ok) {
    (void)strappy_db_exec(db,
                          "ROLLBACK;",
                          "Could not roll back database scan finish",
                          NULL);
  }
  strappy_db_release(db);
  return ok;
}

int strappy_db_finish_discovered_database_scan(
  const char *db_path,
  long long scan_run_id,
  const char *state,
  const char *error_message,
  char **error_out)
{
  return strappy_db_finish_discovered_database_scan_with_policy(
    db_path,
    scan_run_id,
    state,
    error_message,
    0,
    error_out);
}

int strappy_db_finish_incremental_discovered_database_scan(
  const char *db_path,
  long long scan_run_id,
  const char *state,
  const char *error_message,
  char **error_out)
{
  return strappy_db_finish_discovered_database_scan_with_policy(
    db_path,
    scan_run_id,
    state,
    error_message,
    1,
    error_out);
}

int strappy_db_save_discovered_databases_for_scan_run(
  const char *db_path,
  const strappy_discovered_database_input *records,
  size_t count,
  long long scan_run_id,
  char **error_out)
{
  if (scan_run_id <= 0LL) {
    strappy_set_error(error_out, "Database scan run is missing.");
    return 0;
  }
  return strappy_db_semantic_save_discovered_databases(db_path,
                                                       records,
                                                       count,
                                                       0,
                                                       NULL,
                                                       scan_run_id,
                                                       error_out);
}

int strappy_db_save_discovered_databases(
  const char *db_path,
  const strappy_discovered_database_input *records,
  size_t count,
  char **error_out)
{
  return strappy_db_semantic_save_discovered_databases(db_path,
                                                       records,
                                                       count,
                                                       0,
                                                       NULL,
                                                       0LL,
                                                       error_out);
}

int strappy_db_replace_discovered_databases_for_scan_root(
  const char *db_path,
  const strappy_discovered_database_input *records,
  size_t count,
  const char *scan_root,
  char **error_out)
{
  return strappy_db_semantic_save_discovered_databases(db_path,
                                                       records,
                                                       count,
                                                       1,
                                                       scan_root,
                                                       0LL,
                                                       error_out);
}

int strappy_db_list_discovered_databases(
  const char *db_path,
  strappy_discovered_database_record_list *list,
  char **error_out)
{
  static const char *sql =
    "SELECT d.id, d.assistant_database_id, l.path, l.size_bytes, "
    "l.modified_at_s, l.device, l.inode, "
    "CASE WHEN l.validation_state = 'valid' THEN 1 ELSE 0 END, "
    "l.validation_error, l.validation_state, p.decision, p.hidden, "
    "p.auto_hidden, p.hidden_override, p.hidden_reason, "
    "r.path, a.stable_key, COALESCE(a.name, d.display_name), "
    "a.bundle_id, a.container_path, "
    "a.bundle_path, a.source, d.origin_kind, d.location_tail, "
    "strftime('%Y-%m-%dT%H:%M:%fZ', d.first_seen_at_ms / 1000.0, 'unixepoch'), "
    "strftime('%Y-%m-%dT%H:%M:%fZ', l.last_seen_at_ms / 1000.0, 'unixepoch'), "
    "strftime('%Y-%m-%dT%H:%M:%fZ', l.last_scanned_at_ms / 1000.0, 'unixepoch') "
    "FROM databases d "
    "JOIN database_locations l ON l.database_id = d.id AND l.active = 1 "
    "JOIN database_permissions p ON p.database_id = d.id "
    "LEFT JOIN applications a ON a.id = d.application_id "
    "LEFT JOIN scan_roots r ON r.id = l.scan_root_id "
    "ORDER BY l.last_seen_at_ms DESC, d.id DESC, l.path;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (list == NULL) {
    strappy_set_error(error_out, "strappy_db_list_discovered_databases received no output.");
    return 0;
  }
  strappy_discovered_database_record_list_init(list);

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare discovered database list: %s",
                                sqlite3_errmsg(db));
    strappy_db_release(db);
    return 0;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    strappy_discovered_database_record *next_records;

    if (list->count >= (((size_t)-1) / sizeof(strappy_discovered_database_record))) {
      strappy_set_error(error_out, "Discovered database list is too large.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_discovered_database_record_list_destroy(list);
      return 0;
    }

    next_records = (strappy_discovered_database_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(strappy_discovered_database_record));
    if (next_records == NULL) {
      strappy_set_error(error_out, "Could not allocate discovered database list.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_discovered_database_record_list_destroy(list);
      return 0;
    }

    list->records = next_records;
    strappy_discovered_database_record_init(&list->records[list->count]);
    if (!strappy_db_assign_discovered_database_from_statement(
          &list->records[list->count],
          stmt,
          error_out)) {
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_discovered_database_record_list_destroy(list);
      return 0;
    }

    list->count++;
  }

  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read discovered database list: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    strappy_discovered_database_record_list_destroy(list);
    return 0;
  }

  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
}

static char *strappy_db_copy_database_filename(const char *path)
{
  const char *backslash;
  const char *filename;
  const char *slash;

  if (path == NULL) {
    return NULL;
  }
  slash = strrchr(path, '/');
  backslash = strrchr(path, '\\');
  filename = path;
  if ((slash != NULL) &&
      ((backslash == NULL) || (slash > backslash))) {
    filename = slash + 1;
  } else if (backslash != NULL) {
    filename = backslash + 1;
  }
  if (filename[0] == '\0') {
    return NULL;
  }
  return strappy_string_duplicate(filename);
}

int strappy_db_list_approved_database_display_names(
  const char *db_path,
  strappy_database_display_name_record_list *list,
  char **error_out)
{
  static const char *sql =
    "SELECT d.assistant_database_id, l.path "
    "FROM databases d "
    "JOIN database_locations l ON l.database_id = d.id AND l.active = 1 "
    "JOIN database_permissions p ON p.database_id = d.id "
    "WHERE d.assistant_database_id IS NOT NULL "
    "AND d.assistant_database_id <> '' "
    "AND l.validation_state = 'valid' "
    "AND p.decision = 'allowed' "
    "ORDER BY d.id, l.path;";
  strappy_database_display_name_record *record;
  strappy_database_display_name_record *next_records;
  sqlite3 *db;
  sqlite3_stmt *stmt;
  char *path;
  int rc;

  if (list == NULL) {
    strappy_set_error(
      error_out,
      "strappy_db_list_approved_database_display_names received no output.");
    return 0;
  }
  strappy_database_display_name_record_list_init(list);

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(
      error_out,
      "Could not prepare approved database display names: %s",
      sqlite3_errmsg(db));
    strappy_db_release(db);
    return 0;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    if (list->count >=
        (((size_t)-1) / sizeof(strappy_database_display_name_record))) {
      strappy_set_error(error_out,
                        "Approved database display name list is too large.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_database_display_name_record_list_destroy(list);
      return 0;
    }

    next_records = (strappy_database_display_name_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(*next_records));
    if (next_records == NULL) {
      strappy_set_error(
        error_out,
        "Could not allocate approved database display names.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_database_display_name_record_list_destroy(list);
      return 0;
    }
    list->records = next_records;
    record = &list->records[list->count];
    record->assistant_database_id = strappy_db_column_string(stmt, 0);
    record->filename = NULL;
    path = strappy_db_column_string(stmt, 1);
    if (path != NULL) {
      record->filename = strappy_db_copy_database_filename(path);
    }
    free(path);
    if ((record->assistant_database_id == NULL) ||
        (record->filename == NULL)) {
      free(record->assistant_database_id);
      free(record->filename);
      record->assistant_database_id = NULL;
      record->filename = NULL;
      strappy_set_error(
        error_out,
        "Could not copy an approved database display name.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_database_display_name_record_list_destroy(list);
      return 0;
    }
    list->count++;
  }

  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(
      error_out,
      "Could not read approved database display names: %s",
      sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    strappy_database_display_name_record_list_destroy(list);
    return 0;
  }

  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
}

int strappy_db_update_discovered_database_decision(
  const char *db_path,
  long long catalog_id,
  const char *user_decision,
  char **error_out)
{
  return strappy_db_semantic_update_database_permission(db_path,
                                                        catalog_id,
                                                        user_decision,
                                                        0,
                                                        0,
                                                        error_out);
}

int strappy_db_update_discovered_database_hidden(const char *db_path,
                                                 long long catalog_id,
                                                 int hidden,
                                                 char **error_out)
{
  return strappy_db_semantic_update_database_permission(db_path,
                                                        catalog_id,
                                                        NULL,
                                                        1,
                                                        hidden,
                                                        error_out);
}

static int strappy_db_semantic_insert_model_price(sqlite3 *db,
                                                  const char *model_id,
                                                  const char *kind,
                                                  const char *value,
                                                  char **error_out)
{
  static const char *sql =
    "INSERT INTO model_prices (model_id, price_kind, price_decimal) "
    "VALUES (?, ?, ?);";
  sqlite3_stmt *stmt;
  int rc;

  if ((value == NULL) || (value[0] == '\0')) {
    return 1;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 2, kind, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 3, value, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not save model price: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_semantic_insert_model_feature_value(
  sqlite3 *db,
  const char *model_id,
  const char *kind,
  const char *value,
  char **error_out)
{
  static const char *sql =
    "INSERT OR IGNORE INTO model_features "
    "(model_id, feature_kind, feature_value) VALUES (?, ?, ?);";
  sqlite3_stmt *stmt;
  int rc;

  if ((value == NULL) || (value[0] == '\0')) {
    return 1;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 2, kind, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 3, value, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not save model feature: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_semantic_insert_model_feature_array(
  sqlite3 *db,
  const char *model_id,
  const char *kind,
  cJSON *value,
  char **error_out)
{
  int count;
  int index;

  if ((value == NULL) || cJSON_IsNull(value)) {
    return 1;
  }
  if (!cJSON_IsArray(value)) {
    char *text;
    int ok;

    text = strappy_db_json_copy_value_text(value);
    ok = strappy_db_semantic_insert_model_feature_value(db,
                                                        model_id,
                                                        kind,
                                                        text,
                                                        error_out);
    free(text);
    return ok;
  }
  count = cJSON_GetArraySize(value);
  for (index = 0; index < count; index++) {
    char *text;
    int ok;

    text = strappy_db_json_copy_value_text(cJSON_GetArrayItem(value, index));
    ok = strappy_db_semantic_insert_model_feature_value(db,
                                                        model_id,
                                                        kind,
                                                        text,
                                                        error_out);
    free(text);
    if (!ok) {
      return 0;
    }
  }
  return 1;
}

static int strappy_db_semantic_insert_model(sqlite3 *db,
                                            const char *provider_id,
                                            cJSON *model,
                                            long long now_ms,
                                            char **error_out)
{
  static const char *insert_sql =
    "INSERT OR IGNORE INTO models "
    "(id, provider_id, wire_model_id, name, catalog_active, "
    "last_seen_at_ms) VALUES (?, ?, ?, ?, 1, ?);";
  static const char *update_sql =
    "UPDATE models SET canonical_slug = ?, hugging_face_id = ?, name = ?, "
    "description = ?, context_length = ?, created_at_s = ?, "
    "architecture_modality = ?, architecture_tokenizer = ?, "
    "architecture_instruct_type = ?, provider_context_length = ?, "
    "provider_max_completion_tokens = ?, provider_is_moderated = ?, "
    "knowledge_cutoff = ?, expiration_date = ?, details_url = ?, "
    "catalog_active = 1, last_seen_at_ms = ? "
    "WHERE id = ? AND provider_id = ? AND wire_model_id = ?;";
  static const char *clear_prices_sql =
    "DELETE FROM model_prices WHERE model_id = ?;";
  static const char *clear_features_sql =
    "DELETE FROM model_features WHERE model_id = ?;";
  sqlite3_stmt *stmt;
  strappy_openrouter_model_record record;
  cJSON *architecture;
  int rc;
  int ok;

  if (!cJSON_IsObject(model)) {
    strappy_set_error(error_out, "OpenRouter model entry is not an object.");
    return 0;
  }
  strappy_openrouter_model_record_init(&record);
  record.wire_model_id = strappy_db_json_copy_object_text(model, "id");
  if ((record.wire_model_id == NULL) || (record.wire_model_id[0] == '\0')) {
    strappy_openrouter_model_record_destroy(&record);
    strappy_set_error(error_out, "OpenRouter model entry is missing id.");
    return 0;
  }
  record.model_id = strappy_provider_model_identifier(
    provider_id,
    record.wire_model_id,
    error_out);
  if (record.model_id == NULL) {
    strappy_openrouter_model_record_destroy(&record);
    return 0;
  }
  record.canonical_slug =
    strappy_db_json_copy_object_text(model, "canonical_slug");
  record.hugging_face_id =
    strappy_db_json_copy_object_text(model, "hugging_face_id");
  record.name = strappy_db_json_copy_object_text(model, "name");
  if (record.name == NULL) {
    record.name = strappy_string_duplicate(record.wire_model_id);
  }
  record.description = strappy_db_json_copy_object_text(model, "description");
  record.context_length = strappy_db_json_object_integer(model, "context_length");
  record.created = strappy_db_json_object_integer(model, "created");
  record.architecture_modality =
    strappy_db_json_copy_nested_text(model, "architecture", "modality");
  record.architecture_tokenizer =
    strappy_db_json_copy_nested_text(model, "architecture", "tokenizer");
  record.architecture_instruct_type =
    strappy_db_json_copy_nested_text(model, "architecture", "instruct_type");
  record.pricing_prompt =
    strappy_db_json_copy_nested_text(model, "pricing", "prompt");
  record.pricing_completion =
    strappy_db_json_copy_nested_text(model, "pricing", "completion");
  record.pricing_request =
    strappy_db_json_copy_nested_text(model, "pricing", "request");
  record.pricing_image =
    strappy_db_json_copy_nested_text(model, "pricing", "image");
  record.pricing_audio =
    strappy_db_json_copy_nested_text(model, "pricing", "audio");
  record.pricing_web_search =
    strappy_db_json_copy_nested_text(model, "pricing", "web_search");
  record.pricing_internal_reasoning =
    strappy_db_json_copy_nested_text(model, "pricing", "internal_reasoning");
  record.pricing_input_cache_read =
    strappy_db_json_copy_nested_text(model, "pricing", "input_cache_read");
  record.pricing_input_cache_write =
    strappy_db_json_copy_nested_text(model, "pricing", "input_cache_write");
  record.top_provider_context_length =
    strappy_db_json_nested_integer(model, "top_provider", "context_length");
  record.top_provider_max_completion_tokens =
    strappy_db_json_nested_integer(model,
                                   "top_provider",
                                   "max_completion_tokens");
  record.top_provider_is_moderated =
    strappy_db_json_nested_bool(model, "top_provider", "is_moderated");
  record.knowledge_cutoff =
    strappy_db_json_copy_object_text(model, "knowledge_cutoff");
  record.expiration_date =
    strappy_db_json_copy_object_text(model, "expiration_date");
  record.links_details =
    strappy_db_json_copy_nested_text(model, "links", "details");
  if (record.name == NULL) {
    strappy_openrouter_model_record_destroy(&record);
    strappy_set_error(error_out, "Could not allocate OpenRouter model name.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, record.model_id, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 2,
                         provider_id,
                         -1,
                         SQLITE_STATIC) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 3, record.wire_model_id, -1,
                         SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 4, record.name, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 5, (sqlite3_int64)now_ms) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not insert OpenRouter model: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_openrouter_model_record_destroy(&record);
    return 0;
  }
  sqlite3_finalize(stmt);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare OpenRouter model update: %s",
                                sqlite3_errmsg(db));
    strappy_openrouter_model_record_destroy(&record);
    return 0;
  }
  ok = strappy_db_bind_text_or_null(db, stmt, 1, record.canonical_slug,
                                    "Could not bind model update", error_out) &&
       strappy_db_bind_text_or_null(db, stmt, 2, record.hugging_face_id,
                                    "Could not bind model update", error_out) &&
       (sqlite3_bind_text(stmt, 3, record.name, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
       strappy_db_bind_text_or_null(db, stmt, 4, record.description,
                                    "Could not bind model update", error_out) &&
       (sqlite3_bind_int64(stmt, 5, (sqlite3_int64)record.context_length) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 6, (sqlite3_int64)record.created) == SQLITE_OK) &&
       strappy_db_bind_text_or_null(db, stmt, 7, record.architecture_modality,
                                    "Could not bind model update", error_out) &&
       strappy_db_bind_text_or_null(db, stmt, 8, record.architecture_tokenizer,
                                    "Could not bind model update", error_out) &&
       strappy_db_bind_text_or_null(db, stmt, 9, record.architecture_instruct_type,
                                    "Could not bind model update", error_out) &&
       (sqlite3_bind_int64(stmt, 10,
                           (sqlite3_int64)record.top_provider_context_length) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 11,
                           (sqlite3_int64)record.top_provider_max_completion_tokens) == SQLITE_OK) &&
       (sqlite3_bind_int(stmt, 12,
                         record.top_provider_is_moderated ? 1 : 0) == SQLITE_OK) &&
       strappy_db_bind_text_or_null(db, stmt, 13, record.knowledge_cutoff,
                                    "Could not bind model update", error_out) &&
       strappy_db_bind_text_or_null(db, stmt, 14, record.expiration_date,
                                    "Could not bind model update", error_out) &&
       strappy_db_bind_text_or_null(db, stmt, 15, record.links_details,
                                    "Could not bind model update", error_out) &&
       (sqlite3_bind_int64(stmt, 16, (sqlite3_int64)now_ms) == SQLITE_OK) &&
       (sqlite3_bind_text(stmt, 17, record.model_id, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 18,
                          provider_id,
                          -1,
                          SQLITE_STATIC) == SQLITE_OK) &&
       (sqlite3_bind_text(stmt, 19, record.wire_model_id, -1,
                          SQLITE_TRANSIENT) == SQLITE_OK);
  if (ok) {
    rc = sqlite3_step(stmt);
  }
  if (!ok || (rc != SQLITE_DONE) || (sqlite3_changes(db) != 1)) {
    if (ok) {
      if (rc == SQLITE_DONE) {
        strappy_set_error(
          error_out,
          "OpenRouter model account identity did not match its catalog row.");
      } else {
        strappy_set_formatted_error(error_out,
                                    "Could not update OpenRouter model: %s",
                                    sqlite3_errmsg(db));
      }
    }
    sqlite3_finalize(stmt);
    strappy_openrouter_model_record_destroy(&record);
    return 0;
  }
  sqlite3_finalize(stmt);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, clear_prices_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, record.model_id, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not clear model prices: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_openrouter_model_record_destroy(&record);
    return 0;
  }
  sqlite3_finalize(stmt);
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, clear_features_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, record.model_id, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not clear model features: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_openrouter_model_record_destroy(&record);
    return 0;
  }
  sqlite3_finalize(stmt);

  ok = strappy_db_semantic_insert_model_price(db, record.model_id, "prompt",
                                               record.pricing_prompt, error_out) &&
       strappy_db_semantic_insert_model_price(db, record.model_id, "completion",
                                               record.pricing_completion, error_out) &&
       strappy_db_semantic_insert_model_price(db, record.model_id, "request",
                                               record.pricing_request, error_out) &&
       strappy_db_semantic_insert_model_price(db, record.model_id, "image",
                                               record.pricing_image, error_out) &&
       strappy_db_semantic_insert_model_price(db, record.model_id, "audio",
                                               record.pricing_audio, error_out) &&
       strappy_db_semantic_insert_model_price(db, record.model_id, "web_search",
                                               record.pricing_web_search, error_out) &&
       strappy_db_semantic_insert_model_price(db, record.model_id,
                                               "internal_reasoning",
                                               record.pricing_internal_reasoning,
                                               error_out) &&
       strappy_db_semantic_insert_model_price(db, record.model_id,
                                               "input_cache_read",
                                               record.pricing_input_cache_read,
                                               error_out) &&
       strappy_db_semantic_insert_model_price(db, record.model_id,
                                               "input_cache_write",
                                               record.pricing_input_cache_write,
                                               error_out);
  architecture = strappy_db_json_object_child(model, "architecture");
  if (ok) {
    ok = strappy_db_semantic_insert_model_feature_array(
           db, record.model_id, "input_modality",
           cJSON_GetObjectItem(architecture, "input_modalities"), error_out) &&
         strappy_db_semantic_insert_model_feature_array(
           db, record.model_id, "output_modality",
           cJSON_GetObjectItem(architecture, "output_modalities"), error_out) &&
         strappy_db_semantic_insert_model_feature_array(
           db, record.model_id, "parameter",
           cJSON_GetObjectItem(model, "supported_parameters"), error_out) &&
         strappy_db_semantic_insert_model_feature_array(
           db, record.model_id, "voice",
           cJSON_GetObjectItem(model, "supported_voices"), error_out);
  }
  strappy_openrouter_model_record_destroy(&record);
  return ok;
}

static int strappy_db_semantic_save_models(const char *db_path,
                                           const char *provider_id,
                                           const char *json,
                                           char **error_out)
{
  sqlite3 *db;
  cJSON *root;
  cJSON *data;
  long long now_ms;
  int count;
  int index;
  int ok;

  if ((provider_id == NULL) || (provider_id[0] == '\0')) {
    strappy_set_error(error_out, "Model provider id is empty.");
    return 0;
  }
  if ((json == NULL) || (json[0] == '\0')) {
    strappy_set_error(error_out, "OpenRouter model JSON is empty.");
    return 0;
  }
  root = cJSON_Parse(json);
  if (root == NULL) {
    strappy_set_error(error_out, "OpenRouter model JSON could not be parsed.");
    return 0;
  }
  data = cJSON_GetObjectItem(root, "data");
  if (!cJSON_IsArray(data) && cJSON_IsArray(root)) {
    data = root;
  }
  if (!cJSON_IsArray(data)) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "OpenRouter model JSON is missing data array.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    cJSON_Delete(root);
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out) ||
      !strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin model refresh",
                       error_out)) {
    strappy_db_release(db);
    cJSON_Delete(root);
    return 0;
  }
  {
    sqlite3_stmt *stale_stmt = NULL;
    int stale_rc = sqlite3_prepare_v2(db,
      "UPDATE models SET catalog_active = 0 WHERE provider_id = ? "
      "AND id <> " STRAPPY_DB_DEFAULT_MODEL_SQL ";",
      -1, &stale_stmt, NULL);
    if ((stale_rc == SQLITE_OK) &&
        (sqlite3_bind_text(stale_stmt, 1, provider_id, -1,
                           SQLITE_TRANSIENT) == SQLITE_OK)) {
      stale_rc = sqlite3_step(stale_stmt);
    }
    ok = stale_rc == SQLITE_DONE;
    if (!ok) strappy_set_formatted_error(error_out,
      "Could not mark model catalog stale: %s", sqlite3_errmsg(db));
    sqlite3_finalize(stale_stmt);
  }
  now_ms = strappy_db_now_ms();
  count = cJSON_GetArraySize(data);
  for (index = 0; ok && (index < count); index++) {
    ok = strappy_db_semantic_insert_model(db,
                                          provider_id,
                                          cJSON_GetArrayItem(data, index),
                                          now_ms,
                                          error_out);
  }
  if (ok) {
    char *default_model_id = strappy_provider_model_identifier(
      provider_id, STRAPPY_CONFIG_DEFAULT_API_MODEL, error_out);
    sqlite3_stmt *default_stmt = NULL;
    int default_rc = (default_model_id != NULL) ? sqlite3_prepare_v2(db,
      "INSERT OR IGNORE INTO models (id, provider_id, wire_model_id, "
      "name, description, catalog_active, last_seen_at_ms) "
      "VALUES (?, ?, ?, ?, ?, 1, ?);", -1, &default_stmt, NULL) : SQLITE_ERROR;
    if ((default_rc == SQLITE_OK) &&
        (sqlite3_bind_text(default_stmt,1,default_model_id,-1,SQLITE_TRANSIENT)==SQLITE_OK) &&
        (sqlite3_bind_text(default_stmt,2,provider_id,-1,SQLITE_TRANSIENT)==SQLITE_OK) &&
        (sqlite3_bind_text(default_stmt,3,STRAPPY_CONFIG_DEFAULT_API_MODEL,-1,SQLITE_STATIC)==SQLITE_OK) &&
        (sqlite3_bind_text(default_stmt,4,STRAPPY_CONFIG_DEFAULT_API_MODEL,-1,SQLITE_STATIC)==SQLITE_OK) &&
        (sqlite3_bind_text(default_stmt,5,STRAPPY_DB_BUILTIN_DEFAULT_MODEL_DESCRIPTION,-1,SQLITE_STATIC)==SQLITE_OK) &&
        (sqlite3_bind_int64(default_stmt,6,(sqlite3_int64)now_ms)==SQLITE_OK)) default_rc=sqlite3_step(default_stmt);
    ok = default_rc == SQLITE_DONE;
    if (!ok && ((error_out == NULL) || (*error_out == NULL))) strappy_set_error(error_out,"Could not save built-in default model.");
    sqlite3_finalize(default_stmt); free(default_model_id);
  }
  if (ok) {
    sqlite3_stmt *retain_stmt = NULL;
    int retain_rc = sqlite3_prepare_v2(db,
      "UPDATE models SET catalog_active=1,last_seen_at_ms=? WHERE "
      "provider_id=? AND wire_model_id=?;",-1,&retain_stmt,NULL);
    if ((retain_rc==SQLITE_OK) && (sqlite3_bind_int64(retain_stmt,1,(sqlite3_int64)now_ms)==SQLITE_OK) &&
        (sqlite3_bind_text(retain_stmt,2,provider_id,-1,SQLITE_TRANSIENT)==SQLITE_OK) &&
        (sqlite3_bind_text(retain_stmt,3,STRAPPY_CONFIG_DEFAULT_API_MODEL,-1,SQLITE_STATIC)==SQLITE_OK)) retain_rc=sqlite3_step(retain_stmt);
    ok=retain_rc==SQLITE_DONE; sqlite3_finalize(retain_stmt);
  }
  if (ok) {
    char *default_model_id = strappy_provider_model_identifier(
      provider_id, STRAPPY_CONFIG_DEFAULT_API_MODEL, error_out);
    sqlite3_stmt *preference_stmt = NULL;
    int preference_rc = (default_model_id != NULL) ? sqlite3_prepare_v2(
      db,
      "INSERT OR IGNORE INTO model_preferences "
      "(provider_id, wire_model_id, allowed, updated_at_ms) "
      "SELECT m.provider_id, m.wire_model_id, 1, ? "
      "FROM models m WHERE m.id = ?;",
      -1,
      &preference_stmt,
      NULL) : SQLITE_ERROR;
    if ((preference_rc == SQLITE_OK) &&
        (sqlite3_bind_int64(preference_stmt,
                            1,
                            (sqlite3_int64)now_ms) == SQLITE_OK)) {
      preference_rc = sqlite3_bind_text(preference_stmt,
                                        2,
                                        default_model_id,
                                        -1,
                                        SQLITE_TRANSIENT);
    }
    if (preference_rc == SQLITE_OK) {
      preference_rc = sqlite3_step(preference_stmt);
    }
    ok = preference_rc == SQLITE_DONE;
    if (!ok && ((error_out == NULL) || (*error_out == NULL))) {
      strappy_set_formatted_error(error_out,
                                  "Could not whitelist default model: %s",
                                  sqlite3_errmsg(db));
    }
    sqlite3_finalize(preference_stmt);
    free(default_model_id);
  }
  if (ok) {
    char *default_model_id = strappy_provider_model_identifier(
      provider_id, STRAPPY_CONFIG_DEFAULT_API_MODEL, error_out);
    sqlite3_stmt *preference_stmt = NULL;
    int preference_rc = (default_model_id != NULL) ? sqlite3_prepare_v2(
      db,
      "UPDATE model_preferences SET allowed = 1, "
      "updated_at_ms = ? WHERE allowed <> 1 AND provider_id = "
      "(SELECT m.provider_id FROM models m WHERE m.id = ?) "
      "AND wire_model_id = "
      "(SELECT m.wire_model_id FROM models m WHERE m.id = ?);",
      -1,
      &preference_stmt,
      NULL) : SQLITE_ERROR;
    if ((preference_rc == SQLITE_OK) &&
        (sqlite3_bind_int64(preference_stmt,
                            1,
                            (sqlite3_int64)now_ms) == SQLITE_OK) &&
        (sqlite3_bind_text(preference_stmt,
                           2,
                           default_model_id,
                           -1,
                           SQLITE_TRANSIENT) == SQLITE_OK) &&
        (sqlite3_bind_text(preference_stmt,
                           3,
                           default_model_id,
                           -1,
                           SQLITE_TRANSIENT) == SQLITE_OK)) {
      preference_rc = sqlite3_step(preference_stmt);
    }
    ok = preference_rc == SQLITE_DONE;
    if (!ok && ((error_out == NULL) || (*error_out == NULL))) {
      strappy_set_formatted_error(
        error_out,
        "Could not restore default model whitelist: %s",
        sqlite3_errmsg(db));
    }
    sqlite3_finalize(preference_stmt);
    free(default_model_id);
  }
  if (ok) {
    ok = strappy_db_exec(db, "COMMIT;", "Could not commit model refresh",
                         error_out);
  } else {
    (void)strappy_db_exec(db, "ROLLBACK;", "Could not roll back model refresh",
                          NULL);
  }
  strappy_db_release(db);
  cJSON_Delete(root);
  return ok;
}

int strappy_db_save_openrouter_models_json(const char *db_path,
                                           const char *json,
                                           char **error_out)
{
  return strappy_db_semantic_save_models(
    db_path, STRAPPY_PROVIDER_OPENROUTER, json, error_out);
}

int strappy_db_save_account_models_json(const char *db_path,
                                        const char *provider_account_id,
                                        const char *json,
                                        char **error_out)
{
  strappy_provider_account_record account;
  int ok = strappy_db_get_provider_account(db_path, provider_account_id,
                                            &account, error_out);
  if (!ok) return 0;
  ok = (strcmp(account.provider_id, STRAPPY_PROVIDER_OPENROUTER) == 0) &&
       (strcmp(account.lifecycle_state, STRAPPY_PROVIDER_ACCOUNT_ACTIVE) == 0);
  strappy_provider_account_record_destroy(&account);
  if (!ok) { strappy_set_error(error_out,"Catalog refresh requires an active OpenRouter account."); return 0; }
  return strappy_db_semantic_save_models(db_path, STRAPPY_PROVIDER_OPENROUTER,
                                         json, error_out);
}

#define STRAPPY_BUNDLED_CATALOG_MAX_BYTES (512U * 1024U)
#define STRAPPY_BUNDLED_CATALOG_MAX_MODELS 64
#define STRAPPY_BUNDLED_CATALOG_MAX_FEATURES 16
#define STRAPPY_BUNDLED_CATALOG_MAX_IDENTIFIER 128U
#define STRAPPY_BUNDLED_CATALOG_MAX_NAME 160U
#define STRAPPY_BUNDLED_CATALOG_MAX_TOKEN_LIMIT 2000000LL

static int strappy_db_bundled_key_allowed(
  const char *key,
  const char * const *allowed,
  size_t allowed_count)
{
  size_t index;

  if (key == NULL) {
    return 0;
  }
  for (index = 0U; index < allowed_count; index++) {
    if (strcmp(key, allowed[index]) == 0) {
      return 1;
    }
  }
  return 0;
}

static int strappy_db_bundled_validate_object_keys(
  cJSON *object,
  const char * const *allowed,
  size_t allowed_count,
  const char *label,
  char **error_out)
{
  cJSON *child;
  cJSON *other;

  if (!cJSON_IsObject(object)) {
    strappy_set_formatted_error(error_out, "%s is not an object.", label);
    return 0;
  }
  for (child = object->child; child != NULL; child = child->next) {
    if (!strappy_db_bundled_key_allowed(child->string,
                                        allowed,
                                        allowed_count)) {
      strappy_set_formatted_error(error_out,
                                  "%s contains unsupported field '%s'.",
                                  label,
                                  (child->string != NULL) ? child->string : "");
      return 0;
    }
    for (other = object->child; other != child; other = other->next) {
      if ((other->string != NULL) && (child->string != NULL) &&
          (strcmp(other->string, child->string) == 0)) {
        strappy_set_formatted_error(error_out,
                                    "%s repeats field '%s'.",
                                    label,
                                    child->string);
        return 0;
      }
    }
  }
  return 1;
}

static int strappy_db_bundled_required_string(cJSON *object,
                                               const char *key,
                                               size_t maximum_length,
                                               const char **value_out,
                                               char **error_out)
{
  cJSON *value;
  size_t length;

  value = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!cJSON_IsString(value) || (value->valuestring == NULL) ||
      (value->valuestring[0] == '\0')) {
    strappy_set_formatted_error(error_out,
                                "Bundled model field '%s' must be a string.",
                                key);
    return 0;
  }
  length = strlen(value->valuestring);
  if (length > maximum_length) {
    strappy_set_formatted_error(error_out,
                                "Bundled model field '%s' is too long.",
                                key);
    return 0;
  }
  if (value_out != NULL) {
    *value_out = value->valuestring;
  }
  return 1;
}

static int strappy_db_bundled_positive_integer(cJSON *object,
                                                const char *key,
                                                long long maximum,
                                                long long *value_out,
                                                char **error_out)
{
  cJSON *value;
  long long integer;

  value = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!cJSON_IsNumber(value) || (value->valuedouble < 1.0) ||
      (value->valuedouble > (double)maximum)) {
    strappy_set_formatted_error(error_out,
                                "Bundled model field '%s' is invalid.",
                                key);
    return 0;
  }
  integer = (long long)value->valuedouble;
  if ((double)integer != value->valuedouble) {
    strappy_set_formatted_error(error_out,
                                "Bundled model field '%s' is not an integer.",
                                key);
    return 0;
  }
  if (value_out != NULL) {
    *value_out = integer;
  }
  return 1;
}

static int strappy_db_bundled_string_array(cJSON *array,
                                            const char *label,
                                            int allow_empty,
                                            char **error_out)
{
  cJSON *value;
  cJSON *other;
  int count;
  int index;
  int other_index;

  if (!cJSON_IsArray(array)) {
    strappy_set_formatted_error(error_out, "%s must be an array.", label);
    return 0;
  }
  count = cJSON_GetArraySize(array);
  if ((!allow_empty && (count == 0)) ||
      (count > STRAPPY_BUNDLED_CATALOG_MAX_FEATURES)) {
    strappy_set_formatted_error(error_out, "%s has an invalid size.", label);
    return 0;
  }
  for (index = 0; index < count; index++) {
    value = cJSON_GetArrayItem(array, index);
    if (!cJSON_IsString(value) || (value->valuestring == NULL) ||
        (value->valuestring[0] == '\0') ||
        (strlen(value->valuestring) > STRAPPY_BUNDLED_CATALOG_MAX_IDENTIFIER)) {
      strappy_set_formatted_error(error_out,
                                  "%s contains an invalid value.",
                                  label);
      return 0;
    }
    for (other_index = 0; other_index < index; other_index++) {
      other = cJSON_GetArrayItem(array, other_index);
      if (strcmp(other->valuestring, value->valuestring) == 0) {
        strappy_set_formatted_error(error_out,
                                    "%s contains duplicate value '%s'.",
                                    label,
                                    value->valuestring);
        return 0;
      }
    }
  }
  return 1;
}

static int strappy_db_bundled_validate_model(cJSON *model,
                                              char **error_out)
{
  static const char * const model_keys[] = {
    "provider_id", "wire_model_id",
    "display_name", "catalog_active", "billing_kind", "input_modalities",
    "context_window_tokens", "max_output_tokens", "capabilities",
    "reasoning_levels", "reasoning_level_overrides"
  };
  static const char * const capability_keys[] = {
    "reasoning", "local_functions", "hosted_tools"
  };
  const char *provider_id;
  const char *wire_model_id;
  const char *billing_kind;
  cJSON *active;
  cJSON *capabilities;
  cJSON *reasoning;
  cJSON *local_functions;
  cJSON *overrides;
  cJSON *override_value;

  if (!strappy_db_bundled_validate_object_keys(
        model,
        model_keys,
        sizeof(model_keys) / sizeof(model_keys[0]),
        "Bundled model entry",
        error_out) ||
      !strappy_db_bundled_required_string(
        model, "provider_id", STRAPPY_BUNDLED_CATALOG_MAX_IDENTIFIER,
        &provider_id, error_out) ||
      !strappy_db_bundled_required_string(
        model, "wire_model_id", STRAPPY_BUNDLED_CATALOG_MAX_IDENTIFIER,
        &wire_model_id, error_out) ||
      !strappy_db_bundled_required_string(
        model, "display_name", STRAPPY_BUNDLED_CATALOG_MAX_NAME,
        NULL, error_out) ||
      !strappy_db_bundled_required_string(
        model, "billing_kind", STRAPPY_BUNDLED_CATALOG_MAX_IDENTIFIER,
        &billing_kind, error_out)) {
    return 0;
  }
  if ((strcmp(provider_id, STRAPPY_PROVIDER_OPENAI_CHATGPT) != 0) ||
      (strcmp(billing_kind, "chatgpt_plan") != 0)) {
    strappy_set_error(error_out,
                      "Bundled catalog contains a non-ChatGPT model route.");
    return 0;
  }
  active = cJSON_GetObjectItemCaseSensitive(model, "catalog_active");
  capabilities = cJSON_GetObjectItemCaseSensitive(model, "capabilities");
  reasoning = cJSON_GetObjectItemCaseSensitive(capabilities, "reasoning");
  local_functions =
    cJSON_GetObjectItemCaseSensitive(capabilities, "local_functions");
  if (!cJSON_IsBool(active) ||
      !strappy_db_bundled_positive_integer(
        model, "context_window_tokens", STRAPPY_BUNDLED_CATALOG_MAX_TOKEN_LIMIT,
        NULL, error_out) ||
      !strappy_db_bundled_positive_integer(
        model, "max_output_tokens", STRAPPY_BUNDLED_CATALOG_MAX_TOKEN_LIMIT,
        NULL, error_out) ||
      !strappy_db_bundled_validate_object_keys(
        capabilities,
        capability_keys,
        sizeof(capability_keys) / sizeof(capability_keys[0]),
        "Bundled model capabilities",
        error_out) ||
      !cJSON_IsBool(reasoning) || !cJSON_IsBool(local_functions) ||
      !strappy_db_bundled_string_array(
        cJSON_GetObjectItemCaseSensitive(model, "input_modalities"),
        "Bundled model input_modalities", 0, error_out) ||
      !strappy_db_bundled_string_array(
        cJSON_GetObjectItemCaseSensitive(model, "reasoning_levels"),
        "Bundled model reasoning_levels", 1, error_out) ||
      !strappy_db_bundled_string_array(
        cJSON_GetObjectItemCaseSensitive(capabilities, "hosted_tools"),
        "Bundled model hosted_tools", 1, error_out)) {
    if ((error_out != NULL) && (*error_out == NULL)) {
      strappy_set_error(error_out,
                        "Bundled model capability flags are invalid.");
    }
    return 0;
  }
  overrides =
    cJSON_GetObjectItemCaseSensitive(model, "reasoning_level_overrides");
  if (!cJSON_IsObject(overrides)) {
    strappy_set_error(error_out,
                      "Bundled model reasoning overrides must be an object.");
    return 0;
  }
  for (override_value = overrides->child;
       override_value != NULL;
       override_value = override_value->next) {
    if ((override_value->string == NULL) ||
        (override_value->string[0] == '\0') ||
        (strlen(override_value->string) >
          STRAPPY_BUNDLED_CATALOG_MAX_IDENTIFIER) ||
        !cJSON_IsString(override_value) ||
        (override_value->valuestring == NULL) ||
        (override_value->valuestring[0] == '\0') ||
        (strlen(override_value->valuestring) >
          STRAPPY_BUNDLED_CATALOG_MAX_IDENTIFIER)) {
      strappy_set_error(error_out,
                        "Bundled model reasoning override is invalid.");
      return 0;
    }
  }
  return 1;
}

static int strappy_db_bundled_exec_model_statement(sqlite3 *db,
                                                    const char *sql,
                                                    const char *model_id,
                                                    const char *label,
                                                    char **error_out)
{
  sqlite3_stmt *stmt;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if ((rc == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT) == SQLITE_OK)) {
    rc = sqlite3_step(stmt);
  }
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out, "%s: %s", label, sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_bundled_insert_pair(sqlite3 *db,
                                           const char *sql,
                                           const char *model_id,
                                           const char *first,
                                           const char *second,
                                           const char *label,
                                           char **error_out)
{
  sqlite3_stmt *stmt;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if ((rc == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 2, first, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
      ((second == NULL) ||
       (sqlite3_bind_text(stmt, 3, second, -1, SQLITE_TRANSIENT) == SQLITE_OK))) {
    rc = sqlite3_step(stmt);
  }
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out, "%s: %s", label, sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_bundled_import_model(sqlite3 *db,
                                            const char *provider_id,
                                            cJSON *model,
                                            long long now_ms,
                                            char **error_out)
{
  static const char *insert_sql =
    "INSERT OR IGNORE INTO models "
    "(id, provider_id, wire_model_id, name, context_length, "
      "provider_context_length, provider_max_completion_tokens, "
      "catalog_active, last_seen_at_ms) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
  static const char *update_sql =
    "UPDATE models SET name = ?, context_length = ?, "
      "provider_context_length = ?, provider_max_completion_tokens = ?, "
      "catalog_active = ?, last_seen_at_ms = ? "
    "WHERE id = ? AND provider_id = ? AND wire_model_id = ?;";
  static const char *capabilities_sql =
    "INSERT INTO model_capabilities "
    "(model_id, billing_kind, reasoning_enabled, local_functions_enabled, "
      "hosted_tools_enabled) VALUES (?, ?, ?, ?, ?);";
  static const char *feature_sql =
    "INSERT INTO model_features (model_id, feature_kind, feature_value) "
    "VALUES (?, ?, ?);";
  static const char *hosted_tool_sql =
    "INSERT INTO model_hosted_tools (model_id, tool_id) VALUES (?, ?);";
  static const char *override_sql =
    "INSERT INTO model_reasoning_overrides "
    "(model_id, requested_level, wire_level) VALUES (?, ?, ?);";
  char *model_id;
  const char *wire_model_id;
  const char *display_name;
  const char *billing_kind;
  cJSON *capabilities;
  cJSON *array;
  cJSON *value;
  sqlite3_stmt *stmt;
  long long context_tokens;
  long long output_tokens;
  int active;
  int reasoning_enabled;
  int local_functions_enabled;
  int hosted_tools_enabled;
  int count;
  int index;
  int rc;

  context_tokens = 0LL;
  output_tokens = 0LL;
  wire_model_id =
    cJSON_GetObjectItemCaseSensitive(model, "wire_model_id")->valuestring;
  model_id = strappy_provider_model_identifier(provider_id,
                                                wire_model_id,
                                                error_out);
  if (model_id == NULL) {
    return 0;
  }
  display_name =
    cJSON_GetObjectItemCaseSensitive(model, "display_name")->valuestring;
  billing_kind =
    cJSON_GetObjectItemCaseSensitive(model, "billing_kind")->valuestring;
  (void)strappy_db_bundled_positive_integer(
    model, "context_window_tokens", STRAPPY_BUNDLED_CATALOG_MAX_TOKEN_LIMIT,
    &context_tokens, NULL);
  (void)strappy_db_bundled_positive_integer(
    model, "max_output_tokens", STRAPPY_BUNDLED_CATALOG_MAX_TOKEN_LIMIT,
    &output_tokens, NULL);
  active = cJSON_IsTrue(
    cJSON_GetObjectItemCaseSensitive(model, "catalog_active")) ? 1 : 0;
  capabilities = cJSON_GetObjectItemCaseSensitive(model, "capabilities");
  reasoning_enabled = cJSON_IsTrue(
    cJSON_GetObjectItemCaseSensitive(capabilities, "reasoning")) ? 1 : 0;
  local_functions_enabled = cJSON_IsTrue(
    cJSON_GetObjectItemCaseSensitive(capabilities, "local_functions")) ? 1 : 0;
  array = cJSON_GetObjectItemCaseSensitive(capabilities, "hosted_tools");
  hosted_tools_enabled = (cJSON_GetArraySize(array) > 0) ? 1 : 0;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
  if ((rc == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 2, provider_id,
                         -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 3, wire_model_id, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 4, display_name, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
      (sqlite3_bind_int64(stmt, 5, (sqlite3_int64)context_tokens) == SQLITE_OK) &&
      (sqlite3_bind_int64(stmt, 6, (sqlite3_int64)context_tokens) == SQLITE_OK) &&
      (sqlite3_bind_int64(stmt, 7, (sqlite3_int64)output_tokens) == SQLITE_OK) &&
      (sqlite3_bind_int(stmt, 8, active) == SQLITE_OK) &&
      (sqlite3_bind_int64(stmt, 9, (sqlite3_int64)now_ms) == SQLITE_OK)) {
    rc = sqlite3_step(stmt);
  }
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not insert bundled model: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
  if ((rc == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 1, display_name, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)context_tokens) == SQLITE_OK) &&
      (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)context_tokens) == SQLITE_OK) &&
      (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)output_tokens) == SQLITE_OK) &&
      (sqlite3_bind_int(stmt, 5, active) == SQLITE_OK) &&
      (sqlite3_bind_int64(stmt, 6, (sqlite3_int64)now_ms) == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 7, model_id, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 8, provider_id,
                         -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 9, wire_model_id, -1, SQLITE_TRANSIENT) == SQLITE_OK)) {
    rc = sqlite3_step(stmt);
  }
  if ((rc != SQLITE_DONE) || (sqlite3_changes(db) != 1)) {
    strappy_set_formatted_error(error_out,
                                "Could not update bundled model identity: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);

  if (!strappy_db_bundled_exec_model_statement(
        db, "DELETE FROM model_features WHERE model_id = ?;", model_id,
        "Could not clear bundled model features", error_out) ||
      !strappy_db_bundled_exec_model_statement(
        db, "DELETE FROM model_capabilities WHERE model_id = ?;", model_id,
        "Could not clear bundled model capabilities", error_out) ||
      !strappy_db_bundled_exec_model_statement(
        db, "DELETE FROM model_hosted_tools WHERE model_id = ?;", model_id,
        "Could not clear bundled hosted tools", error_out) ||
      !strappy_db_bundled_exec_model_statement(
        db, "DELETE FROM model_reasoning_overrides WHERE model_id = ?;", model_id,
        "Could not clear bundled reasoning overrides", error_out)) {
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, capabilities_sql, -1, &stmt, NULL);
  if ((rc == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 2, billing_kind, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
      (sqlite3_bind_int(stmt, 3, reasoning_enabled) == SQLITE_OK) &&
      (sqlite3_bind_int(stmt, 4, local_functions_enabled) == SQLITE_OK) &&
      (sqlite3_bind_int(stmt, 5, hosted_tools_enabled) == SQLITE_OK)) {
    rc = sqlite3_step(stmt);
  }
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save bundled model capabilities: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);

  array = cJSON_GetObjectItemCaseSensitive(model, "input_modalities");
  count = cJSON_GetArraySize(array);
  for (index = 0; index < count; index++) {
    value = cJSON_GetArrayItem(array, index);
    if (!strappy_db_bundled_insert_pair(db, feature_sql, model_id,
                                         "input_modality", value->valuestring,
                                         "Could not save bundled input modality",
                                         error_out)) {
      return 0;
    }
  }
  array = cJSON_GetObjectItemCaseSensitive(model, "reasoning_levels");
  count = cJSON_GetArraySize(array);
  for (index = 0; index < count; index++) {
    value = cJSON_GetArrayItem(array, index);
    if (!strappy_db_bundled_insert_pair(db, feature_sql, model_id,
                                         "reasoning", value->valuestring,
                                         "Could not save bundled reasoning level",
                                         error_out)) {
      return 0;
    }
  }
  array = cJSON_GetObjectItemCaseSensitive(capabilities, "hosted_tools");
  count = cJSON_GetArraySize(array);
  for (index = 0; index < count; index++) {
    value = cJSON_GetArrayItem(array, index);
    if (!strappy_db_bundled_insert_pair(db, hosted_tool_sql, model_id,
                                         value->valuestring, NULL,
                                         "Could not save bundled hosted tool",
                                         error_out)) {
      return 0;
    }
  }
  value = cJSON_GetObjectItemCaseSensitive(model,
                                            "reasoning_level_overrides")->child;
  while (value != NULL) {
    if (!strappy_db_bundled_insert_pair(db, override_sql, model_id,
                                         value->string, value->valuestring,
                                         "Could not save reasoning override",
                                         error_out)) {
      return 0;
    }
    value = value->next;
  }
  free(model_id);
  return 1;
}

static int strappy_db_import_bundled_models_json_for_provider(
  const char *db_path,
  const char *provider_id,
  const char *json,
  char **error_out)
{
  static const char * const root_keys[] = {
    "schema_version", "catalog_revision", "catalog_source", "models"
  };
  static const char *revision_sql =
    "INSERT OR REPLACE INTO bundled_model_catalogs "
    "(provider_id, schema_version, catalog_revision, catalog_source, "
      "imported_at_ms) VALUES (?, 1, ?, ?, ?);";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  cJSON *root;
  cJSON *schema_version;
  cJSON *revision;
  cJSON *source;
  cJSON *models;
  cJSON *model;
  cJSON *other;
  const char *model_id;
  const char *other_id;
  long long catalog_revision;
  long long current_revision;
  long long now_ms;
  size_t json_length;
  int count;
  int index;
  int other_index;
  int rc;
  int ok;

  if ((provider_id == NULL) ||
      (strcmp(provider_id, STRAPPY_PROVIDER_OPENAI_CHATGPT) != 0)) {
    strappy_set_error(error_out, "Bundled model provider is invalid.");
    return 0;
  }

  if ((json == NULL) || (json[0] == '\0')) {
    strappy_set_error(error_out, "Bundled model catalog is empty.");
    return 0;
  }
  json_length = strlen(json);
  if (json_length > STRAPPY_BUNDLED_CATALOG_MAX_BYTES) {
    strappy_set_error(error_out, "Bundled model catalog is too large.");
    return 0;
  }
  root = cJSON_Parse(json);
  if ((root == NULL) ||
      !strappy_db_bundled_validate_object_keys(
        root, root_keys, sizeof(root_keys) / sizeof(root_keys[0]),
        "Bundled model catalog", error_out)) {
    if ((error_out != NULL) && (*error_out == NULL)) {
      strappy_set_error(error_out, "Bundled model catalog is not valid JSON.");
    }
    cJSON_Delete(root);
    return 0;
  }
  schema_version = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
  revision = cJSON_GetObjectItemCaseSensitive(root, "catalog_revision");
  source = cJSON_GetObjectItemCaseSensitive(root, "catalog_source");
  models = cJSON_GetObjectItemCaseSensitive(root, "models");
  if (!cJSON_IsNumber(schema_version) ||
      (schema_version->valuedouble != 1.0) ||
      !cJSON_IsNumber(revision) || (revision->valuedouble < 1.0) ||
      (revision->valuedouble > (double)INT_MAX) ||
      ((double)((long long)revision->valuedouble) != revision->valuedouble) ||
      !cJSON_IsString(source) || (source->valuestring == NULL) ||
      (strcmp(source->valuestring, "bundled") != 0) ||
      !cJSON_IsArray(models)) {
    cJSON_Delete(root);
    strappy_set_error(error_out,
                      "Bundled model catalog metadata is invalid.");
    return 0;
  }
  catalog_revision = (long long)revision->valuedouble;
  count = cJSON_GetArraySize(models);
  if ((count < 1) || (count > STRAPPY_BUNDLED_CATALOG_MAX_MODELS)) {
    cJSON_Delete(root);
    strappy_set_error(error_out,
                      "Bundled model catalog has an invalid model count.");
    return 0;
  }
  for (index = 0; index < count; index++) {
    model = cJSON_GetArrayItem(models, index);
    if (!strappy_db_bundled_validate_model(model, error_out)) {
      cJSON_Delete(root);
      return 0;
    }
    model_id = cJSON_GetObjectItemCaseSensitive(model, "wire_model_id")->valuestring;
    for (other_index = 0; other_index < index; other_index++) {
      other = cJSON_GetArrayItem(models, other_index);
      other_id =
        cJSON_GetObjectItemCaseSensitive(other, "wire_model_id")->valuestring;
      if (strcmp(model_id, other_id) == 0) {
        cJSON_Delete(root);
        strappy_set_error(error_out,
                          "Bundled model catalog contains duplicate model ids.");
        return 0;
      }
    }
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    cJSON_Delete(root);
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    cJSON_Delete(root);
    return 0;
  }
  current_revision = 0LL;
  stmt = NULL;
  rc = sqlite3_prepare_v2(
    db,
    "SELECT catalog_revision FROM bundled_model_catalogs "
      "WHERE provider_id = ?;",
    -1,
    &stmt,
    NULL);
  if ((rc == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 1, provider_id,
                         -1, SQLITE_TRANSIENT) == SQLITE_OK)) {
    rc = sqlite3_step(stmt);
  }
  if (rc == SQLITE_ROW) {
    current_revision = (long long)sqlite3_column_int64(stmt, 0);
  } else if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read bundled catalog revision: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    cJSON_Delete(root);
    return 0;
  }
  sqlite3_finalize(stmt);
  if (current_revision >= catalog_revision) {
    strappy_db_release(db);
    cJSON_Delete(root);
    return 1;
  }

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;",
                       "Could not begin bundled catalog import", error_out)) {
    strappy_db_release(db);
    cJSON_Delete(root);
    return 0;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db,
    "UPDATE models SET catalog_active = 0 WHERE provider_id = ?;",
    -1, &stmt, NULL);
  if ((rc == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 1, provider_id, -1,
                         SQLITE_TRANSIENT) == SQLITE_OK)) rc = sqlite3_step(stmt);
  ok = rc == SQLITE_DONE;
  if (!ok) strappy_set_formatted_error(error_out,
    "Could not deactivate stale bundled models: %s", sqlite3_errmsg(db));
  sqlite3_finalize(stmt);
  now_ms = strappy_db_now_ms();
  for (index = 0; ok && (index < count); index++) {
    ok = strappy_db_bundled_import_model(db,
                                         provider_id,
                                         cJSON_GetArrayItem(models, index),
                                         now_ms,
                                         error_out);
  }
  if (ok) {
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, revision_sql, -1, &stmt, NULL);
    if ((rc == SQLITE_OK) &&
        (sqlite3_bind_text(stmt, 1, STRAPPY_PROVIDER_OPENAI_CHATGPT,
                           -1, SQLITE_STATIC) == SQLITE_OK) &&
        (sqlite3_bind_int64(stmt, 2,
                            (sqlite3_int64)catalog_revision) == SQLITE_OK) &&
        (sqlite3_bind_text(stmt, 3, source->valuestring,
                           -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
        (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)now_ms) == SQLITE_OK)) {
      rc = sqlite3_step(stmt);
    }
    if (rc != SQLITE_DONE) {
      strappy_set_formatted_error(error_out,
                                  "Could not record bundled catalog revision: %s",
                                  sqlite3_errmsg(db));
      ok = 0;
    }
    sqlite3_finalize(stmt);
  }
  if (ok) {
    ok = strappy_db_exec(db, "COMMIT;",
                         "Could not commit bundled catalog import", error_out);
  } else {
    (void)strappy_db_exec(db, "ROLLBACK;",
                          "Could not roll back bundled catalog import", NULL);
  }
  strappy_db_release(db);
  cJSON_Delete(root);
  return ok;
}

int strappy_db_import_bundled_models_json(const char *db_path,
                                          const char *json,
                                          char **error_out)
{
  int ok = strappy_db_import_bundled_models_json_for_provider(
    db_path, STRAPPY_PROVIDER_OPENAI_CHATGPT, json, error_out);
  if (ok) {
    sqlite3 *db;

    if (!strappy_db_open(db_path, &db, error_out)) return 0;
    if (!strappy_db_ensure_schema(db, error_out) ||
        !strappy_db_exec(db,
          "UPDATE app_preferences SET default_model_id=COALESCE("
          "default_model_id,(SELECT m.id FROM models m WHERE "
          "m.catalog_active=1 ORDER BY m.name,m.id LIMIT 1)) WHERE id=1;",
          "Could not initialize bundled model defaults", error_out)) {
      strappy_db_release(db);
      return 0;
    }
    strappy_db_release(db);
  }
  return ok;
}

int strappy_db_import_bundled_models_json_for_account(
  const char *db_path, const char *provider_account_id, const char *json,
  char **error_out)
{
  strappy_provider_account_record account;
  int ok;

  if (!strappy_db_get_provider_account(db_path, provider_account_id,
                                       &account, error_out)) return 0;
  ok = (strcmp(account.provider_id, STRAPPY_PROVIDER_OPENAI_CHATGPT) == 0) &&
    (strcmp(account.lifecycle_state, STRAPPY_PROVIDER_ACCOUNT_ACTIVE) == 0);
  strappy_provider_account_record_destroy(&account);
  if (!ok) {
    strappy_set_error(error_out,
                      "Bundled models require an active ChatGPT account.");
    return 0;
  }
  return strappy_db_import_bundled_models_json(db_path, json, error_out);
}

static int strappy_db_save_manual_model(const char *db_path,
                                        const char *provider_id,
                                        const strappy_manual_model_input *input,
                                        int require_existing,
                                        char **model_id_out,
                                        char **error_out)
{
  static const char *model_sql =
    "INSERT OR IGNORE INTO models (id,provider_id,wire_model_id,name,"
    "context_length,provider_context_length,provider_max_completion_tokens,"
    "catalog_active,last_seen_at_ms) VALUES (?,?,?,?,?,?,?,1,?) "
    ";";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  const char *display_name;
  char *model_id;
  int rc;
  int ok;
  const char *prices[4];
  static const char *price_kinds[4] = {
    "prompt", "completion", "input_cache_read", "input_cache_write"
  };
  size_t price_index;

  prices[0] = (input != NULL) ? input->pricing_prompt : NULL;
  prices[1] = (input != NULL) ? input->pricing_completion : NULL;
  prices[2] = (input != NULL) ? input->pricing_input_cache_read : NULL;
  prices[3] = (input != NULL) ? input->pricing_input_cache_write : NULL;
  if ((provider_id == NULL) ||
      (strcmp(provider_id, STRAPPY_PROVIDER_OTHER) != 0) ||
      (input == NULL) || (input->wire_model_id == NULL) ||
      (input->wire_model_id[0] == '\0') || (strlen(input->wire_model_id) > 128U) ||
      ((input->display_name != NULL) &&
       (strlen(input->display_name) > 160U)) ||
      (input->context_window_tokens < 0LL) ||
      (input->max_output_tokens < 0LL)) {
    strappy_set_error(error_out, "Manual model metadata is invalid."); return 0;
  }
  for (price_index = 0U; price_index < 4U; price_index++) {
    if ((prices[price_index] != NULL) && (prices[price_index][0] != '\0')) {
      char *end;
      double value;

      errno = 0;
      end = NULL;
      value = strtod(prices[price_index], &end);
      if ((errno == ERANGE) || (end == prices[price_index]) ||
          (end == NULL) || (*end != '\0') || !isfinite(value) ||
          (value < 0.0)) {
        strappy_set_error(error_out, "Manual model pricing is invalid.");
        return 0;
      }
    }
  }
  display_name = ((input->display_name != NULL) &&
                  (input->display_name[0] != '\0')) ?
    input->display_name : input->wire_model_id;
  model_id = strappy_provider_model_identifier(provider_id,
                                                input->wire_model_id,
                                                error_out);
  if (model_id == NULL) return 0;
  if (!strappy_db_open(db_path,&db,error_out)) { free(model_id); return 0; }
  if (!strappy_db_ensure_schema(db,error_out)) { strappy_db_release(db); free(model_id); return 0; }
  stmt=NULL; rc=sqlite3_prepare_v2(db,"SELECT 1 FROM models WHERE id=? AND provider_id=?;",-1,&stmt,NULL);
  if ((rc==SQLITE_OK)&&(sqlite3_bind_text(stmt,1,model_id,-1,SQLITE_TRANSIENT)==SQLITE_OK)&&
      (sqlite3_bind_text(stmt,2,provider_id,-1,SQLITE_TRANSIENT)==SQLITE_OK)) rc=sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (require_existing && (rc!=SQLITE_ROW)) { strappy_db_release(db); free(model_id); strappy_set_error(error_out,"Manual model was not found."); return 0; }
  if (!require_existing && (rc==SQLITE_ROW)) { strappy_db_release(db); free(model_id); strappy_set_error(error_out,"Manual model already exists."); return 0; }
  if ((rc!=SQLITE_ROW)&&(rc!=SQLITE_DONE)) { strappy_db_release(db); free(model_id); strappy_set_error(error_out,"Could not check manual model identity."); return 0; }
  if (!strappy_db_exec(db,"BEGIN IMMEDIATE;","Could not begin manual model write",error_out)) { strappy_db_release(db); free(model_id); return 0; }
  stmt=NULL; rc=sqlite3_prepare_v2(db,model_sql,-1,&stmt,NULL);
  if ((rc==SQLITE_OK)&&(sqlite3_bind_text(stmt,1,model_id,-1,SQLITE_TRANSIENT)==SQLITE_OK)&&
      (sqlite3_bind_text(stmt,2,provider_id,-1,SQLITE_TRANSIENT)==SQLITE_OK)&&
      (sqlite3_bind_text(stmt,3,input->wire_model_id,-1,SQLITE_TRANSIENT)==SQLITE_OK)&&
      (sqlite3_bind_text(stmt,4,display_name,-1,SQLITE_TRANSIENT)==SQLITE_OK)&&
      (sqlite3_bind_int64(stmt,5,(sqlite3_int64)input->context_window_tokens)==SQLITE_OK)&&
      (sqlite3_bind_int64(stmt,6,(sqlite3_int64)input->context_window_tokens)==SQLITE_OK)&&
      (sqlite3_bind_int64(stmt,7,(sqlite3_int64)input->max_output_tokens)==SQLITE_OK)&&
      (sqlite3_bind_int64(stmt,8,(sqlite3_int64)strappy_db_now_ms())==SQLITE_OK)) rc=sqlite3_step(stmt);
  ok=rc==SQLITE_DONE; sqlite3_finalize(stmt);
  if (ok) { stmt=NULL; rc=sqlite3_prepare_v2(db,
    "UPDATE models SET name=?,context_length=?,provider_context_length=?,"
    "provider_max_completion_tokens=?,catalog_active=1,last_seen_at_ms=? "
    "WHERE id=? AND provider_id=? AND wire_model_id=?;",-1,&stmt,NULL);
    if ((rc==SQLITE_OK)&&(sqlite3_bind_text(stmt,1,display_name,-1,SQLITE_TRANSIENT)==SQLITE_OK)&&
        (sqlite3_bind_int64(stmt,2,(sqlite3_int64)input->context_window_tokens)==SQLITE_OK)&&
        (sqlite3_bind_int64(stmt,3,(sqlite3_int64)input->context_window_tokens)==SQLITE_OK)&&
        (sqlite3_bind_int64(stmt,4,(sqlite3_int64)input->max_output_tokens)==SQLITE_OK)&&
        (sqlite3_bind_int64(stmt,5,(sqlite3_int64)strappy_db_now_ms())==SQLITE_OK)&&
        (sqlite3_bind_text(stmt,6,model_id,-1,SQLITE_TRANSIENT)==SQLITE_OK)&&
        (sqlite3_bind_text(stmt,7,provider_id,-1,SQLITE_TRANSIENT)==SQLITE_OK)&&
        (sqlite3_bind_text(stmt,8,input->wire_model_id,-1,SQLITE_TRANSIENT)==SQLITE_OK)) rc=sqlite3_step(stmt);
    ok=(rc==SQLITE_DONE)&&(sqlite3_changes(db)==1); sqlite3_finalize(stmt); }
  if (ok) { stmt=NULL; rc=sqlite3_prepare_v2(db,
    "INSERT OR REPLACE INTO model_capabilities (model_id,billing_kind,reasoning_enabled,local_functions_enabled,hosted_tools_enabled) VALUES (?,'unknown',?,?,0);",-1,&stmt,NULL);
    if ((rc==SQLITE_OK)&&(sqlite3_bind_text(stmt,1,model_id,-1,SQLITE_TRANSIENT)==SQLITE_OK)&&
        (sqlite3_bind_int(stmt,2,input->reasoning_enabled?1:0)==SQLITE_OK)&&
        (sqlite3_bind_int(stmt,3,input->local_functions_enabled?1:0)==SQLITE_OK)) rc=sqlite3_step(stmt);
    ok=rc==SQLITE_DONE; sqlite3_finalize(stmt); }
  if (ok) { stmt=NULL; rc=sqlite3_prepare_v2(db,
    "DELETE FROM model_prices WHERE model_id=? AND price_kind IN "
    "('prompt','completion','input_cache_read','input_cache_write');",
    -1,&stmt,NULL);
    if ((rc==SQLITE_OK)&&(sqlite3_bind_text(stmt,1,model_id,-1,SQLITE_TRANSIENT)==SQLITE_OK)) rc=sqlite3_step(stmt);
    ok=rc==SQLITE_DONE; sqlite3_finalize(stmt); }
  if (ok) { stmt=NULL; rc=sqlite3_prepare_v2(db,
    "DELETE FROM model_features WHERE model_id=? AND feature_kind='input_modality';",
    -1,&stmt,NULL);
    if ((rc==SQLITE_OK)&&(sqlite3_bind_text(stmt,1,model_id,-1,SQLITE_TRANSIENT)==SQLITE_OK)) rc=sqlite3_step(stmt);
    ok=rc==SQLITE_DONE; sqlite3_finalize(stmt); }
  if (ok) ok = strappy_db_semantic_insert_model_feature_value(
    db, model_id, "input_modality", "text", error_out);
  if (ok && input->image_input_enabled) {
    ok = strappy_db_semantic_insert_model_feature_value(
      db, model_id, "input_modality", "image", error_out);
  }
  for (price_index = 0U; ok && (price_index < 4U); price_index++) {
    ok = strappy_db_semantic_insert_model_price(db, model_id,
      price_kinds[price_index], prices[price_index], error_out);
  }
  if (ok) ok=strappy_db_exec(db,"COMMIT;","Could not commit manual model write",error_out);
  else (void)strappy_db_exec(db,"ROLLBACK;","Could not roll back manual model write",NULL);
  strappy_db_release(db);
  if (!ok) { free(model_id); if ((error_out==NULL)||(*error_out==NULL)) strappy_set_error(error_out,"Could not save manual model."); return 0; }
  if (model_id_out != NULL) *model_id_out=model_id; else free(model_id);
  return 1;
}

int strappy_db_create_manual_model(const char *db_path,
                                   const char *provider_id,
                                   const strappy_manual_model_input *input,
                                   char **model_id_out,
                                   char **error_out)
{
  if (model_id_out == NULL) { strappy_set_error(error_out,"Manual model id output is missing."); return 0; }
  *model_id_out=NULL;
  return strappy_db_save_manual_model(db_path,provider_id,input,0,model_id_out,error_out);
}

int strappy_db_update_manual_model(const char *db_path,
                                   const char *provider_id,
                                   const strappy_manual_model_input *input,
                                   char **error_out)
{
  return strappy_db_save_manual_model(db_path,provider_id,input,1,NULL,error_out);
}

int strappy_db_archive_manual_model(const char *db_path,
                                    const char *provider_id,
                                    const char *wire_model_id,
                                    char **error_out)
{
  sqlite3 *db; sqlite3_stmt *stmt; int rc; int changed;
  if (!strappy_db_open(db_path,&db,error_out)) return 0;
  if (!strappy_db_ensure_schema(db,error_out)) { strappy_db_release(db); return 0; }
  stmt=NULL; rc=sqlite3_prepare_v2(db,
    "UPDATE models SET catalog_active=0,last_seen_at_ms=? WHERE provider_id=? AND wire_model_id=? AND provider_id='other';",-1,&stmt,NULL);
  if ((rc==SQLITE_OK)&&(sqlite3_bind_int64(stmt,1,(sqlite3_int64)strappy_db_now_ms())==SQLITE_OK)&&
      (sqlite3_bind_text(stmt,2,provider_id,-1,SQLITE_TRANSIENT)==SQLITE_OK)&&
      (sqlite3_bind_text(stmt,3,wire_model_id,-1,SQLITE_TRANSIENT)==SQLITE_OK)) rc=sqlite3_step(stmt);
  changed=(rc==SQLITE_DONE)?sqlite3_changes(db):0; sqlite3_finalize(stmt); strappy_db_release(db);
  if (changed!=1) { strappy_set_error(error_out,"Manual model was not found."); return 0; }
  return 1;
}

static void strappy_db_filter_model_list(strappy_model_record_list *list,
                                         const char *provider_id,
                                         int allowed_only)
{
  size_t read_index;
  size_t write_index;

  if (list == NULL) {
    return;
  }
  write_index = 0U;
  for (read_index = 0U; read_index < list->count; read_index++) {
    int keep;

    keep = (!allowed_only || list->records[read_index].allowed) &&
      ((provider_id == NULL) ||
       ((list->records[read_index].provider_id != NULL) &&
        (strcmp(list->records[read_index].provider_id, provider_id) == 0)));
    if (keep) {
      if (write_index != read_index) {
        strappy_model_record_destroy(&list->records[write_index]);
        list->records[write_index] = list->records[read_index];
        strappy_model_record_init(&list->records[read_index]);
      }
      write_index++;
    } else {
      strappy_model_record_destroy(&list->records[read_index]);
    }
  }
  list->count = write_index;
}

int strappy_db_list_openrouter_models(
  const char *db_path,
  strappy_openrouter_model_record_list *list,
  char **error_out)
{
  return strappy_db_list_openrouter_models_matching(db_path,
                                                    NULL,
                                                    list,
                                                    error_out);
}

int strappy_db_list_allowed_openrouter_models(
  const char *db_path,
  strappy_openrouter_model_record_list *list,
  char **error_out)
{
  if (!strappy_db_list_openrouter_models(db_path, list, error_out)) {
    return 0;
  }
  strappy_db_filter_model_list(list, NULL, 1);
  return 1;
}

static int strappy_db_semantic_list_models(
  const char *db_path,
  const char *search_text,
  strappy_openrouter_model_record_list *list,
  char **error_out)
{
  static const char *select_columns =
    "SELECT m.id, m.canonical_slug, m.hugging_face_id, m.name, "
    "m.description, m.context_length, m.created_at_s, "
    "m.architecture_modality, m.architecture_tokenizer, "
    "m.architecture_instruct_type, "
    "(SELECT p.price_decimal FROM model_prices p WHERE p.model_id = m.id "
      "AND p.price_kind = 'prompt'), "
    "(SELECT p.price_decimal FROM model_prices p WHERE p.model_id = m.id "
      "AND p.price_kind = 'completion'), "
    "(SELECT p.price_decimal FROM model_prices p WHERE p.model_id = m.id "
      "AND p.price_kind = 'request'), "
    "(SELECT p.price_decimal FROM model_prices p WHERE p.model_id = m.id "
      "AND p.price_kind = 'image'), "
    "(SELECT p.price_decimal FROM model_prices p WHERE p.model_id = m.id "
      "AND p.price_kind = 'audio'), "
    "(SELECT p.price_decimal FROM model_prices p WHERE p.model_id = m.id "
      "AND p.price_kind = 'web_search'), "
    "(SELECT p.price_decimal FROM model_prices p WHERE p.model_id = m.id "
      "AND p.price_kind = 'internal_reasoning'), "
    "(SELECT p.price_decimal FROM model_prices p WHERE p.model_id = m.id "
      "AND p.price_kind = 'input_cache_read'), "
    "(SELECT p.price_decimal FROM model_prices p WHERE p.model_id = m.id "
      "AND p.price_kind = 'input_cache_write'), "
    "m.provider_context_length, m.provider_max_completion_tokens, "
    "m.provider_is_moderated, m.knowledge_cutoff, m.expiration_date, "
    "m.details_url, NULL, NULL, NULL, NULL, NULL, NULL, "
    "strftime('%Y-%m-%dT%H:%M:%fZ', m.last_seen_at_ms / 1000.0, 'unixepoch'), "
    "CASE WHEN m.id = " STRAPPY_DB_DEFAULT_MODEL_SQL
      " THEN 1 ELSE 0 END, "
    "CASE WHEN m.id = " STRAPPY_DB_DEFAULT_MODEL_SQL
      " OR COALESCE(mp.allowed, 0) = 1 THEN 1 ELSE 0 END "
    ", '', m.provider_id, '', m.wire_model_id, "
    "COALESCE(mc.billing_kind, 'metered_api'), "
    "COALESCE(mc.reasoning_enabled, 1), "
    "COALESCE(mc.local_functions_enabled, 1), "
    "COALESCE(mc.hosted_tools_enabled, 1), "
    "EXISTS (SELECT 1 FROM model_features mif WHERE mif.model_id=m.id "
      "AND mif.feature_kind='input_modality' AND mif.feature_value='image') "
    "FROM models m "
    "LEFT JOIN model_capabilities mc ON mc.model_id = m.id "
    "LEFT JOIN model_preferences mp ON mp.provider_id = m.provider_id "
      "AND mp.wire_model_id = m.wire_model_id ";
  static const char *unfiltered_suffix =
    "WHERE m.catalog_active = 1 "
    "ORDER BY LOWER(m.provider_id), "
      "CASE WHEN m.id = " STRAPPY_DB_DEFAULT_MODEL_SQL
      " THEN 0 ELSE 1 END, LOWER(m.name), m.id;";
  static const char *filtered_suffix =
    "WHERE m.catalog_active = 1 AND ("
    "m.id LIKE ?1 ESCAPE '\\' OR COALESCE(m.canonical_slug, '') LIKE ?1 ESCAPE '\\' "
    "OR m.wire_model_id LIKE ?1 ESCAPE '\\' "
    "OR m.provider_id LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.hugging_face_id, '') LIKE ?1 ESCAPE '\\' "
    "OR m.name LIKE ?1 ESCAPE '\\' OR COALESCE(m.description, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.architecture_modality, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.architecture_tokenizer, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.architecture_instruct_type, '') LIKE ?1 ESCAPE '\\' "
    "OR EXISTS (SELECT 1 FROM model_features f WHERE f.model_id = m.id "
      "AND f.feature_value LIKE ?1 ESCAPE '\\') "
    "OR EXISTS (SELECT 1 FROM model_prices p WHERE p.model_id = m.id "
      "AND p.price_decimal LIKE ?1 ESCAPE '\\')) "
    "ORDER BY LOWER(m.provider_id), "
      "CASE WHEN m.id = " STRAPPY_DB_DEFAULT_MODEL_SQL
      " THEN 0 ELSE 1 END, LOWER(m.name), m.id;";
  strappy_db_sql_buffer query;
  sqlite3 *db;
  sqlite3_stmt *stmt;
  char *pattern;
  const char *suffix;
  int rc;

  if (list == NULL) {
    strappy_set_error(error_out, "Model list output is missing.");
    return 0;
  }
  strappy_openrouter_model_record_list_init(list);
  pattern = NULL;
  suffix = unfiltered_suffix;
  if ((search_text != NULL) && (search_text[0] != '\0')) {
    pattern = strappy_db_like_pattern_for_search(search_text, error_out);
    if (pattern == NULL) {
      return 0;
    }
    suffix = filtered_suffix;
  }
  memset(&query, 0, sizeof(query));
  if (!strappy_db_sql_buffer_append(&query, select_columns) ||
      !strappy_db_sql_buffer_append(&query, suffix)) {
    free(pattern);
    strappy_db_sql_buffer_destroy(&query);
    strappy_set_error(error_out, "Could not allocate model catalog query.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    free(pattern);
    strappy_db_sql_buffer_destroy(&query);
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    free(pattern);
    strappy_db_sql_buffer_destroy(&query);
    strappy_db_release(db);
    return 0;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, query.data, -1, &stmt, NULL);
  strappy_db_sql_buffer_destroy(&query);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare model catalog list: %s",
                                sqlite3_errmsg(db));
    free(pattern);
    strappy_db_release(db);
    return 0;
  }
  if ((pattern != NULL) &&
      (sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not bind model catalog search: %s",
                                sqlite3_errmsg(db));
    free(pattern);
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  free(pattern);
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    strappy_openrouter_model_record *records;

    if (list->count >= (((size_t)-1) /
                        sizeof(strappy_openrouter_model_record))) {
      strappy_set_error(error_out, "Model list is too large.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_openrouter_model_record_list_destroy(list);
      return 0;
    }
    records = (strappy_openrouter_model_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(strappy_openrouter_model_record));
    if (records == NULL) {
      strappy_set_error(error_out, "Could not allocate model catalog list.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_openrouter_model_record_list_destroy(list);
      return 0;
    }
    list->records = records;
    strappy_openrouter_model_record_init(&list->records[list->count]);
    if (!strappy_db_assign_model_from_statement(
          &list->records[list->count], stmt, error_out)) {
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_openrouter_model_record_list_destroy(list);
      return 0;
    }
    list->count++;
  }
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read model catalog list: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    strappy_openrouter_model_record_list_destroy(list);
    return 0;
  }
  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
}

int strappy_db_list_openrouter_models_matching(
  const char *db_path,
  const char *search_text,
  strappy_openrouter_model_record_list *list,
  char **error_out)
{
  if (!strappy_db_semantic_list_models(db_path,
                                       search_text,
                                       list,
                                       error_out)) {
    return 0;
  }
  strappy_db_filter_model_list(list,
                               STRAPPY_PROVIDER_OPENROUTER,
                               0);
  return 1;
}

int strappy_db_list_models_matching(const char *db_path,
                                    const char *search_text,
                                    strappy_model_record_list *list,
                                    char **error_out)
{
  return strappy_db_semantic_list_models(db_path,
                                         search_text,
                                         list,
                                         error_out);
}

int strappy_db_list_models(const char *db_path,
                           strappy_model_record_list *list,
                           char **error_out)
{
  return strappy_db_list_models_matching(db_path, NULL, list, error_out);
}

int strappy_db_list_allowed_models(const char *db_path,
                                   strappy_model_record_list *list,
                                   char **error_out)
{
  if (!strappy_db_list_models(db_path, list, error_out)) {
    return 0;
  }
  strappy_db_filter_model_list(list, NULL, 1);
  return 1;
}

int strappy_db_list_models_for_account(
  const char *db_path, const char *provider_account_id,
  const char *search_text, int allowed_only,
  strappy_model_record_list *list, char **error_out)
{
  strappy_provider_account_record account;
  int ok;

  if ((provider_account_id == NULL) || (provider_account_id[0] == '\0')) {
    strappy_set_error(error_out,"Provider account id is empty."); return 0;
  }
  if (!strappy_db_get_provider_account(db_path, provider_account_id,
                                       &account, error_out)) return 0;
  if (strcmp(account.lifecycle_state, STRAPPY_PROVIDER_ACCOUNT_ACTIVE) != 0) {
    strappy_provider_account_record_destroy(&account);
    strappy_set_error(error_out, "Provider account is archived.");
    return 0;
  }
  ok = strappy_db_semantic_list_models(db_path,search_text,list,error_out);
  if (ok) strappy_db_filter_model_list(list,account.provider_id,
                                       allowed_only ? 1 : 0);
  strappy_provider_account_record_destroy(&account);
  if (!ok) return 0;
  return 1;
}

int strappy_db_get_account_model_id(const char *db_path,
                                    const char *provider_account_id,
                                    const char *wire_model_id,
                                    char **model_id_out,
                                    char **error_out)
{
  sqlite3 *db; sqlite3_stmt *stmt; int rc;
  if (model_id_out == NULL) { strappy_set_error(error_out,"Model id output is missing."); return 0; }
  *model_id_out=NULL;
  if (!strappy_db_open(db_path,&db,error_out)) return 0;
  if (!strappy_db_ensure_schema(db,error_out)) { strappy_db_release(db); return 0; }
  stmt=NULL; rc=sqlite3_prepare_v2(db,
    "SELECT m.id FROM models m JOIN provider_accounts a "
    "ON a.provider_id=m.provider_id WHERE a.id=? AND a.lifecycle_state='active' "
    "AND m.wire_model_id=? AND m.catalog_active=1;",-1,&stmt,NULL);
  if ((rc==SQLITE_OK)&&(sqlite3_bind_text(stmt,1,provider_account_id,-1,SQLITE_TRANSIENT)==SQLITE_OK)&&
      (sqlite3_bind_text(stmt,2,wire_model_id,-1,SQLITE_TRANSIENT)==SQLITE_OK)) rc=sqlite3_step(stmt);
  if (rc==SQLITE_ROW) *model_id_out=strappy_db_column_string(stmt,0);
  if (*model_id_out==NULL) strappy_set_error(error_out,"Account model was not found.");
  sqlite3_finalize(stmt); strappy_db_release(db); return *model_id_out!=NULL;
}

int strappy_db_model_exists(sqlite3 *db,
                                   const char *model_id,
                                   char **error_out)
{
  static const char *exists_sql =
    "SELECT 1 FROM models WHERE id = ?;";
  sqlite3_stmt *stmt;
  int rc;

  if ((model_id == NULL) || (model_id[0] == '\0')) {
    strappy_set_error(error_out, "Model id is empty.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, exists_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare model lookup: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  rc = sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind model lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "Model was not found.");
    return 0;
  }
  if (rc != SQLITE_ROW) {
    strappy_set_formatted_error(error_out,
                                "Could not read model lookup: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  return 1;
}

int strappy_db_model_matches_session_account(sqlite3 *db,
                                             long long session_id,
                                             const char *model_id,
                                             char **error_out)
{
  static const char *sql =
    "SELECT CASE WHEN s.provider_account_id IS NULL OR a.provider_id=m.provider_id "
      "THEN 1 ELSE 0 END FROM sessions s JOIN models m ON m.id=? "
      "LEFT JOIN provider_accounts a ON a.id=s.provider_account_id WHERE s.id=?;";
  sqlite3_stmt *stmt;
  int rc;
  int matches;

  if ((db == NULL) || (session_id <= 0LL) ||
      (model_id == NULL) || (model_id[0] == '\0')) {
    strappy_set_error(error_out,
                      "Session model account validation is incomplete.");
    return 0;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  if (rc != SQLITE_ROW) {
    if (rc == SQLITE_DONE) {
      strappy_set_error(error_out, "Session model account was not found.");
    } else {
      strappy_set_formatted_error(error_out,
                                  "Could not validate session model account: %s",
                                  sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    return 0;
  }
  matches = sqlite3_column_int(stmt, 0) ? 1 : 0;
  sqlite3_finalize(stmt);
  if (!matches) {
    strappy_set_error(
      error_out,
      "The selected model and conversation account use different providers.");
    return 0;
  }
  return 1;
}

int strappy_db_upsert_app_setting(sqlite3 *db,
                                         const char *key,
                                         const char *value,
                                         const char *error_prefix,
                                         char **error_out)
{
  sqlite3_stmt *semantic_stmt;
  int semantic_rc;

  (void)key;
  semantic_stmt = NULL;
  semantic_rc = sqlite3_prepare_v2(
    db,
    "UPDATE app_preferences SET default_model_id = ?, updated_at_ms = ? "
    "WHERE id = 1;",
    -1,
    &semantic_stmt,
    NULL);
  if ((semantic_rc != SQLITE_OK) ||
      (sqlite3_bind_text(semantic_stmt, 1, value, -1, SQLITE_TRANSIENT) !=
       SQLITE_OK) ||
      (sqlite3_bind_int64(semantic_stmt,
                          2,
                          (sqlite3_int64)strappy_db_now_ms()) != SQLITE_OK) ||
      (sqlite3_step(semantic_stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not update %s: %s",
                                error_prefix,
                                sqlite3_errmsg(db));
    sqlite3_finalize(semantic_stmt);
    return 0;
  }
  sqlite3_finalize(semantic_stmt);
  return 1;
}

static int strappy_db_copy_default_model(sqlite3 *db,
                                         char **model_id_out,
                                         char **error_out)
{
  static const char *sql =
    "SELECT " STRAPPY_DB_DEFAULT_MODEL_SQL ";";
  sqlite3_stmt *stmt;
  int rc;

  if (model_id_out == NULL) {
    strappy_set_error(error_out, "Default model output is missing.");
    return 0;
  }
  *model_id_out = NULL;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare default model lookup: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *model_id_out = strappy_db_column_string(stmt, 0);
    if ((*model_id_out == NULL) || ((*model_id_out)[0] == '\0')) {
      sqlite3_finalize(stmt);
      free(*model_id_out);
      *model_id_out =
        strappy_string_duplicate(STRAPPY_CONFIG_DEFAULT_MODEL_IDENTIFIER);
      if (*model_id_out == NULL) {
        strappy_set_error(error_out, "Could not allocate default model id.");
        return 0;
      }
      return 1;
    }
  } else if (rc == SQLITE_DONE) {
    *model_id_out =
      strappy_string_duplicate(STRAPPY_CONFIG_DEFAULT_MODEL_IDENTIFIER);
    if (*model_id_out == NULL) {
      sqlite3_finalize(stmt);
      strappy_set_error(error_out, "Could not allocate default model id.");
      return 0;
    }
  } else {
    strappy_set_formatted_error(error_out,
                                "Could not read default model lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

int strappy_db_model_is_effectively_allowed(sqlite3 *db,
                                                   const char *model_id,
                                                   int *allowed_out,
                                                   char **error_out)
{
  static const char *sql =
    "SELECT CASE WHEN ? = " STRAPPY_DB_DEFAULT_MODEL_SQL
    " OR EXISTS (SELECT 1 FROM models m JOIN model_preferences p "
    "ON p.provider_id = m.provider_id "
    "AND p.wire_model_id = m.wire_model_id "
    "WHERE m.id = ? AND p.allowed = 1) THEN 1 ELSE 0 END;";
  sqlite3_stmt *stmt;
  int rc;

  if (allowed_out == NULL) {
    strappy_set_error(error_out, "Allowed model output is missing.");
    return 0;
  }
  *allowed_out = 0;
  if ((model_id == NULL) || (model_id[0] == '\0')) {
    strappy_set_error(error_out, "Model id is empty.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare allowed model lookup: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  rc = sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, model_id, -1, SQLITE_TRANSIENT);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind allowed model lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *allowed_out = sqlite3_column_int(stmt, 0) ? 1 : 0;
    sqlite3_finalize(stmt);
    return 1;
  }

  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    return 1;
  }

  strappy_set_formatted_error(error_out,
                              "Could not read allowed model lookup: %s",
                              sqlite3_errmsg(db));
  return 0;
}

int strappy_db_set_model_allowed_in_db(sqlite3 *db,
                                       const char *model_id,
                                       int allowed,
                                       char **error_out)
{
  static const char *sql =
    "INSERT OR REPLACE INTO model_preferences "
    "(provider_id, wire_model_id, allowed, updated_at_ms) "
    "SELECT m.provider_id, m.wire_model_id, ?, ? "
    "FROM models m WHERE m.id = ?;";
  static const char *is_default_sql =
    "SELECT EXISTS (SELECT 1 FROM models m JOIN models d "
    "ON d.id = " STRAPPY_DB_DEFAULT_MODEL_SQL
    " WHERE m.id = ? AND m.provider_id = d.provider_id "
    "AND m.wire_model_id = d.wire_model_id);";
  sqlite3_stmt *stmt;
  int rc;

  if ((model_id == NULL) || (model_id[0] == '\0')) {
    strappy_set_error(error_out, "Model id is empty.");
    return 0;
  }

  if (!strappy_db_model_exists(db, model_id, error_out)) {
    return 0;
  }

  if (!allowed) {
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, is_default_sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
      rc = sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT);
    }
    if (rc == SQLITE_OK) {
      rc = sqlite3_step(stmt);
    }
    if ((rc != SQLITE_ROW) || sqlite3_column_int(stmt, 0)) {
      if ((rc != SQLITE_ROW) && ((error_out == NULL) || (*error_out == NULL))) {
        strappy_set_formatted_error(error_out,
                                    "Could not check default model: %s",
                                    sqlite3_errmsg(db));
      } else if (rc == SQLITE_ROW) {
        strappy_set_error(error_out, "The default model is always allowed.");
      }
      sqlite3_finalize(stmt);
      return 0;
    }
    sqlite3_finalize(stmt);
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare model setting: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  rc = sqlite3_bind_int(stmt, 1, allowed ? 1 : 0);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 2, (sqlite3_int64)strappy_db_now_ms());
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 3, model_id, -1, SQLITE_TRANSIENT);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind model setting: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save model setting: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

int strappy_db_set_model_allowed(const char *db_path,
                                 const char *model_id,
                                 int allowed,
                                 char **error_out)
{
  sqlite3 *db;
  int ok;

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  ok = strappy_db_set_model_allowed_in_db(db,
                                          model_id,
                                          allowed,
                                          error_out);
  strappy_db_release(db);
  return ok;
}

int strappy_db_set_default_model(const char *db_path,
                                 const char *model_id,
                                 char **error_out)
{
  sqlite3 *db;
  int ok;

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  if (!strappy_db_model_exists(db, model_id, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  ok = strappy_db_upsert_app_setting(db,
                                     STRAPPY_DB_DEFAULT_MODEL_KEY,
                                     model_id,
                                     "default model",
                                     error_out);
  if (ok) {
    ok = strappy_db_set_model_allowed_in_db(db,
                                            model_id,
                                            1,
                                            error_out);
  }

  strappy_db_release(db);
  return ok;
}

int strappy_db_get_default_model(const char *db_path,
                                 char **model_id_out,
                                 char **error_out)
{
  sqlite3 *db;
  int ok;

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  ok = strappy_db_copy_default_model(db, model_id_out, error_out);
  strappy_db_release(db);
  return ok;
}

int strappy_db_set_default_account_model(const char *db_path,
                                         const char *provider_account_id,
                                         const char *model_id,
                                         char **error_out)
{
  sqlite3 *db; sqlite3_stmt *stmt; int rc; int ok;
  if (!strappy_db_open(db_path,&db,error_out)) return 0;
  if (!strappy_db_ensure_schema(db,error_out)) { strappy_db_release(db); return 0; }
  stmt=NULL; rc=sqlite3_prepare_v2(db,
    "SELECT 1 FROM models m JOIN provider_accounts a ON a.provider_id=m.provider_id "
    "WHERE m.id=? AND a.id=? AND m.catalog_active=1 AND a.lifecycle_state='active';",-1,&stmt,NULL);
  if ((rc==SQLITE_OK)&&(sqlite3_bind_text(stmt,1,model_id,-1,SQLITE_TRANSIENT)==SQLITE_OK)&&
      (sqlite3_bind_text(stmt,2,provider_account_id,-1,SQLITE_TRANSIENT)==SQLITE_OK)) rc=sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc!=SQLITE_ROW) { strappy_db_release(db); strappy_set_error(error_out,"Default account and model do not match an active route."); return 0; }
  stmt=NULL; rc=sqlite3_prepare_v2(db,
    "UPDATE app_preferences SET default_model_id=?,default_provider_account_id=?,"
    "updated_at_ms=? WHERE id=1;",-1,&stmt,NULL);
  if ((rc==SQLITE_OK)&&(sqlite3_bind_text(stmt,1,model_id,-1,SQLITE_TRANSIENT)==SQLITE_OK)&&
      (sqlite3_bind_text(stmt,2,provider_account_id,-1,SQLITE_TRANSIENT)==SQLITE_OK)&&
      (sqlite3_bind_int64(stmt,3,(sqlite3_int64)strappy_db_now_ms())==SQLITE_OK)) rc=sqlite3_step(stmt);
  ok=rc==SQLITE_DONE; sqlite3_finalize(stmt);
  if (!ok) strappy_set_formatted_error(error_out,"Could not save default account model: %s",sqlite3_errmsg(db));
  if (ok) ok=strappy_db_set_model_allowed_in_db(db,model_id,1,error_out);
  strappy_db_release(db); return ok;
}

int strappy_db_get_default_account_model(const char *db_path,
                                         char **provider_account_id_out,
                                         char **model_id_out,
                                         char **error_out)
{
  sqlite3 *db; sqlite3_stmt *stmt; int rc;
  if ((provider_account_id_out==NULL)||(model_id_out==NULL)) { strappy_set_error(error_out,"Default account model output is missing."); return 0; }
  *provider_account_id_out=NULL; *model_id_out=NULL;
  if (!strappy_db_open(db_path,&db,error_out)) return 0;
  if (!strappy_db_ensure_schema(db,error_out)) { strappy_db_release(db); return 0; }
  stmt=NULL; rc=sqlite3_prepare_v2(db,
    "SELECT default_provider_account_id,default_model_id FROM app_preferences WHERE id=1;",-1,&stmt,NULL);
  if (rc==SQLITE_OK) rc=sqlite3_step(stmt);
  if (rc==SQLITE_ROW) { *provider_account_id_out=strappy_db_column_string(stmt,0); *model_id_out=strappy_db_column_string(stmt,1); }
  if ((*provider_account_id_out==NULL)||(*model_id_out==NULL)) { free(*provider_account_id_out); free(*model_id_out); *provider_account_id_out=NULL; *model_id_out=NULL; strappy_set_error(error_out,"Default account model is not configured."); }
  sqlite3_finalize(stmt); strappy_db_release(db); return (*model_id_out!=NULL);
}

int strappy_db_set_openrouter_model_allowed(const char *db_path,
                                            const char *model_id,
                                            int allowed,
                                            char **error_out)
{
  return strappy_db_set_model_allowed(db_path,
                                      model_id,
                                      allowed,
                                      error_out);
}

int strappy_db_set_default_openrouter_model(const char *db_path,
                                            const char *model_id,
                                            char **error_out)
{
  return strappy_db_set_default_model(db_path, model_id, error_out);
}

int strappy_db_get_default_openrouter_model(const char *db_path,
                                            char **model_id_out,
                                            char **error_out)
{
  return strappy_db_get_default_model(db_path, model_id_out, error_out);
}

int strappy_db_update_session_model(const char *db_path,
                                    long long session_id,
                                    const char *model_id,
                                    char **error_out)
{
  strappy_session_options options;

  strappy_session_options_init(&options);
  options.model_id = (char *)model_id;
  return strappy_db_update_session_options(db_path,
                                           session_id,
                                           &options,
                                           STRAPPY_SESSION_OPTION_MODEL,
                                           NULL,
                                           NULL,
                                           error_out);
}

int strappy_db_get_session_model(const char *db_path,
                                 long long session_id,
                                 char **model_id_out,
                                 char **error_out)
{
  static const char *sql =
    "SELECT COALESCE(NULLIF(s.model_id, ''), "
      STRAPPY_DB_DEFAULT_MODEL_SQL ") "
    "FROM sessions s WHERE s.id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (model_id_out == NULL) {
    strappy_set_error(error_out, "Session model output is missing.");
    return 0;
  }
  *model_id_out = NULL;
  if (session_id <= 0) {
    strappy_set_error(error_out, "Session id is not valid.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session model lookup: %s",
                                sqlite3_errmsg(db));
    strappy_db_release(db);
    return 0;
  }
  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session model lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *model_id_out = strappy_db_column_string(stmt, 0);
    if ((*model_id_out == NULL) || ((*model_id_out)[0] == '\0')) {
      free(*model_id_out);
      *model_id_out =
        strappy_string_duplicate(STRAPPY_CONFIG_DEFAULT_MODEL_IDENTIFIER);
    }
    if (*model_id_out == NULL) {
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_set_error(error_out, "Could not allocate session model id.");
      return 0;
    }
  } else if (rc == SQLITE_DONE) {
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    strappy_set_error(error_out, "Session was not found.");
    return 0;
  } else {
    strappy_set_formatted_error(error_out,
                                "Could not read session model lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }

  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
}

int strappy_db_get_session_model_route(
  const char *db_path,
  long long session_id,
  strappy_model_route_record *route,
  char **error_out)
{
  static const char *sql =
    "SELECT m.id, a.id, a.provider_id, a.responses_endpoint, m.wire_model_id, "
      "COALESCE(mc.billing_kind, 'metered_api'), "
      "COALESCE(mc.reasoning_enabled, 1), "
      "COALESCE(mc.local_functions_enabled, 1), "
      "COALESCE(mc.hosted_tools_enabled, 1) "
    "FROM sessions s JOIN models m ON m.id = "
      "COALESCE(NULLIF(s.model_id, ''), "
        STRAPPY_DB_DEFAULT_MODEL_SQL ") "
    "JOIN provider_accounts a ON a.id = COALESCE(NULLIF(s.provider_account_id,''),"
      "(SELECT default_provider_account_id FROM app_preferences WHERE id=1)) "
      "AND a.provider_id=m.provider_id "
    "LEFT JOIN model_capabilities mc ON mc.model_id = m.id "
    "WHERE s.id = ? AND a.lifecycle_state = 'active' "
      "AND m.catalog_active = 1 AND "
      "(m.id = " STRAPPY_DB_DEFAULT_MODEL_SQL " OR "
      "EXISTS (SELECT 1 FROM model_preferences mp "
        "WHERE mp.provider_id = m.provider_id "
        "AND mp.wire_model_id = m.wire_model_id AND mp.allowed = 1));";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (route == NULL) {
    strappy_set_error(error_out, "Session model route output is missing.");
    return 0;
  }
  strappy_model_route_record_init(route);
  if (session_id <= 0LL) {
    strappy_set_error(error_out, "Session id is not valid.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  if (!strappy_db_session_exists(db, session_id, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  if (rc != SQLITE_ROW) {
    if (rc == SQLITE_DONE) {
      strappy_set_error(
        error_out,
        "The selected session model is not active and allowed. Choose an "
        "allowed model without changing the conversation's account.");
    } else {
      strappy_set_formatted_error(error_out,
                                  "Could not read session model route: %s",
                                  sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  route->model_id = strappy_db_column_string(stmt, 0);
  route->provider_account_id = strappy_db_column_string(stmt, 1);
  route->provider_id = strappy_db_column_string(stmt, 2);
  route->responses_endpoint = strappy_db_column_string(stmt, 3);
  route->wire_model_id = strappy_db_column_string(stmt, 4);
  route->billing_kind = strappy_db_column_string(stmt, 5);
  route->reasoning_enabled = sqlite3_column_int(stmt, 6) ? 1 : 0;
  route->local_functions_enabled = sqlite3_column_int(stmt, 7) ? 1 : 0;
  route->hosted_tools_enabled = sqlite3_column_int(stmt, 8) ? 1 : 0;
  sqlite3_finalize(stmt);
  strappy_db_release(db);
  if ((route->model_id == NULL) ||
      (route->provider_account_id == NULL) ||
      (route->provider_id == NULL) ||
      (route->wire_model_id == NULL) ||
      (route->billing_kind == NULL)) {
    strappy_model_route_record_destroy(route);
    strappy_set_error(error_out, "Could not allocate session model route.");
    return 0;
  }
  return 1;
}
