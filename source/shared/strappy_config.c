#include "strappy_config.h"

#include "strappy_core.h"
#ifdef __APPLE__
#include "strappy_keychain.h"
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *strappy_trim(char *value)
{
  char *end;

  if (value == NULL) {
    return NULL;
  }

  while ((*value != '\0') && isspace((unsigned char)*value)) {
    value++;
  }

  end = value + strlen(value);
  while ((end > value) && isspace((unsigned char)*(end - 1))) {
    end--;
  }
  *end = '\0';

  return value;
}

static void strappy_strip_quotes(char *value)
{
  size_t length;

  if (value == NULL) {
    return;
  }

  length = strlen(value);
  if (length < 2U) {
    return;
  }

  if (((value[0] == '"') && (value[length - 1U] == '"')) ||
      ((value[0] == '\'') && (value[length - 1U] == '\''))) {
    memmove(value, value + 1, length - 2U);
    value[length - 2U] = '\0';
  }
}

static int strappy_config_set_string(char **target,
                                  const char *value,
                                  char **error_out)
{
  char *copy;

  copy = strappy_string_duplicate(value);
  if (copy == NULL) {
    strappy_set_error(error_out, "Could not allocate Strappy configuration.");
    return 0;
  }

  free(*target);
  *target = copy;
  return 1;
}

static int strappy_config_apply_pair(strappy_config *config,
                                  const char *key,
                                  const char *value,
                                  char **error_out)
{
  if ((key == NULL) || (value == NULL)) {
    return 1;
  }

  if (strcmp(key, "APIENDPOINT") == 0) {
    if (value[0] == '\0') {
      return 1;
    }
    return strappy_config_set_string(&config->api_endpoint, value, error_out);
  }

  if (strcmp(key, "APITOKEN") == 0) {
    if (value[0] == '\0') {
      return 1;
    }
    return strappy_config_set_string(&config->api_token, value, error_out);
  }

  if (strcmp(key, "APIMODEL") == 0) {
    if (value[0] == '\0') {
      return 1;
    }
    return strappy_config_set_string(&config->api_model, value, error_out);
  }

  return 1;
}

static int strappy_config_load_file(strappy_config *config,
                                 const char *path,
                                 int path_is_required,
                                 int *api_endpoint_configured,
                                 int *api_token_configured,
                                 char **error_out)
{
  FILE *file;
  char line[4096];

  file = fopen(path, "r");
  if (file == NULL) {
    if (path_is_required) {
      strappy_set_formatted_error(error_out,
                               "Could not open Strappy config file: %s",
                               path);
      return 0;
    }
    return 1;
  }

  while (fgets(line, sizeof(line), file) != NULL) {
    char *key;
    char *value;
    char *equals;

    key = strappy_trim(line);
    if ((key == NULL) || (key[0] == '\0') || (key[0] == '#')) {
      continue;
    }

    equals = strchr(key, '=');
    if (equals == NULL) {
      continue;
    }

    *equals = '\0';
    value = strappy_trim(equals + 1);
    key = strappy_trim(key);
    strappy_strip_quotes(value);

    if (!strappy_config_apply_pair(config, key, value, error_out)) {
      fclose(file);
      return 0;
    }
    if (value[0] != '\0') {
      if ((api_endpoint_configured != NULL) &&
          (strcmp(key, "APIENDPOINT") == 0)) {
        *api_endpoint_configured = 1;
      } else if ((api_token_configured != NULL) &&
                 (strcmp(key, "APITOKEN") == 0)) {
        *api_token_configured = 1;
      }
    }
  }

  fclose(file);
  return 1;
}

static int strappy_config_apply_environment(strappy_config *config,
                                         int *api_endpoint_configured,
                                         int *api_token_configured,
                                         char **error_out)
{
  const char *value;

  value = getenv("APIENDPOINT");
  if ((value != NULL) &&
      !strappy_config_apply_pair(config, "APIENDPOINT", value, error_out)) {
    return 0;
  }
  if ((value != NULL) && (value[0] != '\0') &&
      (api_endpoint_configured != NULL)) {
    *api_endpoint_configured = 1;
  }

  value = getenv("APITOKEN");
  if ((value != NULL) &&
      !strappy_config_apply_pair(config, "APITOKEN", value, error_out)) {
    return 0;
  }
  if ((value != NULL) && (value[0] != '\0') &&
      (api_token_configured != NULL)) {
    *api_token_configured = 1;
  }

  value = getenv("APIMODEL");
  if ((value != NULL) &&
      !strappy_config_apply_pair(config, "APIMODEL", value, error_out)) {
    return 0;
  }

  return 1;
}

static int strappy_config_apply_keychain(strappy_config *config,
                                         int needs_api_endpoint,
                                         int needs_api_token,
                                         char **error_out)
{
#ifdef __APPLE__
  char *api_endpoint;
  char *api_token;
  int ok;

  if ((config == NULL) || (!needs_api_endpoint && !needs_api_token)) {
    return 1;
  }

  api_endpoint = needs_api_endpoint ? strappy_keychain_copy_api_endpoint() : NULL;
  api_token = needs_api_token ? strappy_keychain_copy_api_token() : NULL;

  ok = 1;
  if ((api_endpoint != NULL) &&
      !strappy_config_apply_pair(config, "APIENDPOINT", api_endpoint, error_out)) {
    ok = 0;
  }
  if (ok && (api_token != NULL) &&
      !strappy_config_apply_pair(config, "APITOKEN", api_token, error_out)) {
    ok = 0;
  }

  free(api_endpoint);
  free(api_token);
  return ok;
#else
  (void)config;
  (void)needs_api_endpoint;
  (void)needs_api_token;
  (void)error_out;
  return 1;
#endif
}

void strappy_config_init(strappy_config *config)
{
  if (config == NULL) {
    return;
  }

  config->api_endpoint = NULL;
  config->api_token = NULL;
  config->api_model = NULL;
  config->guidance_resource_dir = NULL;
  config->tool_allowlist = NULL;
  config->tool_allowlist_count = 0U;
}

void strappy_config_destroy(strappy_config *config)
{
  if (config == NULL) {
    return;
  }

  free(config->api_endpoint);
  free(config->api_token);
  free(config->api_model);
  free(config->guidance_resource_dir);

  strappy_config_init(config);
}

int strappy_config_set_guidance_resource_dir(strappy_config *config,
                                             const char *resource_dir,
                                             char **error_out)
{
  if ((config == NULL) || (resource_dir == NULL) || (resource_dir[0] == '\0')) {
    strappy_set_error(error_out, "Guidance resource directory is not configured.");
    return 0;
  }

  return strappy_config_set_string(&config->guidance_resource_dir,
                                   resource_dir,
                                   error_out);
}

int strappy_config_load(strappy_config *config,
                      const char *env_path,
                      char **error_out)
{
  const char *path;
  int path_is_required;
  int api_endpoint_configured;
  int api_token_configured;

  if (config == NULL) {
    strappy_set_error(error_out, "strappy_config_load received no config.");
    return 0;
  }

  strappy_config_init(config);
  api_endpoint_configured = 0;
  api_token_configured = 0;

  if (!strappy_config_set_string(&config->api_endpoint,
                              STRAPPY_CONFIG_DEFAULT_API_ENDPOINT,
                              error_out)) {
    strappy_config_destroy(config);
    return 0;
  }

  if (!strappy_config_set_string(&config->api_model,
                              STRAPPY_CONFIG_DEFAULT_API_MODEL,
                              error_out)) {
    strappy_config_destroy(config);
    return 0;
  }

  path = env_path;
  path_is_required = 1;
  if (path == NULL) {
    path = STRAPPY_CONFIG_DEFAULT_ENV_PATH;
    path_is_required = 0;
  }

  if (!strappy_config_load_file(config,
                                path,
                                path_is_required,
                                &api_endpoint_configured,
                                &api_token_configured,
                                error_out)) {
    strappy_config_destroy(config);
    return 0;
  }

  if (!strappy_config_apply_environment(config,
                                        &api_endpoint_configured,
                                        &api_token_configured,
                                        error_out)) {
    strappy_config_destroy(config);
    return 0;
  }

  if (!strappy_config_apply_keychain(config,
                                     !api_endpoint_configured,
                                     !api_token_configured,
                                     error_out)) {
    strappy_config_destroy(config);
    return 0;
  }

  return 1;
}
