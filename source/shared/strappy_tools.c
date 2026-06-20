#include "strappy_tools.h"

#include "strappy_core.h"
#include "strappy_db.h"

#include <cJSON.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>

#define STRAPPY_TOOLS_AVAILABILITY_ERROR "error"
#define STRAPPY_TOOLS_AVAILABILITY_SCAN_NEEDED "possible_scan_needed"
#define STRAPPY_TOOLS_AVAILABILITY_WHITELIST_NEEDED "possible_whitelist_needed"
#define STRAPPY_TOOLS_AVAILABILITY_AVAILABLE "available"
#define STRAPPY_DATABASE_MANAGE_HREF "strappy://database-manage"
#define STRAPPY_DATABASE_INFO_MAX_TABLES 64
#define STRAPPY_DATABASE_INFO_ROW_COUNT_MAX_SIZE_BYTES (10LL * 1024LL * 1024LL)

typedef struct strappy_tool_definition {
  const char *name;
  const char *description;
} strappy_tool_definition;

typedef struct strappy_tools_catalog_summary {
  int discovered_count;
  int valid_sqlite_count;
  int approved_count;
  int pending_approval_count;
  int denied_count;
} strappy_tools_catalog_summary;

static const strappy_tool_definition strappy_tool_definitions[] = {
  {
    STRAPPY_TOOL_DATABASE_LIST_INFO,
    "List all user-approved SQLite databases available to Strappy and include "
    "safe file metadata, deterministic schema facts, learned documentation "
    "when available, availability state, and recommended next steps without "
    "full filesystem paths."
  }
};

static const size_t strappy_tool_definition_count =
  sizeof(strappy_tool_definitions) / sizeof(strappy_tool_definitions[0]);

static int strappy_tools_add_empty_parameters(cJSON *function)
{
  cJSON *parameters;
  cJSON *properties;
  cJSON *required;
  cJSON *additional_properties;

  if (function == NULL) {
    return 0;
  }

  parameters = cJSON_CreateObject();
  properties = cJSON_CreateObject();
  required = cJSON_CreateArray();
  additional_properties = cJSON_CreateBool(0);
  if ((parameters == NULL) || (properties == NULL) || (required == NULL) ||
      (additional_properties == NULL)) {
    cJSON_Delete(parameters);
    cJSON_Delete(properties);
    cJSON_Delete(required);
    cJSON_Delete(additional_properties);
    return 0;
  }

  if (cJSON_AddStringToObject(parameters, "type", "object") == NULL) {
    cJSON_Delete(parameters);
    cJSON_Delete(properties);
    cJSON_Delete(required);
    cJSON_Delete(additional_properties);
    return 0;
  }

  if (!cJSON_AddItemToObject(parameters, "properties", properties)) {
    cJSON_Delete(parameters);
    cJSON_Delete(properties);
    cJSON_Delete(required);
    cJSON_Delete(additional_properties);
    return 0;
  }
  properties = NULL;

  if (!cJSON_AddItemToObject(parameters, "required", required)) {
    cJSON_Delete(parameters);
    cJSON_Delete(required);
    cJSON_Delete(additional_properties);
    return 0;
  }
  required = NULL;

  if (!cJSON_AddItemToObject(parameters,
                             "additionalProperties",
                             additional_properties)) {
    cJSON_Delete(parameters);
    cJSON_Delete(additional_properties);
    return 0;
  }
  additional_properties = NULL;

  if (!cJSON_AddItemToObject(function, "parameters", parameters)) {
    cJSON_Delete(parameters);
    return 0;
  }

  return 1;
}

static cJSON *strappy_tools_create_tool_schema(
  const strappy_tool_definition *definition)
{
  cJSON *tool;
  cJSON *function;

  if (definition == NULL) {
    return NULL;
  }

  tool = cJSON_CreateObject();
  function = cJSON_CreateObject();
  if ((tool == NULL) || (function == NULL)) {
    cJSON_Delete(tool);
    cJSON_Delete(function);
    return NULL;
  }

  if ((cJSON_AddStringToObject(tool, "type", "function") == NULL) ||
      (cJSON_AddStringToObject(function, "name", definition->name) == NULL) ||
      (cJSON_AddStringToObject(function,
                               "description",
                               definition->description) == NULL) ||
      !strappy_tools_add_empty_parameters(function) ||
      !cJSON_AddItemToObject(tool, "function", function)) {
    cJSON_Delete(tool);
    cJSON_Delete(function);
    return NULL;
  }

  return tool;
}

char *strappy_tools_request_json(char **error_out)
{
  cJSON *tools;
  char *json;
  size_t index;

  tools = cJSON_CreateArray();
  if (tools == NULL) {
    strappy_set_error(error_out, "Could not allocate tool schema list.");
    return NULL;
  }

  for (index = 0U; index < strappy_tool_definition_count; index++) {
    cJSON *tool;

    tool = strappy_tools_create_tool_schema(&strappy_tool_definitions[index]);
    if ((tool == NULL) || !cJSON_AddItemToArray(tools, tool)) {
      cJSON_Delete(tool);
      cJSON_Delete(tools);
      strappy_set_error(error_out, "Could not build tool schema list.");
      return NULL;
    }
  }

  json = cJSON_PrintUnformatted(tools);
  cJSON_Delete(tools);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize tool schema list.");
    return NULL;
  }

  return json;
}

char *strappy_tools_prompt_fragment(char **error_out)
{
  (void)error_out;
  return strappy_string_duplicate(
    "- database_list_info: Lists all valid SQLite databases the user has "
    "approved for Strappy and includes safe file metadata, schema facts, "
    "learned documentation, and next-step hints. Takes no arguments. Use the "
    "returned database_id values for database-specific tools. Results use "
    "filename, not raw filesystem path. If a database has no learned info, "
    "call database_learn for that database_id before database_query. If "
    "learned info exists, database_query may be used for concrete user "
    "questions. If no database is available, guide the user to the returned "
    "database management link.");
}

static int strappy_tools_validate_empty_arguments(const char *arguments_json,
                                                  char **error_out)
{
  cJSON *root;

  if ((arguments_json == NULL) || (arguments_json[0] == '\0')) {
    return 1;
  }

  root = cJSON_Parse(arguments_json);
  if (root == NULL) {
    strappy_set_error(error_out, "Tool arguments are not valid JSON.");
    return 0;
  }

  if (!cJSON_IsObject(root) || (root->child != NULL)) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "database_list_info takes no arguments.");
    return 0;
  }

  cJSON_Delete(root);
  return 1;
}

static const char *strappy_tools_path_basename(const char *path)
{
  const char *slash;

  if ((path == NULL) || (path[0] == '\0')) {
    return NULL;
  }

  slash = strrchr(path, '/');
  if ((slash == NULL) || (*(slash + 1) == '\0')) {
    return path;
  }

  return slash + 1;
}

static int strappy_tools_add_database_metadata(
  cJSON *object,
  const strappy_discovered_database_record *record)
{
  const char *filename;

  if ((object == NULL) || (record == NULL)) {
    return 0;
  }

  filename = strappy_tools_path_basename(record->path);
  if ((cJSON_AddStringToObject(object,
                               "database_id",
                               record->assistant_database_id) == NULL) ||
      ((filename != NULL) &&
       (cJSON_AddStringToObject(object,
                                "filename",
                                filename) == NULL)) ||
      (cJSON_AddNumberToObject(object,
                               "size_bytes",
                               (double)record->size) == NULL) ||
      (cJSON_AddNumberToObject(object,
                               "modified_at",
                               (double)record->modified_at) == NULL) ||
      ((record->scan_status != NULL) &&
       (cJSON_AddStringToObject(object,
                                "scan_status",
                                record->scan_status) == NULL)) ||
      ((record->first_seen_at != NULL) &&
       (cJSON_AddStringToObject(object,
                                "first_seen_at",
                                record->first_seen_at) == NULL)) ||
      ((record->last_seen_at != NULL) &&
       (cJSON_AddStringToObject(object,
                                "last_seen_at",
                                record->last_seen_at) == NULL)) ||
      ((record->last_scanned_at != NULL) &&
       (cJSON_AddStringToObject(object,
                                "last_scanned_at",
                                record->last_scanned_at) == NULL))) {
    return 0;
  }

  return 1;
}

static void strappy_tools_catalog_summary_init(
  strappy_tools_catalog_summary *summary)
{
  if (summary == NULL) {
    return;
  }

  summary->discovered_count = 0;
  summary->valid_sqlite_count = 0;
  summary->approved_count = 0;
  summary->pending_approval_count = 0;
  summary->denied_count = 0;
}

static int strappy_tools_record_has_database_id(
  const strappy_discovered_database_record *record)
{
  return ((record != NULL) &&
          (record->assistant_database_id != NULL) &&
          (record->assistant_database_id[0] != '\0')) ? 1 : 0;
}

static int strappy_tools_record_decision_is(
  const strappy_discovered_database_record *record,
  const char *decision)
{
  return ((record != NULL) &&
          (record->user_decision != NULL) &&
          (decision != NULL) &&
          (strcmp(record->user_decision, decision) == 0)) ? 1 : 0;
}

static int strappy_tools_record_is_available_database(
  const strappy_discovered_database_record *record)
{
  return ((record != NULL) &&
          record->is_valid_sqlite &&
          strappy_tools_record_has_database_id(record) &&
          strappy_tools_record_decision_is(record, "allowed")) ? 1 : 0;
}

static void strappy_tools_catalog_summary_add_record(
  strappy_tools_catalog_summary *summary,
  const strappy_discovered_database_record *record)
{
  if ((summary == NULL) || (record == NULL)) {
    return;
  }

  summary->discovered_count++;
  if (!record->is_valid_sqlite) {
    return;
  }

  summary->valid_sqlite_count++;
  if (strappy_tools_record_decision_is(record, "allowed") &&
      strappy_tools_record_has_database_id(record)) {
    summary->approved_count++;
  } else if (strappy_tools_record_decision_is(record, "denied")) {
    summary->denied_count++;
  } else {
    summary->pending_approval_count++;
  }
}

static const char *strappy_tools_availability_state(
  const strappy_tools_catalog_summary *summary)
{
  if (summary == NULL) {
    return STRAPPY_TOOLS_AVAILABILITY_ERROR;
  }

  if (summary->approved_count > 0) {
    return STRAPPY_TOOLS_AVAILABILITY_AVAILABLE;
  }

  if (summary->valid_sqlite_count > 0) {
    return STRAPPY_TOOLS_AVAILABILITY_WHITELIST_NEEDED;
  }

  return STRAPPY_TOOLS_AVAILABILITY_SCAN_NEEDED;
}

static int strappy_tools_add_bool_to_object(cJSON *object,
                                            const char *key,
                                            int value)
{
  cJSON *item;

  if ((object == NULL) || (key == NULL)) {
    return 0;
  }

  item = cJSON_CreateBool(value ? 1 : 0);
  if (item == NULL) {
    return 0;
  }

  if (!cJSON_AddItemToObject(object, key, item)) {
    cJSON_Delete(item);
    return 0;
  }

  return 1;
}

static int strappy_tools_add_catalog_summary(
  cJSON *root,
  const strappy_tools_catalog_summary *summary)
{
  cJSON *object;

  if ((root == NULL) || (summary == NULL)) {
    return 0;
  }

  object = cJSON_CreateObject();
  if (object == NULL) {
    return 0;
  }

  if ((cJSON_AddNumberToObject(object,
                               "discovered_count",
                               (double)summary->discovered_count) == NULL) ||
      (cJSON_AddNumberToObject(object,
                               "valid_sqlite_count",
                               (double)summary->valid_sqlite_count) == NULL) ||
      (cJSON_AddNumberToObject(object,
                               "approved_count",
                               (double)summary->approved_count) == NULL) ||
      (cJSON_AddNumberToObject(object,
                               "pending_approval_count",
                               (double)summary->pending_approval_count) == NULL) ||
      (cJSON_AddNumberToObject(object,
                               "denied_count",
                               (double)summary->denied_count) == NULL) ||
      !cJSON_AddItemToObject(root, "catalog_summary", object)) {
    cJSON_Delete(object);
    return 0;
  }

  return 1;
}

static cJSON *strappy_tools_create_database_manage_step(const char *purpose)
{
  cJSON *step;

  step = cJSON_CreateObject();
  if (step == NULL) {
    return NULL;
  }

  if ((cJSON_AddStringToObject(step, "type", "user_action") == NULL) ||
      (cJSON_AddStringToObject(step, "action", "database_manage") == NULL) ||
      (cJSON_AddStringToObject(step, "label", "Manage databases") == NULL) ||
      (cJSON_AddStringToObject(step,
                               "href",
                               STRAPPY_DATABASE_MANAGE_HREF) == NULL) ||
      !strappy_tools_add_bool_to_object(step,
                                        "requires_user_click",
                                        1) ||
      ((purpose != NULL) &&
       (cJSON_AddStringToObject(step, "purpose", purpose) == NULL))) {
    cJSON_Delete(step);
    return NULL;
  }

  return step;
}

static int strappy_tools_add_recommended_next_steps(
  cJSON *root,
  const char *availability_state)
{
  cJSON *steps;
  cJSON *step;

  if ((root == NULL) || (availability_state == NULL)) {
    return 0;
  }

  steps = cJSON_CreateArray();
  if (steps == NULL) {
    return 0;
  }

  if (strcmp(availability_state, STRAPPY_TOOLS_AVAILABILITY_SCAN_NEEDED) == 0) {
    step = strappy_tools_create_database_manage_step(
      "Ask the user to open database management and scan for local SQLite "
      "databases.");
    if ((step == NULL) || !cJSON_AddItemToArray(steps, step)) {
      cJSON_Delete(step);
      cJSON_Delete(steps);
      return 0;
    }
  } else if (strcmp(availability_state,
                    STRAPPY_TOOLS_AVAILABILITY_WHITELIST_NEEDED) == 0) {
    step = strappy_tools_create_database_manage_step(
      "Ask the user to approve which discovered databases Strappy may use.");
    if ((step == NULL) || !cJSON_AddItemToArray(steps, step)) {
      cJSON_Delete(step);
      cJSON_Delete(steps);
      return 0;
    }
  }

  if (!cJSON_AddItemToObject(root, "recommended_next_steps", steps)) {
    cJSON_Delete(steps);
    return 0;
  }

  return 1;
}

static int strappy_tools_add_null_to_object(cJSON *object, const char *key)
{
  cJSON *item;

  if ((object == NULL) || (key == NULL)) {
    return 0;
  }

  item = cJSON_CreateNull();
  if (item == NULL) {
    return 0;
  }

  if (!cJSON_AddItemToObject(object, key, item)) {
    cJSON_Delete(item);
    return 0;
  }

  return 1;
}

static int strappy_tools_add_optional_string_to_object(cJSON *object,
                                                       const char *key,
                                                       const char *value)
{
  if ((object == NULL) || (key == NULL)) {
    return 0;
  }

  if ((value == NULL) || (value[0] == '\0')) {
    return 1;
  }

  return (cJSON_AddStringToObject(object, key, value) != NULL) ? 1 : 0;
}

static int strappy_tools_string_has_value(const char *value)
{
  return ((value != NULL) && (value[0] != '\0')) ? 1 : 0;
}

static char *strappy_tools_sqlite_quote_identifier(const char *identifier)
{
  size_t length;
  size_t quote_count;
  size_t index;
  size_t output_index;
  char *quoted;

  if (identifier == NULL) {
    return NULL;
  }

  length = strlen(identifier);
  quote_count = 0U;
  for (index = 0U; index < length; index++) {
    if (identifier[index] == '"') {
      quote_count++;
    }
  }

  if (length > (((size_t)-1) - quote_count - 3U)) {
    return NULL;
  }

  quoted = (char *)malloc(length + quote_count + 3U);
  if (quoted == NULL) {
    return NULL;
  }

  output_index = 0U;
  quoted[output_index++] = '"';
  for (index = 0U; index < length; index++) {
    if (identifier[index] == '"') {
      quoted[output_index++] = '"';
    }
    quoted[output_index++] = identifier[index];
  }
  quoted[output_index++] = '"';
  quoted[output_index] = '\0';

  return quoted;
}

static char *strappy_tools_build_identifier_sql(const char *prefix,
                                                const char *identifier,
                                                const char *suffix)
{
  char *quoted_identifier;
  char *sql;
  size_t prefix_length;
  size_t quoted_length;
  size_t suffix_length;
  size_t total_length;

  if ((prefix == NULL) || (identifier == NULL) || (suffix == NULL)) {
    return NULL;
  }

  quoted_identifier = strappy_tools_sqlite_quote_identifier(identifier);
  if (quoted_identifier == NULL) {
    return NULL;
  }

  prefix_length = strlen(prefix);
  quoted_length = strlen(quoted_identifier);
  suffix_length = strlen(suffix);
  if ((prefix_length > (((size_t)-1) - quoted_length)) ||
      ((prefix_length + quoted_length) > (((size_t)-1) - suffix_length - 1U))) {
    free(quoted_identifier);
    return NULL;
  }

  total_length = prefix_length + quoted_length + suffix_length;
  sql = (char *)malloc(total_length + 1U);
  if (sql == NULL) {
    free(quoted_identifier);
    return NULL;
  }

  memcpy(sql, prefix, prefix_length);
  memcpy(sql + prefix_length, quoted_identifier, quoted_length);
  memcpy(sql + prefix_length + quoted_length, suffix, suffix_length);
  sql[total_length] = '\0';

  free(quoted_identifier);
  return sql;
}

static const char *strappy_tools_sqlite_column_text(sqlite3_stmt *stmt,
                                                    int column)
{
  const unsigned char *value;

  value = sqlite3_column_text(stmt, column);
  if (value == NULL) {
    return NULL;
  }

  return (const char *)value;
}

static int strappy_tools_prepare_identifier_statement(sqlite3 *db,
                                                      const char *prefix,
                                                      const char *identifier,
                                                      const char *suffix,
                                                      sqlite3_stmt **stmt_out,
                                                      char **error_out)
{
  sqlite3_stmt *stmt;
  char *sql;
  int rc;

  if (stmt_out == NULL) {
    strappy_set_error(error_out, "SQLite statement output is missing.");
    return 0;
  }
  *stmt_out = NULL;

  sql = strappy_tools_build_identifier_sql(prefix, identifier, suffix);
  if (sql == NULL) {
    strappy_set_error(error_out, "Could not allocate SQLite statement.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  free(sql);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare SQLite statement: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  *stmt_out = stmt;
  return 1;
}

static int strappy_tools_add_table_columns(sqlite3 *db,
                                           cJSON *table_object,
                                           const char *table_name,
                                           char **error_out)
{
  sqlite3_stmt *stmt;
  cJSON *columns;
  int rc;

  columns = cJSON_CreateArray();
  if (columns == NULL) {
    strappy_set_error(error_out, "Could not allocate database columns.");
    return 0;
  }

  if (!strappy_tools_prepare_identifier_statement(db,
                                                  "PRAGMA table_info(",
                                                  table_name,
                                                  ");",
                                                  &stmt,
                                                  error_out)) {
    cJSON_Delete(columns);
    return 0;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    cJSON *column;
    const char *name;
    const char *type;
    const char *default_value;

    name = strappy_tools_sqlite_column_text(stmt, 1);
    type = strappy_tools_sqlite_column_text(stmt, 2);
    default_value = NULL;
    if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
      default_value = strappy_tools_sqlite_column_text(stmt, 4);
    }

    if (!strappy_tools_string_has_value(name)) {
      sqlite3_finalize(stmt);
      cJSON_Delete(columns);
      strappy_set_error(error_out, "Database column name is empty.");
      return 0;
    }

    column = cJSON_CreateObject();
    if (column == NULL) {
      sqlite3_finalize(stmt);
      cJSON_Delete(columns);
      strappy_set_error(error_out, "Could not allocate database column.");
      return 0;
    }

    if ((cJSON_AddNumberToObject(column,
                                 "ordinal",
                                 (double)sqlite3_column_int(stmt, 0)) == NULL) ||
        (cJSON_AddStringToObject(column, "name", name) == NULL) ||
        !strappy_tools_add_optional_string_to_object(column, "type", type) ||
        !strappy_tools_add_bool_to_object(column,
                                          "not_null",
                                          sqlite3_column_int(stmt, 3) ? 1 : 0) ||
        (cJSON_AddNumberToObject(column,
                                 "primary_key_position",
                                 (double)sqlite3_column_int(stmt, 5)) == NULL) ||
        !strappy_tools_add_optional_string_to_object(column,
                                                     "default_value",
                                                     default_value) ||
        !cJSON_AddItemToArray(columns, column)) {
      cJSON_Delete(column);
      sqlite3_finalize(stmt);
      cJSON_Delete(columns);
      strappy_set_error(error_out, "Could not build database column result.");
      return 0;
    }
  }

  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read database columns: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    cJSON_Delete(columns);
    return 0;
  }

  sqlite3_finalize(stmt);
  if (!cJSON_AddItemToObject(table_object, "columns", columns)) {
    cJSON_Delete(columns);
    strappy_set_error(error_out, "Could not build database columns result.");
    return 0;
  }

  return 1;
}

static int strappy_tools_add_index_columns(sqlite3 *db,
                                           cJSON *index_object,
                                           const char *index_name,
                                           char **error_out)
{
  sqlite3_stmt *stmt;
  cJSON *columns;
  int rc;

  columns = cJSON_CreateArray();
  if (columns == NULL) {
    strappy_set_error(error_out, "Could not allocate database index columns.");
    return 0;
  }

  if (!strappy_tools_prepare_identifier_statement(db,
                                                  "PRAGMA index_info(",
                                                  index_name,
                                                  ");",
                                                  &stmt,
                                                  error_out)) {
    cJSON_Delete(columns);
    return 0;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    const char *name;
    cJSON *column;

    name = strappy_tools_sqlite_column_text(stmt, 2);
    if (!strappy_tools_string_has_value(name)) {
      continue;
    }

    column = cJSON_CreateString(name);
    if ((column == NULL) || (cJSON_AddItemToArray(columns, column) == 0)) {
      cJSON_Delete(column);
      sqlite3_finalize(stmt);
      cJSON_Delete(columns);
      strappy_set_error(error_out,
                        "Could not build database index column result.");
      return 0;
    }
  }

  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read database index columns: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    cJSON_Delete(columns);
    return 0;
  }

  sqlite3_finalize(stmt);
  if (!cJSON_AddItemToObject(index_object, "columns", columns)) {
    cJSON_Delete(columns);
    strappy_set_error(error_out,
                      "Could not build database index columns result.");
    return 0;
  }

  return 1;
}

static int strappy_tools_add_table_indexes(sqlite3 *db,
                                           cJSON *table_object,
                                           const char *table_name,
                                           char **error_out)
{
  sqlite3_stmt *stmt;
  cJSON *indexes;
  int rc;
  int column_count;

  indexes = cJSON_CreateArray();
  if (indexes == NULL) {
    strappy_set_error(error_out, "Could not allocate database indexes.");
    return 0;
  }

  if (!strappy_tools_prepare_identifier_statement(db,
                                                  "PRAGMA index_list(",
                                                  table_name,
                                                  ");",
                                                  &stmt,
                                                  error_out)) {
    cJSON_Delete(indexes);
    return 0;
  }

  column_count = sqlite3_column_count(stmt);
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    cJSON *index_object;
    const char *name;
    const char *origin;

    name = strappy_tools_sqlite_column_text(stmt, 1);
    if (!strappy_tools_string_has_value(name)) {
      continue;
    }

    origin = NULL;
    if (column_count > 3) {
      origin = strappy_tools_sqlite_column_text(stmt, 3);
    }

    index_object = cJSON_CreateObject();
    if (index_object == NULL) {
      sqlite3_finalize(stmt);
      cJSON_Delete(indexes);
      strappy_set_error(error_out, "Could not allocate database index.");
      return 0;
    }

    if ((cJSON_AddStringToObject(index_object, "name", name) == NULL) ||
        !strappy_tools_add_bool_to_object(index_object,
                                          "unique",
                                          sqlite3_column_int(stmt, 2) ? 1 : 0) ||
        !strappy_tools_add_optional_string_to_object(index_object,
                                                     "origin",
                                                     origin) ||
        ((column_count > 4) &&
         !strappy_tools_add_bool_to_object(index_object,
                                           "partial",
                                           sqlite3_column_int(stmt, 4) ? 1 : 0)) ||
        !strappy_tools_add_index_columns(db, index_object, name, error_out) ||
        !cJSON_AddItemToArray(indexes, index_object)) {
      cJSON_Delete(index_object);
      sqlite3_finalize(stmt);
      cJSON_Delete(indexes);
      if ((error_out != NULL) && (*error_out == NULL)) {
        strappy_set_error(error_out, "Could not build database index result.");
      }
      return 0;
    }
  }

  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read database indexes: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    cJSON_Delete(indexes);
    return 0;
  }

  sqlite3_finalize(stmt);
  if (!cJSON_AddItemToObject(table_object, "indexes", indexes)) {
    cJSON_Delete(indexes);
    strappy_set_error(error_out, "Could not build database indexes result.");
    return 0;
  }

  return 1;
}

static int strappy_tools_add_table_foreign_keys(sqlite3 *db,
                                                cJSON *table_object,
                                                const char *table_name,
                                                char **error_out)
{
  sqlite3_stmt *stmt;
  cJSON *foreign_keys;
  int rc;

  foreign_keys = cJSON_CreateArray();
  if (foreign_keys == NULL) {
    strappy_set_error(error_out, "Could not allocate database foreign keys.");
    return 0;
  }

  if (!strappy_tools_prepare_identifier_statement(db,
                                                  "PRAGMA foreign_key_list(",
                                                  table_name,
                                                  ");",
                                                  &stmt,
                                                  error_out)) {
    cJSON_Delete(foreign_keys);
    return 0;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    cJSON *foreign_key;
    const char *target_table;
    const char *from_column;
    const char *to_column;
    const char *on_update;
    const char *on_delete;
    const char *match;

    target_table = strappy_tools_sqlite_column_text(stmt, 2);
    from_column = strappy_tools_sqlite_column_text(stmt, 3);
    to_column = strappy_tools_sqlite_column_text(stmt, 4);
    on_update = strappy_tools_sqlite_column_text(stmt, 5);
    on_delete = strappy_tools_sqlite_column_text(stmt, 6);
    match = strappy_tools_sqlite_column_text(stmt, 7);

    foreign_key = cJSON_CreateObject();
    if (foreign_key == NULL) {
      sqlite3_finalize(stmt);
      cJSON_Delete(foreign_keys);
      strappy_set_error(error_out, "Could not allocate database foreign key.");
      return 0;
    }

    if ((cJSON_AddNumberToObject(foreign_key,
                                 "id",
                                 (double)sqlite3_column_int(stmt, 0)) == NULL) ||
        (cJSON_AddNumberToObject(foreign_key,
                                 "seq",
                                 (double)sqlite3_column_int(stmt, 1)) == NULL) ||
        !strappy_tools_add_optional_string_to_object(foreign_key,
                                                     "table",
                                                     target_table) ||
        !strappy_tools_add_optional_string_to_object(foreign_key,
                                                     "from_column",
                                                     from_column) ||
        !strappy_tools_add_optional_string_to_object(foreign_key,
                                                     "to_column",
                                                     to_column) ||
        !strappy_tools_add_optional_string_to_object(foreign_key,
                                                     "on_update",
                                                     on_update) ||
        !strappy_tools_add_optional_string_to_object(foreign_key,
                                                     "on_delete",
                                                     on_delete) ||
        !strappy_tools_add_optional_string_to_object(foreign_key,
                                                     "match",
                                                     match) ||
        !cJSON_AddItemToArray(foreign_keys, foreign_key)) {
      cJSON_Delete(foreign_key);
      sqlite3_finalize(stmt);
      cJSON_Delete(foreign_keys);
      strappy_set_error(error_out,
                        "Could not build database foreign key result.");
      return 0;
    }
  }

  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read database foreign keys: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    cJSON_Delete(foreign_keys);
    return 0;
  }

  sqlite3_finalize(stmt);
  if (!cJSON_AddItemToObject(table_object, "foreign_keys", foreign_keys)) {
    cJSON_Delete(foreign_keys);
    strappy_set_error(error_out,
                      "Could not build database foreign keys result.");
    return 0;
  }

  return 1;
}

static int strappy_tools_add_table_row_count(sqlite3 *db,
                                             cJSON *table_object,
                                             const char *table_name,
                                             const char *table_type,
                                             int row_counts_enabled)
{
  sqlite3_stmt *stmt;
  int rc;

  if ((table_type != NULL) && (strcmp(table_type, "view") == 0)) {
    return strappy_tools_add_bool_to_object(table_object,
                                            "row_count_available",
                                            0) &&
           (cJSON_AddStringToObject(table_object,
                                    "row_count_status",
                                    "not_applicable_view") != NULL);
  }

  if (!row_counts_enabled) {
    return strappy_tools_add_bool_to_object(table_object,
                                            "row_count_available",
                                            0) &&
           (cJSON_AddStringToObject(table_object,
                                    "row_count_status",
                                    "skipped_large_database") != NULL);
  }

  if (!strappy_tools_prepare_identifier_statement(db,
                                                  "SELECT COUNT(*) FROM ",
                                                  table_name,
                                                  ";",
                                                  &stmt,
                                                  NULL)) {
    return strappy_tools_add_bool_to_object(table_object,
                                            "row_count_available",
                                            0) &&
           (cJSON_AddStringToObject(table_object,
                                    "row_count_status",
                                    "error") != NULL);
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    sqlite3_int64 count;

    count = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return strappy_tools_add_bool_to_object(table_object,
                                            "row_count_available",
                                            1) &&
           (cJSON_AddNumberToObject(table_object,
                                    "row_count",
                                    (double)count) != NULL) &&
           (cJSON_AddStringToObject(table_object,
                                    "row_count_status",
                                    "available") != NULL);
  }

  sqlite3_finalize(stmt);
  return strappy_tools_add_bool_to_object(table_object,
                                          "row_count_available",
                                          0) &&
         (cJSON_AddStringToObject(table_object,
                                  "row_count_status",
                                  "error") != NULL);
}

static int strappy_tools_open_readonly_database(const char *path,
                                                sqlite3 **db_out,
                                                char **error_out)
{
  sqlite3 *db;
  int rc;

  if (db_out == NULL) {
    strappy_set_error(error_out, "SQLite database output is missing.");
    return 0;
  }
  *db_out = NULL;

  if ((path == NULL) || (path[0] == '\0')) {
    strappy_set_error(error_out, "Cataloged database path is empty.");
    return 0;
  }

  db = NULL;
  rc = sqlite3_open_v2(path,
                       &db,
                       SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
                       NULL);
  if (rc != SQLITE_OK) {
    const char *message;

    message = "unknown sqlite error";
    if (db != NULL) {
      message = sqlite3_errmsg(db);
    }
    strappy_set_formatted_error(error_out,
                                "Could not open approved database read-only: %s",
                                message);
    if (db != NULL) {
      sqlite3_close(db);
    }
    return 0;
  }

  sqlite3_busy_timeout(db, 1000);
  *db_out = db;
  return 1;
}

static cJSON *strappy_tools_read_database_schema(
  const strappy_discovered_database_record *record,
  char **error_out)
{
  static const char *sql =
    "SELECT name, type FROM sqlite_master "
    "WHERE type IN ('table', 'view') "
    "AND name NOT LIKE 'sqlite_%' "
    "ORDER BY type, name "
    "LIMIT 65;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  cJSON *schema;
  cJSON *tables;
  int rc;
  int table_count;
  int truncated;
  int row_counts_enabled;

  if (record == NULL) {
    strappy_set_error(error_out, "Database record is missing.");
    return NULL;
  }

  if (!strappy_tools_open_readonly_database(record->path, &db, error_out)) {
    return NULL;
  }

  schema = cJSON_CreateObject();
  tables = cJSON_CreateArray();
  if ((schema == NULL) || (tables == NULL)) {
    cJSON_Delete(schema);
    cJSON_Delete(tables);
    sqlite3_close(db);
    strappy_set_error(error_out, "Could not allocate database schema result.");
    return NULL;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare database schema lookup: %s",
                                sqlite3_errmsg(db));
    cJSON_Delete(schema);
    cJSON_Delete(tables);
    sqlite3_close(db);
    return NULL;
  }

  table_count = 0;
  truncated = 0;
  row_counts_enabled =
    (record->size <= STRAPPY_DATABASE_INFO_ROW_COUNT_MAX_SIZE_BYTES) ? 1 : 0;

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    cJSON *table_object;
    const char *name;
    const char *type;

    if (table_count >= STRAPPY_DATABASE_INFO_MAX_TABLES) {
      truncated = 1;
      continue;
    }

    name = strappy_tools_sqlite_column_text(stmt, 0);
    type = strappy_tools_sqlite_column_text(stmt, 1);
    if (!strappy_tools_string_has_value(name)) {
      sqlite3_finalize(stmt);
      cJSON_Delete(schema);
      cJSON_Delete(tables);
      sqlite3_close(db);
      strappy_set_error(error_out, "Database table name is empty.");
      return NULL;
    }

    table_object = cJSON_CreateObject();
    if (table_object == NULL) {
      sqlite3_finalize(stmt);
      cJSON_Delete(schema);
      cJSON_Delete(tables);
      sqlite3_close(db);
      strappy_set_error(error_out, "Could not allocate database table result.");
      return NULL;
    }

    if ((cJSON_AddStringToObject(table_object, "name", name) == NULL) ||
        !strappy_tools_add_optional_string_to_object(table_object,
                                                     "type",
                                                     type) ||
        !strappy_tools_add_table_columns(db, table_object, name, error_out) ||
        !strappy_tools_add_table_indexes(db, table_object, name, error_out) ||
        !strappy_tools_add_table_foreign_keys(db,
                                              table_object,
                                              name,
                                              error_out) ||
        !strappy_tools_add_table_row_count(db,
                                           table_object,
                                           name,
                                           type,
                                           row_counts_enabled) ||
        !cJSON_AddItemToArray(tables, table_object)) {
      cJSON_Delete(table_object);
      sqlite3_finalize(stmt);
      cJSON_Delete(schema);
      cJSON_Delete(tables);
      sqlite3_close(db);
      if ((error_out != NULL) && (*error_out == NULL)) {
        strappy_set_error(error_out, "Could not build database schema result.");
      }
      return NULL;
    }

    table_count++;
  }

  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read database schema: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    cJSON_Delete(schema);
    cJSON_Delete(tables);
    sqlite3_close(db);
    return NULL;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);

  if (!cJSON_AddItemToObject(schema, "tables", tables)) {
    cJSON_Delete(schema);
    cJSON_Delete(tables);
    strappy_set_error(error_out, "Could not build database schema result.");
    return NULL;
  }
  tables = NULL;

  if ((cJSON_AddNumberToObject(schema,
                               "table_count",
                               (double)table_count) == NULL) ||
      (cJSON_AddNumberToObject(schema,
                               "max_tables_returned",
                               (double)STRAPPY_DATABASE_INFO_MAX_TABLES) == NULL) ||
      !strappy_tools_add_bool_to_object(schema, "truncated", truncated) ||
      !strappy_tools_add_bool_to_object(schema,
                                        "row_counts_enabled",
                                        row_counts_enabled) ||
      (cJSON_AddStringToObject(schema,
                               "row_count_policy",
                               row_counts_enabled ?
                                 "exact_for_returned_tables" :
                                 "skipped_for_large_database") == NULL)) {
    cJSON_Delete(schema);
    strappy_set_error(error_out, "Could not build database schema result.");
    return NULL;
  }

  return schema;
}

static int strappy_tools_documentation_has_learned_info(
  const strappy_database_documentation_record *record,
  int found)
{
  if (!found || (record == NULL)) {
    return 0;
  }

  return (strappy_tools_string_has_value(record->schema_summary) ||
          strappy_tools_string_has_value(record->table_descriptions_json) ||
          strappy_tools_string_has_value(record->column_descriptions_json) ||
          strappy_tools_string_has_value(record->inferred_purpose) ||
          strappy_tools_string_has_value(record->sensitivity_notes) ||
          strappy_tools_string_has_value(record->suggested_query_examples_json)) ? 1 : 0;
}

static int strappy_tools_add_learned_info(
  cJSON *root,
  const strappy_database_documentation_record *record,
  int found,
  int *has_learned_info_out)
{
  cJSON *learned_info;
  int has_learned_info;

  if (root == NULL) {
    return 0;
  }

  has_learned_info =
    strappy_tools_documentation_has_learned_info(record, found);
  if (has_learned_info_out != NULL) {
    *has_learned_info_out = has_learned_info;
  }

  if (!strappy_tools_add_bool_to_object(root,
                                        "has_learned_info",
                                        has_learned_info)) {
    return 0;
  }

  if (!has_learned_info) {
    return strappy_tools_add_null_to_object(root, "learned_info");
  }

  learned_info = cJSON_CreateObject();
  if (learned_info == NULL) {
    return 0;
  }

  if (!strappy_tools_add_optional_string_to_object(learned_info,
                                                   "schema_summary",
                                                   record->schema_summary) ||
      !strappy_tools_add_optional_string_to_object(
        learned_info,
        "table_descriptions_json",
        record->table_descriptions_json) ||
      !strappy_tools_add_optional_string_to_object(
        learned_info,
        "column_descriptions_json",
        record->column_descriptions_json) ||
      !strappy_tools_add_optional_string_to_object(learned_info,
                                                   "inferred_purpose",
                                                   record->inferred_purpose) ||
      !strappy_tools_add_optional_string_to_object(learned_info,
                                                   "sensitivity_notes",
                                                   record->sensitivity_notes) ||
      !strappy_tools_add_optional_string_to_object(
        learned_info,
        "suggested_query_examples_json",
        record->suggested_query_examples_json) ||
      !strappy_tools_add_optional_string_to_object(learned_info,
                                                   "last_learned_at",
                                                   record->last_learned_at) ||
      !cJSON_AddItemToObject(root, "learned_info", learned_info)) {
    cJSON_Delete(learned_info);
    return 0;
  }

  return 1;
}

static cJSON *strappy_tools_create_database_tool_step(const char *tool_name,
                                                      const char *database_id,
                                                      const char *purpose)
{
  cJSON *step;
  cJSON *arguments;

  step = cJSON_CreateObject();
  arguments = cJSON_CreateObject();
  if ((step == NULL) || (arguments == NULL)) {
    cJSON_Delete(step);
    cJSON_Delete(arguments);
    return NULL;
  }

  if ((cJSON_AddStringToObject(step, "type", "tool_call") == NULL) ||
      (cJSON_AddStringToObject(step, "tool", tool_name) == NULL) ||
      ((purpose != NULL) &&
       (cJSON_AddStringToObject(step, "purpose", purpose) == NULL)) ||
      (cJSON_AddStringToObject(arguments, "database_id", database_id) == NULL) ||
      !cJSON_AddItemToObject(step, "arguments", arguments)) {
    cJSON_Delete(step);
    cJSON_Delete(arguments);
    return NULL;
  }
  arguments = NULL;

  return step;
}

static int strappy_tools_add_database_next_steps(cJSON *root,
                                                 const char *database_id,
                                                 int has_learned_info)
{
  cJSON *steps;
  cJSON *step;

  if ((root == NULL) || (database_id == NULL)) {
    return 0;
  }

  steps = cJSON_CreateArray();
  if (steps == NULL) {
    return 0;
  }

  if (has_learned_info) {
    step = strappy_tools_create_database_tool_step(
      STRAPPY_TOOL_DATABASE_QUERY,
      database_id,
      "Use database_query with this database_id when answering a concrete "
      "user question from this database.");
  } else {
    step = strappy_tools_create_database_tool_step(
      STRAPPY_TOOL_DATABASE_LEARN,
      database_id,
      "Call database_learn for this database_id before querying it.");
  }

  if ((step == NULL) || !cJSON_AddItemToArray(steps, step)) {
    cJSON_Delete(step);
    cJSON_Delete(steps);
    return 0;
  }

  if (!cJSON_AddItemToObject(root, "recommended_next_steps", steps)) {
    cJSON_Delete(steps);
    return 0;
  }

  return 1;
}

static int strappy_tools_add_database_list_info_record(
  const char *session_db_path,
  cJSON *databases,
  const strappy_discovered_database_record *record,
  char **error_out)
{
  strappy_database_documentation_record documentation;
  cJSON *object;
  cJSON *schema;
  char *schema_error;
  int documentation_found;
  int has_learned_info;

  if ((session_db_path == NULL) || (databases == NULL) || (record == NULL)) {
    strappy_set_error(error_out, "Database list info request is incomplete.");
    return 0;
  }

  object = cJSON_CreateObject();
  if (object == NULL) {
    strappy_set_error(error_out, "Could not allocate database info result.");
    return 0;
  }

  strappy_database_documentation_record_init(&documentation);
  documentation_found = 0;
  if (!strappy_db_load_database_documentation(session_db_path,
                                             record->assistant_database_id,
                                             &documentation,
                                             &documentation_found,
                                             error_out)) {
    cJSON_Delete(object);
    return 0;
  }

  if (!strappy_tools_add_database_metadata(object, record)) {
    cJSON_Delete(object);
    strappy_database_documentation_record_destroy(&documentation);
    strappy_set_error(error_out, "Could not build database metadata result.");
    return 0;
  }

  schema_error = NULL;
  schema = strappy_tools_read_database_schema(record, &schema_error);
  if (schema != NULL) {
    if (!cJSON_AddItemToObject(object, "schema", schema)) {
      cJSON_Delete(schema);
      cJSON_Delete(object);
      strappy_database_documentation_record_destroy(&documentation);
      free(schema_error);
      strappy_set_error(error_out, "Could not build database schema result.");
      return 0;
    }
    schema = NULL;
  } else if (!strappy_tools_add_null_to_object(object, "schema") ||
             (cJSON_AddStringToObject(
                object,
                "schema_error",
                (schema_error != NULL) ?
                  schema_error : "Could not read database schema.") == NULL)) {
    cJSON_Delete(object);
    strappy_database_documentation_record_destroy(&documentation);
    free(schema_error);
    strappy_set_error(error_out, "Could not build database schema result.");
    return 0;
  }
  free(schema_error);

  has_learned_info = 0;
  if (!strappy_tools_add_learned_info(object,
                                      &documentation,
                                      documentation_found,
                                      &has_learned_info) ||
      !strappy_tools_add_database_next_steps(object,
                                             record->assistant_database_id,
                                             has_learned_info)) {
    cJSON_Delete(object);
    strappy_database_documentation_record_destroy(&documentation);
    strappy_set_error(error_out, "Could not build database info result.");
    return 0;
  }

  strappy_database_documentation_record_destroy(&documentation);
  if (!cJSON_AddItemToArray(databases, object)) {
    cJSON_Delete(object);
    strappy_set_error(error_out, "Could not build database list result.");
    return 0;
  }

  return 1;
}

static char *strappy_tools_execute_database_list_info(
  const char *session_db_path,
  const char *arguments_json,
  char **error_out)
{
  strappy_discovered_database_record_list list;
  strappy_tools_catalog_summary summary;
  cJSON *root;
  cJSON *databases;
  const char *availability_state;
  char *json;
  size_t index;
  int count;

  if ((session_db_path == NULL) || (session_db_path[0] == '\0')) {
    strappy_set_error(error_out, "Catalog database path is not configured.");
    return NULL;
  }

  if (!strappy_tools_validate_empty_arguments(arguments_json, error_out)) {
    return NULL;
  }

  strappy_discovered_database_record_list_init(&list);
  if (!strappy_db_list_discovered_databases(session_db_path,
                                           &list,
                                           error_out)) {
    return NULL;
  }

  root = cJSON_CreateObject();
  databases = cJSON_CreateArray();
  if ((root == NULL) || (databases == NULL)) {
    cJSON_Delete(root);
    cJSON_Delete(databases);
    strappy_discovered_database_record_list_destroy(&list);
    strappy_set_error(error_out, "Could not allocate tool result.");
    return NULL;
  }

  count = 0;
  strappy_tools_catalog_summary_init(&summary);
  for (index = 0U; index < list.count; index++) {
    const strappy_discovered_database_record *record;

    record = &list.records[index];
    strappy_tools_catalog_summary_add_record(&summary, record);
    if (!strappy_tools_record_is_available_database(record)) {
      continue;
    }

    if (!strappy_tools_add_database_list_info_record(session_db_path,
                                                     databases,
                                                     record,
                                                     error_out)) {
      cJSON_Delete(root);
      cJSON_Delete(databases);
      strappy_discovered_database_record_list_destroy(&list);
      return NULL;
    }

    count++;
  }
  availability_state = strappy_tools_availability_state(&summary);

  if (!cJSON_AddItemToObject(root, "databases", databases)) {
    cJSON_Delete(root);
    cJSON_Delete(databases);
    strappy_discovered_database_record_list_destroy(&list);
    strappy_set_error(error_out, "Could not build tool result.");
    return NULL;
  }
  databases = NULL;

  if ((cJSON_AddNumberToObject(root, "count", (double)count) == NULL) ||
      (cJSON_AddStringToObject(root,
                               "availability_state",
                               availability_state) == NULL) ||
      !strappy_tools_add_catalog_summary(root, &summary) ||
      !strappy_tools_add_recommended_next_steps(root, availability_state)) {
    cJSON_Delete(root);
    strappy_discovered_database_record_list_destroy(&list);
    strappy_set_error(error_out, "Could not build tool result.");
    return NULL;
  }

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  strappy_discovered_database_record_list_destroy(&list);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize tool result.");
    return NULL;
  }

  return json;
}

char *strappy_tools_execute(const char *session_db_path,
                            const char *tool_name,
                            const char *arguments_json,
                            char **error_out)
{
  if ((tool_name == NULL) || (tool_name[0] == '\0')) {
    strappy_set_error(error_out, "Tool name is empty.");
    return NULL;
  }

  if (strcmp(tool_name, STRAPPY_TOOL_DATABASE_LIST_INFO) == 0) {
    return strappy_tools_execute_database_list_info(session_db_path,
                                                   arguments_json,
                                                   error_out);
  }

  strappy_set_formatted_error(error_out, "Tool is not registered: %s", tool_name);
  return NULL;
}
