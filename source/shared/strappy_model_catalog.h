#ifndef STRAPPY_MODEL_CATALOG_H
#define STRAPPY_MODEL_CATALOG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Dispatch the provider-owned catalog policy. Remote catalogs use the
 * credential arguments; bundled catalogs use resource_dir; manual providers
 * deliberately have no refresh/import operation. */
int strappy_model_catalog_update_for_provider(
  const char *provider_id,
  const char *env_path,
  const char *fallback_api_endpoint,
  const char *fallback_api_token,
  const char *resource_dir,
  const char *db_path,
  char **error_out);
int strappy_model_catalog_update_for_account(
  const char *provider_account_id,
  const char *env_path,
  const char *fallback_api_endpoint,
  const char *fallback_api_token,
  const char *resource_dir,
  const char *db_path,
  char **error_out);

int strappy_model_catalog_refresh_openrouter_user_models(
  const char *env_path,
  const char *fallback_api_endpoint,
  const char *fallback_api_token,
  const char *db_path,
  char **error_out);
int strappy_model_catalog_import_bundled_models(
  const char *resource_dir,
  const char *db_path,
  char **error_out);

#ifdef __cplusplus
}
#endif

#endif
