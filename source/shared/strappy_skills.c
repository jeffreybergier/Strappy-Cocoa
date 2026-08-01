#include "strappy_skills.h"

#include "strappy_core.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRAPPY_SKILL_TITLE_MAX_BYTES 128U
#define STRAPPY_SKILL_DESCRIPTION_MAX_BYTES 1024U
#define STRAPPY_SKILL_INSTRUCTIONS_MAX_BYTES (64U * 1024U)
#define STRAPPY_SKILLS_RESOURCE_MAX_BYTES (256U * 1024U)

void strappy_skill_record_init(strappy_skill_record *record)
{
  if (record == NULL) {
    return;
  }
  memset(record, 0, sizeof(*record));
}

void strappy_skill_record_destroy(strappy_skill_record *record)
{
  if (record == NULL) {
    return;
  }
  free(record->identifier);
  free(record->title);
  free(record->description);
  free(record->instructions);
  strappy_skill_record_init(record);
}

void strappy_skill_record_list_init(strappy_skill_record_list *list)
{
  if (list == NULL) {
    return;
  }
  list->records = NULL;
  list->count = 0U;
}

void strappy_skill_record_list_destroy(strappy_skill_record_list *list)
{
  size_t index;

  if (list == NULL) {
    return;
  }
  for (index = 0U; index < list->count; index++) {
    strappy_skill_record_destroy(&list->records[index]);
  }
  free(list->records);
  strappy_skill_record_list_init(list);
}

static int strappy_skills_text_has_non_whitespace(const char *text)
{
  const unsigned char *cursor;

  if (text == NULL) {
    return 0;
  }
  cursor = (const unsigned char *)text;
  while (*cursor != '\0') {
    if ((*cursor != ' ') && (*cursor != '\t') && (*cursor != '\r') &&
        (*cursor != '\n') && (*cursor != '\f') && (*cursor != '\v')) {
      return 1;
    }
    cursor++;
  }
  return 0;
}

static int strappy_skills_identifier_is_valid(const char *identifier)
{
  size_t index;
  size_t length;
  int previous_was_hyphen;

  if (identifier == NULL) {
    return 0;
  }
  length = strlen(identifier);
  if ((length == 0U) || (length > STRAPPY_SKILL_ID_MAX_BYTES)) {
    return 0;
  }
  previous_was_hyphen = 0;
  for (index = 0U; index < length; index++) {
    char value;

    value = identifier[index];
    if (((value >= 'a') && (value <= 'z')) ||
        ((value >= '0') && (value <= '9'))) {
      previous_was_hyphen = 0;
      continue;
    }
    if ((value != '-') || (index == 0U) || ((index + 1U) == length) ||
        previous_was_hyphen) {
      return 0;
    }
    previous_was_hyphen = 1;
  }
  return 1;
}

static int strappy_skills_object_has_only(cJSON *object,
                                           const char * const *names,
                                           size_t name_count,
                                           const char *path,
                                           char **error_out)
{
  cJSON *field;

  if (!cJSON_IsObject(object)) {
    strappy_set_formatted_error(error_out,
                                "Skills resource %s must be an object.",
                                path);
    return 0;
  }
  for (field = object->child; field != NULL; field = field->next) {
    cJSON *other;
    size_t index;
    int recognized;

    if ((field->string == NULL) || (field->string[0] == '\0')) {
      strappy_set_formatted_error(error_out,
                                  "Skills resource %s has an empty field.",
                                  path);
      return 0;
    }
    recognized = 0;
    for (index = 0U; index < name_count; index++) {
      if (strcmp(field->string, names[index]) == 0) {
        recognized = 1;
        break;
      }
    }
    if (!recognized) {
      strappy_set_formatted_error(
        error_out,
        "Skills resource %s has unsupported field: %s",
        path,
        field->string);
      return 0;
    }
    for (other = field->next; other != NULL; other = other->next) {
      if ((other->string != NULL) &&
          (strcmp(field->string, other->string) == 0)) {
        strappy_set_formatted_error(
          error_out,
          "Skills resource %s has duplicate field: %s",
          path,
          field->string);
        return 0;
      }
    }
  }
  return 1;
}

static char *strappy_skills_resource_path(const char *resource_dir,
                                          char **error_out)
{
  size_t directory_length;
  size_t resource_length;
  size_t total_length;
  int needs_slash;
  char *path;

  if ((resource_dir == NULL) || (resource_dir[0] == '\0')) {
    strappy_set_error(error_out,
                      "Skills resource directory is not configured.");
    return NULL;
  }
  directory_length = strlen(resource_dir);
  resource_length = strlen(STRAPPY_SKILLS_RESOURCE_NAME);
  needs_slash = (resource_dir[directory_length - 1U] == '/') ? 0 : 1;
  if (directory_length > ((size_t)-1) - resource_length -
      (size_t)needs_slash - 1U) {
    strappy_set_error(error_out, "Skills resource path is too large.");
    return NULL;
  }
  total_length = directory_length + (size_t)needs_slash + resource_length;
  path = (char *)malloc(total_length + 1U);
  if (path == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate skills resource path.");
    return NULL;
  }
  memcpy(path, resource_dir, directory_length);
  if (needs_slash) {
    path[directory_length++] = '/';
  }
  memcpy(path + directory_length,
         STRAPPY_SKILLS_RESOURCE_NAME,
         resource_length + 1U);
  return path;
}

static cJSON *strappy_skills_read_root(const char *resource_dir,
                                       char **error_out)
{
  static const char *const root_names[] = {
    "schema_version",
    "skills"
  };
  char *path;
  FILE *file;
  long file_length;
  size_t length;
  char *buffer;
  cJSON *root;
  cJSON *schema_version;
  cJSON *skills;

  path = strappy_skills_resource_path(resource_dir, error_out);
  if (path == NULL) {
    return NULL;
  }
  file = fopen(path, "rb");
  if (file == NULL) {
    strappy_set_formatted_error(error_out,
                                "Could not open skills resource: %s",
                                path);
    free(path);
    return NULL;
  }
  if ((fseek(file, 0L, SEEK_END) != 0) ||
      ((file_length = ftell(file)) < 0L) ||
      (fseek(file, 0L, SEEK_SET) != 0)) {
    fclose(file);
    strappy_set_formatted_error(error_out,
                                "Could not read skills resource: %s",
                                path);
    free(path);
    return NULL;
  }
  free(path);
  if ((unsigned long)file_length >
      (unsigned long)STRAPPY_SKILLS_RESOURCE_MAX_BYTES) {
    fclose(file);
    strappy_set_error(error_out, "Skills resource is too large.");
    return NULL;
  }
  length = (size_t)file_length;
  buffer = (char *)malloc(length + 1U);
  if (buffer == NULL) {
    fclose(file);
    strappy_set_error(error_out, "Could not allocate skills resource.");
    return NULL;
  }
  if ((fread(buffer, 1U, length, file) != length) || ferror(file)) {
    free(buffer);
    fclose(file);
    strappy_set_error(error_out, "Could not read skills resource.");
    return NULL;
  }
  buffer[length] = '\0';
  fclose(file);
  root = cJSON_Parse(buffer);
  free(buffer);
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    strappy_set_error(error_out,
                      "Skills resource root must be an object.");
    return NULL;
  }
  if (!strappy_skills_object_has_only(
        root,
        root_names,
        sizeof(root_names) / sizeof(root_names[0]),
        "root",
        error_out)) {
    cJSON_Delete(root);
    return NULL;
  }
  schema_version = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
  skills = cJSON_GetObjectItemCaseSensitive(root, "skills");
  if (!cJSON_IsNumber(schema_version) ||
      (schema_version->valuedouble != (double)STRAPPY_SKILLS_SCHEMA_VERSION)) {
    cJSON_Delete(root);
    strappy_set_formatted_error(
      error_out,
      "Skills resource schema_version must be %d.",
      STRAPPY_SKILLS_SCHEMA_VERSION);
    return NULL;
  }
  if (!cJSON_IsArray(skills)) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Skills resource skills must be an array.");
    return NULL;
  }
  return root;
}

static const char *strappy_skills_required_string(cJSON *object,
                                                   const char *name,
                                                   size_t maximum_bytes,
                                                   int one_line,
                                                   char **error_out)
{
  cJSON *value;
  size_t length;

  value = cJSON_IsObject(object) ?
    cJSON_GetObjectItemCaseSensitive(object, name) : NULL;
  if ((value == NULL) || !cJSON_IsString(value) ||
      (value->valuestring == NULL) ||
      !strappy_skills_text_has_non_whitespace(value->valuestring)) {
    strappy_set_formatted_error(error_out,
                                "Skill requires non-blank %s.",
                                name);
    return NULL;
  }
  length = strlen(value->valuestring);
  if (length > maximum_bytes) {
    strappy_set_formatted_error(error_out,
                                "Skill %s exceeds the %u-byte limit.",
                                name,
                                (unsigned int)maximum_bytes);
    return NULL;
  }
  if (one_line && ((strchr(value->valuestring, '\n') != NULL) ||
                   (strchr(value->valuestring, '\r') != NULL))) {
    strappy_set_formatted_error(error_out,
                                "Skill %s must be one line.",
                                name);
    return NULL;
  }
  return value->valuestring;
}

static int strappy_skills_copy_record(strappy_skill_record *target,
                                      const strappy_skill_record *source,
                                      char **error_out)
{
  if ((target == NULL) || (source == NULL)) {
    strappy_set_error(error_out, "Skill record copy is incomplete.");
    return 0;
  }
  strappy_skill_record_init(target);
  target->identifier = strappy_string_duplicate(source->identifier);
  target->title = strappy_string_duplicate(source->title);
  target->description = strappy_string_duplicate(source->description);
  target->instructions = strappy_string_duplicate(source->instructions);
  if ((target->identifier == NULL) || (target->title == NULL) ||
      (target->description == NULL) || (target->instructions == NULL)) {
    strappy_skill_record_destroy(target);
    strappy_set_error(error_out, "Could not allocate skill record.");
    return 0;
  }
  return 1;
}

static int strappy_skills_append_record(strappy_skill_record_list *list,
                                        const strappy_skill_record *source,
                                        char **error_out)
{
  strappy_skill_record *next;

  if ((list == NULL) || (source == NULL)) {
    strappy_set_error(error_out, "Skill list append is incomplete.");
    return 0;
  }
  if (list->count >= (((size_t)-1) / sizeof(strappy_skill_record))) {
    strappy_set_error(error_out, "Skills resource contains too many skills.");
    return 0;
  }
  next = (strappy_skill_record *)realloc(
    list->records,
    (list->count + 1U) * sizeof(strappy_skill_record));
  if (next == NULL) {
    strappy_set_error(error_out, "Could not allocate skill list.");
    return 0;
  }
  list->records = next;
  if (!strappy_skills_copy_record(&list->records[list->count],
                                  source,
                                  error_out)) {
    return 0;
  }
  list->count++;
  return 1;
}

static const strappy_skill_record *strappy_skills_find_record(
  const strappy_skill_record_list *list,
  const char *identifier)
{
  size_t index;

  if ((list == NULL) || (identifier == NULL)) {
    return NULL;
  }
  for (index = 0U; index < list->count; index++) {
    if (strcmp(list->records[index].identifier, identifier) == 0) {
      return &list->records[index];
    }
  }
  return NULL;
}

static int strappy_skills_load_catalog(const char *resource_dir,
                                       strappy_skill_record_list *list,
                                       char **error_out)
{
  static const char *const skill_names[] = {
    "id",
    "title",
    "description",
    "instructions"
  };
  cJSON *root;
  cJSON *skills;
  cJSON *item;
  int item_index;

  if (list == NULL) {
    strappy_set_error(error_out, "Skill list output is missing.");
    return 0;
  }
  strappy_skill_record_list_init(list);
  root = strappy_skills_read_root(resource_dir, error_out);
  if (root == NULL) {
    return 0;
  }
  skills = cJSON_GetObjectItemCaseSensitive(root, "skills");
  item_index = 0;
  for (item = skills->child; item != NULL; item = item->next) {
    strappy_skill_record record;
    const char *identifier;
    const char *title;
    const char *description;
    const char *instructions;
    char path[64];
    int written;

    strappy_skill_record_init(&record);
    written = snprintf(path, sizeof(path), "skills[%d]", item_index);
    if ((written < 0) || ((size_t)written >= sizeof(path)) ||
        !strappy_skills_object_has_only(
          item,
          skill_names,
          sizeof(skill_names) / sizeof(skill_names[0]),
          path,
          error_out)) {
      cJSON_Delete(root);
      strappy_skill_record_list_destroy(list);
      return 0;
    }
    identifier = strappy_skills_required_string(item,
                                                 "id",
                                                 STRAPPY_SKILL_ID_MAX_BYTES,
                                                 1,
                                                 error_out);
    title = strappy_skills_required_string(item,
                                           "title",
                                           STRAPPY_SKILL_TITLE_MAX_BYTES,
                                           1,
                                           error_out);
    description = strappy_skills_required_string(
      item,
      "description",
      STRAPPY_SKILL_DESCRIPTION_MAX_BYTES,
      1,
      error_out);
    instructions = strappy_skills_required_string(
      item,
      "instructions",
      STRAPPY_SKILL_INSTRUCTIONS_MAX_BYTES,
      0,
      error_out);
    if ((identifier == NULL) || (title == NULL) || (description == NULL) ||
        (instructions == NULL)) {
      cJSON_Delete(root);
      strappy_skill_record_list_destroy(list);
      return 0;
    }
    if (!strappy_skills_identifier_is_valid(identifier)) {
      strappy_set_formatted_error(
        error_out,
        "Skill id must contain lowercase letters, numbers, and single hyphens: %s",
        identifier);
      cJSON_Delete(root);
      strappy_skill_record_list_destroy(list);
      return 0;
    }
    if (strappy_skills_find_record(list, identifier) != NULL) {
      strappy_set_formatted_error(error_out,
                                  "Skill id is duplicated: %s",
                                  identifier);
      cJSON_Delete(root);
      strappy_skill_record_list_destroy(list);
      return 0;
    }
    record.identifier = (char *)identifier;
    record.title = (char *)title;
    record.description = (char *)description;
    record.instructions = (char *)instructions;
    if (!strappy_skills_append_record(list, &record, error_out)) {
      cJSON_Delete(root);
      strappy_skill_record_list_destroy(list);
      return 0;
    }
    item_index++;
  }
  cJSON_Delete(root);
  return 1;
}

static int strappy_skills_identifier_is_allowed(
  const char * const *allowed_identifiers,
  size_t allowed_identifier_count,
  const char *identifier)
{
  size_t index;

  if ((allowed_identifiers == NULL) || (identifier == NULL)) {
    return 0;
  }
  for (index = 0U; index < allowed_identifier_count; index++) {
    if ((allowed_identifiers[index] != NULL) &&
        (strcmp(allowed_identifiers[index], identifier) == 0)) {
      return 1;
    }
  }
  return 0;
}

int strappy_skills_list_allowed(
  const char *resource_dir,
  const char * const *allowed_identifiers,
  size_t allowed_identifier_count,
  strappy_skill_record_list *list,
  char **error_out)
{
  strappy_skill_record_list catalog;
  size_t index;

  if (list == NULL) {
    strappy_set_error(error_out, "Skill list output is missing.");
    return 0;
  }
  strappy_skill_record_list_init(list);
  if ((allowed_identifier_count > 0U) && (allowed_identifiers == NULL)) {
    strappy_set_error(error_out, "Allowed skill list is incomplete.");
    return 0;
  }
  strappy_skill_record_list_init(&catalog);
  if (!strappy_skills_load_catalog(resource_dir, &catalog, error_out)) {
    return 0;
  }
  for (index = 0U; index < allowed_identifier_count; index++) {
    const strappy_skill_record *record;

    if ((allowed_identifiers[index] == NULL) ||
        (allowed_identifiers[index][0] == '\0')) {
      strappy_skill_record_list_destroy(&catalog);
      strappy_skill_record_list_destroy(list);
      strappy_set_error(error_out, "Allowed skill id is empty.");
      return 0;
    }
    record = strappy_skills_find_record(&catalog, allowed_identifiers[index]);
    if (record == NULL) {
      strappy_skill_record_list_destroy(&catalog);
      strappy_skill_record_list_destroy(list);
      strappy_set_formatted_error(error_out,
                                  "Assistant-set skill is not registered: %s",
                                  allowed_identifiers[index]);
      return 0;
    }
    if (!strappy_skills_append_record(list, record, error_out)) {
      strappy_skill_record_list_destroy(&catalog);
      strappy_skill_record_list_destroy(list);
      return 0;
    }
  }
  strappy_skill_record_list_destroy(&catalog);
  return 1;
}

int strappy_skills_read_allowed(
  const char *resource_dir,
  const char * const *allowed_identifiers,
  size_t allowed_identifier_count,
  const char *identifier,
  strappy_skill_record *record,
  char **error_out)
{
  strappy_skill_record_list list;
  const strappy_skill_record *found;
  int ok;

  if (record == NULL) {
    strappy_set_error(error_out, "Skill record output is missing.");
    return 0;
  }
  strappy_skill_record_init(record);
  if (!strappy_skills_identifier_is_valid(identifier)) {
    strappy_set_error(error_out, "skill_read skill_id is invalid.");
    return 0;
  }
  if (!strappy_skills_identifier_is_allowed(allowed_identifiers,
                                             allowed_identifier_count,
                                             identifier)) {
    strappy_set_formatted_error(error_out,
                                "Skill is not allowed by the assistant set: %s",
                                identifier);
    return 0;
  }
  strappy_skill_record_list_init(&list);
  if (!strappy_skills_list_allowed(resource_dir,
                                    allowed_identifiers,
                                    allowed_identifier_count,
                                    &list,
                                    error_out)) {
    return 0;
  }
  found = strappy_skills_find_record(&list, identifier);
  if (found == NULL) {
    strappy_skill_record_list_destroy(&list);
    strappy_set_formatted_error(error_out,
                                "Skill is not registered: %s",
                                identifier);
    return 0;
  }
  ok = strappy_skills_copy_record(record, found, error_out);
  strappy_skill_record_list_destroy(&list);
  return ok;
}

int strappy_skills_validate_allowed(
  const char *resource_dir,
  const char * const *allowed_identifiers,
  size_t allowed_identifier_count,
  char **error_out)
{
  strappy_skill_record_list list;

  strappy_skill_record_list_init(&list);
  if (!strappy_skills_list_allowed(resource_dir,
                                    allowed_identifiers,
                                    allowed_identifier_count,
                                    &list,
                                    error_out)) {
    return 0;
  }
  strappy_skill_record_list_destroy(&list);
  return 1;
}
