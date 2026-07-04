#ifndef STRAPPY_MODEL_CATALOG_H
#define STRAPPY_MODEL_CATALOG_H

#ifdef __cplusplus
extern "C" {
#endif

int strappy_model_catalog_refresh_openrouter_user_models(
  const char *env_path,
  const char *fallback_api_endpoint,
  const char *fallback_api_token,
  const char *db_path,
  char **error_out);

#ifdef __cplusplus
}
#endif

#endif
