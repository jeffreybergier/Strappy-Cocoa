#ifndef STRAPPY_ASSISTANT_SETS_H
#define STRAPPY_ASSISTANT_SETS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STRAPPY_ASSISTANT_SETS_RESOURCE_NAME "AssistantSets.json"
#define STRAPPY_ASSISTANT_SET_WORLD_KNOWLEDGE "world_knowledge"
#define STRAPPY_ASSISTANT_SET_PERSONAL_ASSISTANT "personal_assistant"
#define STRAPPY_ASSISTANT_SET_CODING_ASSISTANT "coding_assistant"
#define STRAPPY_ASSISTANT_SET_DEFAULT STRAPPY_ASSISTANT_SET_PERSONAL_ASSISTANT
#define STRAPPY_ASSISTANT_SET_AVAILABILITY_AVAILABLE "available"
#define STRAPPY_ASSISTANT_SET_AVAILABILITY_COMING_SOON "coming_soon"

typedef struct strappy_assistant_set_record {
  char *identifier;
  char *display_name;
  char *detail;
  char *availability;
} strappy_assistant_set_record;

typedef struct strappy_assistant_set_record_list {
  strappy_assistant_set_record *records;
  size_t count;
} strappy_assistant_set_record_list;

typedef struct strappy_assistant_set_profile {
  char *identifier;
  char *display_name;
  char *detail;
  char *availability;
  char *prompt_resource;
  char **tool_names;
  size_t tool_name_count;
  char **preflight_tool_names;
  size_t preflight_tool_name_count;
  char **quality_check_keys;
  size_t quality_check_key_count;
} strappy_assistant_set_profile;

void strappy_assistant_set_record_list_init(
  strappy_assistant_set_record_list *list);
void strappy_assistant_set_record_list_destroy(
  strappy_assistant_set_record_list *list);
void strappy_assistant_set_profile_init(strappy_assistant_set_profile *profile);
void strappy_assistant_set_profile_destroy(
  strappy_assistant_set_profile *profile);

int strappy_assistant_sets_list(const char *resource_dir,
                                strappy_assistant_set_record_list *list,
                                char **error_out);
int strappy_assistant_sets_load_profile(
  const char *resource_dir,
  const char *identifier,
  strappy_assistant_set_profile *profile,
  char **error_out);
int strappy_assistant_set_profile_is_available(
  const strappy_assistant_set_profile *profile);
int strappy_assistant_set_profile_allows_tool(
  const strappy_assistant_set_profile *profile,
  const char *tool_name);
int strappy_assistant_set_profile_has_quality_check(
  const strappy_assistant_set_profile *profile,
  const char *check_key);

#ifdef __cplusplus
}
#endif

#endif
