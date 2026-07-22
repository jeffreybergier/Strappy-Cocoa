#ifndef STRAPPY_CONFIG_H
#define STRAPPY_CONFIG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STRAPPY_CONFIG_DEFAULT_ENV_PATH ".env"
#define STRAPPY_CONFIG_DEFAULT_API_ENDPOINT \
  "https://openrouter.ai/api/v1/responses"
#define STRAPPY_CONFIG_DEFAULT_API_MODEL "z-ai/glm-5.2"

typedef enum strappy_web_provider {
  STRAPPY_WEB_PROVIDER_NONE = 0,
  STRAPPY_WEB_PROVIDER_AUTO = 1,
  STRAPPY_WEB_PROVIDER_NATIVE = 2,
  STRAPPY_WEB_PROVIDER_EXA = 3,
  STRAPPY_WEB_PROVIDER_PARALLEL = 4
} strappy_web_provider;

typedef struct strappy_config {
  char *api_endpoint;
  char *api_token;
  char *api_model;
  char *guidance_resource_dir;
  const char * const *tool_allowlist;
  size_t tool_allowlist_count;
  strappy_web_provider web_provider;
} strappy_config;

const char *strappy_web_provider_name(strappy_web_provider provider);
int strappy_web_provider_parse(const char *name,
                               strappy_web_provider *provider_out);
int strappy_web_provider_is_enabled(strappy_web_provider provider);

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
