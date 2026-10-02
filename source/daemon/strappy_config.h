#ifndef STRAPPY_CONFIG_H
#define STRAPPY_CONFIG_H
#include "strappy_options.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct strappy_config {
  char *api_endpoint;
  char *api_token;
  char *api_model;
  char *guidance_resource_dir;
  const char * const *tool_allowlist;
  size_t tool_allowlist_count;
  strappy_web_provider web_provider;
} strappy_config;

void strappy_config_init(strappy_config *config);
void strappy_config_destroy(strappy_config *config);
int strappy_config_set_api_model(strappy_config *config,
                                 const char *api_model,
                                 char **error_out);
int strappy_config_set_guidance_resource_dir(strappy_config *config,
                                             const char *resource_dir,
                                             char **error_out);
int strappy_config_load(strappy_config *config,
                        const char *env_path,
                        char **error_out);
int strappy_config_load_with_fallback_credentials(
  strappy_config *config,
  const char *env_path,
  const char *fallback_api_endpoint,
  const char *fallback_api_token,
  char **error_out);

#ifdef __cplusplus
}
#endif

#endif
