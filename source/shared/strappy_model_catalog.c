#include "strappy_model_catalog.h"

#include "strappy_client.h"
#include "strappy_config.h"
#include "strappy_core.h"
#include "strappy_db.h"

#include <stdlib.h>

int strappy_model_catalog_refresh_openrouter_user_models(
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
    ok = strappy_db_save_openrouter_models_json(db_path, json, error_out);
  }

  free(json);
  strappy_config_destroy(&config);
  return ok;
}
