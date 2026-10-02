#ifndef STRAPPY_OPENAI_OAUTH_H
#define STRAPPY_OPENAI_OAUTH_H

#ifdef __cplusplus
extern "C" {
#endif

#define STRAPPY_OPENAI_OAUTH_CREDENTIAL_FORMAT_VERSION 1

typedef int (*strappy_openai_oauth_cancel_callback)(void *user_data);

typedef struct strappy_openai_oauth_configuration {
  const char *client_id;
  const char *device_start_url;
  const char *device_poll_url;
  const char *token_url;
  const char *verification_url;
  const char *device_redirect_uri;
} strappy_openai_oauth_configuration;

typedef struct strappy_openai_oauth_device {
  char *device_auth_id;
  char *user_code;
  long interval_seconds;
} strappy_openai_oauth_device;

typedef struct strappy_openai_oauth_credentials {
  char *access_token;
  char *refresh_token;
  char *account_id;
  long long expires_at_milliseconds;
} strappy_openai_oauth_credentials;

void strappy_openai_oauth_default_configuration(
  strappy_openai_oauth_configuration *configuration);
int strappy_openai_oauth_set_cainfo(const char *path, char **error_out);

void strappy_openai_oauth_device_init(
  strappy_openai_oauth_device *device);
void strappy_openai_oauth_device_destroy(
  strappy_openai_oauth_device *device);
void strappy_openai_oauth_credentials_init(
  strappy_openai_oauth_credentials *credentials);
void strappy_openai_oauth_credentials_destroy(
  strappy_openai_oauth_credentials *credentials);

int strappy_openai_oauth_start_device_authorization(
  const strappy_openai_oauth_configuration *configuration,
  strappy_openai_oauth_device *device,
  strappy_openai_oauth_cancel_callback cancel_callback,
  void *cancel_user_data,
  char **error_out);
int strappy_openai_oauth_complete_device_authorization(
  const strappy_openai_oauth_configuration *configuration,
  const strappy_openai_oauth_device *device,
  strappy_openai_oauth_credentials *credentials,
  strappy_openai_oauth_cancel_callback cancel_callback,
  void *cancel_user_data,
  char **error_out);
int strappy_openai_oauth_refresh_credentials(
  const strappy_openai_oauth_configuration *configuration,
  const char *refresh_token,
  strappy_openai_oauth_credentials *credentials,
  strappy_openai_oauth_cancel_callback cancel_callback,
  void *cancel_user_data,
  char **error_out);

#ifdef __cplusplus
}
#endif

#endif
