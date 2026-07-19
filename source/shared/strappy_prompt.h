#ifndef STRAPPY_PROMPT_H
#define STRAPPY_PROMPT_H

#include "strappy_assistant_sets.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STRAPPY_SYSTEM_PROMPT_RESOURCE_NAME "SystemPrompt.json"
#define STRAPPY_SYSTEM_PROMPT_SCHEMA_VERSION 2

char *strappy_prompt_build(
  const char *resource_dir,
  const strappy_assistant_set_profile *profile,
  int web_search_enabled,
  char **error_out);
char *strappy_prompt_render_resource(const char *resource_dir,
                                     const char *resource_name,
                                     char **error_out);

#ifdef __cplusplus
}
#endif

#endif
