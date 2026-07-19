#ifndef STRAPPY_PROMPT_H
#define STRAPPY_PROMPT_H

#ifdef __cplusplus
extern "C" {
#endif

#define STRAPPY_PROMPT_TEMPLATE_RESOURCE_NAME "PromptSystemDatabase"
#define STRAPPY_PROMPT_TEMPLATE_RESOURCE_TYPE "txt"

char *strappy_prompt_render_system_prompt(const char *template_path,
                                          char **error_out);
char *strappy_prompt_render_resource(const char *resource_dir,
                                     const char *resource_name,
                                     char **error_out);
char *strappy_prompt_resource_directory_from_template_path(
  const char *template_path,
  char **error_out);

#ifdef __cplusplus
}
#endif

#endif
