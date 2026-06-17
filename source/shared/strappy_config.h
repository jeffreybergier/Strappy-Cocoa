#ifndef STRAPPY_CONFIG_H
#define STRAPPY_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#define STRAPPY_CONFIG_DEFAULT_ENV_PATH ".env"
#define STRAPPY_CONFIG_DEFAULT_API_ENDPOINT \
  "https://openrouter.ai/api/v1/chat/completions"
#define STRAPPY_CONFIG_DEFAULT_API_MODEL "~openai/gpt-latest"

typedef struct strappy_config {
  char *api_endpoint;
  char *api_token;
  char *api_model;
} strappy_config;

void strappy_config_init(strappy_config *config);
void strappy_config_destroy(strappy_config *config);
int strappy_config_load(strappy_config *config,
                        const char *env_path,
                        char **error_out);

#ifdef __cplusplus
}
#endif

#endif
