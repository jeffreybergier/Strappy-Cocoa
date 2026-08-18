#include "strappy_model_catalog.h"

#include "strappy_client.h"
#include "strappy_config.h"
#include "strappy_core.h"
#include "strappy_db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRAPPY_BUNDLED_MODELS_FILENAME "BundledModels.json"
#define STRAPPY_BUNDLED_MODELS_MAX_BYTES (512U * 1024U)

static int strappy_model_catalog_refresh_remote_account(
  const char *provider_account_id,
  const char *env_path,
  const char *fallback_api_endpoint,
  const char *fallback_api_token,
  const char *db_path,
  char **error_out)
{
  strappy_config config;
  char *json;
  long http_status;
  int ok;

  if ((db_path == NULL) || (db_path[0] == '\0')) {
    strappy_set_error(error_out, "Session database path is not configured.");
    return 0;
  }

  strappy_config_init(&config);
  if (!strappy_config_load_with_fallback_credentials(&config,
                                                     env_path,
                                                     fallback_api_endpoint,
                                                     fallback_api_token,
                                                     error_out)) {
    strappy_config_destroy(&config);
    return 0;
  }

  json = NULL;
  http_status = 0L;
  ok = strappy_client_fetch_openrouter_user_models_json(&config,
                                                        &json,
                                                        &http_status,
                                                        error_out);
  if (ok) {
    ok = strappy_db_save_account_models_json(db_path, provider_account_id,
                                             json, error_out);
  }

  free(json);
  strappy_config_destroy(&config);
  return ok;
}

static int strappy_model_catalog_import_bundled(
  const char *provider_account_id,
  const char *resource_dir,
  const char *db_path,
  char **error_out)
{
  FILE *file;
  char *path;
  char *json;
  long file_length;
  size_t resource_length;
  size_t filename_length;
  size_t read_length;
  int close_result;
  int ok;

  if ((resource_dir == NULL) || (resource_dir[0] == '\0') ||
      (db_path == NULL) || (db_path[0] == '\0')) {
    strappy_set_error(error_out,
                      "Bundled model catalog paths are not configured.");
    return 0;
  }
  resource_length = strlen(resource_dir);
  filename_length = strlen(STRAPPY_BUNDLED_MODELS_FILENAME);
  if (resource_length > (((size_t)-1) - filename_length - 2U)) {
    strappy_set_error(error_out, "Bundled model catalog path is too long.");
    return 0;
  }
  path = (char *)malloc(resource_length + filename_length + 2U);
  if (path == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate bundled model catalog path.");
    return 0;
  }
  memcpy(path, resource_dir, resource_length);
  if ((resource_length > 0U) &&
      (resource_dir[resource_length - 1U] != '/')) {
    path[resource_length++] = '/';
  }
  memcpy(path + resource_length,
         STRAPPY_BUNDLED_MODELS_FILENAME,
         filename_length + 1U);

  file = fopen(path, "rb");
  if (file == NULL) {
    free(path);
    strappy_set_error(error_out, "Bundled model catalog could not be opened.");
    return 0;
  }
  free(path);
  if ((fseek(file, 0L, SEEK_END) != 0) ||
      ((file_length = ftell(file)) < 0L) ||
      ((unsigned long)file_length >
       (unsigned long)STRAPPY_BUNDLED_MODELS_MAX_BYTES) ||
      (fseek(file, 0L, SEEK_SET) != 0)) {
    fclose(file);
    strappy_set_error(error_out,
                      "Bundled model catalog size is invalid.");
    return 0;
  }
  json = (char *)malloc((size_t)file_length + 1U);
  if (json == NULL) {
    fclose(file);
    strappy_set_error(error_out,
                      "Could not allocate bundled model catalog.");
    return 0;
  }
  read_length = fread(json, 1U, (size_t)file_length, file);
  close_result = fclose(file);
  if ((read_length != (size_t)file_length) || (close_result != 0)) {
    free(json);
    strappy_set_error(error_out, "Bundled model catalog could not be read.");
    return 0;
  }
  json[read_length] = '\0';
  if (provider_account_id != NULL) {
    ok = strappy_db_import_bundled_models_json_for_account(
      db_path, provider_account_id, json, error_out);
  } else {
    ok = strappy_db_import_bundled_models_json(db_path, json, error_out);
  }
  free(json);
  return ok;
}

int strappy_model_catalog_update_for_provider(
  const char *provider_id,
  const char *env_path,
  const char *fallback_api_endpoint,
  const char *fallback_api_token,
  const char *resource_dir,
  const char *db_path,
  char **error_out)
{
  char *account_id;
  int ok;

  account_id = NULL;
  if (!strappy_db_get_designated_provider_account(db_path, provider_id,
                                                  &account_id, error_out)) {
    return 0;
  }
  ok = strappy_model_catalog_update_for_account(
    account_id, env_path, fallback_api_endpoint, fallback_api_token,
    resource_dir, db_path, error_out);
  free(account_id);
  return ok;
}

int strappy_model_catalog_update_for_account(
  const char *provider_account_id,
  const char *env_path,
  const char *fallback_api_endpoint,
  const char *fallback_api_token,
  const char *resource_dir,
  const char *db_path,
  char **error_out)
{
  const strappy_provider_definition *definition;
  strappy_provider_account_record account;

  if (!strappy_db_get_provider_account(db_path, provider_account_id,
                                       &account, error_out)) return 0;
  if (strcmp(account.lifecycle_state, STRAPPY_PROVIDER_ACCOUNT_ACTIVE) != 0) {
    strappy_provider_account_record_destroy(&account);
    strappy_set_error(error_out, "Archived accounts cannot update models.");
    return 0;
  }

  definition = strappy_provider_find(account.provider_id);
  strappy_provider_account_record_destroy(&account);
  if (definition == NULL) {
    strappy_set_error(error_out, "Model catalog provider is not registered.");
    return 0;
  }
  if (!strappy_provider_has_catalog_operation(definition)) {
    strappy_set_error(error_out,
                      "The selected provider has no model catalog operation.");
    return 0;
  }
  if (definition->catalog_kind == STRAPPY_PROVIDER_CATALOG_REMOTE_ACCOUNT) {
    return strappy_model_catalog_refresh_remote_account(
      provider_account_id,
      env_path,
      fallback_api_endpoint,
      fallback_api_token,
      db_path,
      error_out);
  }
  if (definition->catalog_kind == STRAPPY_PROVIDER_CATALOG_BUNDLED) {
    return strappy_model_catalog_import_bundled(provider_account_id,
                                                resource_dir,
                                                db_path,
                                                error_out);
  }
  strappy_set_error(error_out,
                    "The selected provider catalog policy is unsupported.");
  return 0;
}

int strappy_model_catalog_refresh_openrouter_user_models(
  const char *env_path,
  const char *fallback_api_endpoint,
  const char *fallback_api_token,
  const char *db_path,
  char **error_out)
{
  return strappy_model_catalog_update_for_provider(
    STRAPPY_PROVIDER_OPENROUTER,
    env_path,
    fallback_api_endpoint,
    fallback_api_token,
    NULL,
    db_path,
    error_out);
}

int strappy_model_catalog_import_bundled_models(
  const char *resource_dir,
  const char *db_path,
  char **error_out)
{
  return strappy_model_catalog_import_bundled(NULL, resource_dir, db_path,
                                              error_out);
}
