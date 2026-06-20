#include "strappy_tools.h"

#include "strappy_cocoa.h"
#include "strappy_core.h"
#include "strappy_db.h"

#include <cJSON.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STRAPPY_TOOLS_AVAILABILITY_ERROR "error"
#define STRAPPY_TOOLS_AVAILABILITY_SCAN_NEEDED "possible_scan_needed"
#define STRAPPY_TOOLS_AVAILABILITY_WHITELIST_NEEDED "possible_whitelist_needed"
#define STRAPPY_TOOLS_AVAILABILITY_AVAILABLE "available"
#define STRAPPY_DATABASE_MANAGE_HREF "strappy://database-manage"
#define STRAPPY_DATABASE_INFO_MAX_TABLES 64
#define STRAPPY_DATABASE_INFO_ROW_COUNT_MAX_SIZE_BYTES (10LL * 1024LL * 1024LL)
#define STRAPPY_DATABASE_QUERY_MAX_ROWS 100
#define STRAPPY_DATABASE_QUERY_MAX_COLUMNS 64
#define STRAPPY_DATABASE_QUERY_MAX_SQL_BYTES 8192U
#define STRAPPY_DATABASE_QUERY_MAX_CELL_BYTES 4096U
#define STRAPPY_DATABASE_QUERY_MAX_PAYLOAD_BYTES (48U * 1024U)
#define STRAPPY_DATABASE_QUERY_MAX_RESULT_BYTES (64U * 1024U)
#define STRAPPY_DATABASE_QUERY_TIMEOUT_SECONDS 5.0
#define STRAPPY_DATABASE_QUERY_PROGRESS_INSTRUCTIONS 1000
#define STRAPPY_DATABASE_QUERY_MAX_PROGRESS_CALLS 20000
#define STRAPPY_DATABASE_QUERY_JSON_SAFE_INTEGER_MAX 9007199254740991LL
#define STRAPPY_DATABASE_QUERY_JSON_SAFE_INTEGER_MIN (-9007199254740991LL)
#define STRAPPY_HELPER_CONVERT_DATES_MAX_INPUT_BYTES 8192U
#define STRAPPY_HELPER_CONVERT_DATES_MAX_TIMESTAMPS 256U

typedef enum strappy_tool_kind {
  STRAPPY_TOOL_KIND_DATABASE = 1,
  STRAPPY_TOOL_KIND_HELPER = 2
} strappy_tool_kind;

typedef struct strappy_tool_definition {
  const char *name;
  const char *description;
  strappy_tool_kind kind;
} strappy_tool_definition;

typedef struct strappy_tools_catalog_summary {
  int discovered_count;
  int valid_sqlite_count;
  int approved_count;
  int pending_approval_count;
  int denied_count;
} strappy_tools_catalog_summary;

typedef struct strappy_database_query_arguments {
  char *database_id;
  char *sql;
} strappy_database_query_arguments;

typedef struct strappy_helper_convert_dates_arguments {
  char *timestamps;
  strappy_cocoa_timestamp_unit unit;
} strappy_helper_convert_dates_arguments;

typedef struct strappy_helper_text_buffer {
  char *data;
  size_t length;
} strappy_helper_text_buffer;

typedef struct strappy_database_query_authorizer_context {
  int denied_action;
} strappy_database_query_authorizer_context;

typedef struct strappy_database_query_progress_context {
  clock_t started_at;
  int progress_calls;
  int timed_out;
} strappy_database_query_progress_context;

static const strappy_tool_definition strappy_tool_definitions[] = {
  {
    STRAPPY_TOOL_DATABASE_LIST_INFO,
    "List all user-approved SQLite databases available to Strappy and include "
    "safe file metadata, deterministic schema facts, availability state, and "
    "recommended next steps without full filesystem paths.",
    STRAPPY_TOOL_KIND_DATABASE
  },
  {
    STRAPPY_TOOL_DATABASE_QUERY,
    "Run one bounded read-only SQL query against a user-approved SQLite "
    "database. The model supplies only the assistant-visible database_id and "
    "SQL query; Strappy resolves the local path, opens the database read-only, "
    "and enforces statement, row, and result limits.",
    STRAPPY_TOOL_KIND_DATABASE
  },
  {
    STRAPPY_TOOL_HELPER_CONVERT_DATES,
    "Convert a comma-separated list of numeric timestamps to a comma-separated "
    "list of UTC ISO8601 timestamps. The timestamps argument is required; the "
    "optional unit selects unix_seconds, unix_milliseconds, unix_microseconds, "
    "unix_nanoseconds, apple_seconds, apple_milliseconds, apple_microseconds, "
    "or apple_nanoseconds. The default unit is unix_seconds.",
    STRAPPY_TOOL_KIND_HELPER
  }
};

static const size_t strappy_tool_definition_count =
  sizeof(strappy_tool_definitions) / sizeof(strappy_tool_definitions[0]);

static const strappy_tool_definition *strappy_tools_find_definition(
  const char *tool_name)
{
  size_t index;

  if ((tool_name == NULL) || (tool_name[0] == '\0')) {
    return NULL;
  }

  for (index = 0U; index < strappy_tool_definition_count; index++) {
    if (strcmp(strappy_tool_definitions[index].name, tool_name) == 0) {
      return &strappy_tool_definitions[index];
    }
  }

  return NULL;
}

int strappy_tools_is_helper(const char *tool_name)
{
  const strappy_tool_definition *definition;

  definition = strappy_tools_find_definition(tool_name);
  return ((definition != NULL) &&
          (definition->kind == STRAPPY_TOOL_KIND_HELPER)) ? 1 : 0;
}

static void strappy_database_query_arguments_init(
  strappy_database_query_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->database_id = NULL;
  arguments->sql = NULL;
}

static void strappy_database_query_arguments_destroy(
  strappy_database_query_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  free(arguments->database_id);
  free(arguments->sql);
  strappy_database_query_arguments_init(arguments);
}

static void strappy_helper_convert_dates_arguments_init(
  strappy_helper_convert_dates_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->timestamps = NULL;
  arguments->unit = STRAPPY_COCOA_TIMESTAMP_UNIT_UNIX_SECONDS;
}

static void strappy_helper_convert_dates_arguments_destroy(
  strappy_helper_convert_dates_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  free(arguments->timestamps);
  strappy_helper_convert_dates_arguments_init(arguments);
}

static void strappy_helper_text_buffer_init(strappy_helper_text_buffer *buffer)
{
  if (buffer == NULL) {
    return;
  }

  buffer->data = NULL;
  buffer->length = 0U;
}

static void strappy_helper_text_buffer_destroy(strappy_helper_text_buffer *buffer)
{
  if (buffer == NULL) {
    return;
  }

  free(buffer->data);
  strappy_helper_text_buffer_init(buffer);
}

static int strappy_helper_text_buffer_append(
  strappy_helper_text_buffer *buffer,
  const char *text)
{
  size_t length;
  char *next_data;

  if ((buffer == NULL) || (text == NULL)) {
    return 0;
  }

  length = strlen(text);
  if (buffer->length > (((size_t)-1) - length - 1U)) {
    return 0;
  }

  next_data = (char *)realloc(buffer->data, buffer->length + length + 1U);
  if (next_data == NULL) {
    return 0;
  }

  buffer->data = next_data;
  if (length > 0U) {
    memcpy(buffer->data + buffer->length, text, length);
  }
  buffer->length += length;
  buffer->data[buffer->length] = '\0';
  return 1;
}

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

static int strappy_tools_add_required_parameter(cJSON *required,
                                                const char *name)
{
  cJSON *item;

  if ((required == NULL) || (name == NULL)) {
    return 0;
  }

  item = cJSON_CreateString(name);
  if ((item == NULL) || !cJSON_AddItemToArray(required, item)) {
    cJSON_Delete(item);
    return 0;
  }

  return 1;
}

static int strappy_tools_add_string_parameter(cJSON *properties,
                                              const char *name,
                                              const char *description)
{
  cJSON *property;

  if ((properties == NULL) || (name == NULL) || (description == NULL)) {
    return 0;
  }

  property = cJSON_CreateObject();
  if (property == NULL) {
    return 0;
  }

  if ((cJSON_AddStringToObject(property, "type", "string") == NULL) ||
      (cJSON_AddStringToObject(property,
                               "description",
                               description) == NULL) ||
      !cJSON_AddItemToObject(properties, name, property)) {
    cJSON_Delete(property);
    return 0;
  }

  return 1;
}

static int strappy_tools_add_database_query_parameters(cJSON *function)
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

  if ((cJSON_AddStringToObject(parameters, "type", "object") == NULL) ||
      !strappy_tools_add_string_parameter(
        properties,
        "database_id",
        "Assistant-visible database_id returned by database_list_info.") ||
      !strappy_tools_add_string_parameter(
        properties,
        "sql",
        "One read-only SQL SELECT or EXPLAIN query. Do not include PRAGMA, "
        "ATTACH, writes, multiple statements, filesystem paths, open flags, "
        "or limit settings. Large 64-bit integer IDs may be returned as "
        "decimal strings to preserve exact values. For typeless columns, "
        "sample rows before filtering because flag values may be text. If a "
        "BLOB column appears to store text, select substr(CAST(column AS "
        "TEXT),1,N) instead of selecting the raw BLOB.") ||
      !strappy_tools_add_required_parameter(required, "database_id") ||
      !strappy_tools_add_required_parameter(required, "sql")) {
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

static int strappy_tools_add_string_enum_item(cJSON *array, const char *value)
{
  cJSON *item;

  if ((array == NULL) || (value == NULL)) {
    return 0;
  }

  item = cJSON_CreateString(value);
  if ((item == NULL) || !cJSON_AddItemToArray(array, item)) {
    cJSON_Delete(item);
    return 0;
  }

  return 1;
}

static int strappy_tools_add_helper_convert_dates_parameters(cJSON *function)
{
  cJSON *parameters;
  cJSON *properties;
  cJSON *required;
  cJSON *additional_properties;
  cJSON *unit_property;
  cJSON *unit_enum;

  if (function == NULL) {
    return 0;
  }

  parameters = cJSON_CreateObject();
  properties = cJSON_CreateObject();
  required = cJSON_CreateArray();
  additional_properties = cJSON_CreateBool(0);
  unit_property = cJSON_CreateObject();
  unit_enum = cJSON_CreateArray();
  if ((parameters == NULL) || (properties == NULL) || (required == NULL) ||
      (additional_properties == NULL) || (unit_property == NULL) ||
      (unit_enum == NULL)) {
    cJSON_Delete(parameters);
    cJSON_Delete(properties);
    cJSON_Delete(required);
    cJSON_Delete(additional_properties);
    cJSON_Delete(unit_property);
    cJSON_Delete(unit_enum);
    return 0;
  }

  if ((cJSON_AddStringToObject(parameters, "type", "object") == NULL) ||
      !strappy_tools_add_string_parameter(
        properties,
        "timestamps",
        "Comma-separated numeric timestamps to convert. Preserve the order; "
        "do not include labels or surrounding brackets.") ||
      (cJSON_AddStringToObject(unit_property, "type", "string") == NULL) ||
      (cJSON_AddStringToObject(
         unit_property,
         "description",
         "Timestamp unit and epoch. Defaults to unix_seconds when omitted.") == NULL) ||
      !strappy_tools_add_string_enum_item(unit_enum, "unix_seconds") ||
      !strappy_tools_add_string_enum_item(unit_enum, "unix_milliseconds") ||
      !strappy_tools_add_string_enum_item(unit_enum, "unix_microseconds") ||
      !strappy_tools_add_string_enum_item(unit_enum, "unix_nanoseconds") ||
      !strappy_tools_add_string_enum_item(unit_enum, "apple_seconds") ||
      !strappy_tools_add_string_enum_item(unit_enum, "apple_milliseconds") ||
      !strappy_tools_add_string_enum_item(unit_enum, "apple_microseconds") ||
      !strappy_tools_add_string_enum_item(unit_enum, "apple_nanoseconds") ||
      !strappy_tools_add_required_parameter(required, "timestamps")) {
    cJSON_Delete(parameters);
    cJSON_Delete(properties);
    cJSON_Delete(required);
    cJSON_Delete(additional_properties);
    cJSON_Delete(unit_property);
    cJSON_Delete(unit_enum);
    return 0;
  }

  if (!cJSON_AddItemToObject(unit_property, "enum", unit_enum)) {
    cJSON_Delete(parameters);
    cJSON_Delete(properties);
    cJSON_Delete(required);
    cJSON_Delete(additional_properties);
    cJSON_Delete(unit_property);
    cJSON_Delete(unit_enum);
    return 0;
  }
  unit_enum = NULL;

  if (!cJSON_AddItemToObject(properties, "unit", unit_property)) {
    cJSON_Delete(parameters);
    cJSON_Delete(properties);
    cJSON_Delete(required);
    cJSON_Delete(additional_properties);
    cJSON_Delete(unit_property);
    return 0;
  }
  unit_property = NULL;

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
  int added_parameters;

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

  if (strcmp(definition->name, STRAPPY_TOOL_DATABASE_QUERY) == 0) {
    added_parameters = strappy_tools_add_database_query_parameters(function);
  } else if (strcmp(definition->name,
                    STRAPPY_TOOL_HELPER_CONVERT_DATES) == 0) {
    added_parameters =
      strappy_tools_add_helper_convert_dates_parameters(function);
  } else {
    added_parameters = strappy_tools_add_empty_parameters(function);
  }

  if ((cJSON_AddStringToObject(tool, "type", "function") == NULL) ||
      (cJSON_AddStringToObject(function, "name", definition->name) == NULL) ||
      (cJSON_AddStringToObject(function,
                               "description",
                               definition->description) == NULL) ||
      !added_parameters ||
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
    "and next-step hints. Takes no arguments. Use the returned database_id "
    "values for database-specific tools. Results use filename, not raw "
    "filesystem path. If no database is available, guide the user to the "
    "returned database management link.\n"
    "- database_query: Runs one bounded read-only SQL query against an "
    "approved database. Arguments are exactly database_id and sql. Do not "
    "provide paths, open modes, limits, PRAGMA, ATTACH, writes, or multiple "
    "statements. Large 64-bit integer IDs may be returned as decimal strings "
    "to preserve exact values; use those exact values in follow-up queries. "
    "For columns with no declared type, sample rows before filtering because "
    "flag values may be returned as text strings. If a BLOB column appears "
    "to store text, select substr(CAST(column AS TEXT),1,N) instead of "
    "selecting the raw BLOB.\n"
    "- helper_convert_dates: Converts a comma-separated timestamps string to "
    "a comma-separated list of UTC ISO8601 timestamps. Arguments are "
    "timestamps and optional unit. The default unit is unix_seconds. Use "
    "unix_milliseconds, unix_microseconds, unix_nanoseconds, apple_seconds, "
    "apple_milliseconds, apple_microseconds, or apple_nanoseconds when the "
    "database schema or sampled values show those formats. Use this helper "
    "instead of guessing date math from raw numeric timestamps.");
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

static int strappy_tools_parse_database_query_arguments(
  const char *arguments_json,
  strappy_database_query_arguments *arguments,
  char **error_out)
{
  cJSON *root;
  cJSON *database_id;
  cJSON *sql;
  cJSON *child;
  size_t sql_length;

  if (arguments == NULL) {
    strappy_set_error(error_out, "database_query argument output is missing.");
    return 0;
  }
  strappy_database_query_arguments_init(arguments);

  if (!strappy_tools_string_has_value(arguments_json)) {
    strappy_set_error(error_out,
                      "database_query requires database_id and sql arguments.");
    return 0;
  }

  root = cJSON_Parse(arguments_json);
  if (root == NULL) {
    strappy_set_error(error_out, "database_query arguments are not valid JSON.");
    return 0;
  }

  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    strappy_set_error(error_out,
                      "database_query arguments must be a JSON object.");
    return 0;
  }

  for (child = root->child; child != NULL; child = child->next) {
    if ((child->string == NULL) ||
        ((strcmp(child->string, "database_id") != 0) &&
         (strcmp(child->string, "sql") != 0))) {
      cJSON_Delete(root);
      strappy_set_error(error_out,
                        "database_query only accepts database_id and sql.");
      return 0;
    }
  }

  database_id = cJSON_GetObjectItemCaseSensitive(root, "database_id");
  sql = cJSON_GetObjectItemCaseSensitive(root, "sql");
  if (!cJSON_IsString(database_id) ||
      !strappy_tools_string_has_value(database_id->valuestring) ||
      !cJSON_IsString(sql) ||
      !strappy_tools_string_has_value(sql->valuestring)) {
    cJSON_Delete(root);
    strappy_set_error(error_out,
                      "database_query requires non-empty database_id and sql strings.");
    return 0;
  }

  sql_length = strlen(sql->valuestring);
  if (sql_length > STRAPPY_DATABASE_QUERY_MAX_SQL_BYTES) {
    cJSON_Delete(root);
    strappy_set_formatted_error(
      error_out,
      "database_query SQL is too long; maximum is %u bytes.",
      (unsigned int)STRAPPY_DATABASE_QUERY_MAX_SQL_BYTES);
    return 0;
  }

  arguments->database_id = strappy_string_duplicate(database_id->valuestring);
  arguments->sql = strappy_string_duplicate(sql->valuestring);
  cJSON_Delete(root);
  if ((arguments->database_id == NULL) || (arguments->sql == NULL)) {
    strappy_database_query_arguments_destroy(arguments);
    strappy_set_error(error_out, "Could not allocate database_query arguments.");
    return 0;
  }

  return 1;
}

static int strappy_tools_parse_helper_convert_dates_arguments(
  const char *arguments_json,
  strappy_helper_convert_dates_arguments *arguments,
  char **error_out)
{
  cJSON *root;
  cJSON *timestamps;
  cJSON *unit;
  cJSON *child;
  size_t timestamp_length;

  if (arguments == NULL) {
    strappy_set_error(error_out,
                      "helper_convert_dates argument output is missing.");
    return 0;
  }
  strappy_helper_convert_dates_arguments_init(arguments);

  if (!strappy_tools_string_has_value(arguments_json)) {
    strappy_set_error(error_out,
                      "helper_convert_dates requires a timestamps argument.");
    return 0;
  }

  root = cJSON_Parse(arguments_json);
  if (root == NULL) {
    strappy_set_error(error_out,
                      "helper_convert_dates arguments are not valid JSON.");
    return 0;
  }

  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    strappy_set_error(error_out,
                      "helper_convert_dates arguments must be a JSON object.");
    return 0;
  }

  for (child = root->child; child != NULL; child = child->next) {
    if ((child->string == NULL) ||
        ((strcmp(child->string, "timestamps") != 0) &&
         (strcmp(child->string, "unit") != 0))) {
      cJSON_Delete(root);
      strappy_set_error(error_out,
                        "helper_convert_dates only accepts timestamps and unit.");
      return 0;
    }
  }

  timestamps = cJSON_GetObjectItemCaseSensitive(root, "timestamps");
  if (!cJSON_IsString(timestamps) ||
      !strappy_tools_string_has_value(timestamps->valuestring)) {
    cJSON_Delete(root);
    strappy_set_error(error_out,
                      "helper_convert_dates requires a non-empty timestamps string.");
    return 0;
  }

  timestamp_length = strlen(timestamps->valuestring);
  if (timestamp_length > STRAPPY_HELPER_CONVERT_DATES_MAX_INPUT_BYTES) {
    cJSON_Delete(root);
    strappy_set_formatted_error(
      error_out,
      "helper_convert_dates timestamps is too long; maximum is %u bytes.",
      (unsigned int)STRAPPY_HELPER_CONVERT_DATES_MAX_INPUT_BYTES);
    return 0;
  }

  unit = cJSON_GetObjectItemCaseSensitive(root, "unit");
  if (unit != NULL) {
    if (!cJSON_IsString(unit)) {
      cJSON_Delete(root);
      strappy_set_error(error_out,
                        "helper_convert_dates unit must be a string.");
      return 0;
    }
    if (!strappy_cocoa_parse_timestamp_unit(unit->valuestring,
                                            &arguments->unit,
                                            error_out)) {
      cJSON_Delete(root);
      return 0;
    }
  }

  arguments->timestamps = strappy_string_duplicate(timestamps->valuestring);
  cJSON_Delete(root);
  if (arguments->timestamps == NULL) {
    strappy_helper_convert_dates_arguments_destroy(arguments);
    strappy_set_error(error_out,
                      "Could not allocate helper_convert_dates arguments.");
    return 0;
  }

  return 1;
}

static int strappy_tools_helper_is_space(char value)
{
  return ((value == ' ') || (value == '\t') || (value == '\r') ||
          (value == '\n') || (value == '\f') || (value == '\v')) ? 1 : 0;
}

static char *strappy_tools_convert_timestamp_token_to_iso8601(
  const char *token,
  size_t length,
  strappy_cocoa_timestamp_unit unit,
  char **error_out)
{
  char *timestamp;
  char *iso8601;

  timestamp = strappy_string_duplicate_length(token, length);
  if (timestamp == NULL) {
    strappy_set_error(error_out, "Could not allocate timestamp token.");
    return NULL;
  }

  iso8601 = strappy_cocoa_copy_iso8601_timestamp_value(timestamp,
                                                       unit,
                                                       error_out);
  free(timestamp);
  return iso8601;
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

static int strappy_tools_add_hint_to_array(cJSON *hints, const char *hint)
{
  cJSON *item;

  if ((hints == NULL) || (hint == NULL)) {
    return 0;
  }

  item = cJSON_CreateString(hint);
  if ((item == NULL) || !cJSON_AddItemToArray(hints, item)) {
    cJSON_Delete(item);
    return 0;
  }

  return 1;
}

static int strappy_tools_add_database_known_schema_hints(
  cJSON *object,
  const strappy_discovered_database_record *record)
{
  const char *filename;
  cJSON *hints;

  if ((object == NULL) || (record == NULL)) {
    return 0;
  }

  filename = strappy_tools_path_basename(record->path);
  if (filename == NULL) {
    return 1;
  }

  hints = NULL;
  if (strcmp(filename, "Envelope Index") == 0) {
    hints = cJSON_CreateArray();
    if (hints == NULL) {
      return 0;
    }
    if (!strappy_tools_add_hint_to_array(
          hints,
          "Apple Mail Envelope Index: use mailboxes.ROWID -> "
          "messages.mailbox to identify folders. Typeless flags such as "
          "visible and deleted may be text strings, so sample values before "
          "filtering.") ||
        !strappy_tools_add_hint_to_array(
          hints,
          "Apple Mail Envelope Index: messages.ROWID is the local message "
          "key used by the companion Protected Index messages.message_id for "
          "sender and subject. Do not use Envelope Index messages.message_id "
          "for that cross-index lookup.") ||
        !strappy_tools_add_hint_to_array(
          hints,
          "Apple Mail Envelope Index: message_data.message_id maps to "
          "messages.ROWID, and message_data.ROWID maps to the companion "
          "Protected Index message_data.message_data_id for body parts or "
          "summaries.")) {
      cJSON_Delete(hints);
      return 0;
    }
  } else if (strcmp(filename, "Protected Index") == 0) {
    hints = cJSON_CreateArray();
    if (hints == NULL) {
      return 0;
    }
    if (!strappy_tools_add_hint_to_array(
          hints,
          "Apple Mail Protected Index: messages.message_id uses the "
          "companion Envelope Index messages.ROWID local message key for "
          "sender and subject lookups.") ||
        !strappy_tools_add_hint_to_array(
          hints,
          "Apple Mail Protected Index: message_data.message_data_id matches "
          "the companion Envelope Index message_data.ROWID. The data column "
          "may be stored as a BLOB even when it contains message text; query "
          "substr(CAST(data AS TEXT),1,N) for bounded snippets.")) {
      cJSON_Delete(hints);
      return 0;
    }
  }

  if (hints == NULL) {
    return 1;
  }

  if (!cJSON_AddItemToObject(object, "known_schema_hints", hints)) {
    cJSON_Delete(hints);
    return 0;
  }

  return 1;
}

static int strappy_tools_add_database_next_steps(cJSON *root,
                                                 const char *database_id)
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

  step = strappy_tools_create_database_tool_step(
    STRAPPY_TOOL_DATABASE_QUERY,
    database_id,
    "Use database_query with this database_id when answering a concrete user "
    "question from this database.");

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
  cJSON *databases,
  const strappy_discovered_database_record *record,
  char **error_out)
{
  cJSON *object;
  cJSON *schema;
  char *schema_error;

  if ((databases == NULL) || (record == NULL)) {
    strappy_set_error(error_out, "Database list info request is incomplete.");
    return 0;
  }

  object = cJSON_CreateObject();
  if (object == NULL) {
    strappy_set_error(error_out, "Could not allocate database info result.");
    return 0;
  }

  if (!strappy_tools_add_database_metadata(object, record)) {
    cJSON_Delete(object);
    strappy_set_error(error_out, "Could not build database metadata result.");
    return 0;
  }

  schema_error = NULL;
  schema = strappy_tools_read_database_schema(record, &schema_error);
  if (schema != NULL) {
    if (!cJSON_AddItemToObject(object, "schema", schema)) {
      cJSON_Delete(schema);
      cJSON_Delete(object);
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
    free(schema_error);
    strappy_set_error(error_out, "Could not build database schema result.");
    return 0;
  }
  free(schema_error);

  if (!strappy_tools_add_database_known_schema_hints(object, record)) {
    cJSON_Delete(object);
    strappy_set_error(error_out, "Could not build database schema hints.");
    return 0;
  }

  if (!strappy_tools_add_database_next_steps(object,
                                             record->assistant_database_id)) {
    cJSON_Delete(object);
    strappy_set_error(error_out, "Could not build database info result.");
    return 0;
  }

  if (!cJSON_AddItemToArray(databases, object)) {
    cJSON_Delete(object);
    strappy_set_error(error_out, "Could not build database list result.");
    return 0;
  }

  return 1;
}

static int strappy_tools_sql_tail_is_empty(const char *tail)
{
  const char *cursor;

  if (tail == NULL) {
    return 1;
  }

  cursor = tail;
  while (*cursor != '\0') {
    if ((*cursor != ' ') && (*cursor != '\t') && (*cursor != '\r') &&
        (*cursor != '\n') && (*cursor != '\f') && (*cursor != '\v')) {
      return 0;
    }
    cursor++;
  }

  return 1;
}

static char strappy_tools_ascii_lower_char(char value)
{
  if ((value >= 'A') && (value <= 'Z')) {
    return (char)(value + ('a' - 'A'));
  }

  return value;
}

static int strappy_tools_ascii_equal_ignore_case(const char *left,
                                                 const char *right)
{
  size_t index;

  if ((left == NULL) || (right == NULL)) {
    return 0;
  }

  index = 0U;
  while ((left[index] != '\0') && (right[index] != '\0')) {
    if (strappy_tools_ascii_lower_char(left[index]) !=
        strappy_tools_ascii_lower_char(right[index])) {
      return 0;
    }
    index++;
  }

  return ((left[index] == '\0') && (right[index] == '\0')) ? 1 : 0;
}

static int strappy_tools_ascii_starts_with_ignore_case(const char *value,
                                                       const char *prefix)
{
  size_t index;

  if ((value == NULL) || (prefix == NULL)) {
    return 0;
  }

  index = 0U;
  while (prefix[index] != '\0') {
    if (value[index] == '\0') {
      return 0;
    }

    if (strappy_tools_ascii_lower_char(value[index]) !=
        strappy_tools_ascii_lower_char(prefix[index])) {
      return 0;
    }
    index++;
  }

  return 1;
}

static int strappy_tools_database_query_function_is_denied(
  const char *function_name)
{
  if (function_name == NULL) {
    return 0;
  }

  return (strappy_tools_ascii_equal_ignore_case(function_name,
                                                "load_extension") ||
          strappy_tools_ascii_equal_ignore_case(function_name, "readfile") ||
          strappy_tools_ascii_equal_ignore_case(function_name, "writefile") ||
          strappy_tools_ascii_starts_with_ignore_case(function_name,
                                                     "pragma_")) ? 1 : 0;
}

static int strappy_tools_database_query_database_is_allowed(
  const char *database_name)
{
  return ((database_name == NULL) ||
          (strcmp(database_name, "main") == 0)) ? 1 : 0;
}

static int strappy_tools_database_query_authorizer(void *data,
                                                   int action,
                                                   const char *detail1,
                                                   const char *detail2,
                                                   const char *database_name,
                                                   const char *source)
{
  strappy_database_query_authorizer_context *context;

  (void)detail1;
  (void)source;

  context = (strappy_database_query_authorizer_context *)data;

  if ((action == SQLITE_SELECT) || (action == SQLITE_RECURSIVE)) {
    return SQLITE_OK;
  }

  if (action == SQLITE_READ) {
    if (strappy_tools_database_query_database_is_allowed(database_name)) {
      return SQLITE_OK;
    }
  } else if (action == SQLITE_FUNCTION) {
    if (!strappy_tools_database_query_function_is_denied(detail2)) {
      return SQLITE_OK;
    }
  }

  if ((context != NULL) && (context->denied_action == 0)) {
    context->denied_action = action;
  }

  return SQLITE_DENY;
}

static int strappy_tools_database_query_progress(void *data)
{
  strappy_database_query_progress_context *context;
  clock_t now;
  double elapsed_seconds;

  context = (strappy_database_query_progress_context *)data;
  if (context == NULL) {
    return 0;
  }

  context->progress_calls++;
  if (context->progress_calls > STRAPPY_DATABASE_QUERY_MAX_PROGRESS_CALLS) {
    context->timed_out = 1;
    return 1;
  }

  if (context->started_at == (clock_t)-1) {
    return 0;
  }

  now = clock();
  if (now == (clock_t)-1) {
    return 0;
  }

  elapsed_seconds =
    ((double)(now - context->started_at)) / ((double)CLOCKS_PER_SEC);
  if (elapsed_seconds >= STRAPPY_DATABASE_QUERY_TIMEOUT_SECONDS) {
    context->timed_out = 1;
    return 1;
  }

  return 0;
}

static size_t strappy_tools_bounded_strlen(const char *value,
                                           size_t max_length)
{
  size_t length;

  if (value == NULL) {
    return 0U;
  }

  length = 0U;
  while ((length < max_length) && (value[length] != '\0')) {
    length++;
  }

  return length;
}

static size_t strappy_tools_utf8_prefix_length(const char *value,
                                               size_t max_length)
{
  size_t index;

  if (value == NULL) {
    return 0U;
  }

  index = 0U;
  while ((index < max_length) && (value[index] != '\0')) {
    unsigned char current;
    size_t character_length;
    size_t offset;
    int valid;

    current = (unsigned char)value[index];
    character_length = 1U;
    if ((current & 0x80U) == 0U) {
      character_length = 1U;
    } else if ((current & 0xE0U) == 0xC0U) {
      character_length = 2U;
    } else if ((current & 0xF0U) == 0xE0U) {
      character_length = 3U;
    } else if ((current & 0xF8U) == 0xF0U) {
      character_length = 4U;
    }

    if (index > (((size_t)-1) - character_length)) {
      break;
    }

    if ((index + character_length) > max_length) {
      break;
    }

    valid = 1;
    for (offset = 1U; offset < character_length; offset++) {
      unsigned char continuation;

      continuation = (unsigned char)value[index + offset];
      if ((continuation == '\0') ||
          ((continuation & 0xC0U) != 0x80U)) {
        valid = 0;
        break;
      }
    }

    if (!valid) {
      character_length = 1U;
    }

    index += character_length;
  }

  return index;
}

static int strappy_tools_size_add_would_exceed(size_t current,
                                               size_t addition,
                                               size_t limit)
{
  return (current > limit) || (addition > (limit - current));
}

static cJSON *strappy_tools_create_truncated_text_value(
  const char *text,
  size_t byte_count,
  size_t visible_length,
  size_t *payload_bytes_out,
  char **error_out)
{
  cJSON *object;
  char *prefix;
  size_t prefix_length;

  prefix_length = visible_length;
  if (prefix_length > STRAPPY_DATABASE_QUERY_MAX_CELL_BYTES) {
    prefix_length =
      strappy_tools_utf8_prefix_length(text,
                                       STRAPPY_DATABASE_QUERY_MAX_CELL_BYTES);
  }

  prefix = strappy_string_duplicate_length(text, prefix_length);
  if (prefix == NULL) {
    strappy_set_error(error_out, "Could not allocate truncated text value.");
    return NULL;
  }

  object = cJSON_CreateObject();
  if (object == NULL) {
    free(prefix);
    strappy_set_error(error_out, "Could not allocate truncated text value.");
    return NULL;
  }

  if ((cJSON_AddStringToObject(object, "type", "text") == NULL) ||
      (cJSON_AddStringToObject(object, "value", prefix) == NULL) ||
      (cJSON_AddNumberToObject(object,
                               "size_bytes",
                               (double)byte_count) == NULL) ||
      !strappy_tools_add_bool_to_object(object, "truncated", 1) ||
      ((visible_length < byte_count) &&
       !strappy_tools_add_bool_to_object(object,
                                         "contains_nul_bytes",
                                         1))) {
    cJSON_Delete(object);
    free(prefix);
    strappy_set_error(error_out, "Could not build truncated text value.");
    return NULL;
  }

  free(prefix);
  if (payload_bytes_out != NULL) {
    *payload_bytes_out = prefix_length + 64U;
  }

  return object;
}

static cJSON *strappy_tools_create_blob_value(size_t byte_count,
                                              size_t *payload_bytes_out,
                                              char **error_out)
{
  cJSON *object;

  object = cJSON_CreateObject();
  if (object == NULL) {
    strappy_set_error(error_out, "Could not allocate blob value.");
    return NULL;
  }

  if ((cJSON_AddStringToObject(object, "type", "blob") == NULL) ||
      (cJSON_AddNumberToObject(object,
                               "size_bytes",
                               (double)byte_count) == NULL) ||
      !strappy_tools_add_bool_to_object(object, "omitted", 1)) {
    cJSON_Delete(object);
    strappy_set_error(error_out, "Could not build blob value.");
    return NULL;
  }

  if (payload_bytes_out != NULL) {
    *payload_bytes_out = 64U;
  }

  return object;
}

static cJSON *strappy_tools_create_database_query_integer_value(
  sqlite3_int64 integer_value,
  size_t *payload_bytes_out,
  char **error_out)
{
  char buffer[32];
  cJSON *value;
  int written;

  if ((integer_value >= STRAPPY_DATABASE_QUERY_JSON_SAFE_INTEGER_MIN) &&
      (integer_value <= STRAPPY_DATABASE_QUERY_JSON_SAFE_INTEGER_MAX)) {
    value = cJSON_CreateNumber((double)integer_value);
    if (payload_bytes_out != NULL) {
      *payload_bytes_out = 32U;
    }
    return value;
  }

  written = snprintf(buffer,
                     sizeof(buffer),
                     "%lld",
                     (long long)integer_value);
  if ((written <= 0) || ((size_t)written >= sizeof(buffer))) {
    strappy_set_error(error_out, "Could not format integer value.");
    return NULL;
  }

  value = cJSON_CreateString(buffer);
  if (value == NULL) {
    strappy_set_error(error_out, "Could not allocate integer value.");
    return NULL;
  }

  if (payload_bytes_out != NULL) {
    *payload_bytes_out = ((size_t)written) + 2U;
  }

  return value;
}

static cJSON *strappy_tools_create_database_query_value(
  sqlite3_stmt *stmt,
  int column,
  int *cell_size_limit_reached_out,
  size_t *payload_bytes_out,
  char **error_out)
{
  cJSON *value;
  int column_type;
  int byte_count_int;
  size_t byte_count;

  if (payload_bytes_out != NULL) {
    *payload_bytes_out = 0U;
  }

  column_type = sqlite3_column_type(stmt, column);
  if (column_type == SQLITE_NULL) {
    value = cJSON_CreateNull();
    if (payload_bytes_out != NULL) {
      *payload_bytes_out = 4U;
    }
    return value;
  }

  if (column_type == SQLITE_INTEGER) {
    return strappy_tools_create_database_query_integer_value(
      sqlite3_column_int64(stmt, column),
      payload_bytes_out,
      error_out);
  }

  if (column_type == SQLITE_FLOAT) {
    value = cJSON_CreateNumber(sqlite3_column_double(stmt, column));
    if (payload_bytes_out != NULL) {
      *payload_bytes_out = 32U;
    }
    return value;
  }

  byte_count_int = sqlite3_column_bytes(stmt, column);
  if (byte_count_int < 0) {
    strappy_set_error(error_out, "SQLite returned a negative value size.");
    return NULL;
  }
  byte_count = (size_t)byte_count_int;

  if (column_type == SQLITE_BLOB) {
    return strappy_tools_create_blob_value(byte_count,
                                           payload_bytes_out,
                                           error_out);
  }

  if (column_type == SQLITE_TEXT) {
    const char *text;
    size_t visible_length;

    text = strappy_tools_sqlite_column_text(stmt, column);
    if (text == NULL) {
      value = cJSON_CreateNull();
      if (payload_bytes_out != NULL) {
        *payload_bytes_out = 4U;
      }
      return value;
    }

    visible_length = strappy_tools_bounded_strlen(text, byte_count);
    if ((byte_count > STRAPPY_DATABASE_QUERY_MAX_CELL_BYTES) ||
        (visible_length < byte_count)) {
      if (cell_size_limit_reached_out != NULL) {
        *cell_size_limit_reached_out = 1;
      }
      return strappy_tools_create_truncated_text_value(text,
                                                       byte_count,
                                                       visible_length,
                                                       payload_bytes_out,
                                                       error_out);
    }

    value = cJSON_CreateString(text);
    if (payload_bytes_out != NULL) {
      *payload_bytes_out = byte_count;
    }
    return value;
  }

  value = cJSON_CreateNull();
  if (payload_bytes_out != NULL) {
    *payload_bytes_out = 4U;
  }
  return value;
}

static int strappy_tools_add_database_query_limits(cJSON *root)
{
  cJSON *limits;

  if (root == NULL) {
    return 0;
  }

  limits = cJSON_CreateObject();
  if (limits == NULL) {
    return 0;
  }

  if ((cJSON_AddNumberToObject(limits,
                               "max_rows",
                               (double)STRAPPY_DATABASE_QUERY_MAX_ROWS) == NULL) ||
      (cJSON_AddNumberToObject(
         limits,
         "max_columns",
         (double)STRAPPY_DATABASE_QUERY_MAX_COLUMNS) == NULL) ||
      (cJSON_AddNumberToObject(
         limits,
         "max_sql_bytes",
         (double)STRAPPY_DATABASE_QUERY_MAX_SQL_BYTES) == NULL) ||
      (cJSON_AddNumberToObject(
         limits,
         "max_cell_bytes",
         (double)STRAPPY_DATABASE_QUERY_MAX_CELL_BYTES) == NULL) ||
      (cJSON_AddNumberToObject(
         limits,
         "max_result_bytes",
         (double)STRAPPY_DATABASE_QUERY_MAX_RESULT_BYTES) == NULL) ||
      (cJSON_AddNumberToObject(
         limits,
         "timeout_seconds",
         STRAPPY_DATABASE_QUERY_TIMEOUT_SECONDS) == NULL) ||
      !cJSON_AddItemToObject(root, "limits", limits)) {
    cJSON_Delete(limits);
    return 0;
  }

  return 1;
}

static int strappy_tools_add_database_query_columns(sqlite3_stmt *stmt,
                                                    cJSON *root,
                                                    int column_count,
                                                    char **error_out)
{
  cJSON *columns;
  int column;

  columns = cJSON_CreateArray();
  if (columns == NULL) {
    strappy_set_error(error_out, "Could not allocate query columns.");
    return 0;
  }

  for (column = 0; column < column_count; column++) {
    cJSON *column_object;
    const char *name;
    const char *declared_type;

    name = sqlite3_column_name(stmt, column);
    if (name == NULL) {
      name = "";
    }

    declared_type = sqlite3_column_decltype(stmt, column);
    column_object = cJSON_CreateObject();
    if (column_object == NULL) {
      cJSON_Delete(columns);
      strappy_set_error(error_out, "Could not allocate query column.");
      return 0;
    }

    if ((cJSON_AddNumberToObject(column_object,
                                 "index",
                                 (double)column) == NULL) ||
        (cJSON_AddStringToObject(column_object, "name", name) == NULL) ||
        !strappy_tools_add_optional_string_to_object(column_object,
                                                     "declared_type",
                                                     declared_type) ||
        !cJSON_AddItemToArray(columns, column_object)) {
      cJSON_Delete(column_object);
      cJSON_Delete(columns);
      strappy_set_error(error_out, "Could not build query column result.");
      return 0;
    }
  }

  if (!cJSON_AddItemToObject(root, "columns", columns)) {
    cJSON_Delete(columns);
    strappy_set_error(error_out, "Could not build query column result.");
    return 0;
  }

  return 1;
}

static int strappy_tools_add_database_query_rows(
  sqlite3 *db,
  sqlite3_stmt *stmt,
  cJSON *root,
  int column_count,
  strappy_database_query_progress_context *progress,
  int *row_count_out,
  int *row_limit_reached_out,
  int *result_size_limit_reached_out,
  int *cell_size_limit_reached_out,
  char **error_out)
{
  cJSON *rows;
  size_t payload_bytes;
  int row_count;
  int row_limit_reached;
  int result_size_limit_reached;
  int cell_size_limit_reached;
  int rc;

  rows = cJSON_CreateArray();
  if (rows == NULL) {
    strappy_set_error(error_out, "Could not allocate query rows.");
    return 0;
  }

  payload_bytes = 0U;
  row_count = 0;
  row_limit_reached = 0;
  result_size_limit_reached = 0;
  cell_size_limit_reached = 0;

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    cJSON *row;
    size_t row_payload_bytes;
    int column;
    int stop_for_result_size;

    if (row_count >= STRAPPY_DATABASE_QUERY_MAX_ROWS) {
      row_limit_reached = 1;
      break;
    }

    row = cJSON_CreateArray();
    if (row == NULL) {
      cJSON_Delete(rows);
      strappy_set_error(error_out, "Could not allocate query row.");
      return 0;
    }

    row_payload_bytes = 0U;
    stop_for_result_size = 0;
    for (column = 0; column < column_count; column++) {
      cJSON *value;
      size_t value_payload_bytes;
      size_t cell_payload_bytes;

      value = strappy_tools_create_database_query_value(
        stmt,
        column,
        &cell_size_limit_reached,
        &value_payload_bytes,
        error_out);
      if (value == NULL) {
        cJSON_Delete(row);
        cJSON_Delete(rows);
        if ((error_out != NULL) && (*error_out == NULL)) {
          strappy_set_error(error_out, "Could not build query value.");
        }
        return 0;
      }

      cell_payload_bytes = value_payload_bytes;
      if (cell_payload_bytes > (((size_t)-1) - 8U)) {
        cJSON_Delete(value);
        cJSON_Delete(row);
        cJSON_Delete(rows);
        strappy_set_error(error_out, "Query result size overflowed.");
        return 0;
      }
      cell_payload_bytes += 8U;

      if (strappy_tools_size_add_would_exceed(row_payload_bytes,
                                              cell_payload_bytes,
                                              STRAPPY_DATABASE_QUERY_MAX_PAYLOAD_BYTES) ||
          strappy_tools_size_add_would_exceed(payload_bytes,
                                              row_payload_bytes + cell_payload_bytes,
                                              STRAPPY_DATABASE_QUERY_MAX_PAYLOAD_BYTES)) {
        cJSON_Delete(value);
        cJSON_Delete(row);
        result_size_limit_reached = 1;
        stop_for_result_size = 1;
        break;
      }

      if (!cJSON_AddItemToArray(row, value)) {
        cJSON_Delete(value);
        cJSON_Delete(row);
        cJSON_Delete(rows);
        strappy_set_error(error_out, "Could not build query row.");
        return 0;
      }

      row_payload_bytes += cell_payload_bytes;
    }

    if (stop_for_result_size) {
      break;
    }

    if (!cJSON_AddItemToArray(rows, row)) {
      cJSON_Delete(row);
      cJSON_Delete(rows);
      strappy_set_error(error_out, "Could not build query rows.");
      return 0;
    }

    payload_bytes += row_payload_bytes;
    row_count++;
  }

  if ((rc != SQLITE_DONE) && !row_limit_reached &&
      !result_size_limit_reached) {
    if ((progress != NULL) && progress->timed_out) {
      strappy_set_formatted_error(
        error_out,
        "database_query timed out after %.0f seconds.",
        STRAPPY_DATABASE_QUERY_TIMEOUT_SECONDS);
    } else {
      strappy_set_formatted_error(error_out,
                                  "Could not run database_query: %s",
                                  sqlite3_errmsg(db));
    }
    cJSON_Delete(rows);
    return 0;
  }

  if (!cJSON_AddItemToObject(root, "rows", rows)) {
    cJSON_Delete(rows);
    strappy_set_error(error_out, "Could not build query rows.");
    return 0;
  }

  if (row_count_out != NULL) {
    *row_count_out = row_count;
  }
  if (row_limit_reached_out != NULL) {
    *row_limit_reached_out = row_limit_reached;
  }
  if (result_size_limit_reached_out != NULL) {
    *result_size_limit_reached_out = result_size_limit_reached;
  }
  if (cell_size_limit_reached_out != NULL) {
    *cell_size_limit_reached_out = cell_size_limit_reached;
  }

  return 1;
}

static int strappy_tools_prepare_database_query_statement(
  sqlite3 *db,
  const char *sql,
  strappy_database_query_authorizer_context *authorizer_context,
  sqlite3_stmt **stmt_out,
  char **error_out)
{
  sqlite3_stmt *stmt;
  const char *tail;
  int rc;

  if (stmt_out == NULL) {
    strappy_set_error(error_out, "database_query statement output is missing.");
    return 0;
  }
  *stmt_out = NULL;

  if (authorizer_context == NULL) {
    strappy_set_error(error_out, "database_query authorizer context is missing.");
    return 0;
  }

  authorizer_context->denied_action = 0;
  rc = sqlite3_set_authorizer(db,
                              strappy_tools_database_query_authorizer,
                              authorizer_context);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not install database_query authorizer: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  stmt = NULL;
  tail = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, &tail);
  if (rc != SQLITE_OK) {
    if (authorizer_context->denied_action != 0) {
      strappy_set_error(
        error_out,
        "database_query only permits one read-only SELECT or EXPLAIN query "
        "against the approved database. PRAGMA, ATTACH, writes, filesystem "
        "access helpers, and transaction statements are not allowed.");
    } else {
      strappy_set_formatted_error(error_out,
                                  "Could not prepare database_query SQL: %s",
                                  sqlite3_errmsg(db));
    }
    sqlite3_set_authorizer(db, NULL, NULL);
    if (stmt != NULL) {
      sqlite3_finalize(stmt);
    }
    return 0;
  }

  if (stmt == NULL) {
    sqlite3_set_authorizer(db, NULL, NULL);
    strappy_set_error(error_out, "database_query SQL produced no statement.");
    return 0;
  }

  if (!strappy_tools_sql_tail_is_empty(tail)) {
    sqlite3_finalize(stmt);
    sqlite3_set_authorizer(db, NULL, NULL);
    strappy_set_error(error_out,
                      "database_query accepts exactly one SQL statement.");
    return 0;
  }

  if (!sqlite3_stmt_readonly(stmt)) {
    sqlite3_finalize(stmt);
    sqlite3_set_authorizer(db, NULL, NULL);
    strappy_set_error(error_out, "database_query SQL must be read-only.");
    return 0;
  }

  if (sqlite3_bind_parameter_count(stmt) > 0) {
    sqlite3_finalize(stmt);
    sqlite3_set_authorizer(db, NULL, NULL);
    strappy_set_error(error_out,
                      "database_query does not accept bind parameters.");
    return 0;
  }

  if (sqlite3_column_count(stmt) <= 0) {
    sqlite3_finalize(stmt);
    sqlite3_set_authorizer(db, NULL, NULL);
    strappy_set_error(error_out,
                      "database_query SQL must return columns.");
    return 0;
  }

  if (sqlite3_column_count(stmt) > STRAPPY_DATABASE_QUERY_MAX_COLUMNS) {
    sqlite3_finalize(stmt);
    sqlite3_set_authorizer(db, NULL, NULL);
    strappy_set_formatted_error(
      error_out,
      "database_query selected too many columns; maximum is %d.",
      STRAPPY_DATABASE_QUERY_MAX_COLUMNS);
    return 0;
  }

  *stmt_out = stmt;
  return 1;
}

static void strappy_tools_configure_database_query_connection(sqlite3 *db)
{
  if (db == NULL) {
    return;
  }

  sqlite3_limit(db,
                SQLITE_LIMIT_SQL_LENGTH,
                (int)STRAPPY_DATABASE_QUERY_MAX_SQL_BYTES);
  sqlite3_limit(db, SQLITE_LIMIT_COLUMN, STRAPPY_DATABASE_QUERY_MAX_COLUMNS);
  sqlite3_limit(db, SQLITE_LIMIT_ATTACHED, 0);
  sqlite3_limit(db, SQLITE_LIMIT_COMPOUND_SELECT, 8);
  sqlite3_limit(db, SQLITE_LIMIT_FUNCTION_ARG, 32);
  sqlite3_limit(db, SQLITE_LIMIT_LIKE_PATTERN_LENGTH, 1024);
}

static char *strappy_tools_run_database_query(
  const strappy_discovered_database_record *record,
  const char *sql,
  char **error_out)
{
  sqlite3 *db;
  sqlite3_stmt *stmt;
  cJSON *root;
  char *json;
  strappy_database_query_authorizer_context authorizer_context;
  strappy_database_query_progress_context progress;
  int column_count;
  int row_count;
  int row_limit_reached;
  int result_size_limit_reached;
  int cell_size_limit_reached;
  int truncated;
  int finalize_rc;

  if ((record == NULL) || !strappy_tools_string_has_value(sql)) {
    strappy_set_error(error_out, "database_query request is incomplete.");
    return NULL;
  }

  if (!strappy_tools_open_readonly_database(record->path, &db, error_out)) {
    return NULL;
  }

  strappy_tools_configure_database_query_connection(db);
  progress.started_at = clock();
  progress.progress_calls = 0;
  progress.timed_out = 0;
  sqlite3_progress_handler(db,
                           STRAPPY_DATABASE_QUERY_PROGRESS_INSTRUCTIONS,
                           strappy_tools_database_query_progress,
                           &progress);

  stmt = NULL;
  if (!strappy_tools_prepare_database_query_statement(db,
                                                      sql,
                                                      &authorizer_context,
                                                      &stmt,
                                                      error_out)) {
    sqlite3_progress_handler(db, 0, NULL, NULL);
    sqlite3_close(db);
    return NULL;
  }

  column_count = sqlite3_column_count(stmt);
  root = cJSON_CreateObject();
  if (root == NULL) {
    sqlite3_finalize(stmt);
    sqlite3_set_authorizer(db, NULL, NULL);
    sqlite3_progress_handler(db, 0, NULL, NULL);
    sqlite3_close(db);
    strappy_set_error(error_out, "Could not allocate database_query result.");
    return NULL;
  }

  row_count = 0;
  row_limit_reached = 0;
  result_size_limit_reached = 0;
  cell_size_limit_reached = 0;
  if ((cJSON_AddStringToObject(root,
                               "database_id",
                               record->assistant_database_id) == NULL) ||
      !strappy_tools_add_bool_to_object(root, "ok", 1) ||
      (cJSON_AddNumberToObject(root,
                               "column_count",
                               (double)column_count) == NULL) ||
      !strappy_tools_add_database_query_limits(root) ||
      !strappy_tools_add_database_query_columns(stmt,
                                                root,
                                                column_count,
                                                error_out) ||
      !strappy_tools_add_database_query_rows(db,
                                             stmt,
                                             root,
                                             column_count,
                                             &progress,
                                             &row_count,
                                             &row_limit_reached,
                                             &result_size_limit_reached,
                                             &cell_size_limit_reached,
                                             error_out)) {
    cJSON_Delete(root);
    sqlite3_finalize(stmt);
    sqlite3_set_authorizer(db, NULL, NULL);
    sqlite3_progress_handler(db, 0, NULL, NULL);
    sqlite3_close(db);
    if ((error_out != NULL) && (*error_out == NULL)) {
      strappy_set_error(error_out, "Could not build database_query result.");
    }
    return NULL;
  }

  finalize_rc = sqlite3_finalize(stmt);
  sqlite3_set_authorizer(db, NULL, NULL);
  sqlite3_progress_handler(db, 0, NULL, NULL);
  if (finalize_rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not finalize database_query: %s",
                                sqlite3_errmsg(db));
    cJSON_Delete(root);
    sqlite3_close(db);
    return NULL;
  }
  sqlite3_close(db);

  truncated =
    (row_limit_reached || result_size_limit_reached ||
     cell_size_limit_reached) ? 1 : 0;
  if ((cJSON_AddNumberToObject(root,
                               "row_count",
                               (double)row_count) == NULL) ||
      !strappy_tools_add_bool_to_object(root, "truncated", truncated) ||
      !strappy_tools_add_bool_to_object(root,
                                        "row_limit_reached",
                                        row_limit_reached) ||
      !strappy_tools_add_bool_to_object(root,
                                        "result_size_limit_reached",
                                        result_size_limit_reached) ||
      !strappy_tools_add_bool_to_object(root,
                                        "cell_size_limit_reached",
                                        cell_size_limit_reached)) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Could not build database_query result.");
    return NULL;
  }

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize database_query result.");
    return NULL;
  }

  if (strlen(json) > STRAPPY_DATABASE_QUERY_MAX_RESULT_BYTES) {
    free(json);
    strappy_set_formatted_error(
      error_out,
      "database_query result exceeded the %u byte serialized result limit.",
      (unsigned int)STRAPPY_DATABASE_QUERY_MAX_RESULT_BYTES);
    return NULL;
  }

  return json;
}

static const strappy_discovered_database_record *
strappy_tools_find_database_query_record(
  const strappy_discovered_database_record_list *list,
  const char *database_id,
  int *matched_unavailable_out)
{
  size_t index;

  if (matched_unavailable_out != NULL) {
    *matched_unavailable_out = 0;
  }

  if ((list == NULL) || !strappy_tools_string_has_value(database_id)) {
    return NULL;
  }

  for (index = 0U; index < list->count; index++) {
    const strappy_discovered_database_record *record;

    record = &list->records[index];
    if (!strappy_tools_record_has_database_id(record) ||
        (strcmp(record->assistant_database_id, database_id) != 0)) {
      continue;
    }

    if (strappy_tools_record_is_available_database(record)) {
      return record;
    }

    if (matched_unavailable_out != NULL) {
      *matched_unavailable_out = 1;
    }
    return NULL;
  }

  return NULL;
}

static char *strappy_tools_execute_database_query(const char *session_db_path,
                                                  const char *arguments_json,
                                                  char **error_out)
{
  strappy_database_query_arguments arguments;
  strappy_discovered_database_record_list list;
  const strappy_discovered_database_record *record;
  char *json;
  int matched_unavailable;

  if ((session_db_path == NULL) || (session_db_path[0] == '\0')) {
    strappy_set_error(error_out, "Catalog database path is not configured.");
    return NULL;
  }

  strappy_database_query_arguments_init(&arguments);
  if (!strappy_tools_parse_database_query_arguments(arguments_json,
                                                   &arguments,
                                                   error_out)) {
    return NULL;
  }

  strappy_discovered_database_record_list_init(&list);
  if (!strappy_db_list_discovered_databases(session_db_path,
                                           &list,
                                           error_out)) {
    strappy_database_query_arguments_destroy(&arguments);
    return NULL;
  }

  matched_unavailable = 0;
  record = strappy_tools_find_database_query_record(&list,
                                                    arguments.database_id,
                                                    &matched_unavailable);
  if (record == NULL) {
    if (matched_unavailable) {
      strappy_set_error(
        error_out,
        "database_query database_id is not approved or is no longer valid. "
        "Run database_list_info to choose an available database_id.");
    } else {
      strappy_set_error(
        error_out,
        "database_query database_id was not found. Run database_list_info "
        "before querying the database.");
    }
    strappy_discovered_database_record_list_destroy(&list);
    strappy_database_query_arguments_destroy(&arguments);
    return NULL;
  }

  json = strappy_tools_run_database_query(record, arguments.sql, error_out);

  strappy_discovered_database_record_list_destroy(&list);
  strappy_database_query_arguments_destroy(&arguments);
  return json;
}

static char *strappy_tools_execute_helper_convert_dates(
  const char *arguments_json,
  char **error_out)
{
  strappy_helper_convert_dates_arguments arguments;
  strappy_helper_text_buffer buffer;
  const char *cursor;
  size_t timestamp_count;

  strappy_helper_convert_dates_arguments_init(&arguments);
  if (!strappy_tools_parse_helper_convert_dates_arguments(arguments_json,
                                                          &arguments,
                                                          error_out)) {
    return NULL;
  }

  strappy_helper_text_buffer_init(&buffer);
  cursor = arguments.timestamps;
  timestamp_count = 0U;
  while (cursor != NULL) {
    const char *item_start;
    const char *item_end;
    const char *item_delimiter;
    char *iso8601;

    iso8601 = NULL;
    item_start = cursor;
    item_delimiter = cursor;
    while ((*item_delimiter != '\0') && (*item_delimiter != ',')) {
      item_delimiter++;
    }
    item_end = item_delimiter;

    while ((item_start < item_end) &&
           strappy_tools_helper_is_space(*item_start)) {
      item_start++;
    }
    while ((item_end > item_start) &&
           strappy_tools_helper_is_space(*(item_end - 1))) {
      item_end--;
    }

    if (item_start == item_end) {
      strappy_helper_text_buffer_destroy(&buffer);
      strappy_helper_convert_dates_arguments_destroy(&arguments);
      strappy_set_error(error_out,
                        "helper_convert_dates timestamps contains an empty item.");
      return NULL;
    }

    if (timestamp_count >= STRAPPY_HELPER_CONVERT_DATES_MAX_TIMESTAMPS) {
      strappy_helper_text_buffer_destroy(&buffer);
      strappy_helper_convert_dates_arguments_destroy(&arguments);
      strappy_set_formatted_error(
        error_out,
        "helper_convert_dates accepts at most %u timestamps.",
        (unsigned int)STRAPPY_HELPER_CONVERT_DATES_MAX_TIMESTAMPS);
      return NULL;
    }

    iso8601 = strappy_tools_convert_timestamp_token_to_iso8601(
      item_start,
      (size_t)(item_end - item_start),
      arguments.unit,
      error_out);
    if (iso8601 == NULL) {
      strappy_helper_text_buffer_destroy(&buffer);
      strappy_helper_convert_dates_arguments_destroy(&arguments);
      return NULL;
    }

    if (((timestamp_count > 0U) &&
        !strappy_helper_text_buffer_append(&buffer, ",")) ||
        !strappy_helper_text_buffer_append(&buffer, iso8601)) {
      free(iso8601);
      strappy_helper_text_buffer_destroy(&buffer);
      strappy_helper_convert_dates_arguments_destroy(&arguments);
      strappy_set_error(error_out,
                        "Could not build helper_convert_dates result.");
      return NULL;
    }

    free(iso8601);
    timestamp_count++;
    cursor = (*item_delimiter == ',') ? item_delimiter + 1 : NULL;
  }

  strappy_helper_convert_dates_arguments_destroy(&arguments);
  if (timestamp_count == 0U) {
    strappy_helper_text_buffer_destroy(&buffer);
    strappy_set_error(error_out,
                      "helper_convert_dates requires at least one timestamp.");
    return NULL;
  }

  return buffer.data;
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

    if (!strappy_tools_add_database_list_info_record(databases,
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

  if (strcmp(tool_name, STRAPPY_TOOL_DATABASE_QUERY) == 0) {
    return strappy_tools_execute_database_query(session_db_path,
                                                arguments_json,
                                                error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_HELPER_CONVERT_DATES) == 0) {
    return strappy_tools_execute_helper_convert_dates(arguments_json,
                                                      error_out);
  }

  strappy_set_formatted_error(error_out, "Tool is not registered: %s", tool_name);
  return NULL;
}
