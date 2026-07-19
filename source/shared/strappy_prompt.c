#include "strappy_prompt.h"

#include "strappy_core.h"
#include "strappy_quality_policy.h"
#include "strappy_tools.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct strappy_prompt_buffer {
  char *data;
  size_t length;
} strappy_prompt_buffer;

static void strappy_prompt_buffer_destroy(strappy_prompt_buffer *buffer)
{
  if (buffer == NULL) {
    return;
  }
  free(buffer->data);
  buffer->data = NULL;
  buffer->length = 0U;
}

static int strappy_prompt_buffer_append(strappy_prompt_buffer *buffer,
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

static int strappy_prompt_buffer_append_uppercase(
  strappy_prompt_buffer *buffer,
  const char *text)
{
  size_t index;
  size_t length;
  char *uppercase;
  int result;

  if ((buffer == NULL) || (text == NULL)) {
    return 0;
  }
  length = strlen(text);
  uppercase = (char *)malloc(length + 1U);
  if (uppercase == NULL) {
    return 0;
  }
  for (index = 0U; index < length; index++) {
    char value;

    value = text[index];
    uppercase[index] = ((value >= 'a') && (value <= 'z')) ?
      (char)(value - ('a' - 'A')) : value;
  }
  uppercase[length] = '\0';
  result = strappy_prompt_buffer_append(buffer, uppercase);
  free(uppercase);
  return result;
}

static char *strappy_prompt_read_file(const char *path, char **error_out)
{
  FILE *file;
  long file_length;
  size_t length;
  char *buffer;
  size_t bytes_read;

  if ((path == NULL) || (path[0] == '\0')) {
    strappy_set_error(error_out, "Prompt resource path is not configured.");
    return NULL;
  }

  file = fopen(path, "rb");
  if (file == NULL) {
    strappy_set_formatted_error(error_out,
                                "Could not open prompt resource: %s",
                                path);
    return NULL;
  }

  if (fseek(file, 0L, SEEK_END) != 0) {
    fclose(file);
    strappy_set_formatted_error(error_out,
                                "Could not read prompt resource: %s",
                                path);
    return NULL;
  }

  file_length = ftell(file);
  if (file_length < 0L) {
    fclose(file);
    strappy_set_formatted_error(error_out,
                                "Could not measure prompt resource: %s",
                                path);
    return NULL;
  }

  if ((unsigned long)file_length >= (unsigned long)((size_t)-1)) {
    fclose(file);
    strappy_set_error(error_out, "Prompt resource is too large.");
    return NULL;
  }

  length = (size_t)file_length;
  if (fseek(file, 0L, SEEK_SET) != 0) {
    fclose(file);
    strappy_set_formatted_error(error_out,
                                "Could not rewind prompt resource: %s",
                                path);
    return NULL;
  }

  buffer = (char *)malloc(length + 1U);
  if (buffer == NULL) {
    fclose(file);
    strappy_set_error(error_out, "Could not allocate prompt resource.");
    return NULL;
  }

  bytes_read = fread(buffer, 1U, length, file);
  if ((bytes_read != length) || ferror(file)) {
    free(buffer);
    fclose(file);
    strappy_set_formatted_error(error_out,
                                "Could not read prompt resource: %s",
                                path);
    return NULL;
  }

  buffer[length] = '\0';
  fclose(file);
  return buffer;
}

char *strappy_prompt_render_resource(const char *resource_dir,
                                     const char *resource_name,
                                     char **error_out)
{
  size_t directory_length;
  size_t resource_length;
  int needs_slash;
  char *path;
  char *result;

  if ((resource_dir == NULL) || (resource_dir[0] == '\0') ||
      (resource_name == NULL) || (resource_name[0] == '\0') ||
      (strchr(resource_name, '/') != NULL) ||
      (strchr(resource_name, '\\') != NULL) ||
      (strstr(resource_name, "..") != NULL)) {
    strappy_set_error(error_out, "System prompt resource is not configured.");
    return NULL;
  }
  directory_length = strlen(resource_dir);
  resource_length = strlen(resource_name);
  needs_slash = (resource_dir[directory_length - 1U] == '/') ? 0 : 1;
  if (directory_length > ((size_t)-1) - resource_length -
      (size_t)needs_slash - 1U) {
    strappy_set_error(error_out, "System prompt resource path is too large.");
    return NULL;
  }
  path = (char *)malloc(directory_length + (size_t)needs_slash +
                        resource_length + 1U);
  if (path == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate system prompt resource path.");
    return NULL;
  }
  memcpy(path, resource_dir, directory_length);
  if (needs_slash) {
    path[directory_length++] = '/';
  }
  memcpy(path + directory_length, resource_name, resource_length + 1U);
  result = strappy_prompt_read_file(path, error_out);
  free(path);
  return result;
}

static cJSON *strappy_prompt_json_required_object(cJSON *parent,
                                                   const char *key,
                                                   const char *path,
                                                   char **error_out)
{
  cJSON *value;

  value = cJSON_IsObject(parent) ?
    cJSON_GetObjectItemCaseSensitive(parent, key) : NULL;
  if (!cJSON_IsObject(value)) {
    strappy_set_formatted_error(error_out,
                                "System prompt resource requires object %s.%s.",
                                path,
                                key);
    return NULL;
  }
  return value;
}

static const char *strappy_prompt_json_required_string(cJSON *parent,
                                                        const char *key,
                                                        const char *path,
                                                        char **error_out)
{
  cJSON *value;

  value = cJSON_IsObject(parent) ?
    cJSON_GetObjectItemCaseSensitive(parent, key) : NULL;
  if ((value == NULL) || !cJSON_IsString(value) ||
      (value->valuestring == NULL) ||
      (value->valuestring[0] == '\0')) {
    strappy_set_formatted_error(
      error_out,
      "System prompt resource requires non-empty string %s.%s.",
      path,
      key);
    return NULL;
  }
  return value->valuestring;
}

static const char *strappy_prompt_json_required_text(cJSON *parent,
                                                      const char *key,
                                                      const char *path,
                                                      char **error_out)
{
  cJSON *value;

  value = cJSON_IsObject(parent) ?
    cJSON_GetObjectItemCaseSensitive(parent, key) : NULL;
  if (!cJSON_IsString(value) || (value->valuestring == NULL)) {
    strappy_set_formatted_error(
      error_out,
      "System prompt resource requires string %s.%s.",
      path,
      key);
    return NULL;
  }
  return value->valuestring;
}

static int strappy_prompt_json_validate_unique_keys(cJSON *object,
                                                     const char *path,
                                                     char **error_out)
{
  cJSON *item;

  if (!cJSON_IsObject(object)) {
    return 0;
  }
  for (item = object->child; item != NULL; item = item->next) {
    cJSON *other;

    if ((item->string == NULL) || (item->string[0] == '\0')) {
      strappy_set_formatted_error(
        error_out,
        "System prompt resource contains an empty key in %s.",
        path);
      return 0;
    }
    for (other = item->next; other != NULL; other = other->next) {
      if ((other->string != NULL) &&
          (strcmp(item->string, other->string) == 0)) {
        strappy_set_formatted_error(
          error_out,
          "System prompt resource contains duplicate key %s.%s.",
          path,
          item->string);
        return 0;
      }
    }
  }
  return 1;
}

static int strappy_prompt_json_validate_heading(cJSON *section,
                                                 const char *key,
                                                 const char *path,
                                                 char **error_out)
{
  const char *heading;

  heading = strappy_prompt_json_required_string(section,
                                                 key,
                                                 path,
                                                 error_out);
  if (heading == NULL) {
    return 0;
  }
  if ((strchr(heading, '\n') != NULL) || (strchr(heading, '\r') != NULL)) {
    strappy_set_formatted_error(error_out,
                                "System prompt heading %s.%s must be one line.",
                                path,
                                key);
    return 0;
  }
  return 1;
}

static int strappy_prompt_json_validate_section(cJSON *section,
                                                 const char *path,
                                                 char **error_out)
{
  return strappy_prompt_json_validate_unique_keys(section,
                                                  path,
                                                  error_out) &&
    strappy_prompt_json_validate_heading(section,
                                         "heading",
                                         path,
                                         error_out) &&
    (strappy_prompt_json_required_text(section,
                                       "instruction",
                                       path,
                                       error_out) != NULL) &&
    (strappy_prompt_json_required_text(section,
                                       "footer",
                                       path,
                                       error_out) != NULL);
}

static int strappy_prompt_buffer_append_paragraph_break(
  strappy_prompt_buffer *buffer)
{
  if ((buffer == NULL) || (buffer->data == NULL) || (buffer->length == 0U)) {
    return 0;
  }
  if ((buffer->length >= 2U) &&
      (buffer->data[buffer->length - 2U] == '\n') &&
      (buffer->data[buffer->length - 1U] == '\n')) {
    return 1;
  }
  return strappy_prompt_buffer_append(
    buffer,
    (buffer->data[buffer->length - 1U] == '\n') ? "\n" : "\n\n");
}

static int strappy_prompt_buffer_append_section_open(
  strappy_prompt_buffer *buffer,
  cJSON *section,
  const char *path,
  char **error_out)
{
  const char *heading;
  const char *instruction;
  int ok;

  heading = strappy_prompt_json_required_string(section,
                                                 "heading",
                                                 path,
                                                 error_out);
  instruction = strappy_prompt_json_required_text(section,
                                                   "instruction",
                                                   path,
                                                   error_out);
  if ((heading == NULL) || (instruction == NULL)) {
    return 0;
  }
  ok = strappy_prompt_buffer_append(buffer, "# ") &&
    strappy_prompt_buffer_append(buffer, heading) &&
    strappy_prompt_buffer_append_paragraph_break(buffer);
  if (ok && (instruction[0] != '\0')) {
    ok = strappy_prompt_buffer_append(buffer, instruction) &&
      strappy_prompt_buffer_append_paragraph_break(buffer);
  }
  return ok;
}

static int strappy_prompt_buffer_append_section_footer(
  strappy_prompt_buffer *buffer,
  cJSON *section,
  const char *path,
  char **error_out)
{
  const char *footer;
  int ok;

  footer = strappy_prompt_json_required_text(section,
                                              "footer",
                                              path,
                                              error_out);
  if (footer == NULL) {
    return 0;
  }
  ok = strappy_prompt_buffer_append_paragraph_break(buffer);
  if (ok && (footer[0] != '\0')) {
    ok = strappy_prompt_buffer_append(buffer, footer) &&
      strappy_prompt_buffer_append_paragraph_break(buffer);
  }
  return ok;
}

static int strappy_prompt_json_validate_guidance(cJSON *guidance,
                                                  char **error_out)
{
  cJSON *item;
  size_t index;

  if (!strappy_prompt_json_validate_unique_keys(guidance,
                                                "audit_guidance",
                                                error_out)) {
    return 0;
  }
  for (item = guidance->child; item != NULL; item = item->next) {
    const char *requirement;

    if (!cJSON_IsObject(item)) {
      strappy_set_formatted_error(
        error_out,
        "System prompt audit guidance must be an object: %s.",
        item->string);
      return 0;
    }
    if (strappy_quality_policy_find(item->string) == NULL) {
      strappy_set_formatted_error(
        error_out,
        "System prompt resource has unknown audit guidance: %s.",
        item->string);
      return 0;
    }
    if (!strappy_prompt_json_validate_unique_keys(item,
                                                  item->string,
                                                  error_out)) {
      return 0;
    }
    requirement = strappy_prompt_json_required_string(item,
                                                       "requirement",
                                                       item->string,
                                                       error_out);
    if ((requirement == NULL) ||
        ((strcmp(requirement, "required") != 0) &&
         (strcmp(requirement, "conditional") != 0))) {
      if (requirement != NULL) {
        strappy_set_formatted_error(
          error_out,
          "System prompt audit requirement is invalid: %s.",
          item->string);
      }
      return 0;
    }
    if (strappy_prompt_json_required_string(item,
                                            "instruction",
                                            item->string,
                                            error_out) == NULL) {
      return 0;
    }
  }
  for (index = 0U; index < strappy_quality_policy_count(); index++) {
    const strappy_quality_check_definition *definition;

    definition = strappy_quality_policy_at(index);
    if ((definition == NULL) ||
        !cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(
          guidance,
          definition->check_key))) {
      strappy_set_formatted_error(
        error_out,
        "System prompt resource is missing audit guidance: %s.",
        (definition != NULL) ? definition->check_key : "(unknown)");
      return 0;
    }
  }
  return 1;
}

static int strappy_prompt_json_validate_root(cJSON *root, char **error_out)
{
  cJSON *schema_version;
  cJSON *sections;
  cJSON *tools;
  cJSON *audit;
  cJSON *goal;
  cJSON *invariant_section;
  cJSON *guidance;

  if (!cJSON_IsObject(root) ||
      !strappy_prompt_json_validate_unique_keys(root, "root", error_out)) {
    if ((error_out == NULL) || (*error_out == NULL)) {
      strappy_set_error(error_out,
                        "System prompt resource root must be an object.");
    }
    return 0;
  }
  schema_version = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
  if ((schema_version == NULL) || !cJSON_IsNumber(schema_version) ||
      (schema_version->valuedouble !=
       (double)STRAPPY_SYSTEM_PROMPT_SCHEMA_VERSION)) {
    strappy_set_formatted_error(
      error_out,
      "System prompt resource schema_version must be %d.",
      STRAPPY_SYSTEM_PROMPT_SCHEMA_VERSION);
    return 0;
  }
  sections = strappy_prompt_json_required_object(root,
                                                 "sections",
                                                 "root",
                                                 error_out);
  tools = (sections != NULL) ?
    strappy_prompt_json_required_object(sections,
                                        "tools",
                                        "sections",
                                        error_out) : NULL;
  audit = (sections != NULL) ?
    strappy_prompt_json_required_object(sections,
                                        "audit",
                                        "sections",
                                        error_out) : NULL;
  goal = (sections != NULL) ?
    strappy_prompt_json_required_object(sections,
                                        "goal",
                                        "sections",
                                        error_out) : NULL;
  invariant_section = (sections != NULL) ?
    strappy_prompt_json_required_object(sections,
                                        "invariant",
                                        "sections",
                                        error_out) : NULL;
  guidance = strappy_prompt_json_required_object(root,
                                                 "audit_guidance",
                                                 "root",
                                                 error_out);
  if ((sections == NULL) || (tools == NULL) || (audit == NULL) ||
      (goal == NULL) || (invariant_section == NULL) ||
      (guidance == NULL)) {
    return 0;
  }
  if (!strappy_prompt_json_validate_unique_keys(sections,
                                                "sections",
                                                error_out) ||
      !strappy_prompt_json_validate_section(tools,
                                            "sections.tools",
                                            error_out) ||
      !strappy_prompt_json_validate_section(audit,
                                            "sections.audit",
                                            error_out) ||
      !strappy_prompt_json_validate_section(goal,
                                            "sections.goal",
                                            error_out) ||
      !strappy_prompt_json_validate_section(invariant_section,
                                            "sections.invariant",
                                            error_out) ||
      !strappy_prompt_json_validate_guidance(guidance, error_out)) {
    return 0;
  }
  return 1;
}

static cJSON *strappy_prompt_json_read_root(const char *resource_dir,
                                            char **error_out)
{
  char *text;
  cJSON *root;
  const char *parse_error;

  text = strappy_prompt_render_resource(resource_dir,
                                        STRAPPY_SYSTEM_PROMPT_RESOURCE_NAME,
                                        error_out);
  if (text == NULL) {
    return NULL;
  }
  root = cJSON_Parse(text);
  if (root == NULL) {
    parse_error = cJSON_GetErrorPtr();
    if ((parse_error != NULL) && (parse_error[0] != '\0')) {
      strappy_set_formatted_error(
        error_out,
        "Could not parse system prompt resource near: %.48s",
        parse_error);
    } else {
      strappy_set_error(error_out,
                        "Could not parse system prompt resource.");
    }
    free(text);
    return NULL;
  }
  free(text);
  if (!strappy_prompt_json_validate_root(root, error_out)) {
    cJSON_Delete(root);
    return NULL;
  }
  return root;
}

char *strappy_prompt_build(
  const char *resource_dir,
  const strappy_assistant_set_profile *profile,
  int web_search_enabled,
  char **error_out)
{
  strappy_prompt_buffer buffer;
  char *tools_markdown;
  cJSON *root;
  cJSON *sections;
  cJSON *tools_section;
  cJSON *audit_section;
  cJSON *goal_section;
  cJSON *invariant_section;
  cJSON *guidance;
  size_t index;
  int ok;

  buffer.data = NULL;
  buffer.length = 0U;
  tools_markdown = NULL;
  root = NULL;
  if ((resource_dir == NULL) || (resource_dir[0] == '\0') ||
      (profile == NULL) || (profile->goal == NULL) ||
      (profile->goal[0] == '\0') || (profile->tool_names == NULL) ||
      (profile->tool_name_count == 0U) ||
      (profile->quality_check_keys == NULL) ||
      (profile->quality_check_key_count == 0U)) {
    strappy_set_error(error_out,
                      "System prompt configuration is incomplete.");
    return NULL;
  }

  root = strappy_prompt_json_read_root(resource_dir, error_out);
  if (root == NULL) {
    return NULL;
  }
  tools_markdown = strappy_tools_prompt_markdown_filtered(
    resource_dir,
    (const char * const *)profile->tool_names,
    profile->tool_name_count,
    web_search_enabled,
    error_out);
  if (tools_markdown == NULL) {
    cJSON_Delete(root);
    return NULL;
  }
  sections = cJSON_GetObjectItemCaseSensitive(root, "sections");
  tools_section = cJSON_GetObjectItemCaseSensitive(sections, "tools");
  audit_section = cJSON_GetObjectItemCaseSensitive(sections, "audit");
  goal_section = cJSON_GetObjectItemCaseSensitive(sections, "goal");
  invariant_section = cJSON_GetObjectItemCaseSensitive(sections, "invariant");
  guidance = cJSON_GetObjectItemCaseSensitive(root, "audit_guidance");

  ok = strappy_prompt_buffer_append_section_open(&buffer,
                                                  tools_section,
                                                  "sections.tools",
                                                  error_out) &&
    strappy_prompt_buffer_append(&buffer, tools_markdown) &&
    strappy_prompt_buffer_append_section_footer(&buffer,
                                                 tools_section,
                                                 "sections.tools",
                                                 error_out) &&
    strappy_prompt_buffer_append_section_open(&buffer,
                                               audit_section,
                                               "sections.audit",
                                               error_out);

  for (index = 0U; ok && (index < profile->quality_check_key_count); index++) {
    const strappy_quality_check_definition *definition;
    cJSON *check_guidance;
    const char *requirement;
    const char *instruction;

    definition = strappy_quality_policy_find(
      profile->quality_check_keys[index]);
    if (definition == NULL) {
      strappy_set_formatted_error(
        error_out,
        "Answer-quality check is not registered: %s",
        profile->quality_check_keys[index]);
      ok = 0;
      break;
    }
    if ((definition->tool_name != NULL) &&
        !strappy_assistant_set_profile_allows_tool(profile,
                                                   definition->tool_name)) {
      strappy_set_formatted_error(
        error_out,
        "Answer-quality tool is not allowed by the assistant set: %s",
        definition->tool_name);
      ok = 0;
      break;
    }
    if (!web_search_enabled &&
        (definition->evaluation_kind ==
         STRAPPY_QUALITY_CHECK_WEB_REFERENCE)) {
      continue;
    }
    check_guidance = cJSON_GetObjectItemCaseSensitive(guidance,
                                                      definition->check_key);
    requirement = strappy_prompt_json_required_string(check_guidance,
                                                       "requirement",
                                                       definition->check_key,
                                                       error_out);
    instruction = strappy_prompt_json_required_string(check_guidance,
                                                       "instruction",
                                                       definition->check_key,
                                                       error_out);
    if ((requirement == NULL) || (instruction == NULL)) {
      ok = 0;
      break;
    }
    ok = strappy_prompt_buffer_append(&buffer, "- `") &&
      strappy_prompt_buffer_append(&buffer, definition->check_key) &&
      strappy_prompt_buffer_append(&buffer, "`: ") &&
      strappy_prompt_buffer_append_uppercase(&buffer, requirement) &&
      strappy_prompt_buffer_append(&buffer, ": ") &&
      strappy_prompt_buffer_append(&buffer, instruction) &&
      strappy_prompt_buffer_append(&buffer, "\n");
  }

  if (ok) {
    ok = strappy_prompt_buffer_append_section_footer(&buffer,
                                                     audit_section,
                                                     "sections.audit",
                                                     error_out) &&
      strappy_prompt_buffer_append_section_open(&buffer,
                                                 goal_section,
                                                 "sections.goal",
                                                 error_out) &&
      strappy_prompt_buffer_append(&buffer, profile->goal) &&
      strappy_prompt_buffer_append_section_footer(&buffer,
                                                   goal_section,
                                                   "sections.goal",
                                                   error_out) &&
      strappy_prompt_buffer_append_section_open(&buffer,
                                                 invariant_section,
                                                 "sections.invariant",
                                                 error_out) &&
      strappy_prompt_buffer_append_section_footer(&buffer,
                                                   invariant_section,
                                                   "sections.invariant",
                                                   error_out);
  }

  cJSON_Delete(root);
  free(tools_markdown);
  if (!ok) {
    strappy_prompt_buffer_destroy(&buffer);
    if ((error_out == NULL) || (*error_out == NULL)) {
      strappy_set_error(error_out, "Could not allocate system prompt.");
    }
    return NULL;
  }
  return buffer.data;
}
