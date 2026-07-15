#include "strappy_tools.h"

#include "strappy_cocoa.h"
#include "strappy_core.h"
#include "strappy_db.h"

#include <cJSON.h>
#include <errno.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define STRAPPY_TOOL_GUIDANCE_RESOURCE "GuidanceTools.json"
#define STRAPPY_TOOL_SERVER_TOOLS_KEY "server_tools"
#define STRAPPY_TOOL_SERVER_FEATURE_KEY "feature"
#define STRAPPY_TOOL_SERVER_FEATURE_WEB_SEARCH "web_search"
#define STRAPPY_TOOL_DISPLAY_METADATA_KEY "x-strappy-display"
#define STRAPPY_TOOL_DISPLAY_LABEL_KEY "label"
#define STRAPPY_TOOL_DISPLAY_PROMOTED_ARGUMENT_KEY "promoted_argument"
#define STRAPPY_TOOL_DISPLAY_PROMOTED_PATH_KEY "promoted_path"
#define STRAPPY_TOOL_DISPLAY_TRANSFORM_KEY "transform"
#define STRAPPY_DATABASE_GUIDANCE_RESOURCE "GuidanceDatabase.json"
#define STRAPPY_FONTAWESOME_ICONS_RESOURCE "FontAwesomeIcons.json"
#define STRAPPY_DATABASE_QUERY_MAX_ROWS 100
#define STRAPPY_DATABASE_QUERY_MAX_COLUMNS 64
#define STRAPPY_DATABASE_QUERY_MAX_DATABASE_ID_BYTES 64U
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
#define STRAPPY_HELPER_INFO_MAX_VALUE_BYTES 2048U
#define STRAPPY_HELPER_INFO_MAX_CONTENT_BYTES 4096U
#define STRAPPY_HELPER_INFO_DEFAULT_LIMIT 20
#define STRAPPY_DATABASE_CONTEXT_HINT_LIMIT 40
#define STRAPPY_DATABASE_CONTEXT_TABLE_LIMIT 20
#define STRAPPY_DATABASE_CONTEXT_VIEW_LIMIT 20
#define STRAPPY_DATABASE_CONTEXT_QUERY_GUIDANCE \
  "Use database_query to inspect columns and view definitions with SELECT " \
  "type, sql FROM sqlite_schema WHERE name = 'table_or_view_name', then use " \
  "targeted SELECT queries to inspect rows."
#define STRAPPY_DATABASE_LIST_EMPTY_GUIDANCE \
  "The user has not approved any databases for use."
#define STRAPPY_HELPER_SESSION_NAME_MAX_BYTES 96U
#define STRAPPY_HELPER_FONTAWESOME_MAX_QUERY_BYTES 128U
#define STRAPPY_HELPER_FONTAWESOME_MAX_SHORTCODE_BYTES 96U
#define STRAPPY_HELPER_FONTAWESOME_MAX_SHORTCODES 32U
#define STRAPPY_HELPER_FONTAWESOME_RESULT_LIMIT 20

typedef enum strappy_tool_kind {
  STRAPPY_TOOL_KIND_DATABASE = 1,
  STRAPPY_TOOL_KIND_HELPER = 2
} strappy_tool_kind;

typedef struct strappy_tool_definition {
  const char *name;
  strappy_tool_kind kind;
} strappy_tool_definition;

typedef struct strappy_server_tool_definition {
  const char *type;
  const char *feature;
} strappy_server_tool_definition;

typedef struct strappy_database_query_arguments {
  char *database_id;
  char *sql;
} strappy_database_query_arguments;

typedef struct strappy_helper_datetime_arguments {
  char **items;
  size_t item_count;
  strappy_cocoa_timestamp_unit unit;
} strappy_helper_datetime_arguments;

typedef struct strappy_memory_user_fact_remember_arguments {
  char *fact;
} strappy_memory_user_fact_remember_arguments;

typedef struct strappy_helper_session_name_write_arguments {
  char *name;
} strappy_helper_session_name_write_arguments;

typedef struct strappy_helper_info_forget_arguments {
  long long id;
} strappy_helper_info_forget_arguments;

typedef struct strappy_memory_database_hint_remember_arguments {
  char *database_id;
  char *hint;
} strappy_memory_database_hint_remember_arguments;

typedef struct strappy_database_context_read_arguments {
  char *database_id;
} strappy_database_context_read_arguments;

typedef struct strappy_helper_fontawesome_shortcode_search_arguments {
  char *query;
} strappy_helper_fontawesome_shortcode_search_arguments;

typedef struct strappy_helper_fontawesome_shortcode_confirm_arguments {
  char **shortcodes;
  size_t shortcode_count;
} strappy_helper_fontawesome_shortcode_confirm_arguments;

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

static int strappy_tools_helper_is_space(char value);

static const strappy_tool_definition strappy_tool_definitions[] = {
  { STRAPPY_TOOL_DATABASE_LIST_INFO, STRAPPY_TOOL_KIND_DATABASE },
  { STRAPPY_TOOL_DATABASE_QUERY, STRAPPY_TOOL_KIND_DATABASE },
  { STRAPPY_TOOL_HELPER_DATETIME_TO_ISO8601, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_HELPER_DATETIME_FROM_ISO8601, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_MEMORY_USER_FACT_READ, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_MEMORY_USER_FACT_FORGET, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_DATABASE_CONTEXT_READ, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_MEMORY_DATABASE_HINT_FORGET, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_SEARCH, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_CONFIRM, STRAPPY_TOOL_KIND_HELPER }
};

static const size_t strappy_tool_definition_count =
  sizeof(strappy_tool_definitions) / sizeof(strappy_tool_definitions[0]);

static const strappy_server_tool_definition strappy_server_tool_definitions[] = {
  { STRAPPY_TOOL_OPENROUTER_WEB_SEARCH,
    STRAPPY_TOOL_SERVER_FEATURE_WEB_SEARCH },
  { STRAPPY_TOOL_OPENROUTER_WEB_FETCH,
    STRAPPY_TOOL_SERVER_FEATURE_WEB_SEARCH }
};

static const size_t strappy_server_tool_definition_count =
  sizeof(strappy_server_tool_definitions) /
  sizeof(strappy_server_tool_definitions[0]);

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

static const strappy_server_tool_definition *
strappy_tools_find_server_definition(const char *tool_type)
{
  size_t index;

  if ((tool_type == NULL) || (tool_type[0] == '\0')) {
    return NULL;
  }

  for (index = 0U; index < strappy_server_tool_definition_count; index++) {
    if (strcmp(strappy_server_tool_definitions[index].type, tool_type) == 0) {
      return &strappy_server_tool_definitions[index];
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
  strappy_helper_datetime_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->items = NULL;
  arguments->item_count = 0U;
  arguments->unit = (strappy_cocoa_timestamp_unit)0;
}

static void strappy_helper_datetime_arguments_destroy(
  strappy_helper_datetime_arguments *arguments)
{
  size_t index;

  if (arguments == NULL) {
    return;
  }

  for (index = 0U; index < arguments->item_count; index++) {
    free(arguments->items[index]);
  }
  free(arguments->items);
  strappy_helper_datetime_arguments_init(arguments);
}

static void strappy_memory_user_fact_remember_arguments_init(
  strappy_memory_user_fact_remember_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->fact = NULL;
}

static void strappy_memory_user_fact_remember_arguments_destroy(
  strappy_memory_user_fact_remember_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  free(arguments->fact);
  strappy_memory_user_fact_remember_arguments_init(arguments);
}

static void strappy_helper_session_name_write_arguments_init(
  strappy_helper_session_name_write_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->name = NULL;
}

static void strappy_helper_session_name_write_arguments_destroy(
  strappy_helper_session_name_write_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  free(arguments->name);
  strappy_helper_session_name_write_arguments_init(arguments);
}

static void strappy_helper_info_forget_arguments_init(
  strappy_helper_info_forget_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->id = 0;
}

static void strappy_memory_database_hint_remember_arguments_init(
  strappy_memory_database_hint_remember_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->database_id = NULL;
  arguments->hint = NULL;
}

static void strappy_memory_database_hint_remember_arguments_destroy(
  strappy_memory_database_hint_remember_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  free(arguments->database_id);
  free(arguments->hint);
  strappy_memory_database_hint_remember_arguments_init(arguments);
}

static void strappy_database_context_read_arguments_init(
  strappy_database_context_read_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->database_id = NULL;
}

static void strappy_database_context_read_arguments_destroy(
  strappy_database_context_read_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  free(arguments->database_id);
  strappy_database_context_read_arguments_init(arguments);
}

static void strappy_helper_fontawesome_shortcode_search_arguments_init(
  strappy_helper_fontawesome_shortcode_search_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->query = NULL;
}

static void strappy_helper_fontawesome_shortcode_search_arguments_destroy(
  strappy_helper_fontawesome_shortcode_search_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  free(arguments->query);
  strappy_helper_fontawesome_shortcode_search_arguments_init(arguments);
}

static void strappy_helper_fontawesome_shortcode_confirm_arguments_init(
  strappy_helper_fontawesome_shortcode_confirm_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->shortcodes = NULL;
  arguments->shortcode_count = 0U;
}

static void strappy_helper_fontawesome_shortcode_confirm_arguments_destroy(
  strappy_helper_fontawesome_shortcode_confirm_arguments *arguments)
{
  size_t index;

  if (arguments == NULL) {
    return;
  }

  for (index = 0U; index < arguments->shortcode_count; index++) {
    free(arguments->shortcodes[index]);
  }
  free(arguments->shortcodes);
  strappy_helper_fontawesome_shortcode_confirm_arguments_init(arguments);
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

static int strappy_tools_display_transform_matches_type(
  const char *transform,
  const char *property_type)
{
  if ((transform == NULL) || (transform[0] == '\0') ||
      (strcmp(transform, "text") == 0)) {
    return (strcmp(property_type, "array") != 0) &&
      (strcmp(property_type, "object") != 0);
  }
  if (strcmp(transform, "database_filename") == 0) {
    return strcmp(property_type, "string") == 0;
  }
  if (strcmp(transform, "url") == 0) {
    return strcmp(property_type, "string") == 0;
  }
  if (strcmp(transform, "database_hint_filename") == 0) {
    return strcmp(property_type, "integer") == 0;
  }
  if (strcmp(transform, "comma_separated") == 0) {
    return strcmp(property_type, "array") == 0;
  }
  if (strcmp(transform, "identifier") == 0) {
    return (strcmp(property_type, "integer") == 0) ||
      (strcmp(property_type, "string") == 0);
  }
  return 0;
}

static const char *strappy_tools_schema_property_value_type(cJSON *type)
{
  cJSON *item;
  const char *value_type;
  int null_count;

  if (cJSON_IsString(type) && (type->valuestring != NULL) &&
      (type->valuestring[0] != '\0')) {
    return type->valuestring;
  }
  if (!cJSON_IsArray(type)) {
    return NULL;
  }

  value_type = NULL;
  null_count = 0;
  for (item = type->child; item != NULL; item = item->next) {
    if (!cJSON_IsString(item) || (item->valuestring == NULL) ||
        (item->valuestring[0] == '\0')) {
      return NULL;
    }
    if (strcmp(item->valuestring, "null") == 0) {
      null_count++;
    } else if (value_type == NULL) {
      value_type = item->valuestring;
    } else {
      return NULL;
    }
  }
  return ((null_count == 1) && (value_type != NULL)) ? value_type : NULL;
}

static int strappy_tools_validate_display_metadata(cJSON *tool,
                                                    const char *tool_name,
                                                    char **error_out)
{
  cJSON *display;
  cJSON *field;
  cJSON *promoted_argument;
  cJSON *transform;
  cJSON *function;
  cJSON *parameters;
  cJSON *properties;
  cJSON *property;
  cJSON *property_type;
  const char *property_type_name;
  const char *transform_name;
  int promoted_argument_count;
  int transform_count;

  display = cJSON_GetObjectItemCaseSensitive(
    tool,
    STRAPPY_TOOL_DISPLAY_METADATA_KEY);
  if (display == NULL) {
    return 1;
  }
  if (!cJSON_IsObject(display)) {
    strappy_set_formatted_error(error_out,
                                "%s for %s must be an object.",
                                STRAPPY_TOOL_DISPLAY_METADATA_KEY,
                                tool_name);
    return 0;
  }
  promoted_argument_count = 0;
  transform_count = 0;
  for (field = display->child; field != NULL; field = field->next) {
    if ((field->string == NULL) ||
        ((strcmp(field->string,
                 STRAPPY_TOOL_DISPLAY_PROMOTED_ARGUMENT_KEY) != 0) &&
         (strcmp(field->string, STRAPPY_TOOL_DISPLAY_TRANSFORM_KEY) != 0))) {
      strappy_set_formatted_error(error_out,
                                  "%s for %s contains an unsupported field.",
                                  STRAPPY_TOOL_DISPLAY_METADATA_KEY,
                                  tool_name);
      return 0;
    }
    if (strcmp(field->string,
               STRAPPY_TOOL_DISPLAY_PROMOTED_ARGUMENT_KEY) == 0) {
      promoted_argument_count++;
    } else {
      transform_count++;
    }
  }
  if ((promoted_argument_count != 1) || (transform_count > 1)) {
    strappy_set_formatted_error(
      error_out,
      "%s for %s must contain one promoted_argument and at most one transform.",
      STRAPPY_TOOL_DISPLAY_METADATA_KEY,
      tool_name);
    return 0;
  }

  promoted_argument = cJSON_GetObjectItemCaseSensitive(
    display,
    STRAPPY_TOOL_DISPLAY_PROMOTED_ARGUMENT_KEY);
  if (!cJSON_IsString(promoted_argument) ||
      (promoted_argument->valuestring == NULL) ||
      (promoted_argument->valuestring[0] == '\0')) {
    strappy_set_formatted_error(
      error_out,
      "%s for %s must contain a promoted_argument string.",
      STRAPPY_TOOL_DISPLAY_METADATA_KEY,
      tool_name);
    return 0;
  }

  transform = cJSON_GetObjectItemCaseSensitive(
    display,
    STRAPPY_TOOL_DISPLAY_TRANSFORM_KEY);
  transform_name = NULL;
  if ((transform != NULL) &&
      (!cJSON_IsString(transform) || (transform->valuestring == NULL) ||
       (transform->valuestring[0] == '\0'))) {
    strappy_set_formatted_error(error_out,
                                "%s transform for %s must be a string.",
                                STRAPPY_TOOL_DISPLAY_METADATA_KEY,
                                tool_name);
    return 0;
  }
  if (transform != NULL) {
    transform_name = transform->valuestring;
  }

  function = cJSON_GetObjectItemCaseSensitive(tool, "function");
  parameters = cJSON_IsObject(function) ?
    cJSON_GetObjectItemCaseSensitive(function, "parameters") : NULL;
  properties = cJSON_IsObject(parameters) ?
    cJSON_GetObjectItemCaseSensitive(parameters, "properties") : NULL;
  property = cJSON_IsObject(properties) ?
    cJSON_GetObjectItemCaseSensitive(properties,
                                     promoted_argument->valuestring) : NULL;
  property_type = cJSON_IsObject(property) ?
    cJSON_GetObjectItemCaseSensitive(property, "type") : NULL;
  property_type_name =
    strappy_tools_schema_property_value_type(property_type);
  if (property_type_name == NULL) {
    strappy_set_formatted_error(
      error_out,
      "%s promoted_argument for %s must name a typed schema property.",
      STRAPPY_TOOL_DISPLAY_METADATA_KEY,
      tool_name);
    return 0;
  }
  if (!strappy_tools_display_transform_matches_type(
        transform_name,
        property_type_name)) {
    strappy_set_formatted_error(error_out,
                                "%s transform for %s does not match %s.",
                                STRAPPY_TOOL_DISPLAY_METADATA_KEY,
                                tool_name,
                                property_type_name);
    return 0;
  }
  return 1;
}

static const char *strappy_tools_server_schema_type(cJSON *tool)
{
  cJSON *type;

  if (!cJSON_IsObject(tool)) {
    return NULL;
  }
  type = cJSON_GetObjectItemCaseSensitive(tool, "type");
  if (!cJSON_IsString(type) || (type->valuestring == NULL) ||
      (type->valuestring[0] == '\0')) {
    return NULL;
  }
  return type->valuestring;
}

static int strappy_tools_validate_server_display_metadata(
  cJSON *tool,
  const char *tool_type,
  char **error_out)
{
  cJSON *display;
  cJSON *field;
  cJSON *label;
  cJSON *promoted_path;
  cJSON *path_part;
  cJSON *transform;
  int label_count;
  int promoted_path_count;
  int transform_count;

  display = cJSON_GetObjectItemCaseSensitive(
    tool,
    STRAPPY_TOOL_DISPLAY_METADATA_KEY);
  if (!cJSON_IsObject(display)) {
    strappy_set_formatted_error(error_out,
                                "%s for %s must be an object.",
                                STRAPPY_TOOL_DISPLAY_METADATA_KEY,
                                tool_type);
    return 0;
  }

  label_count = 0;
  promoted_path_count = 0;
  transform_count = 0;
  for (field = display->child; field != NULL; field = field->next) {
    if (field->string == NULL) {
      strappy_set_formatted_error(error_out,
                                  "%s for %s contains an unsupported field.",
                                  STRAPPY_TOOL_DISPLAY_METADATA_KEY,
                                  tool_type);
      return 0;
    }
    if (strcmp(field->string, STRAPPY_TOOL_DISPLAY_LABEL_KEY) == 0) {
      label_count++;
    } else if (strcmp(field->string,
                      STRAPPY_TOOL_DISPLAY_PROMOTED_PATH_KEY) == 0) {
      promoted_path_count++;
    } else if (strcmp(field->string,
                      STRAPPY_TOOL_DISPLAY_TRANSFORM_KEY) == 0) {
      transform_count++;
    } else {
      strappy_set_formatted_error(error_out,
                                  "%s for %s contains an unsupported field.",
                                  STRAPPY_TOOL_DISPLAY_METADATA_KEY,
                                  tool_type);
      return 0;
    }
  }
  if ((label_count != 1) || (promoted_path_count != 1) ||
      (transform_count > 1)) {
    strappy_set_formatted_error(
      error_out,
      "%s for %s must contain one label, one promoted_path, and at most one transform.",
      STRAPPY_TOOL_DISPLAY_METADATA_KEY,
      tool_type);
    return 0;
  }

  label = cJSON_GetObjectItemCaseSensitive(display,
                                           STRAPPY_TOOL_DISPLAY_LABEL_KEY);
  promoted_path = cJSON_GetObjectItemCaseSensitive(
    display,
    STRAPPY_TOOL_DISPLAY_PROMOTED_PATH_KEY);
  transform = cJSON_GetObjectItemCaseSensitive(
    display,
    STRAPPY_TOOL_DISPLAY_TRANSFORM_KEY);
  if (!cJSON_IsString(label) || (label->valuestring == NULL) ||
      (label->valuestring[0] == '\0')) {
    strappy_set_formatted_error(error_out,
                                "%s label for %s must be a non-empty string.",
                                STRAPPY_TOOL_DISPLAY_METADATA_KEY,
                                tool_type);
    return 0;
  }
  if (!cJSON_IsArray(promoted_path) ||
      (cJSON_GetArraySize(promoted_path) < 1) ||
      (cJSON_GetArraySize(promoted_path) > 8)) {
    strappy_set_formatted_error(error_out,
                                "%s promoted_path for %s must contain 1 to 8 keys.",
                                STRAPPY_TOOL_DISPLAY_METADATA_KEY,
                                tool_type);
    return 0;
  }
  for (path_part = promoted_path->child;
       path_part != NULL;
       path_part = path_part->next) {
    if (!cJSON_IsString(path_part) || (path_part->valuestring == NULL) ||
        (path_part->valuestring[0] == '\0')) {
      strappy_set_formatted_error(
        error_out,
        "%s promoted_path for %s must contain non-empty string keys.",
        STRAPPY_TOOL_DISPLAY_METADATA_KEY,
        tool_type);
      return 0;
    }
  }
  if ((transform != NULL) &&
      (!cJSON_IsString(transform) || (transform->valuestring == NULL) ||
       !strappy_tools_display_transform_matches_type(transform->valuestring,
                                                     "string"))) {
    strappy_set_formatted_error(error_out,
                                "%s transform for %s is not supported.",
                                STRAPPY_TOOL_DISPLAY_METADATA_KEY,
                                tool_type);
    return 0;
  }
  return 1;
}

static int strappy_tools_validate_guidance_server_tools(cJSON *server_tools,
                                                        char **error_out)
{
  cJSON *tool;
  size_t definition_index;

  if (!cJSON_IsArray(server_tools)) {
    strappy_set_error(
      error_out,
      "Tool guidance resource must contain a server_tools array.");
    return 0;
  }

  for (tool = server_tools->child; tool != NULL; tool = tool->next) {
    const char *type;
    const strappy_server_tool_definition *definition;
    cJSON *feature;
    cJSON *field;

    type = strappy_tools_server_schema_type(tool);
    if (type == NULL) {
      strappy_set_error(error_out,
                        "Tool guidance contains a malformed server tool.");
      return 0;
    }
    definition = strappy_tools_find_server_definition(type);
    if (definition == NULL) {
      strappy_set_formatted_error(error_out,
                                  "Tool guidance contains unsupported server tool: %s",
                                  type);
      return 0;
    }
    for (field = tool->child; field != NULL; field = field->next) {
      if ((field->string == NULL) ||
          ((strcmp(field->string, "type") != 0) &&
           (strcmp(field->string, STRAPPY_TOOL_SERVER_FEATURE_KEY) != 0) &&
           (strcmp(field->string, STRAPPY_TOOL_DISPLAY_METADATA_KEY) != 0))) {
        strappy_set_formatted_error(
          error_out,
          "Tool guidance server tool %s contains an unsupported field.",
          type);
        return 0;
      }
    }
    feature = cJSON_GetObjectItemCaseSensitive(
      tool,
      STRAPPY_TOOL_SERVER_FEATURE_KEY);
    if (!cJSON_IsString(feature) || (feature->valuestring == NULL) ||
        (strcmp(feature->valuestring, definition->feature) != 0)) {
      strappy_set_formatted_error(error_out,
                                  "Tool guidance server tool %s has an invalid feature.",
                                  type);
      return 0;
    }
    if (!strappy_tools_validate_server_display_metadata(tool,
                                                        type,
                                                        error_out)) {
      return 0;
    }
  }

  for (definition_index = 0U;
       definition_index < strappy_server_tool_definition_count;
       definition_index++) {
    int matches;

    matches = 0;
    for (tool = server_tools->child; tool != NULL; tool = tool->next) {
      const char *type;

      type = strappy_tools_server_schema_type(tool);
      if ((type != NULL) &&
          (strcmp(type,
                  strappy_server_tool_definitions[definition_index].type) ==
           0)) {
        matches++;
      }
    }
    if (matches != 1) {
      strappy_set_formatted_error(
        error_out,
        "Tool guidance must contain server tool %s exactly once.",
        strappy_server_tool_definitions[definition_index].type);
      return 0;
    }
  }
  return 1;
}

static void strappy_tools_remove_display_metadata(cJSON *tools)
{
  cJSON *tool;

  if (!cJSON_IsArray(tools)) {
    return;
  }
  for (tool = tools->child; tool != NULL; tool = tool->next) {
    if (cJSON_IsObject(tool)) {
      cJSON_DeleteItemFromObjectCaseSensitive(
        tool,
        STRAPPY_TOOL_DISPLAY_METADATA_KEY);
    }
  }
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
    if (!strappy_tools_validate_display_metadata(tool, name, error_out)) {
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

static int strappy_tools_validate_guidance_root(cJSON *root,
                                                char **error_out)
{
  cJSON *tools;
  cJSON *server_tools;

  if (!cJSON_IsObject(root)) {
    strappy_set_error(error_out, "Tool guidance root must be an object.");
    return 0;
  }
  tools = cJSON_GetObjectItemCaseSensitive(root, "tools");
  server_tools = cJSON_GetObjectItemCaseSensitive(
    root,
    STRAPPY_TOOL_SERVER_TOOLS_KEY);
  return strappy_tools_validate_guidance_tools(tools, error_out) &&
    strappy_tools_validate_guidance_server_tools(server_tools, error_out);
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
  if (!strappy_tools_validate_guidance_root(root, error_out)) {
    cJSON_Delete(root);
    return NULL;
  }

  strappy_tools_remove_display_metadata(tools);
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
  if (!strappy_tools_validate_guidance_root(root, error_out)) {
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
      if (copy != NULL) {
        cJSON_DeleteItemFromObjectCaseSensitive(
          copy,
          STRAPPY_TOOL_DISPLAY_METADATA_KEY);
      }
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

char *strappy_tools_display_registry_json(const char *resource_dir,
                                          char **error_out)
{
  cJSON *root;
  cJSON *tools;
  cJSON *server_tools;
  cJSON *registry;
  cJSON *tool;
  char *json;

  root = strappy_tools_read_json_resource(resource_dir,
                                          STRAPPY_TOOL_GUIDANCE_RESOURCE,
                                          error_out);
  if (root == NULL) {
    return NULL;
  }

  tools = cJSON_GetObjectItemCaseSensitive(root, "tools");
  server_tools = cJSON_GetObjectItemCaseSensitive(
    root,
    STRAPPY_TOOL_SERVER_TOOLS_KEY);
  if (!strappy_tools_validate_guidance_root(root, error_out)) {
    cJSON_Delete(root);
    return NULL;
  }

  registry = cJSON_CreateObject();
  if (registry == NULL) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Could not allocate tool display registry.");
    return NULL;
  }

  for (tool = tools->child; tool != NULL; tool = tool->next) {
    const char *name;
    cJSON *display;
    cJSON *copy;

    name = strappy_tools_tool_schema_name(tool);
    display = cJSON_GetObjectItemCaseSensitive(
      tool,
      STRAPPY_TOOL_DISPLAY_METADATA_KEY);
    if (display == NULL) {
      continue;
    }
    copy = cJSON_Duplicate(display, 1);
    if ((copy == NULL) || !cJSON_AddItemToObject(registry, name, copy)) {
      cJSON_Delete(copy);
      cJSON_Delete(registry);
      cJSON_Delete(root);
      strappy_set_error(error_out, "Could not build tool display registry.");
      return NULL;
    }
  }

  for (tool = server_tools->child; tool != NULL; tool = tool->next) {
    const char *type;
    cJSON *display;
    cJSON *copy;

    type = strappy_tools_server_schema_type(tool);
    display = cJSON_GetObjectItemCaseSensitive(
      tool,
      STRAPPY_TOOL_DISPLAY_METADATA_KEY);
    copy = cJSON_Duplicate(display, 1);
    if ((copy == NULL) ||
        (cJSON_AddBoolToObject(copy, "response_item", 1) == NULL) ||
        !cJSON_AddItemToObject(registry, type, copy)) {
      cJSON_Delete(copy);
      cJSON_Delete(registry);
      cJSON_Delete(root);
      strappy_set_error(error_out,
                        "Could not build server tool display registry.");
      return NULL;
    }
  }

  json = cJSON_PrintUnformatted(registry);
  cJSON_Delete(registry);
  cJSON_Delete(root);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize tool display registry.");
    return NULL;
  }
  return json;
}

static int strappy_tools_responses_copy_member(cJSON *target,
                                                cJSON *source,
                                                const char *name,
                                                int required,
                                                char **error_out)
{
  cJSON *member;
  cJSON *copy;

  member = cJSON_GetObjectItem(source, name);
  if (member == NULL) {
    if (required) {
      strappy_set_formatted_error(error_out,
                                  "Tool guidance is missing function.%s.",
                                  name);
      return 0;
    }
    return 1;
  }

  copy = cJSON_Duplicate(member, 1);
  if ((copy == NULL) || !cJSON_AddItemToObject(target, name, copy)) {
    cJSON_Delete(copy);
    strappy_set_formatted_error(error_out,
                                "Could not copy Responses tool field: %s",
                                name);
    return 0;
  }
  return 1;
}

static int strappy_tools_responses_append_server_tools(
  cJSON *tools,
  const char *resource_dir,
  int web_search_enabled,
  char **error_out)
{
  cJSON *root;
  cJSON *server_tools;
  cJSON *server_tool;

  root = strappy_tools_read_json_resource(resource_dir,
                                          STRAPPY_TOOL_GUIDANCE_RESOURCE,
                                          error_out);
  if (root == NULL) {
    return 0;
  }
  if (!strappy_tools_validate_guidance_root(root, error_out)) {
    cJSON_Delete(root);
    return 0;
  }

  server_tools = cJSON_GetObjectItemCaseSensitive(
    root,
    STRAPPY_TOOL_SERVER_TOOLS_KEY);
  for (server_tool = server_tools->child;
       server_tool != NULL;
       server_tool = server_tool->next) {
    const char *type;
    cJSON *feature;
    cJSON *response_tool;

    type = strappy_tools_server_schema_type(server_tool);
    feature = cJSON_GetObjectItemCaseSensitive(
      server_tool,
      STRAPPY_TOOL_SERVER_FEATURE_KEY);
    if (cJSON_IsString(feature) && (feature->valuestring != NULL) &&
        (strcmp(feature->valuestring,
                STRAPPY_TOOL_SERVER_FEATURE_WEB_SEARCH) == 0) &&
        !web_search_enabled) {
      continue;
    }
    response_tool = cJSON_CreateObject();
    if ((response_tool == NULL) ||
        (cJSON_AddStringToObject(response_tool, "type", type) == NULL) ||
        !cJSON_AddItemToArray(tools, response_tool)) {
      cJSON_Delete(response_tool);
      cJSON_Delete(root);
      strappy_set_formatted_error(error_out,
                                  "Could not add Responses server tool: %s",
                                  type);
      return 0;
    }
  }
  cJSON_Delete(root);
  return 1;
}

static char *strappy_tools_responses_json_from_chat_json(
  const char *chat_tools_json,
  const char *resource_dir,
  int web_search_enabled,
  char **error_out)
{
  cJSON *chat_tools;
  cJSON *responses_tools;
  cJSON *chat_tool;
  char *json;

  chat_tools = cJSON_Parse(chat_tools_json);
  if (!cJSON_IsArray(chat_tools)) {
    cJSON_Delete(chat_tools);
    strappy_set_error(error_out, "Chat tool schema list is invalid.");
    return NULL;
  }

  responses_tools = cJSON_CreateArray();
  if (responses_tools == NULL) {
    cJSON_Delete(chat_tools);
    strappy_set_error(error_out, "Could not allocate Responses tool list.");
    return NULL;
  }

  for (chat_tool = chat_tools->child;
       chat_tool != NULL;
       chat_tool = chat_tool->next) {
    cJSON *function;
    cJSON *response_tool;

    function = cJSON_GetObjectItem(chat_tool, "function");
    if (!cJSON_IsObject(function)) {
      cJSON_Delete(responses_tools);
      cJSON_Delete(chat_tools);
      strappy_set_error(error_out,
                        "Chat tool schema is missing its function object.");
      return NULL;
    }

    response_tool = cJSON_CreateObject();
    if ((response_tool == NULL) ||
        (cJSON_AddStringToObject(response_tool, "type", "function") == NULL) ||
        !strappy_tools_responses_copy_member(response_tool,
                                             function,
                                             "name",
                                             1,
                                             error_out) ||
        !strappy_tools_responses_copy_member(response_tool,
                                             function,
                                             "description",
                                             0,
                                             error_out) ||
        !strappy_tools_responses_copy_member(response_tool,
                                             function,
                                             "parameters",
                                             1,
                                             error_out)) {
      cJSON_Delete(response_tool);
      cJSON_Delete(responses_tools);
      cJSON_Delete(chat_tools);
      return NULL;
    }

    if (!strappy_tools_responses_copy_member(response_tool,
                                             function,
                                             "strict",
                                             0,
                                             error_out)) {
      cJSON_Delete(response_tool);
      cJSON_Delete(responses_tools);
      cJSON_Delete(chat_tools);
      return NULL;
    }
    if ((cJSON_GetObjectItem(response_tool, "strict") == NULL) &&
        (cJSON_AddNullToObject(response_tool, "strict") == NULL)) {
      cJSON_Delete(response_tool);
      cJSON_Delete(responses_tools);
      cJSON_Delete(chat_tools);
      strappy_set_error(error_out,
                        "Could not add Responses tool strict setting.");
      return NULL;
    }

    if (!cJSON_AddItemToArray(responses_tools, response_tool)) {
      cJSON_Delete(response_tool);
      cJSON_Delete(responses_tools);
      cJSON_Delete(chat_tools);
      strappy_set_error(error_out, "Could not append Responses tool schema.");
      return NULL;
    }
  }

  if (!strappy_tools_responses_append_server_tools(responses_tools,
                                                   resource_dir,
                                                   web_search_enabled,
                                                   error_out)) {
    cJSON_Delete(responses_tools);
    cJSON_Delete(chat_tools);
    return NULL;
  }

  json = cJSON_PrintUnformatted(responses_tools);
  cJSON_Delete(responses_tools);
  cJSON_Delete(chat_tools);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize Responses tool list.");
  }
  return json;
}

char *strappy_tools_responses_request_json_filtered(
  const char *resource_dir,
  const char * const *allowed_names,
  size_t allowed_name_count,
  int web_search_enabled,
  char **error_out)
{
  char *chat_tools_json;
  char *responses_tools_json;

  chat_tools_json =
    strappy_tools_request_json_filtered(resource_dir,
                                        allowed_names,
                                        allowed_name_count,
                                        error_out);
  if (chat_tools_json == NULL) {
    return NULL;
  }

  responses_tools_json =
    strappy_tools_responses_json_from_chat_json(chat_tools_json,
                                                resource_dir,
                                                web_search_enabled,
                                                error_out);
  free(chat_tools_json);
  return responses_tools_json;
}

char *strappy_tools_responses_request_json(const char *resource_dir,
                                           int web_search_enabled,
                                           char **error_out)
{
  return strappy_tools_responses_request_json_filtered(resource_dir,
                                                       NULL,
                                                       0U,
                                                       web_search_enabled,
                                                       error_out);
}

char *strappy_tools_tool_guidance_string(const char *resource_dir,
                                         const char *section_name,
                                         const char *key,
                                         char **error_out)
{
  cJSON *root;
  cJSON *section;
  cJSON *item;
  char *value;

  if ((section_name == NULL) || (section_name[0] == '\0') ||
      (key == NULL) || (key[0] == '\0')) {
    strappy_set_error(error_out, "Tool guidance lookup is incomplete.");
    return NULL;
  }

  root = strappy_tools_read_json_resource(resource_dir,
                                          STRAPPY_TOOL_GUIDANCE_RESOURCE,
                                          error_out);
  if (root == NULL) {
    return NULL;
  }

  section = cJSON_GetObjectItem(root, section_name);
  if (!cJSON_IsObject(section)) {
    cJSON_Delete(root);
    strappy_set_formatted_error(error_out,
                                "Tool guidance is missing %s.",
                                section_name);
    return NULL;
  }

  item = cJSON_GetObjectItem(section, key);
  if (!cJSON_IsString(item) || (item->valuestring == NULL) ||
      (item->valuestring[0] == '\0')) {
    cJSON_Delete(root);
    strappy_set_formatted_error(error_out,
                                "Tool guidance is missing %s.%s.",
                                section_name,
                                key);
    return NULL;
  }

  value = strappy_string_duplicate(item->valuestring);
  cJSON_Delete(root);
  if (value == NULL) {
    strappy_set_error(error_out, "Could not allocate tool guidance string.");
  }
  return value;
}

static char *strappy_tools_tool_guidance_optional_string(
  const char *resource_dir,
  const char *section_name,
  const char *key)
{
  char *error;
  char *value;

  error = NULL;
  value = strappy_tools_tool_guidance_string(resource_dir,
                                             section_name,
                                             key,
                                             &error);
  free(error);
  return value;
}

static void strappy_tools_set_error_with_tool_guidance(char **error_out,
                                                       const char *message,
                                                       const char *resource_dir,
                                                       const char *guidance_key)
{
  char *guidance;

  guidance = strappy_tools_tool_guidance_optional_string(resource_dir,
                                                         "tool_errors",
                                                         guidance_key);
  if ((guidance != NULL) && (guidance[0] != '\0')) {
    strappy_set_formatted_error(error_out, "%s %s", message, guidance);
  } else {
    strappy_set_error(error_out, message);
  }

  free(guidance);
}

static int strappy_tools_validate_empty_arguments(const char *tool_name,
                                                   const char *arguments_json,
                                                   char **error_out)
{
  cJSON *root;

  if ((tool_name == NULL) || (tool_name[0] == '\0')) {
    strappy_set_error(error_out, "Tool name is missing.");
    return 0;
  }
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
    strappy_set_formatted_error(error_out,
                                "%s takes no arguments.",
                                tool_name);
    return 0;
  }

  cJSON_Delete(root);
  return 1;
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

static char *strappy_tools_build_empty_result(char **error_out)
{
  char *json;

  json = strappy_string_duplicate("{}");
  if (json == NULL) {
    strappy_set_error(error_out, "Could not allocate empty tool result.");
  }
  return json;
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

static int strappy_tools_string_has_value(const char *value)
{
  return ((value != NULL) && (value[0] != '\0')) ? 1 : 0;
}

static int strappy_tools_string_has_non_whitespace(const char *value)
{
  const char *cursor;

  if (value == NULL) {
    return 0;
  }
  for (cursor = value; cursor[0] != '\0'; cursor++) {
    if ((cursor[0] != ' ') && (cursor[0] != '\t') &&
        (cursor[0] != '\r') && (cursor[0] != '\n') &&
        (cursor[0] != '\f') && (cursor[0] != '\v')) {
      return 1;
    }
  }
  return 0;
}

static int strappy_tools_string_is_null_report_value(const char *value)
{
  const char *start;
  const char *end;

  if (value == NULL) {
    return 0;
  }
  start = value;
  while ((start[0] == ' ') || (start[0] == '\t') ||
         (start[0] == '\r') || (start[0] == '\n') ||
         (start[0] == '\f') || (start[0] == '\v')) {
    start++;
  }
  end = start + strlen(start);
  while ((end > start) &&
         ((end[-1] == ' ') || (end[-1] == '\t') ||
          (end[-1] == '\r') || (end[-1] == '\n') ||
          (end[-1] == '\f') || (end[-1] == '\v'))) {
    end--;
  }
  return (((end - start) == 4) &&
          ((start[0] == 'n') || (start[0] == 'N')) &&
          ((start[1] == 'u') || (start[1] == 'U')) &&
          ((start[2] == 'l') || (start[2] == 'L')) &&
          ((start[3] == 'l') || (start[3] == 'L'))) ? 1 : 0;
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

static cJSON *strappy_tools_parse_report_arguments_object(
  const char *tool_name,
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
  if (cJSON_IsNull(root) ||
      (cJSON_IsString(root) &&
       ((root->valuestring == NULL) ||
        !strappy_tools_string_has_non_whitespace(root->valuestring) ||
        strappy_tools_string_is_null_report_value(root->valuestring)))) {
    cJSON_Delete(root);
    root = cJSON_CreateObject();
    if (root == NULL) {
      strappy_set_formatted_error(error_out,
                                  "Could not allocate %s arguments.",
                                  tool_name);
    }
    return root;
  }
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    strappy_set_formatted_error(error_out,
                                "%s arguments must be a JSON object or null.",
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

static int strappy_tools_copy_nullable_report_string_argument(
  const char *tool_name,
  cJSON *root,
  const char *name,
  size_t max_bytes,
  char **value_out,
  char **error_out)
{
  cJSON *item;

  if (value_out == NULL) {
    strappy_set_error(error_out, "Tool report string output is missing.");
    return 0;
  }
  *value_out = NULL;
  item = cJSON_GetObjectItemCaseSensitive(root, name);
  if (cJSON_IsNull(item)) {
    return 1;
  }
  if (!strappy_tools_copy_string_argument(tool_name,
                                          root,
                                          name,
                                          0,
                                          max_bytes,
                                          value_out,
                                          error_out)) {
    return 0;
  }
  if ((*value_out != NULL) &&
      (!strappy_tools_string_has_non_whitespace(*value_out) ||
       strappy_tools_string_is_null_report_value(*value_out))) {
    free(*value_out);
    *value_out = NULL;
  }
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
  size_t database_id_length;
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

  database_id_length = strlen(database_id->valuestring);
  if (database_id_length > STRAPPY_DATABASE_QUERY_MAX_DATABASE_ID_BYTES) {
    cJSON_Delete(root);
    strappy_set_formatted_error(
      error_out,
      "database_query database_id is too long; maximum is %u bytes.",
      (unsigned int)STRAPPY_DATABASE_QUERY_MAX_DATABASE_ID_BYTES);
    return 0;
  }

  if (!strappy_tools_string_has_non_whitespace(sql->valuestring)) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "database_query sql must not be blank.");
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
  strappy_helper_datetime_arguments *arguments,
  char **error_out)
{
  const char *allowed_names[2];
  cJSON *root;
  cJSON *items;
  cJSON *unit;
  int item_count;
  int index;
  size_t total_length;

  if ((tool_name == NULL) || (items_argument_name == NULL) ||
      (arguments == NULL)) {
    strappy_set_error(error_out, "Datetime helper argument output is missing.");
    return 0;
  }
  strappy_helper_datetime_arguments_init(arguments);

  root = strappy_tools_parse_arguments_object(tool_name,
                                              arguments_json,
                                              error_out);
  if (root == NULL) {
    return 0;
  }

  allowed_names[0] = items_argument_name;
  allowed_names[1] = "unit";
  if (!strappy_tools_json_object_accepts_only(
        root,
        tool_name,
        allowed_names,
        sizeof(allowed_names) / sizeof(allowed_names[0]),
        error_out)) {
    cJSON_Delete(root);
    return 0;
  }

  unit = cJSON_GetObjectItemCaseSensitive(root, "unit");
  if (!cJSON_IsString(unit) ||
      !strappy_tools_string_has_non_whitespace(unit->valuestring)) {
    cJSON_Delete(root);
    strappy_set_formatted_error(error_out,
                                "%s requires a non-empty unit string.",
                                tool_name);
    return 0;
  }
  if (!strappy_cocoa_parse_timestamp_unit(unit->valuestring,
                                          &arguments->unit,
                                          error_out)) {
    cJSON_Delete(root);
    return 0;
  }

  items = cJSON_GetObjectItemCaseSensitive(root, items_argument_name);
  if (!cJSON_IsArray(items)) {
    cJSON_Delete(root);
    strappy_set_formatted_error(error_out,
                                "%s %s must be an array of strings.",
                                tool_name,
                                items_argument_name);
    return 0;
  }

  item_count = cJSON_GetArraySize(items);
  if ((item_count < 1) ||
      ((size_t)item_count > STRAPPY_HELPER_DATETIME_MAX_ITEMS)) {
    cJSON_Delete(root);
    strappy_set_formatted_error(
      error_out,
      "%s %s must contain between 1 and %u items.",
      tool_name,
      items_argument_name,
      (unsigned int)STRAPPY_HELPER_DATETIME_MAX_ITEMS);
    return 0;
  }

  arguments->items = (char **)calloc((size_t)item_count, sizeof(char *));
  if (arguments->items == NULL) {
    cJSON_Delete(root);
    strappy_set_formatted_error(error_out,
                                "Could not allocate %s arguments.",
                                tool_name);
    return 0;
  }
  arguments->item_count = (size_t)item_count;

  total_length = 0U;
  for (index = 0; index < item_count; index++) {
    cJSON *item;
    const char *value;
    size_t length;
    size_t start;
    size_t end;

    item = cJSON_GetArrayItem(items, index);
    if (!cJSON_IsString(item) || (item->valuestring == NULL)) {
      cJSON_Delete(root);
      strappy_helper_datetime_arguments_destroy(arguments);
      strappy_set_formatted_error(error_out,
                                  "%s %s items must be strings.",
                                  tool_name,
                                  items_argument_name);
      return 0;
    }

    value = item->valuestring;
    length = strlen(value);
    if (length > (STRAPPY_HELPER_DATETIME_MAX_INPUT_BYTES - total_length)) {
      cJSON_Delete(root);
      strappy_helper_datetime_arguments_destroy(arguments);
      strappy_set_formatted_error(
        error_out,
        "%s %s exceeds the %u-byte combined input limit.",
        tool_name,
        items_argument_name,
        (unsigned int)STRAPPY_HELPER_DATETIME_MAX_INPUT_BYTES);
      return 0;
    }
    total_length += length;

    start = 0U;
    end = length;
    while ((start < end) && strappy_tools_helper_is_space(value[start])) {
      start++;
    }
    while ((end > start) && strappy_tools_helper_is_space(value[end - 1U])) {
      end--;
    }
    if (start == end) {
      cJSON_Delete(root);
      strappy_helper_datetime_arguments_destroy(arguments);
      strappy_set_formatted_error(error_out,
                                  "%s %s contains an empty item.",
                                  tool_name,
                                  items_argument_name);
      return 0;
    }

    arguments->items[index] =
      strappy_string_duplicate_length(value + start, end - start);
    if (arguments->items[index] == NULL) {
      cJSON_Delete(root);
      strappy_helper_datetime_arguments_destroy(arguments);
      strappy_set_formatted_error(error_out,
                                  "Could not allocate %s arguments.",
                                  tool_name);
      return 0;
    }
  }

  cJSON_Delete(root);
  return 1;
}

static int strappy_tools_parse_memory_user_fact_remember_arguments(
  const char *arguments_json,
  strappy_memory_user_fact_remember_arguments *arguments,
  char **error_out)
{
  static const char *const allowed_names[] = { "fact" };
  cJSON *root;
  int ok;

  if (arguments == NULL) {
    strappy_set_error(error_out,
                      "memory_user_fact_remember argument output is missing.");
    return 0;
  }
  strappy_memory_user_fact_remember_arguments_init(arguments);

  root = strappy_tools_parse_report_arguments_object(
    STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER,
    arguments_json,
    error_out);
  if (root == NULL) {
    return 0;
  }

  ok = strappy_tools_json_object_accepts_only(
         root,
         STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER,
         allowed_names,
         sizeof(allowed_names) / sizeof(allowed_names[0]),
         error_out) &&
       strappy_tools_copy_nullable_report_string_argument(
         STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER,
         root,
         "fact",
         STRAPPY_HELPER_INFO_MAX_VALUE_BYTES,
         &arguments->fact,
         error_out);
  cJSON_Delete(root);
  if (!ok) {
    strappy_memory_user_fact_remember_arguments_destroy(arguments);
  }
  return ok;
}

static int strappy_tools_parse_helper_session_name_write_arguments(
  const char *arguments_json,
  strappy_helper_session_name_write_arguments *arguments,
  char **error_out)
{
  static const char *const allowed_names[] = { "name" };
  cJSON *root;
  int ok;

  if (arguments == NULL) {
    strappy_set_error(
      error_out,
      "helper_session_name_write argument output is missing.");
    return 0;
  }
  strappy_helper_session_name_write_arguments_init(arguments);

  root = strappy_tools_parse_arguments_object(
    STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE,
    arguments_json,
    error_out);
  if (root == NULL) {
    return 0;
  }

  ok = strappy_tools_json_object_accepts_only(
         root,
         STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE,
         allowed_names,
         sizeof(allowed_names) / sizeof(allowed_names[0]),
         error_out) &&
       strappy_tools_copy_string_argument(
         STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE,
         root,
         "name",
         1,
         STRAPPY_HELPER_SESSION_NAME_MAX_BYTES,
         &arguments->name,
         error_out);
  cJSON_Delete(root);
  if (!ok) {
    strappy_helper_session_name_write_arguments_destroy(arguments);
  }
  return ok;
}

static int strappy_tools_parse_database_context_read_arguments(
  const char *arguments_json,
  strappy_database_context_read_arguments *arguments,
  char **error_out)
{
  static const char *const allowed_names[] = { "database_id" };
  cJSON *root;
  int ok;

  if (arguments == NULL) {
    strappy_set_error(error_out,
                      "database_context_read argument output is missing.");
    return 0;
  }
  strappy_database_context_read_arguments_init(arguments);

  root = strappy_tools_parse_report_arguments_object(
    STRAPPY_TOOL_DATABASE_CONTEXT_READ,
    arguments_json,
    error_out);
  if (root == NULL) {
    return 0;
  }

  ok = strappy_tools_json_object_accepts_only(
         root,
         STRAPPY_TOOL_DATABASE_CONTEXT_READ,
         allowed_names,
         sizeof(allowed_names) / sizeof(allowed_names[0]),
         error_out) &&
       strappy_tools_copy_nullable_report_string_argument(
         STRAPPY_TOOL_DATABASE_CONTEXT_READ,
         root,
         "database_id",
         STRAPPY_HELPER_INFO_MAX_SHORT_BYTES,
         &arguments->database_id,
         error_out);
  cJSON_Delete(root);
  if (!ok) {
    strappy_database_context_read_arguments_destroy(arguments);
    return 0;
  }
  return 1;
}

static int strappy_tools_parse_helper_fontawesome_shortcode_search_arguments(
  const char *arguments_json,
  strappy_helper_fontawesome_shortcode_search_arguments *arguments,
  char **error_out)
{
  static const char *const allowed_names[] = { "query" };
  cJSON *root;
  int ok;

  if (arguments == NULL) {
    strappy_set_error(
      error_out,
      "helper_fontawesome_shortcode_search argument output is missing.");
    return 0;
  }
  strappy_helper_fontawesome_shortcode_search_arguments_init(arguments);

  root = strappy_tools_parse_arguments_object(
    STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_SEARCH,
    arguments_json,
    error_out);
  if (root == NULL) {
    return 0;
  }

  ok = strappy_tools_json_object_accepts_only(
         root,
         STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_SEARCH,
         allowed_names,
         sizeof(allowed_names) / sizeof(allowed_names[0]),
         error_out) &&
       strappy_tools_copy_string_argument(
         STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_SEARCH,
         root,
         "query",
         1,
         STRAPPY_HELPER_FONTAWESOME_MAX_QUERY_BYTES,
         &arguments->query,
         error_out);
  cJSON_Delete(root);
  if (!ok) {
    strappy_helper_fontawesome_shortcode_search_arguments_destroy(arguments);
    return 0;
  }
  if (!strappy_tools_string_has_non_whitespace(arguments->query)) {
    strappy_helper_fontawesome_shortcode_search_arguments_destroy(arguments);
    strappy_set_error(
      error_out,
      "helper_fontawesome_shortcode_search query must not be blank.");
    return 0;
  }
  return 1;
}

static int strappy_tools_parse_helper_fontawesome_shortcode_confirm_arguments(
  const char *arguments_json,
  strappy_helper_fontawesome_shortcode_confirm_arguments *arguments,
  char **error_out)
{
  static const char *const allowed_names[] = {
    "shortcodes"
  };
  cJSON *root;
  cJSON *shortcodes;
  cJSON *item;
  int count;
  int index;

  if (arguments == NULL) {
    strappy_set_error(
      error_out,
      "helper_fontawesome_shortcode_confirm argument output is missing.");
    return 0;
  }
  strappy_helper_fontawesome_shortcode_confirm_arguments_init(arguments);

  root = strappy_tools_parse_arguments_object(
    STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_CONFIRM,
    arguments_json,
    error_out);
  if (root == NULL) {
    return 0;
  }

  if (!strappy_tools_json_object_accepts_only(
        root,
        STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_CONFIRM,
        allowed_names,
        sizeof(allowed_names) / sizeof(allowed_names[0]),
        error_out)) {
    cJSON_Delete(root);
    return 0;
  }

  shortcodes = cJSON_GetObjectItemCaseSensitive(root, "shortcodes");
  if (!cJSON_IsArray(shortcodes)) {
    cJSON_Delete(root);
    strappy_set_error(
      error_out,
      "helper_fontawesome_shortcode_confirm requires a shortcodes array.");
    return 0;
  }

  count = cJSON_GetArraySize(shortcodes);
  if ((count <= 0) ||
      (count > (int)STRAPPY_HELPER_FONTAWESOME_MAX_SHORTCODES)) {
    cJSON_Delete(root);
    strappy_set_formatted_error(
      error_out,
      "helper_fontawesome_shortcode_confirm shortcodes must contain 1 to %u items.",
      (unsigned int)STRAPPY_HELPER_FONTAWESOME_MAX_SHORTCODES);
    return 0;
  }

  arguments->shortcodes = (char **)calloc((size_t)count, sizeof(char *));
  if (arguments->shortcodes == NULL) {
    cJSON_Delete(root);
    strappy_set_error(error_out,
                      "Could not allocate Font Awesome shortcode arguments.");
    return 0;
  }

  index = 0;
  for (item = shortcodes->child; item != NULL; item = item->next) {
    size_t length;

    if (!cJSON_IsString(item) || (item->valuestring == NULL)) {
      cJSON_Delete(root);
      strappy_helper_fontawesome_shortcode_confirm_arguments_destroy(arguments);
      strappy_set_error(
        error_out,
        "helper_fontawesome_shortcode_confirm shortcodes must be strings.");
      return 0;
    }

    length = strlen(item->valuestring);
    if (length > STRAPPY_HELPER_FONTAWESOME_MAX_SHORTCODE_BYTES) {
      cJSON_Delete(root);
      strappy_helper_fontawesome_shortcode_confirm_arguments_destroy(arguments);
      strappy_set_formatted_error(
        error_out,
        "helper_fontawesome_shortcode_confirm shortcode is too long; maximum is %u bytes.",
        (unsigned int)STRAPPY_HELPER_FONTAWESOME_MAX_SHORTCODE_BYTES);
      return 0;
    }

    arguments->shortcodes[index] = strappy_string_duplicate(item->valuestring);
    if (arguments->shortcodes[index] == NULL) {
      cJSON_Delete(root);
      strappy_helper_fontawesome_shortcode_confirm_arguments_destroy(arguments);
      strappy_set_error(error_out,
                        "Could not allocate Font Awesome shortcode argument.");
      return 0;
    }
    index++;
  }

  arguments->shortcode_count = (size_t)count;
  cJSON_Delete(root);
  return 1;
}

static int strappy_tools_parse_memory_database_hint_remember_arguments(
  const char *arguments_json,
  strappy_memory_database_hint_remember_arguments *arguments,
  char **error_out)
{
  static const char *const allowed_names[] = { "database_id", "hint" };
  cJSON *root;
  int ok;

  if (arguments == NULL) {
    strappy_set_error(
      error_out,
      "memory_database_hint_remember argument output is missing.");
    return 0;
  }
  strappy_memory_database_hint_remember_arguments_init(arguments);

  root = strappy_tools_parse_report_arguments_object(
    STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER,
    arguments_json,
    error_out);
  if (root == NULL) {
    return 0;
  }

  ok = strappy_tools_json_object_accepts_only(
         root,
         STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER,
         allowed_names,
         sizeof(allowed_names) / sizeof(allowed_names[0]),
         error_out) &&
       strappy_tools_copy_nullable_report_string_argument(
         STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER,
         root,
         "database_id",
         STRAPPY_HELPER_INFO_MAX_SHORT_BYTES,
         &arguments->database_id,
         error_out) &&
       strappy_tools_copy_nullable_report_string_argument(
         STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER,
         root,
         "hint",
         STRAPPY_HELPER_INFO_MAX_CONTENT_BYTES,
         &arguments->hint,
         error_out);
  cJSON_Delete(root);
  if (!ok) {
    strappy_memory_database_hint_remember_arguments_destroy(arguments);
    return 0;
  }
  if ((arguments->hint != NULL) && (arguments->database_id == NULL)) {
    strappy_memory_database_hint_remember_arguments_destroy(arguments);
    strappy_set_error(
      error_out,
      "memory_database_hint_remember requires database_id when hint is provided.");
    return 0;
  }
  return 1;
}

static int strappy_tools_helper_is_space(char value)
{
  return ((value == ' ') || (value == '\t') || (value == '\r') ||
          (value == '\n') || (value == '\f') || (value == '\v')) ? 1 : 0;
}

static char *strappy_tools_copy_normalized_session_name(const char *name,
                                                        char **error_out)
{
  char *normalized;
  size_t start;
  size_t end;
  size_t index;
  size_t output_index;
  int pending_space;

  if (name == NULL) {
    strappy_set_error(error_out, "Session name is empty.");
    return NULL;
  }

  start = 0U;
  end = strlen(name);
  while ((start < end) && strappy_tools_helper_is_space(name[start])) {
    start++;
  }
  while ((end > start) && strappy_tools_helper_is_space(name[end - 1U])) {
    end--;
  }
  if (start == end) {
    strappy_set_error(error_out, "Session name is empty.");
    return NULL;
  }

  normalized = (char *)malloc((end - start) + 1U);
  if (normalized == NULL) {
    strappy_set_error(error_out, "Could not allocate session name.");
    return NULL;
  }

  output_index = 0U;
  pending_space = 0;
  for (index = start; index < end; index++) {
    if (strappy_tools_helper_is_space(name[index])) {
      pending_space = 1;
      continue;
    }

    if (pending_space && (output_index > 0U)) {
      normalized[output_index++] = ' ';
    }
    pending_space = 0;
    normalized[output_index++] = name[index];
  }

  if (output_index == 0U) {
    free(normalized);
    strappy_set_error(error_out, "Session name is empty.");
    return NULL;
  }
  if (output_index > STRAPPY_HELPER_SESSION_NAME_MAX_BYTES) {
    free(normalized);
    strappy_set_formatted_error(
      error_out,
      "helper_session_name_write name is too long; maximum is %u bytes.",
      (unsigned int)STRAPPY_HELPER_SESSION_NAME_MAX_BYTES);
    return NULL;
  }

  normalized[output_index] = '\0';
  return normalized;
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

static int strappy_tools_database_file_is_missing(const char *path)
{
  struct stat info;

  if ((path == NULL) || (path[0] == '\0')) {
    return 0;
  }

  if (stat(path, &info) == 0) {
    return 0;
  }

  return ((errno == ENOENT) || (errno == ENOTDIR)) ? 1 : 0;
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
                                  "Could not create memory user fact schema",
                                  error_out) ||
      !strappy_tools_catalog_exec(db,
                                  user_info_status_index_sql,
                                  "Could not create memory user fact index",
                                  error_out) ||
      !strappy_tools_catalog_exec(db,
                                  database_info_sql,
                                  "Could not create memory database hint schema",
                                  error_out) ||
      !strappy_tools_catalog_exec(db,
                                  database_info_status_index_sql,
                                  "Could not create memory database hint index",
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
    strappy_set_error(error_out, "Memory database output is missing.");
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
                                "Could not open memory database: %s",
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

static int strappy_tools_add_user_info_row(cJSON *array,
                                           sqlite3_stmt *stmt,
                                           char **error_out)
{
  cJSON *object;
  const char *fact;
  const char *date_saved;

  if ((array == NULL) || (stmt == NULL)) {
    strappy_set_error(error_out, "User fact row request is incomplete.");
    return 0;
  }

  fact = strappy_tools_sqlite_column_text(stmt, 1);
  date_saved = strappy_tools_sqlite_column_text(stmt, 2);
  if ((fact == NULL) || (date_saved == NULL)) {
    strappy_set_error(error_out, "Stored user fact is incomplete.");
    return 0;
  }

  object = cJSON_CreateObject();
  if (object == NULL) {
    strappy_set_error(error_out, "Could not allocate user fact row.");
    return 0;
  }

  if ((cJSON_AddNumberToObject(object,
                               "id",
                               (double)sqlite3_column_int64(stmt, 0)) == NULL) ||
      (cJSON_AddStringToObject(object, "fact", fact) == NULL) ||
      (cJSON_AddStringToObject(object, "date_saved", date_saved) == NULL) ||
      !cJSON_AddItemToArray(array, object)) {
    cJSON_Delete(object);
    strappy_set_error(error_out, "Could not build user fact row.");
    return 0;
  }

  return 1;
}

static char *strappy_tools_read_user_info(sqlite3 *db, char **error_out)
{
  static const char *sql =
    "SELECT id, value, created_at "
    "FROM helper_user_info "
    "WHERE status = 'active' AND kind = 'fact' "
    "ORDER BY created_at DESC, id DESC "
    "LIMIT ?1;";
  sqlite3_stmt *stmt;
  cJSON *facts;
  char *json;
  int rc;

  if (db == NULL) {
    strappy_set_error(error_out, "memory_user_fact_read request is incomplete.");
    return NULL;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare memory_user_fact_read: %s",
                                sqlite3_errmsg(db));
    return NULL;
  }

  if (sqlite3_bind_int(stmt, 1, STRAPPY_HELPER_INFO_DEFAULT_LIMIT) !=
      SQLITE_OK) {
    sqlite3_finalize(stmt);
    strappy_set_formatted_error(error_out,
                                "Could not bind memory_user_fact_read: %s",
                                sqlite3_errmsg(db));
    return NULL;
  }

  facts = cJSON_CreateArray();
  if (facts == NULL) {
    sqlite3_finalize(stmt);
    strappy_set_error(error_out, "Could not allocate user fact result.");
    return NULL;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    if (!strappy_tools_add_user_info_row(facts, stmt, error_out)) {
      cJSON_Delete(facts);
      sqlite3_finalize(stmt);
      return NULL;
    }
  }

  if (rc != SQLITE_DONE) {
    cJSON_Delete(facts);
    strappy_set_formatted_error(error_out,
                                "Could not read memory_user_fact rows: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return NULL;
  }
  sqlite3_finalize(stmt);

  json = cJSON_PrintUnformatted(facts);
  cJSON_Delete(facts);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize user fact result.");
    return NULL;
  }

  return json;
}

static char *strappy_tools_remember_user_info(
  sqlite3 *db,
  const strappy_memory_user_fact_remember_arguments *arguments,
  char **error_out)
{
  static const char *sql =
    "INSERT INTO helper_user_info "
    "(kind, subject, predicate, value, status) "
    "VALUES ('fact', 'user', 'fact', ?, 'active');";
  sqlite3_stmt *stmt;
  int rc;

  if ((db == NULL) || (arguments == NULL)) {
    strappy_set_error(error_out,
                      "memory_user_fact_remember request is incomplete.");
    return NULL;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare memory_user_fact_remember: %s",
                                sqlite3_errmsg(db));
    return NULL;
  }

  if (sqlite3_bind_text(stmt,
                        1,
                        arguments->fact,
                        -1,
                        SQLITE_TRANSIENT) != SQLITE_OK) {
    sqlite3_finalize(stmt);
    if ((error_out != NULL) && (*error_out == NULL)) {
      strappy_set_formatted_error(error_out,
                                  "Could not bind memory_user_fact_remember: %s",
                                  sqlite3_errmsg(db));
    }
    return NULL;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save memory_user_fact: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return NULL;
  }
  sqlite3_finalize(stmt);

  return strappy_tools_build_empty_result(error_out);
}

static char *strappy_tools_forget_info_row(sqlite3 *db,
                                           const char *table_name,
                                           long long id,
                                           char **error_out)
{
  sqlite3_stmt *stmt;
  char *sql;
  int rc;

  if ((db == NULL) || !strappy_tools_string_has_value(table_name) ||
      (id <= 0)) {
    strappy_set_error(error_out, "Forget memory request is incomplete.");
    return NULL;
  }

  sql = strappy_tools_build_identifier_sql(
    "UPDATE ",
    table_name,
    " SET status = 'forgotten', "
    "updated_at = (strftime('%Y-%m-%dT%H:%M:%fZ','now')) "
    "WHERE id = ? AND status = 'active';");
  if (sql == NULL) {
    strappy_set_error(error_out, "Could not allocate forget memory SQL.");
    return NULL;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  free(sql);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare forget memory: %s",
                                sqlite3_errmsg(db));
    return NULL;
  }

  if (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)id) != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind forget memory: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return NULL;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not forget memory row: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return NULL;
  }
  sqlite3_finalize(stmt);

  return strappy_tools_build_empty_result(error_out);
}

static int strappy_tools_add_database_info_row(cJSON *array,
                                               sqlite3_stmt *stmt,
                                               char **error_out)
{
  cJSON *object;
  const char *hint;
  const char *date_saved;

  if ((array == NULL) || (stmt == NULL)) {
    strappy_set_error(error_out, "Database hint row request is incomplete.");
    return 0;
  }

  hint = strappy_tools_sqlite_column_text(stmt, 1);
  date_saved = strappy_tools_sqlite_column_text(stmt, 2);
  if ((hint == NULL) || (date_saved == NULL)) {
    strappy_set_error(error_out, "Stored database hint is incomplete.");
    return 0;
  }

  object = cJSON_CreateObject();
  if (object == NULL) {
    strappy_set_error(error_out, "Could not allocate database hint row.");
    return 0;
  }

  if ((cJSON_AddNumberToObject(object,
                               "id",
                               (double)sqlite3_column_int64(stmt, 0)) == NULL) ||
      (cJSON_AddStringToObject(object, "hint", hint) == NULL) ||
      (cJSON_AddStringToObject(object, "date_saved", date_saved) == NULL) ||
      !cJSON_AddItemToArray(array, object)) {
    cJSON_Delete(object);
    strappy_set_error(error_out, "Could not build database hint row.");
    return 0;
  }

  return 1;
}

static int strappy_tools_read_database_hints(
  sqlite3 *db,
  const strappy_discovered_database_record *record,
  cJSON *hints,
  int *truncated_out,
  char **error_out)
{
  static const char *sql =
    "SELECT id, content, created_at "
    "FROM helper_database_info "
    "WHERE status = 'active' AND kind = 'hint' "
    "AND (database_catalog_id = ?1 OR database_assistant_id = ?2) "
    "ORDER BY created_at DESC, id DESC "
    "LIMIT ?3;";
  sqlite3_stmt *stmt;
  int rc;
  int count;

  if ((db == NULL) || (record == NULL) ||
      !strappy_tools_string_has_value(record->assistant_database_id) ||
      (hints == NULL) ||
      (truncated_out == NULL)) {
    strappy_set_error(error_out, "Database hint read request is incomplete.");
    return 0;
  }
  *truncated_out = 0;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare database hint read: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  if ((sqlite3_bind_int64(stmt,
                          1,
                          (sqlite3_int64)record->catalog_id) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt,
                         2,
                         record->assistant_database_id,
                         -1,
                         SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_int(stmt,
                        3,
                        STRAPPY_DATABASE_CONTEXT_HINT_LIMIT + 1) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not bind database hint read: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  count = 0;
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    if (count >= STRAPPY_DATABASE_CONTEXT_HINT_LIMIT) {
      *truncated_out = 1;
      continue;
    }
    if (!strappy_tools_add_database_info_row(hints, stmt, error_out)) {
      sqlite3_finalize(stmt);
      return 0;
    }
    count++;
  }

  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read database hints: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_tools_read_database_object_names(sqlite3 *db,
                                                    const char *object_type,
                                                    int limit,
                                                    cJSON *names,
                                                    int *truncated_out,
                                                    char **error_out)
{
  static const char *sql =
    "SELECT name FROM sqlite_master "
    "WHERE type = ?1 AND name NOT LIKE 'sqlite_%' "
    "ORDER BY name "
    "LIMIT ?2;";
  sqlite3_stmt *stmt;
  int rc;
  int count;

  if ((db == NULL) || !strappy_tools_string_has_value(object_type) ||
      (limit <= 0) || (names == NULL) || (truncated_out == NULL)) {
    strappy_set_error(error_out,
                      "Database object-name read request is incomplete.");
    return 0;
  }
  *truncated_out = 0;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare database object-name read: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  if ((sqlite3_bind_text(stmt,
                         1,
                         object_type,
                         -1,
                         SQLITE_STATIC) != SQLITE_OK) ||
      (sqlite3_bind_int(stmt, 2, limit + 1) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not bind database object-name read: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  count = 0;
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    cJSON *name_item;
    const char *name;

    if (count >= limit) {
      *truncated_out = 1;
      continue;
    }

    name = strappy_tools_sqlite_column_text(stmt, 0);
    if (!strappy_tools_string_has_value(name)) {
      strappy_set_error(error_out, "Stored database object name is empty.");
      sqlite3_finalize(stmt);
      return 0;
    }

    name_item = cJSON_CreateString(name);
    if ((name_item == NULL) || !cJSON_AddItemToArray(names, name_item)) {
      cJSON_Delete(name_item);
      strappy_set_error(error_out, "Could not build database object-name list.");
      sqlite3_finalize(stmt);
      return 0;
    }
    count++;
  }

  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read database object names: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

static char *strappy_tools_read_database_info(
  sqlite3 *catalog_db,
  const strappy_discovered_database_record *record,
  char **error_out)
{
  sqlite3 *database;
  cJSON *root;
  cJSON *hints;
  cJSON *tables;
  cJSON *views;
  char *json;
  int hints_truncated;
  int tables_truncated;
  int views_truncated;

  if ((catalog_db == NULL) || (record == NULL) ||
      !strappy_tools_string_has_value(record->path)) {
    strappy_set_error(error_out,
                      "database_context_read request is incomplete.");
    return NULL;
  }

  root = cJSON_CreateObject();
  hints = cJSON_CreateArray();
  tables = cJSON_CreateArray();
  views = cJSON_CreateArray();
  if ((root == NULL) || (hints == NULL) || (tables == NULL) ||
      (views == NULL)) {
    cJSON_Delete(root);
    cJSON_Delete(hints);
    cJSON_Delete(tables);
    cJSON_Delete(views);
    strappy_set_error(error_out, "Could not allocate database context result.");
    return NULL;
  }

  if (!strappy_tools_read_database_hints(catalog_db,
                                         record,
                                         hints,
                                         &hints_truncated,
                                         error_out)) {
    cJSON_Delete(root);
    cJSON_Delete(hints);
    cJSON_Delete(tables);
    cJSON_Delete(views);
    return NULL;
  }

  database = NULL;
  if (!strappy_tools_open_readonly_database(record->path,
                                            &database,
                                            error_out)) {
    cJSON_Delete(root);
    cJSON_Delete(hints);
    cJSON_Delete(tables);
    cJSON_Delete(views);
    return NULL;
  }

  if (!strappy_tools_read_database_object_names(
        database,
        "table",
        STRAPPY_DATABASE_CONTEXT_TABLE_LIMIT,
        tables,
        &tables_truncated,
        error_out) ||
      !strappy_tools_read_database_object_names(
        database,
        "view",
        STRAPPY_DATABASE_CONTEXT_VIEW_LIMIT,
        views,
        &views_truncated,
        error_out)) {
    sqlite3_close(database);
    cJSON_Delete(root);
    cJSON_Delete(hints);
    cJSON_Delete(tables);
    cJSON_Delete(views);
    return NULL;
  }
  sqlite3_close(database);

  if ((((tables->child != NULL) || (views->child != NULL)) &&
       (cJSON_AddStringToObject(root,
                               "guidance",
                               STRAPPY_DATABASE_CONTEXT_QUERY_GUIDANCE) ==
        NULL)) ||
      !cJSON_AddItemToObject(root, "remembered_hints", hints)) {
    cJSON_Delete(root);
    cJSON_Delete(hints);
    cJSON_Delete(tables);
    cJSON_Delete(views);
    strappy_set_error(error_out, "Could not build database context result.");
    return NULL;
  }
  hints = NULL;
  if (!strappy_tools_add_bool_to_object(root,
                                        "remembered_hints_truncated",
                                        hints_truncated) ||
      !cJSON_AddItemToObject(root, "tables", tables)) {
    cJSON_Delete(root);
    cJSON_Delete(tables);
    cJSON_Delete(views);
    strappy_set_error(error_out, "Could not build database context result.");
    return NULL;
  }
  tables = NULL;
  if (!strappy_tools_add_bool_to_object(root,
                                        "tables_truncated",
                                        tables_truncated) ||
      !cJSON_AddItemToObject(root, "views", views)) {
    cJSON_Delete(root);
    cJSON_Delete(views);
    strappy_set_error(error_out, "Could not build database context result.");
    return NULL;
  }
  views = NULL;
  if (!strappy_tools_add_bool_to_object(root,
                                        "views_truncated",
                                        views_truncated)) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Could not build database context result.");
    return NULL;
  }

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize database context result.");
    return NULL;
  }

  return json;
}

static char *strappy_tools_remember_database_info(
  sqlite3 *db,
  const strappy_discovered_database_record *record,
  const strappy_memory_database_hint_remember_arguments *arguments,
  char **error_out)
{
  static const char *sql =
    "INSERT INTO helper_database_info "
    "(database_catalog_id, database_assistant_id, database_size, "
    "database_modified_at, kind, title, content, status) "
    "VALUES (?, ?, ?, ?, 'hint', 'Database hint', ?, 'active');";
  sqlite3_stmt *stmt;
  int rc;

  if ((db == NULL) || (record == NULL) || (arguments == NULL)) {
    strappy_set_error(error_out,
                      "memory_database_hint_remember request is incomplete.");
    return NULL;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(
      error_out,
      "Could not prepare memory_database_hint_remember: %s",
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
      (sqlite3_bind_text(stmt,
                         5,
                         arguments->hint,
                         -1,
                         SQLITE_TRANSIENT) != SQLITE_OK)) {
    sqlite3_finalize(stmt);
    if ((error_out != NULL) && (*error_out == NULL)) {
      strappy_set_formatted_error(
        error_out,
        "Could not bind memory_database_hint_remember: %s",
        sqlite3_errmsg(db));
    }
    return NULL;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save memory_database_hint: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return NULL;
  }
  sqlite3_finalize(stmt);

  return strappy_tools_build_empty_result(error_out);
}

static int strappy_tools_add_database_list_info_record(
  cJSON *databases,
  const strappy_discovered_database_record *record,
  char **error_out)
{
  cJSON *object;

  if ((databases == NULL) || (record == NULL) ||
      (record->assistant_database_id == NULL) ||
      (record->path == NULL)) {
    strappy_set_error(error_out, "Database list info request is incomplete.");
    return 0;
  }

  object = cJSON_CreateObject();
  if (object == NULL) {
    strappy_set_error(error_out, "Could not allocate database list result.");
    return 0;
  }

  if ((cJSON_AddStringToObject(object,
                               "database_id",
                               record->assistant_database_id) == NULL) ||
      (((record->app_name != NULL) && (record->app_name[0] != '\0')) ?
         (cJSON_AddStringToObject(object,
                                  "app_name",
                                  record->app_name) == NULL) :
         !strappy_tools_add_null_to_object(object, "app_name")) ||
      (cJSON_AddStringToObject(object, "path", record->path) == NULL) ||
      (cJSON_AddNumberToObject(object,
                               "size_bytes",
                               (double)record->size) == NULL) ||
      (cJSON_AddNumberToObject(object,
                               "modified_at",
                               (double)record->modified_at) == NULL)) {
    cJSON_Delete(object);
    strappy_set_error(error_out, "Could not build database metadata result.");
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
    cJSON *column_name;
    const char *name;

    name = sqlite3_column_name(stmt, column);
    if (name == NULL) {
      name = "";
    }

    column_name = cJSON_CreateString(name);
    if (column_name == NULL) {
      cJSON_Delete(columns);
      strappy_set_error(error_out, "Could not allocate query column.");
      return 0;
    }

    if (!cJSON_AddItemToArray(columns, column_name)) {
      cJSON_Delete(column_name);
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
  int *row_limit_reached_out,
  int *result_size_limit_reached_out,
  char **error_out)
{
  cJSON *rows;
  size_t payload_bytes;
  int row_count;
  int row_limit_reached;
  int result_size_limit_reached;
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

  if (row_limit_reached_out != NULL) {
    *row_limit_reached_out = row_limit_reached;
  }
  if (result_size_limit_reached_out != NULL) {
    *result_size_limit_reached_out = result_size_limit_reached;
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
  int row_limit_reached;
  int result_size_limit_reached;
  int rows_truncated;
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

  row_limit_reached = 0;
  result_size_limit_reached = 0;
  if (!strappy_tools_add_database_query_columns(stmt,
                                                root,
                                                column_count,
                                                error_out) ||
      !strappy_tools_add_database_query_rows(db,
                                             stmt,
                                             root,
                                             column_count,
                                             &progress,
                                             &row_limit_reached,
                                             &result_size_limit_reached,
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

  rows_truncated = (row_limit_reached || result_size_limit_reached) ? 1 : 0;
  if (!strappy_tools_add_bool_to_object(root,
                                        "rows_truncated",
                                        rows_truncated)) {
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

  if (strappy_tools_database_file_is_missing(record->path)) {
    strappy_tools_set_error_with_tool_guidance(
      error_out,
      "database_query approved database file is missing.",
      resource_dir,
      "database_query_missing_file");
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
  strappy_tools_datetime_token_converter converter,
  char **error_out)
{
  strappy_helper_datetime_arguments arguments;
  cJSON *results;
  char *json;
  size_t index;

  strappy_helper_datetime_arguments_init(&arguments);
  if (!strappy_tools_parse_helper_datetime_arguments(tool_name,
                                                     arguments_json,
                                                     items_argument_name,
                                                     &arguments,
                                                     error_out)) {
    return NULL;
  }

  results = cJSON_CreateArray();
  if (results == NULL) {
    strappy_helper_datetime_arguments_destroy(&arguments);
    strappy_set_formatted_error(error_out,
                                "Could not build %s result.",
                                tool_name);
    return NULL;
  }

  for (index = 0U; index < arguments.item_count; index++) {
    char *converted;
    cJSON *result;

    converted = converter(arguments.items[index],
                          strlen(arguments.items[index]),
                          arguments.unit,
                          error_out);
    if (converted == NULL) {
      cJSON_Delete(results);
      strappy_helper_datetime_arguments_destroy(&arguments);
      return NULL;
    }

    result = cJSON_CreateString(converted);
    free(converted);
    if ((result == NULL) || !cJSON_AddItemToArray(results, result)) {
      cJSON_Delete(result);
      cJSON_Delete(results);
      strappy_helper_datetime_arguments_destroy(&arguments);
      strappy_set_formatted_error(error_out,
                                  "Could not build %s result.",
                                  tool_name);
      return NULL;
    }
  }

  json = cJSON_PrintUnformatted(results);
  cJSON_Delete(results);
  strappy_helper_datetime_arguments_destroy(&arguments);
  if (json == NULL) {
    strappy_set_formatted_error(error_out,
                                "Could not serialize %s result.",
                                tool_name);
    return NULL;
  }
  return json;
}

static char *strappy_tools_execute_helper_datetime_to_iso8601(
  const char *arguments_json,
  char **error_out)
{
  return strappy_tools_execute_helper_datetime(
    STRAPPY_TOOL_HELPER_DATETIME_TO_ISO8601,
    arguments_json,
    "timestamps",
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
    strappy_tools_convert_iso8601_token_to_timestamp,
    error_out);
}

static unsigned char strappy_tools_ascii_lower(unsigned char value)
{
  if ((value >= 'A') && (value <= 'Z')) {
    return (unsigned char)(value + ('a' - 'A'));
  }

  return value;
}

static int strappy_tools_ascii_equal_length_ignore_case(
  const char *text,
  const char *needle,
  size_t needle_length)
{
  size_t index;

  if ((text == NULL) || (needle == NULL)) {
    return 0;
  }
  if (strlen(text) != needle_length) {
    return 0;
  }

  for (index = 0U; index < needle_length; index++) {
    if (strappy_tools_ascii_lower((unsigned char)text[index]) !=
        strappy_tools_ascii_lower((unsigned char)needle[index])) {
      return 0;
    }
  }

  return 1;
}

static int strappy_tools_ascii_contains_length_ignore_case(
  const char *text,
  const char *needle,
  size_t needle_length)
{
  size_t text_length;
  size_t offset;
  size_t index;

  if ((text == NULL) || (needle == NULL) || (needle_length == 0U)) {
    return 0;
  }

  text_length = strlen(text);
  if (needle_length > text_length) {
    return 0;
  }

  for (offset = 0U; offset <= (text_length - needle_length); offset++) {
    for (index = 0U; index < needle_length; index++) {
      if (strappy_tools_ascii_lower((unsigned char)text[offset + index]) !=
          strappy_tools_ascii_lower((unsigned char)needle[index])) {
        break;
      }
    }
    if (index == needle_length) {
      return 1;
    }
  }

  return 0;
}

static int strappy_tools_fontawesome_query_char(char value)
{
  if ((value >= 'A') && (value <= 'Z')) {
    return 1;
  }
  if ((value >= 'a') && (value <= 'z')) {
    return 1;
  }
  if ((value >= '0') && (value <= '9')) {
    return 1;
  }
  return ((value == '-') || (value == '_')) ? 1 : 0;
}

static int strappy_tools_fontawesome_text_score(const char *text,
                                                const char *query)
{
  const char *cursor;
  size_t query_length;
  int score;

  if (!strappy_tools_string_has_value(text) ||
      !strappy_tools_string_has_value(query)) {
    return 0;
  }

  query_length = strlen(query);
  score = 0;
  if (strappy_tools_ascii_equal_length_ignore_case(text,
                                                   query,
                                                   query_length)) {
    score += 240;
  } else if (strappy_tools_ascii_contains_length_ignore_case(text,
                                                            query,
                                                            query_length)) {
    score += 120;
  }

  cursor = query;
  while (*cursor != '\0') {
    const char *token_start;
    size_t token_length;

    while ((*cursor != '\0') &&
           !strappy_tools_fontawesome_query_char(*cursor)) {
      cursor++;
    }

    token_start = cursor;
    while ((*cursor != '\0') &&
           strappy_tools_fontawesome_query_char(*cursor)) {
      cursor++;
    }

    token_length = (size_t)(cursor - token_start);
    if (token_length == 0U) {
      continue;
    }

    if (strappy_tools_ascii_equal_length_ignore_case(text,
                                                     token_start,
                                                     token_length)) {
      score += 80;
    } else if (strappy_tools_ascii_contains_length_ignore_case(text,
                                                              token_start,
                                                              token_length)) {
      score += 30;
    }
  }

  return score;
}

static int strappy_tools_fontawesome_string_array_score(cJSON *icon,
                                                        const char *key,
                                                        const char *query,
                                                        char **error_out)
{
  cJSON *array;
  cJSON *item;
  int score;

  if ((icon == NULL) || (key == NULL)) {
    strappy_set_error(error_out, "Font Awesome icon search is incomplete.");
    return -1;
  }

  array = cJSON_GetObjectItemCaseSensitive(icon, key);
  if (array == NULL) {
    return 0;
  }
  if (!cJSON_IsArray(array)) {
    strappy_set_formatted_error(
      error_out,
      "Font Awesome icon resource contains malformed %s.",
      key);
    return -1;
  }

  score = 0;
  for (item = array->child; item != NULL; item = item->next) {
    if (!cJSON_IsString(item)) {
      strappy_set_formatted_error(
        error_out,
        "Font Awesome icon resource contains malformed %s item.",
        key);
      return -1;
    }
    score += strappy_tools_fontawesome_text_score(item->valuestring, query);
  }

  return score;
}

static int strappy_tools_fontawesome_icon_score(
  cJSON *icon,
  const strappy_helper_fontawesome_shortcode_search_arguments *arguments,
  char **error_out)
{
  cJSON *name;
  cJSON *label;
  cJSON *style;
  cJSON *shortcode;
  int score;
  int array_score;

  if ((icon == NULL) || (arguments == NULL)) {
    strappy_set_error(error_out, "Font Awesome icon search is incomplete.");
    return -1;
  }

  name = cJSON_GetObjectItemCaseSensitive(icon, "name");
  label = cJSON_GetObjectItemCaseSensitive(icon, "label");
  style = cJSON_GetObjectItemCaseSensitive(icon, "style");
  shortcode = cJSON_GetObjectItemCaseSensitive(icon, "shortcode");
  if (!cJSON_IsString(name) ||
      !cJSON_IsString(label) ||
      !cJSON_IsString(style) ||
      !cJSON_IsString(shortcode)) {
    strappy_set_error(error_out,
                      "Font Awesome icon resource contains a malformed icon.");
    return -1;
  }

  if (!strappy_tools_string_has_value(arguments->query)) {
    return 1;
  }

  score = 0;
  score += 4 * strappy_tools_fontawesome_text_score(name->valuestring,
                                                    arguments->query);
  score += 3 * strappy_tools_fontawesome_text_score(label->valuestring,
                                                    arguments->query);
  score += strappy_tools_fontawesome_text_score(shortcode->valuestring,
                                                arguments->query);
  array_score = strappy_tools_fontawesome_string_array_score(icon,
                                                             "aliases",
                                                             arguments->query,
                                                             error_out);
  if (array_score < 0) {
    return -1;
  }
  score += 4 * array_score;
  array_score = strappy_tools_fontawesome_string_array_score(icon,
                                                             "search_terms",
                                                             arguments->query,
                                                             error_out);
  if (array_score < 0) {
    return -1;
  }
  score += 2 * array_score;
  array_score = strappy_tools_fontawesome_string_array_score(icon,
                                                             "categories",
                                                             arguments->query,
                                                             error_out);
  if (array_score < 0) {
    return -1;
  }
  score += array_score;

  return score;
}

static int strappy_tools_fontawesome_find_icon_by_shortcode(cJSON *catalog_icons,
                                                            const char *shortcode,
                                                            cJSON **icon_out,
                                                            char **error_out)
{
  cJSON *icon;

  if ((catalog_icons == NULL) || (shortcode == NULL) || (icon_out == NULL)) {
    strappy_set_error(error_out,
                      "Font Awesome shortcode lookup is incomplete.");
    return 0;
  }
  *icon_out = NULL;

  for (icon = catalog_icons->child; icon != NULL; icon = icon->next) {
    cJSON *catalog_shortcode;

    catalog_shortcode = cJSON_GetObjectItemCaseSensitive(icon, "shortcode");
    if (!cJSON_IsString(catalog_shortcode)) {
      strappy_set_error(error_out,
                        "Font Awesome icon resource contains a malformed icon.");
      return 0;
    }

    if (strcmp(catalog_shortcode->valuestring, shortcode) == 0) {
      *icon_out = icon;
      return 1;
    }
  }

  return 1;
}

static int strappy_tools_fontawesome_add_candidate(
  cJSON *icon,
  int score,
  cJSON **candidates,
  int *scores,
  int *candidate_count,
  int limit,
  char **error_out)
{
  cJSON *shortcode;
  cJSON *copy;
  int position;
  int shift_start;
  int index;

  if ((candidates == NULL) || (scores == NULL) ||
      (candidate_count == NULL) || (limit <= 0)) {
    strappy_set_error(error_out, "Font Awesome candidate storage is incomplete.");
    return 0;
  }

  if (score <= 0) {
    return 1;
  }

  position = 0;
  while ((position < *candidate_count) && (scores[position] >= score)) {
    position++;
  }
  if (position >= limit) {
    return 1;
  }

  shortcode = cJSON_IsObject(icon) ?
    cJSON_GetObjectItemCaseSensitive(icon, "shortcode") : NULL;
  if (!cJSON_IsString(shortcode) || (shortcode->valuestring == NULL)) {
    strappy_set_error(error_out,
                      "Font Awesome icon resource contains a malformed icon.");
    return 0;
  }
  copy = cJSON_CreateString(shortcode->valuestring);
  if (copy == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate Font Awesome search result.");
    return 0;
  }

  if (*candidate_count < limit) {
    shift_start = *candidate_count;
    (*candidate_count)++;
  } else {
    cJSON_Delete(candidates[limit - 1]);
    shift_start = limit - 1;
  }

  for (index = shift_start; index > position; index--) {
    candidates[index] = candidates[index - 1];
    scores[index] = scores[index - 1];
  }

  candidates[position] = copy;
  scores[position] = score;
  return 1;
}

static void strappy_tools_fontawesome_clear_candidates(cJSON **candidates,
                                                       int candidate_count)
{
  int index;

  if (candidates == NULL) {
    return;
  }

  for (index = 0; index < candidate_count; index++) {
    cJSON_Delete(candidates[index]);
    candidates[index] = NULL;
  }
}

static char *strappy_tools_execute_helper_fontawesome_shortcode_search(
  const char *resource_dir,
  const char *arguments_json,
  char **error_out)
{
  strappy_helper_fontawesome_shortcode_search_arguments arguments;
  cJSON *catalog;
  cJSON *catalog_icons;
  cJSON *catalog_icon;
  cJSON *root;
  cJSON *candidates[STRAPPY_HELPER_FONTAWESOME_RESULT_LIMIT];
  int scores[STRAPPY_HELPER_FONTAWESOME_RESULT_LIMIT];
  int index;
  int candidate_count;
  char *json;

  strappy_helper_fontawesome_shortcode_search_arguments_init(&arguments);
  if (!strappy_tools_parse_helper_fontawesome_shortcode_search_arguments(
        arguments_json,
        &arguments,
        error_out)) {
    return NULL;
  }

  catalog = strappy_tools_read_json_resource(resource_dir,
                                             STRAPPY_FONTAWESOME_ICONS_RESOURCE,
                                             error_out);
  if (catalog == NULL) {
    strappy_helper_fontawesome_shortcode_search_arguments_destroy(&arguments);
    return NULL;
  }

  catalog_icons = cJSON_GetObjectItemCaseSensitive(catalog, "icons");
  if (!cJSON_IsArray(catalog_icons)) {
    cJSON_Delete(catalog);
    strappy_helper_fontawesome_shortcode_search_arguments_destroy(&arguments);
    strappy_set_error(error_out,
                      "Font Awesome icon resource must contain an icons array.");
    return NULL;
  }

  for (index = 0;
       index < STRAPPY_HELPER_FONTAWESOME_RESULT_LIMIT;
       index++) {
    candidates[index] = NULL;
    scores[index] = 0;
  }
  candidate_count = 0;

  for (catalog_icon = catalog_icons->child;
       catalog_icon != NULL;
       catalog_icon = catalog_icon->next) {
    int score;

    score = strappy_tools_fontawesome_icon_score(catalog_icon,
                                                 &arguments,
                                                 error_out);
    if (score < 0) {
      strappy_tools_fontawesome_clear_candidates(candidates, candidate_count);
      cJSON_Delete(catalog);
      strappy_helper_fontawesome_shortcode_search_arguments_destroy(&arguments);
      return NULL;
    }
    if (score > 0) {
      if (!strappy_tools_fontawesome_add_candidate(catalog_icon,
                                                   score,
                                                   candidates,
                                                   scores,
                                                   &candidate_count,
                                                   STRAPPY_HELPER_FONTAWESOME_RESULT_LIMIT,
                                                   error_out)) {
        strappy_tools_fontawesome_clear_candidates(candidates, candidate_count);
        cJSON_Delete(catalog);
        strappy_helper_fontawesome_shortcode_search_arguments_destroy(&arguments);
        return NULL;
      }
    }
  }

  root = cJSON_CreateArray();
  if (root == NULL) {
    strappy_tools_fontawesome_clear_candidates(candidates, candidate_count);
    cJSON_Delete(catalog);
    strappy_helper_fontawesome_shortcode_search_arguments_destroy(&arguments);
    strappy_set_error(error_out,
                      "Could not allocate Font Awesome search result.");
    return NULL;
  }

  for (index = 0; index < candidate_count; index++) {
    if (!cJSON_AddItemToArray(root, candidates[index])) {
      cJSON_Delete(candidates[index]);
      candidates[index] = NULL;
      cJSON_Delete(root);
      strappy_tools_fontawesome_clear_candidates(candidates, candidate_count);
      cJSON_Delete(catalog);
      strappy_helper_fontawesome_shortcode_search_arguments_destroy(&arguments);
      strappy_set_error(error_out,
                        "Could not add Font Awesome shortcode to search result.");
      return NULL;
    }
    candidates[index] = NULL;
  }

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  cJSON_Delete(catalog);
  strappy_helper_fontawesome_shortcode_search_arguments_destroy(&arguments);
  if (json == NULL) {
    strappy_set_error(error_out,
                      "Could not serialize Font Awesome search result.");
    return NULL;
  }

  return json;
}

static char *strappy_tools_execute_helper_fontawesome_shortcode_confirm(
  const char *resource_dir,
  const char *arguments_json,
  char **error_out)
{
  strappy_helper_fontawesome_shortcode_confirm_arguments arguments;
  cJSON *catalog;
  cJSON *catalog_icons;
  cJSON *root;
  cJSON *confirmed;
  cJSON *not_found;
  size_t index;
  char *json;

  strappy_helper_fontawesome_shortcode_confirm_arguments_init(&arguments);
  if (!strappy_tools_parse_helper_fontawesome_shortcode_confirm_arguments(
        arguments_json,
        &arguments,
        error_out)) {
    return NULL;
  }

  catalog = strappy_tools_read_json_resource(resource_dir,
                                             STRAPPY_FONTAWESOME_ICONS_RESOURCE,
                                             error_out);
  if (catalog == NULL) {
    strappy_helper_fontawesome_shortcode_confirm_arguments_destroy(&arguments);
    return NULL;
  }

  catalog_icons = cJSON_GetObjectItemCaseSensitive(catalog, "icons");
  if (!cJSON_IsArray(catalog_icons)) {
    cJSON_Delete(catalog);
    strappy_helper_fontawesome_shortcode_confirm_arguments_destroy(&arguments);
    strappy_set_error(error_out,
                      "Font Awesome icon resource must contain an icons array.");
    return NULL;
  }

  root = cJSON_CreateObject();
  confirmed = cJSON_CreateArray();
  not_found = cJSON_CreateArray();
  if ((root == NULL) || (confirmed == NULL) || (not_found == NULL)) {
    cJSON_Delete(not_found);
    cJSON_Delete(confirmed);
    cJSON_Delete(root);
    cJSON_Delete(catalog);
    strappy_helper_fontawesome_shortcode_confirm_arguments_destroy(&arguments);
    strappy_set_error(error_out,
                      "Could not allocate Font Awesome confirm result.");
    return NULL;
  }

  if (!cJSON_AddItemToObject(root, "confirmed", confirmed)) {
    cJSON_Delete(not_found);
    cJSON_Delete(confirmed);
    cJSON_Delete(root);
    cJSON_Delete(catalog);
    strappy_helper_fontawesome_shortcode_confirm_arguments_destroy(&arguments);
    strappy_set_error(error_out, "Could not build Font Awesome confirm result.");
    return NULL;
  }
  confirmed = NULL;

  if (!cJSON_AddItemToObject(root, "not_found", not_found)) {
    cJSON_Delete(not_found);
    cJSON_Delete(root);
    cJSON_Delete(catalog);
    strappy_helper_fontawesome_shortcode_confirm_arguments_destroy(&arguments);
    strappy_set_error(error_out, "Could not build Font Awesome confirm result.");
    return NULL;
  }
  not_found = NULL;

  for (index = 0U; index < arguments.shortcode_count; index++) {
    cJSON *icon;
    cJSON *item;
    cJSON *target;

    icon = NULL;
    if (!strappy_tools_fontawesome_find_icon_by_shortcode(catalog_icons,
                                                          arguments.shortcodes[index],
                                                          &icon,
                                                          error_out)) {
      cJSON_Delete(root);
      cJSON_Delete(catalog);
      strappy_helper_fontawesome_shortcode_confirm_arguments_destroy(&arguments);
      return NULL;
    }

    target = cJSON_GetObjectItemCaseSensitive(
      root,
      (icon != NULL) ? "confirmed" : "not_found");
    item = cJSON_CreateString(arguments.shortcodes[index]);
    if ((item == NULL) || !cJSON_AddItemToArray(target, item)) {
      cJSON_Delete(item);
      cJSON_Delete(root);
      cJSON_Delete(catalog);
      strappy_helper_fontawesome_shortcode_confirm_arguments_destroy(&arguments);
      strappy_set_error(error_out,
                        "Could not add shortcode to Font Awesome confirm result.");
      return NULL;
    }
  }

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  cJSON_Delete(catalog);
  strappy_helper_fontawesome_shortcode_confirm_arguments_destroy(&arguments);
  if (json == NULL) {
    strappy_set_error(error_out,
                      "Could not serialize Font Awesome confirm result.");
    return NULL;
  }

  return json;
}

static char *strappy_tools_execute_memory_user_fact_read(
  const char *session_db_path,
  const char *arguments_json,
  char **error_out)
{
  sqlite3 *db;
  char *json;

  if (!strappy_tools_validate_empty_arguments(
        STRAPPY_TOOL_MEMORY_USER_FACT_READ,
        arguments_json,
        error_out)) {
    return NULL;
  }

  db = NULL;
  if (!strappy_tools_open_helper_info_database(session_db_path,
                                               &db,
                                               error_out)) {
    return NULL;
  }

  json = strappy_tools_read_user_info(db, error_out);
  sqlite3_close(db);
  return json;
}

static char *strappy_tools_execute_memory_user_fact_remember(
  const char *session_db_path,
  const char *arguments_json,
  char **error_out)
{
  strappy_memory_user_fact_remember_arguments arguments;
  sqlite3 *db;
  char *json;

  strappy_memory_user_fact_remember_arguments_init(&arguments);
  if (!strappy_tools_parse_memory_user_fact_remember_arguments(arguments_json,
                                                               &arguments,
                                                               error_out)) {
    return NULL;
  }
  if (arguments.fact == NULL) {
    json = strappy_tools_build_empty_result(error_out);
    strappy_memory_user_fact_remember_arguments_destroy(&arguments);
    return json;
  }

  db = NULL;
  if (!strappy_tools_open_helper_info_database(session_db_path,
                                               &db,
                                               error_out)) {
    strappy_memory_user_fact_remember_arguments_destroy(&arguments);
    return NULL;
  }

  json = strappy_tools_remember_user_info(db, &arguments, error_out);
  sqlite3_close(db);
  strappy_memory_user_fact_remember_arguments_destroy(&arguments);
  return json;
}

static char *strappy_tools_execute_memory_user_fact_forget(
  const char *session_db_path,
  const char *arguments_json,
  char **error_out)
{
  strappy_helper_info_forget_arguments arguments;
  sqlite3 *db;
  char *json;

  if (!strappy_tools_parse_id_argument(STRAPPY_TOOL_MEMORY_USER_FACT_FORGET,
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

static char *strappy_tools_execute_helper_session_name_write(
  const char *session_db_path,
  long long active_session_id,
  const char *arguments_json,
  char **error_out)
{
  strappy_helper_session_name_write_arguments arguments;
  char *name;
  char *json;

  if (active_session_id <= 0) {
    strappy_set_error(error_out,
                      "helper_session_name_write requires an active session.");
    return NULL;
  }

  strappy_helper_session_name_write_arguments_init(&arguments);
  if (!strappy_tools_parse_helper_session_name_write_arguments(arguments_json,
                                                               &arguments,
                                                               error_out)) {
    return NULL;
  }

  name = strappy_tools_copy_normalized_session_name(arguments.name, error_out);
  strappy_helper_session_name_write_arguments_destroy(&arguments);
  if (name == NULL) {
    return NULL;
  }

  if (!strappy_db_update_session_name(session_db_path,
                                      active_session_id,
                                      name,
                                      error_out)) {
    free(name);
    return NULL;
  }

  json = strappy_tools_build_empty_result(error_out);
  free(name);
  return json;
}

static char *strappy_tools_execute_database_context_read(
  const char *session_db_path,
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
  if (arguments.database_id == NULL) {
    json = strappy_tools_build_empty_result(error_out);
    strappy_database_context_read_arguments_destroy(&arguments);
    return json;
  }

  strappy_discovered_database_record_list_init(&list);
  record = NULL;
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
      strappy_set_error(error_out,
                        "database_context_read database_id is not approved "
                        "or is no longer valid.");
    } else {
      strappy_set_error(error_out,
                        "database_context_read database_id was not found.");
    }
    strappy_discovered_database_record_list_destroy(&list);
    strappy_database_context_read_arguments_destroy(&arguments);
    return NULL;
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
                                          record,
                                          error_out);
  sqlite3_close(db);
  strappy_discovered_database_record_list_destroy(&list);
  strappy_database_context_read_arguments_destroy(&arguments);
  return json;
}

static char *strappy_tools_execute_memory_database_hint_remember(
  const char *session_db_path,
  const char *resource_dir,
  const char *arguments_json,
  char **error_out)
{
  strappy_memory_database_hint_remember_arguments arguments;
  strappy_discovered_database_record_list list;
  const strappy_discovered_database_record *record;
  sqlite3 *db;
  char *json;
  int matched_unavailable;

  strappy_memory_database_hint_remember_arguments_init(&arguments);
  if (!strappy_tools_parse_memory_database_hint_remember_arguments(
        arguments_json,
        &arguments,
        error_out)) {
    return NULL;
  }
  if (arguments.hint == NULL) {
    json = strappy_tools_build_empty_result(error_out);
    strappy_memory_database_hint_remember_arguments_destroy(&arguments);
    return json;
  }

  strappy_discovered_database_record_list_init(&list);
  if (!strappy_db_list_discovered_databases(session_db_path,
                                           &list,
                                           error_out)) {
    strappy_memory_database_hint_remember_arguments_destroy(&arguments);
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
        "memory_database_hint_remember database_id is not approved or is no "
        "longer valid.",
        resource_dir,
        "memory_database_hint_remember_unavailable_database_id");
    } else {
      strappy_tools_set_error_with_database_guidance(
        error_out,
        "memory_database_hint_remember database_id was not found.",
        resource_dir,
        "memory_database_hint_remember_missing_database_id");
    }
    strappy_discovered_database_record_list_destroy(&list);
    strappy_memory_database_hint_remember_arguments_destroy(&arguments);
    return NULL;
  }

  db = NULL;
  if (!strappy_tools_open_helper_info_database(session_db_path,
                                               &db,
                                               error_out)) {
    strappy_discovered_database_record_list_destroy(&list);
    strappy_memory_database_hint_remember_arguments_destroy(&arguments);
    return NULL;
  }

  json = strappy_tools_remember_database_info(db,
                                              record,
                                              &arguments,
                                              error_out);
  sqlite3_close(db);
  strappy_discovered_database_record_list_destroy(&list);
  strappy_memory_database_hint_remember_arguments_destroy(&arguments);
  return json;
}

static char *strappy_tools_execute_memory_database_hint_forget(
  const char *session_db_path,
  const char *arguments_json,
  char **error_out)
{
  strappy_helper_info_forget_arguments arguments;
  sqlite3 *db;
  char *json;

  if (!strappy_tools_parse_id_argument(STRAPPY_TOOL_MEMORY_DATABASE_HINT_FORGET,
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
  const char *arguments_json,
  char **error_out)
{
  strappy_discovered_database_record_list list;
  cJSON *root;
  cJSON *databases;
  char *json;
  size_t index;
  int empty;

  if ((session_db_path == NULL) || (session_db_path[0] == '\0')) {
    strappy_set_error(error_out, "Catalog database path is not configured.");
    return NULL;
  }

  if (!strappy_tools_validate_empty_arguments(STRAPPY_TOOL_DATABASE_LIST_INFO,
                                              arguments_json,
                                              error_out)) {
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

  for (index = 0U; index < list.count; index++) {
    const strappy_discovered_database_record *record;

    record = &list.records[index];
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
  }

  empty = (databases->child == NULL) ? 1 : 0;
  if (!cJSON_AddItemToObject(root, "databases", databases)) {
    cJSON_Delete(root);
    cJSON_Delete(databases);
    strappy_discovered_database_record_list_destroy(&list);
    strappy_set_error(error_out, "Could not build database list result.");
    return NULL;
  }
  databases = NULL;
  if (empty &&
      (cJSON_AddStringToObject(root,
                              "guidance",
                              STRAPPY_DATABASE_LIST_EMPTY_GUIDANCE) == NULL)) {
    cJSON_Delete(root);
    strappy_discovered_database_record_list_destroy(&list);
    strappy_set_error(error_out, "Could not build database list result.");
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
                            long long active_session_id,
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

  if (strcmp(tool_name, STRAPPY_TOOL_MEMORY_USER_FACT_READ) == 0) {
    return strappy_tools_execute_memory_user_fact_read(session_db_path,
                                                       arguments_json,
                                                       error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER) == 0) {
    return strappy_tools_execute_memory_user_fact_remember(session_db_path,
                                                           arguments_json,
                                                           error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_MEMORY_USER_FACT_FORGET) == 0) {
    return strappy_tools_execute_memory_user_fact_forget(session_db_path,
                                                         arguments_json,
                                                         error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE) == 0) {
    return strappy_tools_execute_helper_session_name_write(session_db_path,
                                                           active_session_id,
                                                           arguments_json,
                                                           error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_DATABASE_CONTEXT_READ) == 0) {
    return strappy_tools_execute_database_context_read(session_db_path,
                                                       arguments_json,
                                                       error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER) == 0) {
    return strappy_tools_execute_memory_database_hint_remember(session_db_path,
                                                               resource_dir,
                                                               arguments_json,
                                                               error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_MEMORY_DATABASE_HINT_FORGET) == 0) {
    return strappy_tools_execute_memory_database_hint_forget(session_db_path,
                                                             arguments_json,
                                                             error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_SEARCH) == 0) {
    return strappy_tools_execute_helper_fontawesome_shortcode_search(resource_dir,
                                                                     arguments_json,
                                                                     error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_CONFIRM) == 0) {
    return strappy_tools_execute_helper_fontawesome_shortcode_confirm(
      resource_dir,
      arguments_json,
      error_out);
  }

  strappy_set_formatted_error(error_out, "Tool is not registered: %s", tool_name);
  return NULL;
}
