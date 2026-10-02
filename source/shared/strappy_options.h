#ifndef STRAPPY_OPTIONS_H
#define STRAPPY_OPTIONS_H

#include "strappy_provider.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STRAPPY_CONFIG_DEFAULT_ENV_PATH ".env"
#define STRAPPY_CONFIG_DEFAULT_API_ENDPOINT \
  STRAPPY_PROVIDER_OPENROUTER_RESPONSES_ENDPOINT
#define STRAPPY_CONFIG_DEFAULT_API_MODEL \
  "~deepseek/deepseek-v4-flash-latest"
#define STRAPPY_CONFIG_DEFAULT_MODEL_IDENTIFIER \
  STRAPPY_PROVIDER_OPENROUTER ":" STRAPPY_CONFIG_DEFAULT_API_MODEL

typedef enum strappy_web_provider {
  STRAPPY_WEB_PROVIDER_NONE = 0,
  STRAPPY_WEB_PROVIDER_AUTO = 1,
  STRAPPY_WEB_PROVIDER_NATIVE = 2,
  STRAPPY_WEB_PROVIDER_EXA = 3,
  STRAPPY_WEB_PROVIDER_PARALLEL = 4
} strappy_web_provider;

const char *strappy_web_provider_name(strappy_web_provider provider);
int strappy_web_provider_parse(const char *name,
                               strappy_web_provider *provider_out);
int strappy_web_provider_is_enabled(strappy_web_provider provider);

#ifdef __cplusplus
}
#endif
#endif
