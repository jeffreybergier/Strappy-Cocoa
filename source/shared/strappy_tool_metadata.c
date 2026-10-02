#include "strappy_tool_metadata.h"

#include "strappy_core.h"

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
#define STRAPPY_TOOL_DESCRIPTION_KEY "description"
#define STRAPPY_TOOL_DISPLAY_METADATA_KEY "x-strappy-display"
#define STRAPPY_TOOL_DISPLAY_LABEL_KEY "label"
#define STRAPPY_TOOL_DISPLAY_PROMOTED_ARGUMENT_KEY "promoted_argument"
#define STRAPPY_TOOL_DISPLAY_PROMOTED_PATH_KEY "promoted_path"
#define STRAPPY_TOOL_DISPLAY_TRANSFORM_KEY "transform"
#define STRAPPY_TOOL_RESULT_GUIDANCE_KEY "result_guidance"
#define STRAPPY_TOOL_RESULT_GUIDANCE_ON_EMPTY_KEY "onEmpty"
#define STRAPPY_TOOL_RESULT_GUIDANCE_ON_NOT_EMPTY_KEY "onNotEmpty"
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
#define STRAPPY_DATABASE_CONTEXT_TABLE_LIMIT 20
#define STRAPPY_DATABASE_CONTEXT_VIEW_LIMIT 20
#define STRAPPY_HELPER_SESSION_NAME_MAX_BYTES 96U
#define STRAPPY_HELPER_FONTAWESOME_MAX_QUERY_BYTES 128U
#define STRAPPY_HELPER_FONTAWESOME_MAX_SHORTCODE_BYTES 96U
#define STRAPPY_HELPER_FONTAWESOME_MAX_SHORTCODES 32U
#define STRAPPY_HELPER_FONTAWESOME_RESULT_LIMIT 20

typedef enum strappy_tool_kind {
  STRAPPY_TOOL_KIND_DATABASE = 1,
  STRAPPY_TOOL_KIND_HELPER = 2,
  STRAPPY_TOOL_KIND_DEVELOPER = 3
} strappy_tool_kind;

typedef struct strappy_tool_definition {
  const char *name;
  strappy_tool_kind kind;
} strappy_tool_definition;

typedef struct strappy_server_tool_definition {
  const char *type;
  const char *feature;
  const char *response_item_type;
} strappy_server_tool_definition;

typedef struct strappy_tools_text_buffer {
  char *data;
  size_t length;
} strappy_tools_text_buffer;

static const strappy_tool_definition strappy_tool_definitions[] = {
  { STRAPPY_TOOL_DATABASE_LIST, STRAPPY_TOOL_KIND_DATABASE },
  { STRAPPY_TOOL_DATABASE_QUERY, STRAPPY_TOOL_KIND_DATABASE },
  { STRAPPY_TOOL_BASH, STRAPPY_TOOL_KIND_DEVELOPER },
  { STRAPPY_TOOL_FILE_READ, STRAPPY_TOOL_KIND_DEVELOPER },
  { STRAPPY_TOOL_FILE_WRITE, STRAPPY_TOOL_KIND_DEVELOPER },
  { STRAPPY_TOOL_FILE_EDIT, STRAPPY_TOOL_KIND_DEVELOPER },
  { STRAPPY_TOOL_DATETIME_TO_ISO8601, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_DATETIME_FROM_ISO8601, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_MEMORY_READ, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_MEMORY_SAVE, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_MEMORY_DELETE, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_SKILLS_LIST, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_SKILL_READ, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_SESSION_RENAME, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_DATABASE_CONTEXT, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_DATABASE_STUDY, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_FONTAWESOME_SEARCH, STRAPPY_TOOL_KIND_HELPER },
  { STRAPPY_TOOL_FONTAWESOME_CONFIRM, STRAPPY_TOOL_KIND_HELPER }
};

static const size_t strappy_tool_definition_count =
  sizeof(strappy_tool_definitions) / sizeof(strappy_tool_definitions[0]);

static const strappy_server_tool_definition strappy_server_tool_definitions[] = {
  { STRAPPY_TOOL_OPENROUTER_WEB_SEARCH,
    STRAPPY_TOOL_SERVER_FEATURE_WEB_SEARCH,
    NULL },
  { STRAPPY_TOOL_OPENROUTER_WEB_FETCH,
    STRAPPY_TOOL_SERVER_FEATURE_WEB_SEARCH,
    NULL },
  { STRAPPY_TOOL_WEB_SEARCH,
    STRAPPY_TOOL_SERVER_FEATURE_WEB_SEARCH,
    STRAPPY_RESPONSE_ITEM_WEB_SEARCH_CALL }
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

int strappy_tools_is_registered(const char *tool_name)
{
  return (strappy_tools_find_definition(tool_name) != NULL) ||
    (strappy_tools_find_server_definition(tool_name) != NULL);
}

int strappy_tools_is_server(const char *tool_name)
{
  return (strappy_tools_find_server_definition(tool_name) != NULL) ? 1 : 0;
}

char *strappy_tools_resource_path(const char *resource_dir,
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

char *strappy_tools_read_file(const char *path, char **error_out)
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

cJSON *strappy_tools_read_json_resource(const char *resource_dir,
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

static const char *strappy_tools_tool_schema_description(cJSON *tool)
{
  cJSON *function;
  cJSON *description;

  function = cJSON_IsObject(tool) ?
    cJSON_GetObjectItemCaseSensitive(tool, "function") : NULL;
  description = cJSON_IsObject(function) ?
    cJSON_GetObjectItemCaseSensitive(function,
                                     STRAPPY_TOOL_DESCRIPTION_KEY) : NULL;
  if ((description == NULL) || !cJSON_IsString(description) ||
      (description->valuestring == NULL) ||
      (description->valuestring[0] == '\0')) {
    return NULL;
  }
  return description->valuestring;
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

  if ((type != NULL) && cJSON_IsString(type) &&
      (type->valuestring != NULL) &&
      (type->valuestring[0] != '\0')) {
    return type->valuestring;
  }
  if ((type == NULL) || !cJSON_IsArray(type)) {
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
    cJSON *description;
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
           (strcmp(field->string, STRAPPY_TOOL_DESCRIPTION_KEY) != 0) &&
           (strcmp(field->string, STRAPPY_TOOL_SERVER_FEATURE_KEY) != 0) &&
           (strcmp(field->string, STRAPPY_TOOL_DISPLAY_METADATA_KEY) != 0))) {
        strappy_set_formatted_error(
          error_out,
          "Tool guidance server tool %s contains an unsupported field.",
          type);
        return 0;
      }
    }
    description = cJSON_GetObjectItemCaseSensitive(
      tool,
      STRAPPY_TOOL_DESCRIPTION_KEY);
    if ((description == NULL) || !cJSON_IsString(description) ||
        (description->valuestring == NULL) ||
        (description->valuestring[0] == '\0')) {
      strappy_set_formatted_error(
        error_out,
        "Tool guidance server tool %s is missing its description.",
        type);
      return 0;
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
    const char *description;

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
    description = strappy_tools_tool_schema_description(tool);
    if (description == NULL) {
      strappy_set_formatted_error(
        error_out,
        "Tool guidance schema %s is missing its description.",
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

static int strappy_tools_server_schema_is_enabled(
  cJSON *server_tool,
  const char * const *allowed_names,
  size_t allowed_name_count,
  strappy_provider_kind provider,
  strappy_web_provider web_provider)
{
  const strappy_provider_definition *definition;
  const char *type;
  cJSON *feature;

  type = strappy_tools_server_schema_type(server_tool);
  definition = strappy_provider_for_kind(provider);
  if ((type == NULL) || (definition == NULL) ||
      !strappy_provider_supports_hosted_tool(definition, type)) {
    return 0;
  }
  if ((allowed_names != NULL) && (allowed_name_count > 0U) &&
      !strappy_tools_name_is_allowed(type, allowed_names, allowed_name_count) &&
      !((definition->request_profile ==
           STRAPPY_PROVIDER_REQUEST_CHATGPT_CODEX) &&
        (strcmp(type, STRAPPY_TOOL_WEB_SEARCH) == 0) &&
        strappy_tools_name_is_allowed(STRAPPY_TOOL_OPENROUTER_WEB_SEARCH,
                                      allowed_names,
                                      allowed_name_count))) {
    return 0;
  }
  feature = cJSON_GetObjectItemCaseSensitive(
    server_tool,
    STRAPPY_TOOL_SERVER_FEATURE_KEY);
  if (cJSON_IsString(feature) && (feature->valuestring != NULL) &&
      (strcmp(feature->valuestring,
              STRAPPY_TOOL_SERVER_FEATURE_WEB_SEARCH) == 0) &&
      !strappy_web_provider_is_enabled(web_provider)) {
    return 0;
  }
  return 1;
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
    const strappy_server_tool_definition *definition;
    cJSON *display;
    cJSON *response_copy;
    cJSON *copy;

    type = strappy_tools_server_schema_type(tool);
    definition = strappy_tools_find_server_definition(type);
    display = cJSON_GetObjectItemCaseSensitive(
      tool,
      STRAPPY_TOOL_DISPLAY_METADATA_KEY);
    copy = cJSON_Duplicate(display, 1);
    response_copy = NULL;
    if ((definition != NULL) && (definition->response_item_type != NULL)) {
      response_copy = cJSON_Duplicate(display, 1);
    }
    if ((copy == NULL) ||
        ((definition != NULL) &&
         (definition->response_item_type != NULL) &&
         (response_copy == NULL)) ||
        (cJSON_AddBoolToObject(copy, "response_item", 1) == NULL)) {
      cJSON_Delete(copy);
      cJSON_Delete(response_copy);
      cJSON_Delete(registry);
      cJSON_Delete(root);
      strappy_set_error(error_out,
                        "Could not build server tool display registry.");
      return NULL;
    }
    if (!cJSON_AddItemToObject(registry, type, copy)) {
      cJSON_Delete(copy);
      cJSON_Delete(response_copy);
      cJSON_Delete(registry);
      cJSON_Delete(root);
      strappy_set_error(error_out,
                        "Could not build server tool display registry.");
      return NULL;
    }
    copy = NULL;
    if ((response_copy != NULL) &&
        ((cJSON_AddBoolToObject(response_copy, "response_item", 1) == NULL) ||
         !cJSON_AddItemToObject(registry,
                               definition->response_item_type,
                               response_copy))) {
      cJSON_Delete(response_copy);
      cJSON_Delete(registry);
      cJSON_Delete(root);
      strappy_set_error(error_out,
                        "Could not build server tool response display registry.");
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
  const char * const *allowed_names,
  size_t allowed_name_count,
  strappy_provider_kind provider,
  strappy_web_provider web_provider,
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
    cJSON *response_tool;

    type = strappy_tools_server_schema_type(server_tool);
    if (!strappy_tools_server_schema_is_enabled(server_tool,
                                                allowed_names,
                                                allowed_name_count,
                                                provider,
                                                web_provider)) {
      continue;
    }
    response_tool = cJSON_CreateObject();
    if ((response_tool == NULL) ||
        (cJSON_AddStringToObject(response_tool, "type", type) == NULL)) {
      cJSON_Delete(response_tool);
      cJSON_Delete(root);
      strappy_set_formatted_error(error_out,
                                  "Could not add Responses server tool: %s",
                                  type);
      return 0;
    }
    if ((strcmp(type, STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) == 0) ||
        (strcmp(type, STRAPPY_TOOL_OPENROUTER_WEB_FETCH) == 0)) {
      cJSON *parameters;
      const char *engine;

      engine = strappy_web_provider_name(web_provider);
      parameters = cJSON_CreateObject();
      if ((engine == NULL) || (parameters == NULL) ||
          (cJSON_AddStringToObject(parameters, "engine", engine) == NULL) ||
          !cJSON_AddItemToObject(response_tool, "parameters", parameters)) {
        cJSON_Delete(parameters);
        cJSON_Delete(response_tool);
        cJSON_Delete(root);
        strappy_set_formatted_error(error_out,
                                    "Could not configure Responses server tool: %s",
                                    type);
        return 0;
      }
    }
    if (!cJSON_AddItemToArray(tools, response_tool)) {
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
  const char * const *allowed_names,
  size_t allowed_name_count,
  strappy_provider_kind provider,
  strappy_web_provider web_provider,
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

    {
      cJSON *name;

      name = cJSON_GetObjectItemCaseSensitive(function, "name");
      if (!cJSON_IsString(name) || (name->valuestring == NULL)) {
        cJSON_Delete(responses_tools);
        cJSON_Delete(chat_tools);
        strappy_set_error(error_out,
                          "Chat tool schema is missing its function name.");
        return NULL;
      }
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
                                                   allowed_names,
                                                   allowed_name_count,
                                                   provider,
                                                   web_provider,
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
  strappy_web_provider web_provider,
  char **error_out)
{
  return strappy_tools_responses_request_json_filtered_for_provider(
    resource_dir,
    allowed_names,
    allowed_name_count,
    STRAPPY_PROVIDER_KIND_OPENROUTER,
    web_provider,
    error_out);
}

char *strappy_tools_responses_request_json_filtered_for_provider(
  const char *resource_dir,
  const char * const *allowed_names,
  size_t allowed_name_count,
  strappy_provider_kind provider,
  strappy_web_provider web_provider,
  char **error_out)
{
  char *chat_tools_json;
  char *responses_tools_json;

  if (strappy_provider_for_kind(provider) == NULL) {
    strappy_set_error(error_out, "Responses tool provider is not registered.");
    return NULL;
  }
  if (strappy_web_provider_name(web_provider) == NULL) {
    strappy_set_error(error_out, "Web provider is invalid.");
    return NULL;
  }

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
                                                allowed_names,
                                                allowed_name_count,
                                                provider,
                                                web_provider,
                                                error_out);
  free(chat_tools_json);
  return responses_tools_json;
}

char *strappy_tools_responses_request_json(const char *resource_dir,
                                           strappy_web_provider web_provider,
                                           char **error_out)
{
  return strappy_tools_responses_request_json_filtered(resource_dir,
                                                       NULL,
                                                       0U,
                                                       web_provider,
                                                       error_out);
}

static void strappy_tools_text_buffer_destroy(
  strappy_tools_text_buffer *buffer)
{
  if (buffer == NULL) {
    return;
  }
  free(buffer->data);
  buffer->data = NULL;
  buffer->length = 0U;
}

static int strappy_tools_text_buffer_append(strappy_tools_text_buffer *buffer,
                                            const char *text)
{
  size_t length;
  char *next;

  if ((buffer == NULL) || (text == NULL)) {
    return 0;
  }
  length = strlen(text);
  if (buffer->length > ((size_t)-1) - length - 1U) {
    return 0;
  }
  next = (char *)realloc(buffer->data, buffer->length + length + 1U);
  if (next == NULL) {
    return 0;
  }
  buffer->data = next;
  memcpy(buffer->data + buffer->length, text, length + 1U);
  buffer->length += length;
  return 1;
}

static int strappy_tools_prompt_append(
  strappy_tools_text_buffer *buffer,
  const char *name,
  const char *description)
{
  return (name != NULL) && (description != NULL) &&
    strappy_tools_text_buffer_append(buffer, "- `") &&
    strappy_tools_text_buffer_append(buffer, name) &&
    strappy_tools_text_buffer_append(buffer, "`: ") &&
    strappy_tools_text_buffer_append(buffer, description) &&
    strappy_tools_text_buffer_append(buffer, "\n");
}

char *strappy_tools_prompt_markdown_filtered(
  const char *resource_dir,
  const char * const *allowed_names,
  size_t allowed_name_count,
  strappy_web_provider web_provider,
  char **error_out)
{
  return strappy_tools_prompt_markdown_filtered_for_provider(
    resource_dir,
    allowed_names,
    allowed_name_count,
    STRAPPY_PROVIDER_KIND_OPENROUTER,
    web_provider,
    error_out);
}

char *strappy_tools_prompt_markdown_filtered_for_provider(
  const char *resource_dir,
  const char * const *allowed_names,
  size_t allowed_name_count,
  strappy_provider_kind provider,
  strappy_web_provider web_provider,
  char **error_out)
{
  cJSON *root;
  cJSON *tools;
  cJSON *server_tools;
  cJSON *tool;
  strappy_tools_text_buffer buffer;
  size_t index;

  buffer.data = NULL;
  buffer.length = 0U;
  if (strappy_provider_for_kind(provider) == NULL) {
    strappy_set_error(error_out, "Prompt tool provider is not registered.");
    return NULL;
  }
  if (strappy_web_provider_name(web_provider) == NULL) {
    strappy_set_error(error_out, "Web provider is invalid.");
    return NULL;
  }
  if ((allowed_names == NULL) && (allowed_name_count > 0U)) {
    strappy_set_error(error_out, "Prompt tool allowlist is incomplete.");
    return NULL;
  }
  for (index = 0U; index < allowed_name_count; index++) {
    if ((allowed_names[index] == NULL) ||
        !strappy_tools_is_registered(allowed_names[index])) {
      strappy_set_formatted_error(
        error_out,
        "Prompt tool is not registered: %s",
        (allowed_names[index] != NULL) ? allowed_names[index] : "(null)");
      return NULL;
    }
  }

  root = strappy_tools_read_json_resource(resource_dir,
                                          STRAPPY_TOOL_GUIDANCE_RESOURCE,
                                          error_out);
  if (root == NULL) {
    return NULL;
  }
  if (!strappy_tools_validate_guidance_root(root, error_out)) {
    cJSON_Delete(root);
    return NULL;
  }
  tools = cJSON_GetObjectItemCaseSensitive(root, "tools");
  server_tools = cJSON_GetObjectItemCaseSensitive(
    root,
    STRAPPY_TOOL_SERVER_TOOLS_KEY);

  for (tool = tools->child; tool != NULL; tool = tool->next) {
    const char *name;

    name = strappy_tools_tool_schema_name(tool);
    if (((allowed_names == NULL) || (allowed_name_count == 0U) ||
         strappy_tools_name_is_allowed(name,
                                       allowed_names,
                                       allowed_name_count)) &&
        !strappy_tools_prompt_append(
          &buffer,
          name,
          strappy_tools_tool_schema_description(tool))) {
      strappy_tools_text_buffer_destroy(&buffer);
      cJSON_Delete(root);
      strappy_set_error(error_out,
                        "Could not allocate prompt tool descriptions.");
      return NULL;
    }
  }
  for (tool = server_tools->child; tool != NULL; tool = tool->next) {
    cJSON *description;

    if (!strappy_tools_server_schema_is_enabled(tool,
                                                allowed_names,
                                                allowed_name_count,
                                                provider,
                                                web_provider)) {
      continue;
    }
    description = cJSON_GetObjectItemCaseSensitive(
      tool,
      STRAPPY_TOOL_DESCRIPTION_KEY);
    if (!strappy_tools_prompt_append(
          &buffer,
          strappy_tools_server_schema_type(tool),
          description->valuestring)) {
      strappy_tools_text_buffer_destroy(&buffer);
      cJSON_Delete(root);
      strappy_set_error(error_out,
                        "Could not allocate prompt tool descriptions.");
      return NULL;
    }
  }
  cJSON_Delete(root);
  if (buffer.data == NULL) {
    buffer.data = strappy_string_duplicate("None.\n");
    if (buffer.data == NULL) {
      strappy_set_error(error_out,
                        "Could not allocate prompt tool descriptions.");
    }
  }
  return buffer.data;
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
