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
#define STRAPPY_TOOLS_AVAILABILITY_NO_APPROVED_DATABASES "no_approved_databases"
#define STRAPPY_TOOLS_AVAILABILITY_AVAILABLE "available"
#define STRAPPY_TOOL_GUIDANCE_RESOURCE "ToolGuidance.json"
#define STRAPPY_DATABASE_GUIDANCE_RESOURCE "DatabaseGuidance.json"
#define STRAPPY_DATABASE_INFO_MAX_TABLES 64
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
#define STRAPPY_HELPER_DATETIME_MAX_INPUT_BYTES 8192U
#define STRAPPY_HELPER_DATETIME_MAX_ITEMS 256U
#define STRAPPY_HELPER_INFO_MAX_SHORT_BYTES 128U
#define STRAPPY_HELPER_INFO_MAX_QUERY_BYTES 512U
#define STRAPPY_HELPER_INFO_MAX_VALUE_BYTES 2048U
#define STRAPPY_HELPER_INFO_MAX_CONTENT_BYTES 4096U
#define STRAPPY_HELPER_INFO_DEFAULT_CONFIDENCE 0.75
#define STRAPPY_HELPER_INFO_DEFAULT_LIMIT 20
#define STRAPPY_HELPER_INFO_MAX_LIMIT 50

typedef enum strappy_tool_kind {
  STRAPPY_TOOL_KIND_DATABASE = 1,
  STRAPPY_TOOL_KIND_HELPER = 2
} strappy_tool_kind;

typedef struct strappy_tool_definition {
  const char *name;
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

typedef struct strappy_helper_datetime_arguments {
  char *items;
  strappy_cocoa_timestamp_unit unit;
} strappy_helper_datetime_arguments;

typedef struct strappy_helper_user_info_remember_arguments {
  char *kind;
  char *subject;
  char *predicate;
  char *value;
  char *source;
  double confidence;
} strappy_helper_user_info_remember_arguments;

typedef struct strappy_helper_user_info_read_arguments {
  char *query;
  char *kind;
  int limit;
} strappy_helper_user_info_read_arguments;

typedef struct strappy_helper_info_forget_arguments {
  long long id;
} strappy_helper_info_forget_arguments;

typedef struct strappy_helper_database_info_remember_arguments {
  char *database_id;
  char *kind;
  char *title;
  char *content;
  char *evidence;
  double confidence;
} strappy_helper_database_info_remember_arguments;

typedef struct strappy_database_context_read_arguments {
  char *database_id;
  char *query;
  char *kind;
  int limit;
} strappy_database_context_read_arguments;

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

typedef char *(*strappy_tools_datetime_token_converter)(
  const char *token,
  size_t length,
  strappy_cocoa_timestamp_unit unit,
  char **error_out);

static const strappy_tool_definition strappy_tool_definitions[] = {
  { STRAPPY_TOOL_DATABASE_LIST_INFO, STRAPPY_TOOL_KIND_DATABASE },
  { STRAPPY_TOOL_DATABASE_QUERY, STRAPPY_TOOL_KIND_DATABASE },
  { STRAPPY_TOOL_HELPER_DATETIME_TO_ISO8601, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_HELPER_DATETIME_FROM_ISO8601, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_HELPER_USER_INFO_READ, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_HELPER_USER_INFO_REMEMBER, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_HELPER_USER_INFO_FORGET, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_DATABASE_CONTEXT_READ, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_HELPER_DATABASE_INFO_REMEMBER, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_HELPER_DATABASE_INFO_FORGET, STRAPPY_TOOL_KIND_HELPER }
};

static const size_t strappy_tool_definition_count =
  sizeof(strappy_tool_definitions) / sizeof(strappy_tool_definitions[0]);

static int strappy_tools_add_database_schema_context(
  cJSON *object,
  const strappy_discovered_database_record *record,
  char **error_out);

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

static void strappy_helper_datetime_arguments_init(
  strappy_helper_datetime_arguments *arguments,
  strappy_cocoa_timestamp_unit default_unit)
{
  if (arguments == NULL) {
    return;
  }

  arguments->items = NULL;
  arguments->unit = default_unit;
}

static void strappy_helper_datetime_arguments_destroy(
  strappy_helper_datetime_arguments *arguments,
  strappy_cocoa_timestamp_unit default_unit)
{
  if (arguments == NULL) {
    return;
  }

  free(arguments->items);
  strappy_helper_datetime_arguments_init(arguments, default_unit);
}

static void strappy_helper_user_info_remember_arguments_init(
  strappy_helper_user_info_remember_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->kind = NULL;
  arguments->subject = NULL;
  arguments->predicate = NULL;
  arguments->value = NULL;
  arguments->source = NULL;
  arguments->confidence = STRAPPY_HELPER_INFO_DEFAULT_CONFIDENCE;
}

static void strappy_helper_user_info_remember_arguments_destroy(
  strappy_helper_user_info_remember_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  free(arguments->kind);
  free(arguments->subject);
  free(arguments->predicate);
  free(arguments->value);
  free(arguments->source);
  strappy_helper_user_info_remember_arguments_init(arguments);
}

static void strappy_helper_user_info_read_arguments_init(
  strappy_helper_user_info_read_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->query = NULL;
  arguments->kind = NULL;
  arguments->limit = STRAPPY_HELPER_INFO_DEFAULT_LIMIT;
}

static void strappy_helper_user_info_read_arguments_destroy(
  strappy_helper_user_info_read_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  free(arguments->query);
  free(arguments->kind);
  strappy_helper_user_info_read_arguments_init(arguments);
}

static void strappy_helper_info_forget_arguments_init(
  strappy_helper_info_forget_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->id = 0;
}

static void strappy_helper_database_info_remember_arguments_init(
  strappy_helper_database_info_remember_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->database_id = NULL;
  arguments->kind = NULL;
  arguments->title = NULL;
  arguments->content = NULL;
  arguments->evidence = NULL;
  arguments->confidence = STRAPPY_HELPER_INFO_DEFAULT_CONFIDENCE;
}

static void strappy_helper_database_info_remember_arguments_destroy(
  strappy_helper_database_info_remember_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  free(arguments->database_id);
  free(arguments->kind);
  free(arguments->title);
  free(arguments->content);
  free(arguments->evidence);
  strappy_helper_database_info_remember_arguments_init(arguments);
}

static void strappy_database_context_read_arguments_init(
  strappy_database_context_read_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->database_id = NULL;
  arguments->query = NULL;
  arguments->kind = NULL;
  arguments->limit = STRAPPY_HELPER_INFO_DEFAULT_LIMIT;
}

static void strappy_database_context_read_arguments_destroy(
  strappy_database_context_read_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  free(arguments->database_id);
  free(arguments->query);
  free(arguments->kind);
  strappy_database_context_read_arguments_init(arguments);
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

static char *strappy_tools_resource_path(const char *resource_dir,
                                         const char *filename,
                                         char **error_out)
{
  size_t dir_length;
  size_t filename_length;
  size_t separator_length;
  size_t total_length;
  char *path;
  char *cursor;

  if ((resource_dir == NULL) || (resource_dir[0] == '\0')) {
    strappy_set_error(error_out, "Guidance resource directory is not configured.");
    return NULL;
  }
  if ((filename == NULL) || (filename[0] == '\0')) {
    strappy_set_error(error_out, "Guidance resource filename is not configured.");
    return NULL;
  }

  dir_length = strlen(resource_dir);
  filename_length = strlen(filename);
  separator_length = (resource_dir[dir_length - 1U] == '/') ? 0U : 1U;
  if (dir_length > (((size_t)-1) - separator_length - filename_length - 1U)) {
    strappy_set_error(error_out, "Guidance resource path is too large.");
    return NULL;
  }

  total_length = dir_length + separator_length + filename_length;
  path = (char *)malloc(total_length + 1U);
  if (path == NULL) {
    strappy_set_error(error_out, "Could not allocate guidance resource path.");
    return NULL;
  }

  cursor = path;
  memcpy(cursor, resource_dir, dir_length);
  cursor += dir_length;
  if (separator_length != 0U) {
    *cursor = '/';
    cursor++;
  }
  memcpy(cursor, filename, filename_length);
  cursor += filename_length;
  *cursor = '\0';

  return path;
}

static char *strappy_tools_read_file(const char *path, char **error_out)
{
  FILE *file;
  long file_length;
  size_t length;
  size_t bytes_read;
  char *buffer;

  if ((path == NULL) || (path[0] == '\0')) {
    strappy_set_error(error_out, "Guidance resource path is not configured.");
    return NULL;
  }

  file = fopen(path, "rb");
  if (file == NULL) {
    strappy_set_formatted_error(error_out,
                                "Could not open guidance resource: %s",
                                path);
    return NULL;
  }

  if (fseek(file, 0L, SEEK_END) != 0) {
    fclose(file);
    strappy_set_formatted_error(error_out,
                                "Could not read guidance resource: %s",
                                path);
    return NULL;
  }

  file_length = ftell(file);
  if (file_length < 0L) {
    fclose(file);
    strappy_set_formatted_error(error_out,
                                "Could not measure guidance resource: %s",
                                path);
    return NULL;
  }
  length = (size_t)file_length;

  if (fseek(file, 0L, SEEK_SET) != 0) {
    fclose(file);
    strappy_set_formatted_error(error_out,
                                "Could not rewind guidance resource: %s",
                                path);
    return NULL;
  }

  buffer = (char *)malloc(length + 1U);
  if (buffer == NULL) {
    fclose(file);
    strappy_set_error(error_out, "Could not allocate guidance resource.");
    return NULL;
  }

  bytes_read = fread(buffer, 1U, length, file);
  if ((bytes_read != length) || ferror(file)) {
    free(buffer);
    fclose(file);
    strappy_set_formatted_error(error_out,
                                "Could not read guidance resource: %s",
                                path);
    return NULL;
  }

  buffer[length] = '\0';
  fclose(file);
  return buffer;
}

static cJSON *strappy_tools_read_json_resource(const char *resource_dir,
                                               const char *filename,
                                               char **error_out)
{
  char *path;
  char *text;
  cJSON *root;
  const char *parse_error;

  path = strappy_tools_resource_path(resource_dir, filename, error_out);
  if (path == NULL) {
    return NULL;
  }

  text = strappy_tools_read_file(path, error_out);
  if (text == NULL) {
    free(path);
    return NULL;
  }

  root = cJSON_Parse(text);
  if (root == NULL) {
    parse_error = cJSON_GetErrorPtr();
    if ((parse_error != NULL) && (parse_error[0] != '\0')) {
      strappy_set_formatted_error(error_out,
                                  "Could not parse guidance resource %s near: %.48s",
                                  path,
                                  parse_error);
    } else {
      strappy_set_formatted_error(error_out,
                                  "Could not parse guidance resource: %s",
                                  path);
    }
    free(text);
    free(path);
    return NULL;
  }

  free(text);
  free(path);

  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Guidance resource root must be a JSON object.");
    return NULL;
  }

  return root;
}

static const char *strappy_tools_tool_schema_name(cJSON *tool)
{
  cJSON *function;
  cJSON *name;

  if (!cJSON_IsObject(tool)) {
    return NULL;
  }

  function = cJSON_GetObjectItem(tool, "function");
  if (!cJSON_IsObject(function)) {
    return NULL;
  }

  name = cJSON_GetObjectItem(function, "name");
  if (!cJSON_IsString(name) || (name->valuestring == NULL) ||
      (name->valuestring[0] == '\0')) {
    return NULL;
  }

  return name->valuestring;
}

static int strappy_tools_schema_list_contains(cJSON *tools,
                                              const char *tool_name)
{
  cJSON *tool;

  if (!cJSON_IsArray(tools) || (tool_name == NULL)) {
    return 0;
  }

  for (tool = tools->child; tool != NULL; tool = tool->next) {
    const char *name;

    name = strappy_tools_tool_schema_name(tool);
    if ((name != NULL) && (strcmp(name, tool_name) == 0)) {
      return 1;
    }
  }

  return 0;
}

static int strappy_tools_validate_guidance_tools(cJSON *tools,
                                                 char **error_out)
{
  cJSON *tool;
  size_t index;

  if (!cJSON_IsArray(tools)) {
    strappy_set_error(error_out, "Tool guidance resource must contain a tools array.");
    return 0;
  }

  for (tool = tools->child; tool != NULL; tool = tool->next) {
    const char *name;

    name = strappy_tools_tool_schema_name(tool);
    if (name == NULL) {
      strappy_set_error(error_out, "Tool guidance contains a malformed tool schema.");
      return 0;
    }
    if (strappy_tools_find_definition(name) == NULL) {
      strappy_set_formatted_error(error_out,
                                  "Tool guidance contains unsupported tool: %s",
                                  name);
      return 0;
    }
  }

  for (index = 0U; index < strappy_tool_definition_count; index++) {
    if (!strappy_tools_schema_list_contains(tools,
                                            strappy_tool_definitions[index].name)) {
      strappy_set_formatted_error(error_out,
                                  "Tool guidance is missing tool schema: %s",
                                  strappy_tool_definitions[index].name);
      return 0;
    }
  }

  return 1;
}

char *strappy_tools_request_json(const char *resource_dir, char **error_out)
{
  cJSON *root;
  cJSON *tools;
  char *json;

  root = strappy_tools_read_json_resource(resource_dir,
                                          STRAPPY_TOOL_GUIDANCE_RESOURCE,
                                          error_out);
  if (root == NULL) {
    return NULL;
  }

  tools = cJSON_GetObjectItem(root, "tools");
  if (!strappy_tools_validate_guidance_tools(tools, error_out)) {
    cJSON_Delete(root);
    return NULL;
  }

  json = cJSON_PrintUnformatted(tools);
  cJSON_Delete(root);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize tool schema list.");
    return NULL;
  }

  return json;
}

static int strappy_tools_name_is_allowed(const char *name,
                                         const char * const *allowed_names,
                                         size_t allowed_name_count)
{
  size_t index;

  if ((name == NULL) || (allowed_names == NULL) || (allowed_name_count == 0U)) {
    return 0;
  }

  for (index = 0U; index < allowed_name_count; index++) {
    if ((allowed_names[index] != NULL) &&
        (strcmp(name, allowed_names[index]) == 0)) {
      return 1;
    }
  }

  return 0;
}

char *strappy_tools_request_json_filtered(const char *resource_dir,
                                          const char * const *allowed_names,
                                          size_t allowed_name_count,
                                          char **error_out)
{
  cJSON *root;
  cJSON *tools;
  cJSON *filtered;
  cJSON *tool;
  char *json;

  if ((allowed_names == NULL) || (allowed_name_count == 0U)) {
    return strappy_tools_request_json(resource_dir, error_out);
  }

  root = strappy_tools_read_json_resource(resource_dir,
                                          STRAPPY_TOOL_GUIDANCE_RESOURCE,
                                          error_out);
  if (root == NULL) {
    return NULL;
  }

  tools = cJSON_GetObjectItem(root, "tools");
  if (!strappy_tools_validate_guidance_tools(tools, error_out)) {
    cJSON_Delete(root);
    return NULL;
  }

  filtered = cJSON_CreateArray();
  if (filtered == NULL) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Could not allocate filtered tool schema list.");
    return NULL;
  }

  for (tool = tools->child; tool != NULL; tool = tool->next) {
    const char *name;

    name = strappy_tools_tool_schema_name(tool);
    if (strappy_tools_name_is_allowed(name, allowed_names, allowed_name_count)) {
      cJSON *copy;

      copy = cJSON_Duplicate(tool, 1);
      if ((copy == NULL) || !cJSON_AddItemToArray(filtered, copy)) {
        cJSON_Delete(copy);
        cJSON_Delete(filtered);
        cJSON_Delete(root);
        strappy_set_error(error_out, "Could not copy filtered tool schema.");
        return NULL;
      }
    }
  }

  json = cJSON_PrintUnformatted(filtered);
  cJSON_Delete(filtered);
  cJSON_Delete(root);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize filtered tool schema list.");
    return NULL;
  }

  return json;
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

static int strappy_tools_string_ends_with(const char *value,
                                          const char *suffix)
{
  size_t value_length;
  size_t suffix_length;

  if ((value == NULL) || (suffix == NULL)) {
    return 0;
  }

  value_length = strlen(value);
  suffix_length = strlen(suffix);
  if (suffix_length > value_length) {
    return 0;
  }

  return (strcmp(value + value_length - suffix_length, suffix) == 0) ? 1 : 0;
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
	                               (double)record->modified_at) == NULL)) {
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

  return STRAPPY_TOOLS_AVAILABILITY_NO_APPROVED_DATABASES;
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

static int strappy_tools_copy_string_array_to_object(cJSON *object,
                                                     const char *key,
                                                     cJSON *source,
                                                     char **error_out)
{
  cJSON *array;
  cJSON *item;
  int count;

  if ((object == NULL) || (key == NULL) || (source == NULL)) {
    return 1;
  }

  if (!cJSON_IsArray(source)) {
    strappy_set_formatted_error(error_out,
                                "Database guidance %s must be an array.",
                                key);
    return 0;
  }

  array = cJSON_CreateArray();
  if (array == NULL) {
    strappy_set_error(error_out, "Could not allocate database guidance array.");
    return 0;
  }

  count = 0;
  for (item = source->child; item != NULL; item = item->next) {
    cJSON *copy;

    if (!cJSON_IsString(item) || (item->valuestring == NULL) ||
        (item->valuestring[0] == '\0')) {
      cJSON_Delete(array);
      strappy_set_formatted_error(error_out,
                                  "Database guidance %s contains a malformed item.",
                                  key);
      return 0;
    }
    copy = cJSON_CreateString(item->valuestring);
    if ((copy == NULL) || !cJSON_AddItemToArray(array, copy)) {
      cJSON_Delete(copy);
      cJSON_Delete(array);
      strappy_set_error(error_out, "Could not build database guidance array.");
      return 0;
    }
    count++;
  }

  if (count == 0) {
    cJSON_Delete(array);
    return 1;
  }

  if (!cJSON_AddItemToObject(object, key, array)) {
    cJSON_Delete(array);
    return 0;
  }

  return 1;
}

typedef enum strappy_tools_database_guidance_match_kind {
  STRAPPY_TOOLS_DATABASE_GUIDANCE_MATCH_EXACT,
  STRAPPY_TOOLS_DATABASE_GUIDANCE_MATCH_SUFFIX,
  STRAPPY_TOOLS_DATABASE_GUIDANCE_MATCH_CONTAINS
} strappy_tools_database_guidance_match_kind;

static int strappy_tools_check_database_guidance_match(
  cJSON *match,
  const char *key,
  const char *candidate,
  strappy_tools_database_guidance_match_kind kind,
  int *saw_condition,
  int *matches,
  char **error_out)
{
  cJSON *value;
  int condition_matches;

  if ((match == NULL) || (key == NULL) || (saw_condition == NULL) ||
      (matches == NULL)) {
    return 0;
  }

  value = cJSON_GetObjectItem(match, key);
  if (value == NULL) {
    return 1;
  }

  if (!cJSON_IsString(value) || (value->valuestring == NULL) ||
      (value->valuestring[0] == '\0')) {
    strappy_set_formatted_error(error_out,
                                "Database guidance match.%s is malformed.",
                                key);
    return 0;
  }

  *saw_condition = 1;
  condition_matches = 0;
  if (candidate != NULL) {
    if (kind == STRAPPY_TOOLS_DATABASE_GUIDANCE_MATCH_EXACT) {
      condition_matches =
        (strcmp(candidate, value->valuestring) == 0) ? 1 : 0;
    } else if (kind == STRAPPY_TOOLS_DATABASE_GUIDANCE_MATCH_SUFFIX) {
      condition_matches =
        strappy_tools_string_ends_with(candidate, value->valuestring);
    } else if (kind == STRAPPY_TOOLS_DATABASE_GUIDANCE_MATCH_CONTAINS) {
      condition_matches =
        (strstr(candidate, value->valuestring) != NULL) ? 1 : 0;
    }
  }

  if (!condition_matches) {
    *matches = 0;
  }

  return 1;
}

static int strappy_tools_database_guidance_rule_matches(
  cJSON *rule,
  const char *filename,
  const char *path,
  int *matches_out,
  char **error_out)
{
  cJSON *match;
  int saw_condition;
  int matches;

  if ((rule == NULL) || (matches_out == NULL)) {
    return 0;
  }

  *matches_out = 0;
  match = cJSON_GetObjectItem(rule, "match");
  if (!cJSON_IsObject(match)) {
    strappy_set_error(error_out,
                      "Database guidance description rule is missing match.");
    return 0;
  }

  saw_condition = 0;
  matches = 1;
  if (!strappy_tools_check_database_guidance_match(
        match,
        "filename",
        filename,
        STRAPPY_TOOLS_DATABASE_GUIDANCE_MATCH_EXACT,
        &saw_condition,
        &matches,
        error_out) ||
      !strappy_tools_check_database_guidance_match(
        match,
        "path_suffix",
        path,
        STRAPPY_TOOLS_DATABASE_GUIDANCE_MATCH_SUFFIX,
        &saw_condition,
        &matches,
        error_out) ||
      !strappy_tools_check_database_guidance_match(
        match,
        "path_contains",
        path,
        STRAPPY_TOOLS_DATABASE_GUIDANCE_MATCH_CONTAINS,
        &saw_condition,
        &matches,
        error_out)) {
    return 0;
  }

  if (!saw_condition) {
    strappy_set_error(
      error_out,
      "Database guidance match must include filename, path_suffix, or path_contains.");
    return 0;
  }

  *matches_out = matches;
  return 1;
}

static int strappy_tools_add_database_guidance(
  cJSON *object,
  const strappy_discovered_database_record *record,
  const char *resource_dir,
  int include_short_description,
  int include_full_description,
  int include_related_databases,
  char **error_out)
{
  const char *filename;
  const char *description;
  const char *description_short;
  cJSON *root;
  cJSON *rules;
  cJSON *default_description;
  cJSON *default_description_short;
  cJSON *related_databases;
  cJSON *rule;

  if ((object == NULL) || (record == NULL)) {
    return 0;
  }

  root = strappy_tools_read_json_resource(resource_dir,
                                          STRAPPY_DATABASE_GUIDANCE_RESOURCE,
                                          error_out);
  if (root == NULL) {
    return 0;
  }

  default_description = cJSON_GetObjectItem(root, "default_description");
  if (include_full_description &&
      (!cJSON_IsString(default_description) ||
      (default_description->valuestring == NULL) ||
      (default_description->valuestring[0] == '\0'))) {
    cJSON_Delete(root);
    strappy_set_error(error_out,
                      "Database guidance is missing default_description.");
    return 0;
  }

  default_description_short =
    cJSON_GetObjectItem(root, "default_description_short");
  if (include_short_description &&
      (!cJSON_IsString(default_description_short) ||
       (default_description_short->valuestring == NULL) ||
       (default_description_short->valuestring[0] == '\0'))) {
    cJSON_Delete(root);
    strappy_set_error(
      error_out,
      "Database guidance is missing default_description_short.");
    return 0;
  }

  rules = cJSON_GetObjectItem(root, "database_descriptions");
  if (!cJSON_IsArray(rules)) {
    cJSON_Delete(root);
    strappy_set_error(error_out,
                      "Database guidance must contain database_descriptions.");
    return 0;
  }

  filename = strappy_tools_path_basename(record->path);
  description =
    include_full_description ? default_description->valuestring : NULL;
  description_short =
    include_short_description ? default_description_short->valuestring : NULL;
  related_databases = NULL;
  for (rule = rules->child; rule != NULL; rule = rule->next) {
    int rule_matches;

    if (!cJSON_IsObject(rule)) {
      cJSON_Delete(root);
      strappy_set_error(error_out,
                        "Database guidance contains a malformed description.");
      return 0;
    }

    if (!strappy_tools_database_guidance_rule_matches(rule,
                                                      filename,
                                                      record->path,
                                                      &rule_matches,
                                                      error_out)) {
      cJSON_Delete(root);
      return 0;
    }
    if (!rule_matches) {
      continue;
    }

    {
      cJSON *rule_description;
      cJSON *rule_description_short;

      rule_description = cJSON_GetObjectItem(rule, "description");
      if (include_full_description &&
          (!cJSON_IsString(rule_description) ||
          (rule_description->valuestring == NULL) ||
          (rule_description->valuestring[0] == '\0'))) {
        cJSON_Delete(root);
        strappy_set_error(error_out,
                          "Database guidance description rule is malformed.");
        return 0;
      }

      rule_description_short = cJSON_GetObjectItem(rule, "description_short");
      if (include_short_description &&
          (!cJSON_IsString(rule_description_short) ||
           (rule_description_short->valuestring == NULL) ||
           (rule_description_short->valuestring[0] == '\0'))) {
        cJSON_Delete(root);
        strappy_set_error(
          error_out,
          "Database guidance description_short rule is malformed.");
        return 0;
      }

      if (include_full_description) {
        description = rule_description->valuestring;
      }
      if (include_short_description) {
        description_short = rule_description_short->valuestring;
      }
      related_databases = cJSON_GetObjectItem(rule, "related_databases");
    }
    break;
  }

  if ((include_short_description &&
       (cJSON_AddStringToObject(object,
                                "description_short",
                                description_short) == NULL)) ||
      (include_full_description &&
       (cJSON_AddStringToObject(object, "description", description) == NULL)) ||
      (include_related_databases &&
       !strappy_tools_copy_string_array_to_object(object,
                                                  "related_databases",
                                                  related_databases,
                                                  error_out))) {
    cJSON_Delete(root);
    if ((error_out != NULL) && (*error_out == NULL)) {
      strappy_set_error(error_out, "Could not build database guidance result.");
    }
    return 0;
  }

  cJSON_Delete(root);
  return 1;
}

static cJSON *strappy_tools_create_database_manage_step(const char *action,
                                                        const char *label,
                                                        const char *href,
                                                        const char *purpose)
{
  cJSON *step;

  step = cJSON_CreateObject();
  if (step == NULL) {
    return NULL;
  }

  if ((cJSON_AddStringToObject(step, "type", "user_action") == NULL) ||
      (cJSON_AddStringToObject(step, "action", action) == NULL) ||
      (cJSON_AddStringToObject(step, "label", label) == NULL) ||
      (cJSON_AddStringToObject(step, "href", href) == NULL) ||
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

static char *strappy_tools_database_guidance_string(const char *resource_dir,
                                                    const char *section_name,
                                                    const char *key,
                                                    char **error_out)
{
  cJSON *root;
  cJSON *section;
  cJSON *value;
  char *copy;

  if ((section_name == NULL) || (key == NULL)) {
    strappy_set_error(error_out, "Database guidance lookup is incomplete.");
    return NULL;
  }

  root = strappy_tools_read_json_resource(resource_dir,
                                          STRAPPY_DATABASE_GUIDANCE_RESOURCE,
                                          error_out);
  if (root == NULL) {
    return NULL;
  }

  section = cJSON_GetObjectItem(root, section_name);
  value = cJSON_IsObject(section) ? cJSON_GetObjectItem(section, key) : NULL;
  if ((value == NULL) || !cJSON_IsString(value) ||
      (value->valuestring == NULL) ||
      (value->valuestring[0] == '\0')) {
    cJSON_Delete(root);
    strappy_set_formatted_error(error_out,
                                "Database guidance is missing %s.%s.",
                                section_name,
                                key);
    return NULL;
  }

  copy = strappy_string_duplicate(value->valuestring);
  cJSON_Delete(root);
  if (copy == NULL) {
    strappy_set_error(error_out, "Could not allocate database guidance string.");
    return NULL;
  }

  return copy;
}

static char *strappy_tools_database_guidance_optional_string(
  const char *resource_dir,
  const char *section_name,
  const char *key)
{
  char *error;
  char *value;

  error = NULL;
  value = strappy_tools_database_guidance_string(resource_dir,
                                                 section_name,
                                                 key,
                                                 &error);
  free(error);
  return value;
}

static void strappy_tools_set_error_with_database_guidance(
  char **error_out,
  const char *message,
  const char *resource_dir,
  const char *guidance_key)
{
  char *guidance;

  guidance = strappy_tools_database_guidance_optional_string(
    resource_dir,
    "recovery_guidance",
    guidance_key);

  if ((guidance != NULL) && (guidance[0] != '\0')) {
    strappy_set_formatted_error(error_out, "%s %s", message, guidance);
  } else {
    strappy_set_error(error_out, message);
  }

  free(guidance);
}

static int strappy_tools_add_database_user_action(
  cJSON *root,
  const char *availability_state,
  const char *resource_dir,
  char **error_out)
{
  cJSON *step;
  char *action;
  char *label;
  char *href;
  char *purpose;

  if ((root == NULL) || (availability_state == NULL)) {
    return 0;
  }

  if (strcmp(availability_state,
             STRAPPY_TOOLS_AVAILABILITY_NO_APPROVED_DATABASES) != 0) {
    return 1;
  }

  purpose = strappy_tools_database_guidance_string(resource_dir,
                                                   "user_actions",
                                                   "manage_needed",
                                                   error_out);
  if (purpose == NULL) {
    return 0;
  }

  action = strappy_tools_database_guidance_string(resource_dir,
                                                  "user_actions",
                                                  "manage_action",
                                                  error_out);
  if (action == NULL) {
    free(purpose);
    return 0;
  }

  label = strappy_tools_database_guidance_string(resource_dir,
                                                 "user_actions",
                                                 "manage_label",
                                                 error_out);
  if (label == NULL) {
    free(action);
    free(purpose);
    return 0;
  }

  href = strappy_tools_database_guidance_string(resource_dir,
                                                "user_actions",
                                                "manage_href",
                                                error_out);
  if (href == NULL) {
    free(label);
    free(action);
    free(purpose);
    return 0;
  }

  step = strappy_tools_create_database_manage_step(action,
                                                   label,
                                                   href,
                                                   purpose);
  free(href);
  free(label);
  free(action);
  free(purpose);
  if ((step == NULL) || !cJSON_AddItemToObject(root, "user_action", step)) {
    cJSON_Delete(step);
    strappy_set_error(error_out, "Could not build database user action.");
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

static cJSON *strappy_tools_parse_arguments_object(const char *tool_name,
                                                   const char *arguments_json,
                                                   char **error_out)
{
  cJSON *root;

  if (!strappy_tools_string_has_value(arguments_json)) {
    root = cJSON_CreateObject();
    if (root == NULL) {
      strappy_set_formatted_error(error_out,
                                  "Could not allocate %s arguments.",
                                  tool_name);
    }
    return root;
  }

  root = cJSON_Parse(arguments_json);
  if (root == NULL) {
    strappy_set_formatted_error(error_out,
                                "%s arguments are not valid JSON.",
                                tool_name);
    return NULL;
  }

  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    strappy_set_formatted_error(error_out,
                                "%s arguments must be a JSON object.",
                                tool_name);
    return NULL;
  }

  return root;
}

static int strappy_tools_json_object_accepts_only(
  cJSON *root,
  const char *tool_name,
  const char *const *allowed_names,
  size_t allowed_count,
  char **error_out)
{
  cJSON *child;

  if ((root == NULL) || (tool_name == NULL) || (allowed_names == NULL)) {
    strappy_set_error(error_out, "Tool argument validation is incomplete.");
    return 0;
  }

  for (child = root->child; child != NULL; child = child->next) {
    size_t index;
    int found;

    if (child->string == NULL) {
      strappy_set_formatted_error(error_out,
                                  "%s arguments contain an unnamed field.",
                                  tool_name);
      return 0;
    }

    found = 0;
    for (index = 0U; index < allowed_count; index++) {
      if (strcmp(child->string, allowed_names[index]) == 0) {
        found = 1;
        break;
      }
    }

    if (!found) {
      strappy_set_formatted_error(error_out,
                                  "%s does not accept argument '%s'.",
                                  tool_name,
                                  child->string);
      return 0;
    }
  }

  return 1;
}

static int strappy_tools_copy_string_argument(const char *tool_name,
                                              cJSON *root,
                                              const char *name,
                                              int required,
                                              size_t max_bytes,
                                              char **value_out,
                                              char **error_out)
{
  cJSON *item;
  size_t length;

  if (value_out == NULL) {
    strappy_set_error(error_out, "Tool string argument output is missing.");
    return 0;
  }
  *value_out = NULL;

  item = cJSON_GetObjectItemCaseSensitive(root, name);
  if (item == NULL) {
    if (required) {
      strappy_set_formatted_error(error_out,
                                  "%s requires a non-empty %s string.",
                                  tool_name,
                                  name);
      return 0;
    }
    return 1;
  }

  if (!cJSON_IsString(item)) {
    strappy_set_formatted_error(error_out,
                                "%s %s must be a string.",
                                tool_name,
                                name);
    return 0;
  }

  if (!strappy_tools_string_has_value(item->valuestring)) {
    if (required) {
      strappy_set_formatted_error(error_out,
                                  "%s requires a non-empty %s string.",
                                  tool_name,
                                  name);
      return 0;
    }
    return 1;
  }

  length = strlen(item->valuestring);
  if (length > max_bytes) {
    strappy_set_formatted_error(error_out,
                                "%s %s is too long; maximum is %u bytes.",
                                tool_name,
                                name,
                                (unsigned int)max_bytes);
    return 0;
  }

  *value_out = strappy_string_duplicate(item->valuestring);
  if (*value_out == NULL) {
    strappy_set_formatted_error(error_out,
                                "Could not allocate %s %s argument.",
                                tool_name,
                                name);
    return 0;
  }

  return 1;
}

static int strappy_tools_parse_limit_argument(const char *tool_name,
                                              cJSON *root,
                                              int *limit_out,
                                              char **error_out)
{
  cJSON *item;

  if (limit_out == NULL) {
    strappy_set_error(error_out, "Tool limit argument output is missing.");
    return 0;
  }

  item = cJSON_GetObjectItemCaseSensitive(root, "limit");
  if (item == NULL) {
    *limit_out = STRAPPY_HELPER_INFO_DEFAULT_LIMIT;
    return 1;
  }

  if (!cJSON_IsNumber(item) || ((double)item->valueint != item->valuedouble) ||
      (item->valueint <= 0) ||
      (item->valueint > STRAPPY_HELPER_INFO_MAX_LIMIT)) {
    strappy_set_formatted_error(
      error_out,
      "%s limit must be an integer from 1 to %d.",
      tool_name,
      STRAPPY_HELPER_INFO_MAX_LIMIT);
    return 0;
  }

  *limit_out = item->valueint;
  return 1;
}

static int strappy_tools_parse_confidence_argument(const char *tool_name,
                                                   cJSON *root,
                                                   double *confidence_out,
                                                   char **error_out)
{
  cJSON *item;

  if (confidence_out == NULL) {
    strappy_set_error(error_out, "Tool confidence argument output is missing.");
    return 0;
  }

  item = cJSON_GetObjectItemCaseSensitive(root, "confidence");
  if (item == NULL) {
    *confidence_out = STRAPPY_HELPER_INFO_DEFAULT_CONFIDENCE;
    return 1;
  }

  if (!cJSON_IsNumber(item) || (item->valuedouble < 0.0) ||
      (item->valuedouble > 1.0)) {
    strappy_set_formatted_error(error_out,
                                "%s confidence must be a number from 0.0 to 1.0.",
                                tool_name);
    return 0;
  }

  *confidence_out = item->valuedouble;
  return 1;
}

static int strappy_tools_parse_id_argument(const char *tool_name,
                                           const char *arguments_json,
                                           strappy_helper_info_forget_arguments *arguments,
                                           char **error_out)
{
  static const char *const allowed_names[] = { "id" };
  cJSON *root;
  cJSON *id;

  if (arguments == NULL) {
    strappy_set_formatted_error(error_out,
                                "%s argument output is missing.",
                                tool_name);
    return 0;
  }
  strappy_helper_info_forget_arguments_init(arguments);

  root = strappy_tools_parse_arguments_object(tool_name,
                                              arguments_json,
                                              error_out);
  if (root == NULL) {
    return 0;
  }

  if (!strappy_tools_json_object_accepts_only(root,
                                              tool_name,
                                              allowed_names,
                                              sizeof(allowed_names) /
                                                sizeof(allowed_names[0]),
                                              error_out)) {
    cJSON_Delete(root);
    return 0;
  }

  id = cJSON_GetObjectItemCaseSensitive(root, "id");
  if (!cJSON_IsNumber(id) || ((double)id->valueint != id->valuedouble) ||
      (id->valueint <= 0)) {
    cJSON_Delete(root);
    strappy_set_formatted_error(error_out,
                                "%s requires a positive integer id.",
                                tool_name);
    return 0;
  }

  arguments->id = (long long)id->valueint;
  cJSON_Delete(root);
  return 1;
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

static int strappy_tools_parse_helper_datetime_arguments(
  const char *tool_name,
  const char *arguments_json,
  const char *items_argument_name,
  strappy_cocoa_timestamp_unit default_unit,
  strappy_helper_datetime_arguments *arguments,
  char **error_out)
{
  const char *allowed_names[2];
  cJSON *root;
  cJSON *unit;
  int ok;

  if ((tool_name == NULL) || (items_argument_name == NULL) ||
      (arguments == NULL)) {
    strappy_set_error(error_out, "Datetime helper argument output is missing.");
    return 0;
  }
  strappy_helper_datetime_arguments_init(arguments, default_unit);

  root = strappy_tools_parse_arguments_object(tool_name,
                                              arguments_json,
                                              error_out);
  if (root == NULL) {
    return 0;
  }

  allowed_names[0] = items_argument_name;
  allowed_names[1] = "unit";
  ok = strappy_tools_json_object_accepts_only(root,
                                              tool_name,
                                              allowed_names,
                                              sizeof(allowed_names) /
                                                sizeof(allowed_names[0]),
                                              error_out) &&
       strappy_tools_copy_string_argument(tool_name,
                                          root,
                                          items_argument_name,
                                          1,
                                          STRAPPY_HELPER_DATETIME_MAX_INPUT_BYTES,
                                          &arguments->items,
                                          error_out);
  if (!ok) {
    cJSON_Delete(root);
    strappy_helper_datetime_arguments_destroy(arguments, default_unit);
    return 0;
  }

  unit = cJSON_GetObjectItemCaseSensitive(root, "unit");
  if (unit != NULL) {
    if (!cJSON_IsString(unit)) {
      cJSON_Delete(root);
      strappy_helper_datetime_arguments_destroy(arguments, default_unit);
      strappy_set_formatted_error(error_out,
                                  "%s unit must be a string.",
                                  tool_name);
      return 0;
    }
    if (!strappy_cocoa_parse_timestamp_unit(unit->valuestring,
                                            &arguments->unit,
                                            error_out)) {
      cJSON_Delete(root);
      strappy_helper_datetime_arguments_destroy(arguments, default_unit);
      return 0;
    }
  }

  cJSON_Delete(root);
  return 1;
}

static int strappy_tools_parse_helper_user_info_read_arguments(
  const char *arguments_json,
  strappy_helper_user_info_read_arguments *arguments,
  char **error_out)
{
  static const char *const allowed_names[] = { "query", "kind", "limit" };
  cJSON *root;
  int ok;

  if (arguments == NULL) {
    strappy_set_error(error_out,
                      "helper_user_info_read argument output is missing.");
    return 0;
  }
  strappy_helper_user_info_read_arguments_init(arguments);

  root = strappy_tools_parse_arguments_object(STRAPPY_TOOL_HELPER_USER_INFO_READ,
                                              arguments_json,
                                              error_out);
  if (root == NULL) {
    return 0;
  }

  ok = strappy_tools_json_object_accepts_only(
         root,
         STRAPPY_TOOL_HELPER_USER_INFO_READ,
         allowed_names,
         sizeof(allowed_names) / sizeof(allowed_names[0]),
         error_out) &&
       strappy_tools_copy_string_argument(STRAPPY_TOOL_HELPER_USER_INFO_READ,
                                          root,
                                          "query",
                                          0,
                                          STRAPPY_HELPER_INFO_MAX_QUERY_BYTES,
                                          &arguments->query,
                                          error_out) &&
       strappy_tools_copy_string_argument(STRAPPY_TOOL_HELPER_USER_INFO_READ,
                                          root,
                                          "kind",
                                          0,
                                          STRAPPY_HELPER_INFO_MAX_SHORT_BYTES,
                                          &arguments->kind,
                                          error_out) &&
       strappy_tools_parse_limit_argument(STRAPPY_TOOL_HELPER_USER_INFO_READ,
                                          root,
                                          &arguments->limit,
                                          error_out);
  cJSON_Delete(root);
  if (!ok) {
    strappy_helper_user_info_read_arguments_destroy(arguments);
  }
  return ok;
}

static int strappy_tools_parse_helper_user_info_remember_arguments(
  const char *arguments_json,
  strappy_helper_user_info_remember_arguments *arguments,
  char **error_out)
{
  static const char *const allowed_names[] = {
    "kind", "subject", "predicate", "value", "confidence", "source"
  };
  cJSON *root;
  int ok;

  if (arguments == NULL) {
    strappy_set_error(error_out,
                      "helper_user_info_remember argument output is missing.");
    return 0;
  }
  strappy_helper_user_info_remember_arguments_init(arguments);

  root = strappy_tools_parse_arguments_object(
    STRAPPY_TOOL_HELPER_USER_INFO_REMEMBER,
    arguments_json,
    error_out);
  if (root == NULL) {
    return 0;
  }

  ok = strappy_tools_json_object_accepts_only(
         root,
         STRAPPY_TOOL_HELPER_USER_INFO_REMEMBER,
         allowed_names,
         sizeof(allowed_names) / sizeof(allowed_names[0]),
         error_out) &&
       strappy_tools_copy_string_argument(
         STRAPPY_TOOL_HELPER_USER_INFO_REMEMBER,
         root,
         "kind",
         1,
         STRAPPY_HELPER_INFO_MAX_SHORT_BYTES,
         &arguments->kind,
         error_out) &&
       strappy_tools_copy_string_argument(
         STRAPPY_TOOL_HELPER_USER_INFO_REMEMBER,
         root,
         "subject",
         1,
         STRAPPY_HELPER_INFO_MAX_SHORT_BYTES,
         &arguments->subject,
         error_out) &&
       strappy_tools_copy_string_argument(
         STRAPPY_TOOL_HELPER_USER_INFO_REMEMBER,
         root,
         "predicate",
         1,
         STRAPPY_HELPER_INFO_MAX_SHORT_BYTES,
         &arguments->predicate,
         error_out) &&
       strappy_tools_copy_string_argument(
         STRAPPY_TOOL_HELPER_USER_INFO_REMEMBER,
         root,
         "value",
         1,
         STRAPPY_HELPER_INFO_MAX_VALUE_BYTES,
         &arguments->value,
         error_out) &&
       strappy_tools_copy_string_argument(
         STRAPPY_TOOL_HELPER_USER_INFO_REMEMBER,
         root,
         "source",
         0,
         STRAPPY_HELPER_INFO_MAX_SHORT_BYTES,
         &arguments->source,
         error_out) &&
       strappy_tools_parse_confidence_argument(
         STRAPPY_TOOL_HELPER_USER_INFO_REMEMBER,
         root,
         &arguments->confidence,
         error_out);
  cJSON_Delete(root);
  if (!ok) {
    strappy_helper_user_info_remember_arguments_destroy(arguments);
  }
  return ok;
}

static int strappy_tools_parse_database_context_read_arguments(
  const char *arguments_json,
  strappy_database_context_read_arguments *arguments,
  char **error_out)
{
  static const char *const allowed_names[] = {
    "database_id", "query", "kind", "limit"
  };
  cJSON *root;
  int ok;

  if (arguments == NULL) {
    strappy_set_error(error_out,
                      "database_context_read argument output is missing.");
    return 0;
  }
  strappy_database_context_read_arguments_init(arguments);

  root = strappy_tools_parse_arguments_object(
    STRAPPY_TOOL_DATABASE_CONTEXT_READ,
    arguments_json,
    error_out);
  if (root == NULL) {
    return 0;
  }

  if (root->child == NULL) {
    cJSON_Delete(root);
    strappy_set_error(
      error_out,
      "database_context_read requires at least one argument: database_id, "
      "query, kind, or limit.");
    return 0;
  }

  ok = strappy_tools_json_object_accepts_only(
         root,
         STRAPPY_TOOL_DATABASE_CONTEXT_READ,
         allowed_names,
         sizeof(allowed_names) / sizeof(allowed_names[0]),
         error_out) &&
       strappy_tools_copy_string_argument(
         STRAPPY_TOOL_DATABASE_CONTEXT_READ,
         root,
         "database_id",
         0,
         STRAPPY_HELPER_INFO_MAX_SHORT_BYTES,
         &arguments->database_id,
         error_out) &&
       strappy_tools_copy_string_argument(
         STRAPPY_TOOL_DATABASE_CONTEXT_READ,
         root,
         "query",
         0,
         STRAPPY_HELPER_INFO_MAX_QUERY_BYTES,
         &arguments->query,
         error_out) &&
       strappy_tools_copy_string_argument(
         STRAPPY_TOOL_DATABASE_CONTEXT_READ,
         root,
         "kind",
         0,
         STRAPPY_HELPER_INFO_MAX_SHORT_BYTES,
         &arguments->kind,
         error_out) &&
       strappy_tools_parse_limit_argument(STRAPPY_TOOL_DATABASE_CONTEXT_READ,
                                          root,
                                          &arguments->limit,
                                          error_out);
  cJSON_Delete(root);
  if (!ok) {
    strappy_database_context_read_arguments_destroy(arguments);
  }
  return ok;
}

static int strappy_tools_parse_helper_database_info_remember_arguments(
  const char *arguments_json,
  strappy_helper_database_info_remember_arguments *arguments,
  char **error_out)
{
  static const char *const allowed_names[] = {
    "database_id", "kind", "title", "content", "evidence", "confidence"
  };
  cJSON *root;
  int ok;

  if (arguments == NULL) {
    strappy_set_error(
      error_out,
      "helper_database_info_remember argument output is missing.");
    return 0;
  }
  strappy_helper_database_info_remember_arguments_init(arguments);

  root = strappy_tools_parse_arguments_object(
    STRAPPY_TOOL_HELPER_DATABASE_INFO_REMEMBER,
    arguments_json,
    error_out);
  if (root == NULL) {
    return 0;
  }

  ok = strappy_tools_json_object_accepts_only(
         root,
         STRAPPY_TOOL_HELPER_DATABASE_INFO_REMEMBER,
         allowed_names,
         sizeof(allowed_names) / sizeof(allowed_names[0]),
         error_out) &&
       strappy_tools_copy_string_argument(
         STRAPPY_TOOL_HELPER_DATABASE_INFO_REMEMBER,
         root,
         "database_id",
         1,
         STRAPPY_HELPER_INFO_MAX_SHORT_BYTES,
         &arguments->database_id,
         error_out) &&
       strappy_tools_copy_string_argument(
         STRAPPY_TOOL_HELPER_DATABASE_INFO_REMEMBER,
         root,
         "kind",
         1,
         STRAPPY_HELPER_INFO_MAX_SHORT_BYTES,
         &arguments->kind,
         error_out) &&
       strappy_tools_copy_string_argument(
         STRAPPY_TOOL_HELPER_DATABASE_INFO_REMEMBER,
         root,
         "title",
         1,
         STRAPPY_HELPER_INFO_MAX_SHORT_BYTES,
         &arguments->title,
         error_out) &&
       strappy_tools_copy_string_argument(
         STRAPPY_TOOL_HELPER_DATABASE_INFO_REMEMBER,
         root,
         "content",
         1,
         STRAPPY_HELPER_INFO_MAX_CONTENT_BYTES,
         &arguments->content,
         error_out) &&
       strappy_tools_copy_string_argument(
         STRAPPY_TOOL_HELPER_DATABASE_INFO_REMEMBER,
         root,
         "evidence",
         0,
         STRAPPY_HELPER_INFO_MAX_CONTENT_BYTES,
         &arguments->evidence,
         error_out) &&
       strappy_tools_parse_confidence_argument(
         STRAPPY_TOOL_HELPER_DATABASE_INFO_REMEMBER,
         root,
         &arguments->confidence,
         error_out);
  cJSON_Delete(root);
  if (!ok) {
    strappy_helper_database_info_remember_arguments_destroy(arguments);
  }
  return ok;
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

static char *strappy_tools_convert_iso8601_token_to_timestamp(
  const char *token,
  size_t length,
  strappy_cocoa_timestamp_unit unit,
  char **error_out)
{
  char *iso8601;
  char *timestamp;

  iso8601 = strappy_string_duplicate_length(token, length);
  if (iso8601 == NULL) {
    strappy_set_error(error_out, "Could not allocate ISO8601 datetime token.");
    return NULL;
  }

  timestamp = strappy_cocoa_copy_timestamp_value_from_iso8601(iso8601,
                                                              unit,
                                                              error_out);
  free(iso8601);
  return timestamp;
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
	    int primary_key_position;

	    name = strappy_tools_sqlite_column_text(stmt, 1);
	    type = strappy_tools_sqlite_column_text(stmt, 2);
	    primary_key_position = sqlite3_column_int(stmt, 5);

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

	    if ((cJSON_AddStringToObject(column, "name", name) == NULL) ||
	        !strappy_tools_add_optional_string_to_object(column, "type", type) ||
	        ((primary_key_position > 0) &&
	         (cJSON_AddNumberToObject(column,
	                                  "primary_key_position",
	                                  (double)primary_key_position) == NULL)) ||
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

static int strappy_tools_catalog_exec(sqlite3 *db,
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

static int strappy_tools_ensure_helper_info_schema(sqlite3 *db,
                                                   char **error_out)
{
  static const char *user_info_sql =
    "CREATE TABLE IF NOT EXISTS helper_user_info ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "kind TEXT NOT NULL,"
    "subject TEXT NOT NULL,"
    "predicate TEXT NOT NULL,"
    "value TEXT NOT NULL,"
    "confidence REAL NOT NULL DEFAULT 0.75,"
    "source TEXT NOT NULL DEFAULT 'model_observed',"
    "status TEXT NOT NULL DEFAULT 'active',"
    "created_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
    "updated_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
    "last_used_at TEXT"
    ");";
  static const char *user_info_status_index_sql =
    "CREATE INDEX IF NOT EXISTS helper_user_info_status_idx "
    "ON helper_user_info(status, kind, updated_at);";
  static const char *database_info_sql =
    "CREATE TABLE IF NOT EXISTS helper_database_info ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "database_catalog_id INTEGER NOT NULL,"
    "database_assistant_id TEXT NOT NULL,"
    "database_size INTEGER NOT NULL DEFAULT 0,"
    "database_modified_at INTEGER NOT NULL DEFAULT 0,"
    "kind TEXT NOT NULL,"
    "title TEXT NOT NULL,"
    "content TEXT NOT NULL,"
    "evidence TEXT,"
    "confidence REAL NOT NULL DEFAULT 0.75,"
    "status TEXT NOT NULL DEFAULT 'active',"
    "created_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
    "updated_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
    "last_used_at TEXT,"
    "FOREIGN KEY(database_catalog_id) REFERENCES discovered_databases(id)"
    ");";
  static const char *database_info_status_index_sql =
    "CREATE INDEX IF NOT EXISTS helper_database_info_status_idx "
    "ON helper_database_info(database_catalog_id, status, kind, updated_at);";

  if ((db == NULL) ||
      !strappy_tools_catalog_exec(db,
                                  user_info_sql,
                                  "Could not create helper user info schema",
                                  error_out) ||
      !strappy_tools_catalog_exec(db,
                                  user_info_status_index_sql,
                                  "Could not create helper user info index",
                                  error_out) ||
      !strappy_tools_catalog_exec(db,
                                  database_info_sql,
                                  "Could not create helper database info schema",
                                  error_out) ||
      !strappy_tools_catalog_exec(db,
                                  database_info_status_index_sql,
                                  "Could not create helper database info index",
                                  error_out)) {
    return 0;
  }

  return 1;
}

static int strappy_tools_open_helper_info_database(const char *session_db_path,
                                                  sqlite3 **db_out,
                                                  char **error_out)
{
  sqlite3 *db;
  int rc;

  if (db_out == NULL) {
    strappy_set_error(error_out, "Helper info database output is missing.");
    return 0;
  }
  *db_out = NULL;

  if ((session_db_path == NULL) || (session_db_path[0] == '\0')) {
    strappy_set_error(error_out, "Catalog database path is not configured.");
    return 0;
  }

  if (!strappy_db_initialize(session_db_path, error_out)) {
    return 0;
  }

  db = NULL;
  rc = sqlite3_open_v2(session_db_path,
                       &db,
                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                         SQLITE_OPEN_FULLMUTEX,
                       NULL);
  if (rc != SQLITE_OK) {
    const char *message;

    message = "unknown sqlite error";
    if (db != NULL) {
      message = sqlite3_errmsg(db);
    }
    strappy_set_formatted_error(error_out,
                                "Could not open helper info database: %s",
                                message);
    if (db != NULL) {
      sqlite3_close(db);
    }
    return 0;
  }

  sqlite3_busy_timeout(db, 5000);
  if (!strappy_tools_ensure_helper_info_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  *db_out = db;
  return 1;
}

static int strappy_tools_bind_optional_text(sqlite3 *db,
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

static char *strappy_tools_create_like_pattern(const char *query)
{
  size_t length;
  size_t index;
  size_t output_index;
  size_t extra;
  char *pattern;

  if (!strappy_tools_string_has_value(query)) {
    return NULL;
  }

  length = strlen(query);
  extra = 0U;
  for (index = 0U; index < length; index++) {
    if ((query[index] == '%') || (query[index] == '_') ||
        (query[index] == '\\')) {
      extra++;
    }
  }

  if ((length > (((size_t)-1) - extra - 3U))) {
    return NULL;
  }

  pattern = (char *)malloc(length + extra + 3U);
  if (pattern == NULL) {
    return NULL;
  }

  output_index = 0U;
  pattern[output_index++] = '%';
  for (index = 0U; index < length; index++) {
    if ((query[index] == '%') || (query[index] == '_') ||
        (query[index] == '\\')) {
      pattern[output_index++] = '\\';
    }
    pattern[output_index++] = query[index];
  }
  pattern[output_index++] = '%';
  pattern[output_index] = '\0';

  return pattern;
}

static int strappy_tools_add_user_info_row(cJSON *array,
                                           sqlite3_stmt *stmt,
                                           char **error_out)
{
  cJSON *object;
  const char *kind;
  const char *subject;
  const char *predicate;
  const char *value;
  const char *source;
  const char *created_at;
  const char *updated_at;

  if ((array == NULL) || (stmt == NULL)) {
    strappy_set_error(error_out, "User info row request is incomplete.");
    return 0;
  }

  kind = strappy_tools_sqlite_column_text(stmt, 1);
  subject = strappy_tools_sqlite_column_text(stmt, 2);
  predicate = strappy_tools_sqlite_column_text(stmt, 3);
  value = strappy_tools_sqlite_column_text(stmt, 4);
  source = strappy_tools_sqlite_column_text(stmt, 6);
  created_at = strappy_tools_sqlite_column_text(stmt, 7);
  updated_at = strappy_tools_sqlite_column_text(stmt, 8);

  object = cJSON_CreateObject();
  if (object == NULL) {
    strappy_set_error(error_out, "Could not allocate user info row.");
    return 0;
  }

  if ((cJSON_AddNumberToObject(object,
                               "id",
                               (double)sqlite3_column_int64(stmt, 0)) == NULL) ||
      !strappy_tools_add_optional_string_to_object(object, "kind", kind) ||
      !strappy_tools_add_optional_string_to_object(object, "subject", subject) ||
      !strappy_tools_add_optional_string_to_object(object,
                                                   "predicate",
                                                   predicate) ||
      !strappy_tools_add_optional_string_to_object(object, "value", value) ||
      (cJSON_AddNumberToObject(object,
                               "confidence",
                               sqlite3_column_double(stmt, 5)) == NULL) ||
      !strappy_tools_add_optional_string_to_object(object, "source", source) ||
      !strappy_tools_add_optional_string_to_object(object,
                                                   "created_at",
                                                   created_at) ||
      !strappy_tools_add_optional_string_to_object(object,
                                                   "updated_at",
                                                   updated_at) ||
      !cJSON_AddItemToArray(array, object)) {
    cJSON_Delete(object);
    strappy_set_error(error_out, "Could not build user info row.");
    return 0;
  }

  return 1;
}

static char *strappy_tools_read_user_info(
  sqlite3 *db,
  const strappy_helper_user_info_read_arguments *arguments,
  char **error_out)
{
  static const char *sql =
    "SELECT id, kind, subject, predicate, value, confidence, source, "
    "created_at, updated_at "
    "FROM helper_user_info "
    "WHERE status = 'active' "
    "AND (?1 IS NULL OR kind = ?1) "
    "AND (?2 IS NULL OR subject LIKE ?2 ESCAPE '\\' "
    "OR predicate LIKE ?2 ESCAPE '\\' "
    "OR value LIKE ?2 ESCAPE '\\') "
    "ORDER BY updated_at DESC, id DESC "
    "LIMIT ?3;";
  sqlite3_stmt *stmt;
  cJSON *root;
  cJSON *facts;
  char *pattern;
  char *json;
  int rc;
  int count;

  if ((db == NULL) || (arguments == NULL)) {
    strappy_set_error(error_out, "helper_user_info_read request is incomplete.");
    return NULL;
  }

  pattern = strappy_tools_create_like_pattern(arguments->query);
  if ((arguments->query != NULL) && (pattern == NULL)) {
    strappy_set_error(error_out, "Could not allocate user info search pattern.");
    return NULL;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    free(pattern);
    strappy_set_formatted_error(error_out,
                                "Could not prepare helper_user_info_read: %s",
                                sqlite3_errmsg(db));
    return NULL;
  }

  if (!strappy_tools_bind_optional_text(db,
                                        stmt,
                                        1,
                                        arguments->kind,
                                        "Could not bind helper_user_info_read",
                                        error_out) ||
      !strappy_tools_bind_optional_text(db,
                                        stmt,
                                        2,
                                        pattern,
                                        "Could not bind helper_user_info_read",
                                        error_out) ||
      (sqlite3_bind_int(stmt, 3, arguments->limit) != SQLITE_OK)) {
    free(pattern);
    sqlite3_finalize(stmt);
    if ((error_out != NULL) && (*error_out == NULL)) {
      strappy_set_formatted_error(error_out,
                                  "Could not bind helper_user_info_read: %s",
                                  sqlite3_errmsg(db));
    }
    return NULL;
  }
  free(pattern);

  root = cJSON_CreateObject();
  facts = cJSON_CreateArray();
  if ((root == NULL) || (facts == NULL)) {
    cJSON_Delete(root);
    cJSON_Delete(facts);
    sqlite3_finalize(stmt);
    strappy_set_error(error_out, "Could not allocate user info result.");
    return NULL;
  }

  count = 0;
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    if (!strappy_tools_add_user_info_row(facts, stmt, error_out)) {
      cJSON_Delete(root);
      cJSON_Delete(facts);
      sqlite3_finalize(stmt);
      return NULL;
    }
    count++;
  }

  if (rc != SQLITE_DONE) {
    cJSON_Delete(root);
    cJSON_Delete(facts);
    strappy_set_formatted_error(error_out,
                                "Could not read helper_user_info: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return NULL;
  }
  sqlite3_finalize(stmt);

  if (!strappy_tools_add_bool_to_object(root, "ok", 1) ||
      !cJSON_AddItemToObject(root, "facts", facts)) {
    cJSON_Delete(root);
    cJSON_Delete(facts);
    strappy_set_error(error_out, "Could not build user info result.");
    return NULL;
  }
  facts = NULL;
  if (cJSON_AddNumberToObject(root, "count", (double)count) == NULL) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Could not build user info result.");
    return NULL;
  }

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize user info result.");
    return NULL;
  }

  return json;
}

static char *strappy_tools_remember_user_info(
  sqlite3 *db,
  const strappy_helper_user_info_remember_arguments *arguments,
  char **error_out)
{
  static const char *sql =
    "INSERT INTO helper_user_info "
    "(kind, subject, predicate, value, confidence, source, status) "
    "VALUES (?, ?, ?, ?, ?, COALESCE(?, 'model_observed'), 'active');";
  sqlite3_stmt *stmt;
  cJSON *root;
  char *json;
  long long id;
  int rc;

  if ((db == NULL) || (arguments == NULL)) {
    strappy_set_error(error_out,
                      "helper_user_info_remember request is incomplete.");
    return NULL;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare helper_user_info_remember: %s",
                                sqlite3_errmsg(db));
    return NULL;
  }

  if ((sqlite3_bind_text(stmt, 1, arguments->kind, -1, SQLITE_TRANSIENT) !=
       SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 2, arguments->subject, -1, SQLITE_TRANSIENT) !=
       SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 3, arguments->predicate, -1, SQLITE_TRANSIENT) !=
       SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 4, arguments->value, -1, SQLITE_TRANSIENT) !=
       SQLITE_OK) ||
      (sqlite3_bind_double(stmt, 5, arguments->confidence) != SQLITE_OK) ||
      !strappy_tools_bind_optional_text(db,
                                        stmt,
                                        6,
                                        arguments->source,
                                        "Could not bind helper_user_info_remember",
                                        error_out)) {
    sqlite3_finalize(stmt);
    if ((error_out != NULL) && (*error_out == NULL)) {
      strappy_set_formatted_error(error_out,
                                  "Could not bind helper_user_info_remember: %s",
                                  sqlite3_errmsg(db));
    }
    return NULL;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save helper_user_info: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return NULL;
  }
  sqlite3_finalize(stmt);

  id = (long long)sqlite3_last_insert_rowid(db);
  root = cJSON_CreateObject();
  if ((root == NULL) ||
      !strappy_tools_add_bool_to_object(root, "ok", 1) ||
      (cJSON_AddNumberToObject(root, "id", (double)id) == NULL) ||
      (cJSON_AddStringToObject(root, "status", "active") == NULL)) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Could not build user info remember result.");
    return NULL;
  }

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL) {
    strappy_set_error(error_out,
                      "Could not serialize user info remember result.");
    return NULL;
  }

  return json;
}

static char *strappy_tools_forget_info_row(sqlite3 *db,
                                           const char *table_name,
                                           long long id,
                                           char **error_out)
{
  sqlite3_stmt *stmt;
  cJSON *root;
  char *sql;
  char *json;
  int changed;
  int rc;

  if ((db == NULL) || !strappy_tools_string_has_value(table_name) ||
      (id <= 0)) {
    strappy_set_error(error_out, "Forget helper request is incomplete.");
    return NULL;
  }

  sql = strappy_tools_build_identifier_sql(
    "UPDATE ",
    table_name,
    " SET status = 'forgotten', "
    "updated_at = (strftime('%Y-%m-%dT%H:%M:%fZ','now')) "
    "WHERE id = ? AND status = 'active';");
  if (sql == NULL) {
    strappy_set_error(error_out, "Could not allocate forget helper SQL.");
    return NULL;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  free(sql);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare forget helper: %s",
                                sqlite3_errmsg(db));
    return NULL;
  }

  if (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)id) != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind forget helper: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return NULL;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not forget helper info: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return NULL;
  }
  sqlite3_finalize(stmt);

  changed = sqlite3_changes(db);
  root = cJSON_CreateObject();
  if ((root == NULL) ||
      !strappy_tools_add_bool_to_object(root, "ok", 1) ||
      (cJSON_AddNumberToObject(root, "id", (double)id) == NULL) ||
      !strappy_tools_add_bool_to_object(root,
                                        "forgotten",
                                        (changed > 0) ? 1 : 0)) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Could not build forget helper result.");
    return NULL;
  }

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize forget helper result.");
    return NULL;
  }

  return json;
}

static int strappy_tools_add_database_info_row(cJSON *array,
                                               sqlite3_stmt *stmt,
                                               char **error_out)
{
  cJSON *object;
  const char *database_id;
  const char *path;
  const char *filename;
  const char *kind;
  const char *title;
  const char *content;
  const char *evidence;
  const char *created_at;
  const char *updated_at;
  sqlite3_int64 stored_size;
  sqlite3_int64 stored_modified_at;
  sqlite3_int64 current_size;
  sqlite3_int64 current_modified_at;
  int database_available;
  int possibly_stale;

  if ((array == NULL) || (stmt == NULL)) {
    strappy_set_error(error_out, "Database info row request is incomplete.");
    return 0;
  }

  database_id = strappy_tools_sqlite_column_text(stmt, 1);
  path = strappy_tools_sqlite_column_text(stmt, 2);
  filename = strappy_tools_path_basename(path);
  kind = strappy_tools_sqlite_column_text(stmt, 3);
  title = strappy_tools_sqlite_column_text(stmt, 4);
  content = strappy_tools_sqlite_column_text(stmt, 5);
  evidence = strappy_tools_sqlite_column_text(stmt, 6);
  stored_size = sqlite3_column_int64(stmt, 8);
  stored_modified_at = sqlite3_column_int64(stmt, 9);
  database_available = (sqlite3_column_type(stmt, 10) != SQLITE_NULL) ? 1 : 0;
  current_size = database_available ? sqlite3_column_int64(stmt, 10) : 0;
  current_modified_at = database_available ? sqlite3_column_int64(stmt, 11) : 0;
  created_at = strappy_tools_sqlite_column_text(stmt, 12);
  updated_at = strappy_tools_sqlite_column_text(stmt, 13);
  possibly_stale =
    (!database_available || (stored_size != current_size) ||
     (stored_modified_at != current_modified_at)) ? 1 : 0;

  object = cJSON_CreateObject();
  if (object == NULL) {
    strappy_set_error(error_out, "Could not allocate database info row.");
    return 0;
  }

  if ((cJSON_AddNumberToObject(object,
                               "id",
                               (double)sqlite3_column_int64(stmt, 0)) == NULL) ||
      !strappy_tools_add_optional_string_to_object(object,
                                                   "database_id",
                                                   database_id) ||
      !strappy_tools_add_optional_string_to_object(object,
                                                   "filename",
                                                   filename) ||
      !strappy_tools_add_optional_string_to_object(object, "kind", kind) ||
      !strappy_tools_add_optional_string_to_object(object, "title", title) ||
      !strappy_tools_add_optional_string_to_object(object, "content", content) ||
      !strappy_tools_add_optional_string_to_object(object,
                                                   "evidence",
                                                   evidence) ||
      (cJSON_AddNumberToObject(object,
                               "confidence",
                               sqlite3_column_double(stmt, 7)) == NULL) ||
      !strappy_tools_add_bool_to_object(object,
                                        "database_available",
                                        database_available) ||
      !strappy_tools_add_bool_to_object(object,
                                        "possibly_stale",
                                        possibly_stale) ||
      !strappy_tools_add_optional_string_to_object(object,
                                                   "created_at",
                                                   created_at) ||
      !strappy_tools_add_optional_string_to_object(object,
                                                   "updated_at",
                                                   updated_at) ||
      !cJSON_AddItemToArray(array, object)) {
    cJSON_Delete(object);
    strappy_set_error(error_out, "Could not build database info row.");
    return 0;
  }

  return 1;
}

static char *strappy_tools_read_database_info(
  sqlite3 *db,
  const strappy_database_context_read_arguments *arguments,
  const strappy_discovered_database_record *record,
  const char *resource_dir,
  char **error_out)
{
  static const char *sql =
    "SELECT h.id, "
    "COALESCE(d.assistant_database_id, h.database_assistant_id), "
    "d.path, h.kind, h.title, h.content, h.evidence, h.confidence, "
    "h.database_size, h.database_modified_at, d.size, d.modified_at, "
    "h.created_at, h.updated_at "
    "FROM helper_database_info h "
    "LEFT JOIN discovered_databases d ON d.id = h.database_catalog_id "
    "WHERE h.status = 'active' "
    "AND (?1 IS NULL OR h.database_assistant_id = ?1 "
    "OR d.assistant_database_id = ?1) "
    "AND (?2 IS NULL OR h.kind = ?2) "
    "AND (?3 IS NULL OR h.title LIKE ?3 ESCAPE '\\' "
    "OR h.content LIKE ?3 ESCAPE '\\' "
    "OR h.evidence LIKE ?3 ESCAPE '\\') "
    "ORDER BY h.updated_at DESC, h.id DESC "
    "LIMIT ?4;";
  sqlite3_stmt *stmt;
  cJSON *root;
  cJSON *items;
  char *pattern;
  char *json;
  int rc;
  int count;

  if ((db == NULL) || (arguments == NULL)) {
    strappy_set_error(error_out,
                      "database_context_read request is incomplete.");
    return NULL;
  }

  pattern = strappy_tools_create_like_pattern(arguments->query);
  if ((arguments->query != NULL) && (pattern == NULL)) {
    strappy_set_error(error_out,
                      "Could not allocate database info search pattern.");
    return NULL;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    free(pattern);
    strappy_set_formatted_error(
      error_out,
      "Could not prepare database_context_read: %s",
      sqlite3_errmsg(db));
    return NULL;
  }

  if (!strappy_tools_bind_optional_text(
        db,
        stmt,
        1,
        arguments->database_id,
        "Could not bind database_context_read",
        error_out) ||
      !strappy_tools_bind_optional_text(db,
                                        stmt,
                                        2,
                                        arguments->kind,
                                        "Could not bind database_context_read",
                                        error_out) ||
      !strappy_tools_bind_optional_text(db,
                                        stmt,
                                        3,
                                        pattern,
                                        "Could not bind database_context_read",
                                        error_out) ||
      (sqlite3_bind_int(stmt, 4, arguments->limit) != SQLITE_OK)) {
    free(pattern);
    sqlite3_finalize(stmt);
    if ((error_out != NULL) && (*error_out == NULL)) {
      strappy_set_formatted_error(
        error_out,
        "Could not bind database_context_read: %s",
        sqlite3_errmsg(db));
    }
    return NULL;
  }
  free(pattern);

  root = cJSON_CreateObject();
  items = cJSON_CreateArray();
  if ((root == NULL) || (items == NULL)) {
    cJSON_Delete(root);
    cJSON_Delete(items);
    sqlite3_finalize(stmt);
    strappy_set_error(error_out, "Could not allocate database info result.");
    return NULL;
  }

  count = 0;
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    if (!strappy_tools_add_database_info_row(items, stmt, error_out)) {
      cJSON_Delete(root);
      cJSON_Delete(items);
      sqlite3_finalize(stmt);
      return NULL;
    }
    count++;
  }

  if (rc != SQLITE_DONE) {
    cJSON_Delete(root);
    cJSON_Delete(items);
    strappy_set_formatted_error(error_out,
                                "Could not read helper_database_info: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return NULL;
  }
  sqlite3_finalize(stmt);

  if ((record != NULL) &&
      (!strappy_tools_add_database_metadata(root, record) ||
       !strappy_tools_add_database_guidance(root,
                                            record,
                                            resource_dir,
                                            0,
                                            1,
                                            1,
                                            error_out) ||
       !strappy_tools_add_database_schema_context(root, record, error_out))) {
    cJSON_Delete(root);
    cJSON_Delete(items);
    if ((error_out != NULL) && (*error_out == NULL)) {
      strappy_set_error(error_out, "Could not build database context result.");
    }
    return NULL;
  }

  if (!strappy_tools_add_bool_to_object(root, "ok", 1) ||
      !cJSON_AddItemToObject(root, "database_info", items)) {
    cJSON_Delete(root);
    cJSON_Delete(items);
    strappy_set_error(error_out, "Could not build database info result.");
    return NULL;
  }
  items = NULL;
  if (cJSON_AddNumberToObject(root, "count", (double)count) == NULL) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Could not build database info result.");
    return NULL;
  }

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize database info result.");
    return NULL;
  }

  return json;
}

static char *strappy_tools_remember_database_info(
  sqlite3 *db,
  const strappy_discovered_database_record *record,
  const strappy_helper_database_info_remember_arguments *arguments,
  char **error_out)
{
  static const char *sql =
    "INSERT INTO helper_database_info "
    "(database_catalog_id, database_assistant_id, database_size, "
    "database_modified_at, kind, title, content, evidence, confidence, status) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 'active');";
  sqlite3_stmt *stmt;
  cJSON *root;
  char *json;
  long long id;
  int rc;

  if ((db == NULL) || (record == NULL) || (arguments == NULL)) {
    strappy_set_error(error_out,
                      "helper_database_info_remember request is incomplete.");
    return NULL;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(
      error_out,
      "Could not prepare helper_database_info_remember: %s",
      sqlite3_errmsg(db));
    return NULL;
  }

  if ((sqlite3_bind_int64(stmt, 1, (sqlite3_int64)record->catalog_id) !=
       SQLITE_OK) ||
      (sqlite3_bind_text(stmt,
                         2,
                         record->assistant_database_id,
                         -1,
                         SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)record->size) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)record->modified_at) !=
       SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 5, arguments->kind, -1, SQLITE_TRANSIENT) !=
       SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 6, arguments->title, -1, SQLITE_TRANSIENT) !=
       SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 7, arguments->content, -1, SQLITE_TRANSIENT) !=
       SQLITE_OK) ||
      !strappy_tools_bind_optional_text(
        db,
        stmt,
        8,
        arguments->evidence,
        "Could not bind helper_database_info_remember",
        error_out) ||
      (sqlite3_bind_double(stmt, 9, arguments->confidence) != SQLITE_OK)) {
    sqlite3_finalize(stmt);
    if ((error_out != NULL) && (*error_out == NULL)) {
      strappy_set_formatted_error(
        error_out,
        "Could not bind helper_database_info_remember: %s",
        sqlite3_errmsg(db));
    }
    return NULL;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save helper_database_info: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return NULL;
  }
  sqlite3_finalize(stmt);

  id = (long long)sqlite3_last_insert_rowid(db);
  root = cJSON_CreateObject();
  if ((root == NULL) ||
      !strappy_tools_add_bool_to_object(root, "ok", 1) ||
      (cJSON_AddNumberToObject(root, "id", (double)id) == NULL) ||
      (cJSON_AddStringToObject(root,
                               "database_id",
                               record->assistant_database_id) == NULL) ||
      (cJSON_AddStringToObject(root, "status", "active") == NULL)) {
    cJSON_Delete(root);
    strappy_set_error(error_out,
                      "Could not build database info remember result.");
    return NULL;
  }

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL) {
    strappy_set_error(error_out,
                      "Could not serialize database info remember result.");
    return NULL;
  }

  return json;
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
      !strappy_tools_add_bool_to_object(schema, "truncated", truncated)) {
    cJSON_Delete(schema);
    strappy_set_error(error_out, "Could not build database schema result.");
    return NULL;
  }

  return schema;
}

static int strappy_tools_add_database_schema_context(
  cJSON *object,
  const strappy_discovered_database_record *record,
  char **error_out)
{
  cJSON *schema;
  char *schema_error;

  if ((object == NULL) || (record == NULL)) {
    return 0;
  }

  schema_error = NULL;
  schema = strappy_tools_read_database_schema(record, &schema_error);
  if (schema != NULL) {
    if (!cJSON_AddItemToObject(object, "schema", schema)) {
      cJSON_Delete(schema);
      free(schema_error);
      strappy_set_error(error_out, "Could not build database schema result.");
      return 0;
    }
    free(schema_error);
    return 1;
  }

  if (!strappy_tools_add_null_to_object(object, "schema") ||
      (cJSON_AddStringToObject(object,
                               "schema_error",
                               (schema_error != NULL) ?
                                 schema_error : "Could not read database schema.") ==
       NULL)) {
    free(schema_error);
    strappy_set_error(error_out, "Could not build database schema result.");
    return 0;
  }

  free(schema_error);
  return 1;
}

static int strappy_tools_add_database_list_info_record(
  cJSON *databases,
  const strappy_discovered_database_record *record,
  const char *resource_dir,
  char **error_out)
{
  cJSON *object;

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

  if (!strappy_tools_add_database_guidance(object,
                                           record,
                                           resource_dir,
                                           1,
                                           0,
                                           1,
                                           error_out)) {
    cJSON_Delete(object);
    if ((error_out != NULL) && (*error_out == NULL)) {
      strappy_set_error(error_out, "Could not build database guidance result.");
    }
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
                                                  const char *resource_dir,
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
      strappy_tools_set_error_with_database_guidance(
        error_out,
        "database_query database_id is not approved or is no longer valid.",
        resource_dir,
        "database_query_unavailable_database_id");
    } else {
      strappy_tools_set_error_with_database_guidance(
        error_out,
        "database_query database_id was not found.",
        resource_dir,
        "database_query_missing_database_id");
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

static char *strappy_tools_execute_helper_datetime(
  const char *tool_name,
  const char *arguments_json,
  const char *items_argument_name,
  strappy_cocoa_timestamp_unit default_unit,
  strappy_tools_datetime_token_converter converter,
  char **error_out)
{
  strappy_helper_datetime_arguments arguments;
  strappy_helper_text_buffer buffer;
  const char *cursor;
  size_t item_count;

  strappy_helper_datetime_arguments_init(&arguments, default_unit);
  if (!strappy_tools_parse_helper_datetime_arguments(tool_name,
                                                     arguments_json,
                                                     items_argument_name,
                                                     default_unit,
                                                     &arguments,
                                                     error_out)) {
    return NULL;
  }

  strappy_helper_text_buffer_init(&buffer);
  cursor = arguments.items;
  item_count = 0U;
  while (cursor != NULL) {
    const char *item_start;
    const char *item_end;
    const char *item_delimiter;
    char *converted;

    converted = NULL;
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
      strappy_helper_datetime_arguments_destroy(&arguments, default_unit);
      strappy_set_formatted_error(error_out,
                                  "%s %s contains an empty item.",
                                  tool_name,
                                  items_argument_name);
      return NULL;
    }

    if (item_count >= STRAPPY_HELPER_DATETIME_MAX_ITEMS) {
      strappy_helper_text_buffer_destroy(&buffer);
      strappy_helper_datetime_arguments_destroy(&arguments, default_unit);
      strappy_set_formatted_error(
        error_out,
        "%s accepts at most %u items.",
        tool_name,
        (unsigned int)STRAPPY_HELPER_DATETIME_MAX_ITEMS);
      return NULL;
    }

    converted = converter(item_start,
                          (size_t)(item_end - item_start),
                          arguments.unit,
                          error_out);
    if (converted == NULL) {
      strappy_helper_text_buffer_destroy(&buffer);
      strappy_helper_datetime_arguments_destroy(&arguments, default_unit);
      return NULL;
    }

    if (((item_count > 0U) &&
        !strappy_helper_text_buffer_append(&buffer, ",")) ||
        !strappy_helper_text_buffer_append(&buffer, converted)) {
      free(converted);
      strappy_helper_text_buffer_destroy(&buffer);
      strappy_helper_datetime_arguments_destroy(&arguments, default_unit);
      strappy_set_formatted_error(error_out,
                                  "Could not build %s result.",
                                  tool_name);
      return NULL;
    }

    free(converted);
    item_count++;
    cursor = (*item_delimiter == ',') ? item_delimiter + 1 : NULL;
  }

  strappy_helper_datetime_arguments_destroy(&arguments, default_unit);
  if (item_count == 0U) {
    strappy_helper_text_buffer_destroy(&buffer);
    strappy_set_formatted_error(error_out,
                                "%s requires at least one item.",
                                tool_name);
    return NULL;
  }

  return buffer.data;
}

static char *strappy_tools_execute_helper_datetime_to_iso8601(
  const char *arguments_json,
  char **error_out)
{
  return strappy_tools_execute_helper_datetime(
    STRAPPY_TOOL_HELPER_DATETIME_TO_ISO8601,
    arguments_json,
    "timestamps",
    STRAPPY_COCOA_TIMESTAMP_UNIT_UNIX_SECONDS,
    strappy_tools_convert_timestamp_token_to_iso8601,
    error_out);
}

static char *strappy_tools_execute_helper_datetime_from_iso8601(
  const char *arguments_json,
  char **error_out)
{
  return strappy_tools_execute_helper_datetime(
    STRAPPY_TOOL_HELPER_DATETIME_FROM_ISO8601,
    arguments_json,
    "datetimes",
    STRAPPY_COCOA_TIMESTAMP_UNIT_APPLE_SECONDS,
    strappy_tools_convert_iso8601_token_to_timestamp,
    error_out);
}

static char *strappy_tools_execute_helper_user_info_read(
  const char *session_db_path,
  const char *arguments_json,
  char **error_out)
{
  strappy_helper_user_info_read_arguments arguments;
  sqlite3 *db;
  char *json;

  strappy_helper_user_info_read_arguments_init(&arguments);
  if (!strappy_tools_parse_helper_user_info_read_arguments(arguments_json,
                                                           &arguments,
                                                           error_out)) {
    return NULL;
  }

  db = NULL;
  if (!strappy_tools_open_helper_info_database(session_db_path,
                                               &db,
                                               error_out)) {
    strappy_helper_user_info_read_arguments_destroy(&arguments);
    return NULL;
  }

  json = strappy_tools_read_user_info(db, &arguments, error_out);
  sqlite3_close(db);
  strappy_helper_user_info_read_arguments_destroy(&arguments);
  return json;
}

static char *strappy_tools_execute_helper_user_info_remember(
  const char *session_db_path,
  const char *arguments_json,
  char **error_out)
{
  strappy_helper_user_info_remember_arguments arguments;
  sqlite3 *db;
  char *json;

  strappy_helper_user_info_remember_arguments_init(&arguments);
  if (!strappy_tools_parse_helper_user_info_remember_arguments(arguments_json,
                                                               &arguments,
                                                               error_out)) {
    return NULL;
  }

  db = NULL;
  if (!strappy_tools_open_helper_info_database(session_db_path,
                                               &db,
                                               error_out)) {
    strappy_helper_user_info_remember_arguments_destroy(&arguments);
    return NULL;
  }

  json = strappy_tools_remember_user_info(db, &arguments, error_out);
  sqlite3_close(db);
  strappy_helper_user_info_remember_arguments_destroy(&arguments);
  return json;
}

static char *strappy_tools_execute_helper_user_info_forget(
  const char *session_db_path,
  const char *arguments_json,
  char **error_out)
{
  strappy_helper_info_forget_arguments arguments;
  sqlite3 *db;
  char *json;

  if (!strappy_tools_parse_id_argument(STRAPPY_TOOL_HELPER_USER_INFO_FORGET,
                                       arguments_json,
                                       &arguments,
                                       error_out)) {
    return NULL;
  }

  db = NULL;
  if (!strappy_tools_open_helper_info_database(session_db_path,
                                               &db,
                                               error_out)) {
    return NULL;
  }

  json = strappy_tools_forget_info_row(db,
                                       "helper_user_info",
                                       arguments.id,
                                       error_out);
  sqlite3_close(db);
  return json;
}

static char *strappy_tools_execute_database_context_read(
  const char *session_db_path,
  const char *resource_dir,
  const char *arguments_json,
  char **error_out)
{
  strappy_database_context_read_arguments arguments;
  strappy_discovered_database_record_list list;
  const strappy_discovered_database_record *record;
  sqlite3 *db;
  char *json;
  int matched_unavailable;

  strappy_database_context_read_arguments_init(&arguments);
  if (!strappy_tools_parse_database_context_read_arguments(arguments_json,
                                                               &arguments,
                                                               error_out)) {
    return NULL;
  }

  strappy_discovered_database_record_list_init(&list);
  record = NULL;
  if (strappy_tools_string_has_value(arguments.database_id)) {
    if (!strappy_db_list_discovered_databases(session_db_path,
                                             &list,
                                             error_out)) {
      strappy_database_context_read_arguments_destroy(&arguments);
      return NULL;
    }

    matched_unavailable = 0;
    record = strappy_tools_find_database_query_record(&list,
                                                      arguments.database_id,
                                                      &matched_unavailable);
    if (record == NULL) {
      if (matched_unavailable) {
        strappy_tools_set_error_with_database_guidance(
          error_out,
          "database_context_read database_id is not approved or is no "
          "longer valid.",
          resource_dir,
          "database_context_read_unavailable_database_id");
      } else {
        strappy_tools_set_error_with_database_guidance(
          error_out,
          "database_context_read database_id was not found.",
          resource_dir,
          "database_context_read_missing_database_id");
      }
      strappy_discovered_database_record_list_destroy(&list);
      strappy_database_context_read_arguments_destroy(&arguments);
      return NULL;
    }
  }

  db = NULL;
  if (!strappy_tools_open_helper_info_database(session_db_path,
                                               &db,
                                               error_out)) {
    strappy_discovered_database_record_list_destroy(&list);
    strappy_database_context_read_arguments_destroy(&arguments);
    return NULL;
  }

  json = strappy_tools_read_database_info(db,
                                          &arguments,
                                          record,
                                          resource_dir,
                                          error_out);
  sqlite3_close(db);
  strappy_discovered_database_record_list_destroy(&list);
  strappy_database_context_read_arguments_destroy(&arguments);
  return json;
}

static char *strappy_tools_execute_helper_database_info_remember(
  const char *session_db_path,
  const char *resource_dir,
  const char *arguments_json,
  char **error_out)
{
  strappy_helper_database_info_remember_arguments arguments;
  strappy_discovered_database_record_list list;
  const strappy_discovered_database_record *record;
  sqlite3 *db;
  char *json;
  int matched_unavailable;

  strappy_helper_database_info_remember_arguments_init(&arguments);
  if (!strappy_tools_parse_helper_database_info_remember_arguments(
        arguments_json,
        &arguments,
        error_out)) {
    return NULL;
  }

  strappy_discovered_database_record_list_init(&list);
  if (!strappy_db_list_discovered_databases(session_db_path,
                                           &list,
                                           error_out)) {
    strappy_helper_database_info_remember_arguments_destroy(&arguments);
    return NULL;
  }

  matched_unavailable = 0;
  record = strappy_tools_find_database_query_record(&list,
                                                    arguments.database_id,
                                                    &matched_unavailable);
  if (record == NULL) {
    if (matched_unavailable) {
      strappy_tools_set_error_with_database_guidance(
        error_out,
        "helper_database_info_remember database_id is not approved or is no "
        "longer valid.",
        resource_dir,
        "helper_database_info_remember_unavailable_database_id");
    } else {
      strappy_tools_set_error_with_database_guidance(
        error_out,
        "helper_database_info_remember database_id was not found.",
        resource_dir,
        "helper_database_info_remember_missing_database_id");
    }
    strappy_discovered_database_record_list_destroy(&list);
    strappy_helper_database_info_remember_arguments_destroy(&arguments);
    return NULL;
  }

  db = NULL;
  if (!strappy_tools_open_helper_info_database(session_db_path,
                                               &db,
                                               error_out)) {
    strappy_discovered_database_record_list_destroy(&list);
    strappy_helper_database_info_remember_arguments_destroy(&arguments);
    return NULL;
  }

  json = strappy_tools_remember_database_info(db,
                                              record,
                                              &arguments,
                                              error_out);
  sqlite3_close(db);
  strappy_discovered_database_record_list_destroy(&list);
  strappy_helper_database_info_remember_arguments_destroy(&arguments);
  return json;
}

static char *strappy_tools_execute_helper_database_info_forget(
  const char *session_db_path,
  const char *arguments_json,
  char **error_out)
{
  strappy_helper_info_forget_arguments arguments;
  sqlite3 *db;
  char *json;

  if (!strappy_tools_parse_id_argument(STRAPPY_TOOL_HELPER_DATABASE_INFO_FORGET,
                                       arguments_json,
                                       &arguments,
                                       error_out)) {
    return NULL;
  }

  db = NULL;
  if (!strappy_tools_open_helper_info_database(session_db_path,
                                               &db,
                                               error_out)) {
    return NULL;
  }

  json = strappy_tools_forget_info_row(db,
                                       "helper_database_info",
                                       arguments.id,
                                       error_out);
  sqlite3_close(db);
  return json;
}

static char *strappy_tools_execute_database_list_info(
  const char *session_db_path,
  const char *resource_dir,
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
                                                     resource_dir,
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
      !strappy_tools_add_database_user_action(root,
                                              availability_state,
                                              resource_dir,
                                              error_out)) {
    cJSON_Delete(root);
    strappy_discovered_database_record_list_destroy(&list);
    if ((error_out != NULL) && (*error_out == NULL)) {
      strappy_set_error(error_out, "Could not build tool result.");
    }
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
                            const char *resource_dir,
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
                                                   resource_dir,
                                                   arguments_json,
                                                   error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_DATABASE_QUERY) == 0) {
    return strappy_tools_execute_database_query(session_db_path,
                                                resource_dir,
                                                arguments_json,
                                                error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_HELPER_DATETIME_TO_ISO8601) == 0) {
    return strappy_tools_execute_helper_datetime_to_iso8601(arguments_json,
                                                            error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_HELPER_DATETIME_FROM_ISO8601) == 0) {
    return strappy_tools_execute_helper_datetime_from_iso8601(arguments_json,
                                                              error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_HELPER_USER_INFO_READ) == 0) {
    return strappy_tools_execute_helper_user_info_read(session_db_path,
                                                       arguments_json,
                                                       error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_HELPER_USER_INFO_REMEMBER) == 0) {
    return strappy_tools_execute_helper_user_info_remember(session_db_path,
                                                           arguments_json,
                                                           error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_HELPER_USER_INFO_FORGET) == 0) {
    return strappy_tools_execute_helper_user_info_forget(session_db_path,
                                                         arguments_json,
                                                         error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_DATABASE_CONTEXT_READ) == 0) {
    return strappy_tools_execute_database_context_read(session_db_path,
                                                       resource_dir,
                                                       arguments_json,
                                                       error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_HELPER_DATABASE_INFO_REMEMBER) == 0) {
    return strappy_tools_execute_helper_database_info_remember(session_db_path,
                                                               resource_dir,
                                                               arguments_json,
                                                               error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_HELPER_DATABASE_INFO_FORGET) == 0) {
    return strappy_tools_execute_helper_database_info_forget(session_db_path,
                                                             arguments_json,
                                                             error_out);
  }

  strappy_set_formatted_error(error_out, "Tool is not registered: %s", tool_name);
  return NULL;
}
