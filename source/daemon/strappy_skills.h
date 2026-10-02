#ifndef STRAPPY_SKILLS_H
#define STRAPPY_SKILLS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STRAPPY_SKILLS_RESOURCE_NAME "GuidanceSkills.json"
#define STRAPPY_SKILLS_SCHEMA_VERSION 1
#define STRAPPY_SKILL_ID_MAX_BYTES 64U

typedef struct strappy_skill_record {
  char *identifier;
  char *title;
  char *description;
  char *instructions;
} strappy_skill_record;

typedef struct strappy_skill_record_list {
  strappy_skill_record *records;
  size_t count;
} strappy_skill_record_list;

void strappy_skill_record_init(strappy_skill_record *record);
void strappy_skill_record_destroy(strappy_skill_record *record);
void strappy_skill_record_list_init(strappy_skill_record_list *list);
void strappy_skill_record_list_destroy(strappy_skill_record_list *list);

int strappy_skills_list_allowed(
  const char *resource_dir,
  const char * const *allowed_identifiers,
  size_t allowed_identifier_count,
  strappy_skill_record_list *list,
  char **error_out);
int strappy_skills_read_allowed(
  const char *resource_dir,
  const char * const *allowed_identifiers,
  size_t allowed_identifier_count,
  const char *identifier,
  strappy_skill_record *record,
  char **error_out);
int strappy_skills_validate_allowed(
  const char *resource_dir,
  const char * const *allowed_identifiers,
  size_t allowed_identifier_count,
  char **error_out);

#ifdef __cplusplus
}
#endif

#endif
