#ifndef STRAPPY_SESSION_EXECUTION_H
#define STRAPPY_SESSION_EXECUTION_H
#include "strappy_session.h"
#include "strappy_client.h"
int strappy_session_initialize_store(const char *db_path,
                                     char **error_out);

int strappy_session_set_default_model(const char *db_path,
                                      const char *model_id,
                                      char **error_out);

int strappy_session_set_model_allowed(const char *db_path,
                                      const char *model_id,
                                      int allowed,
                                      char **error_out);

int strappy_session_set_default_openrouter_model(const char *db_path,
                                                 const char *model_id,
                                                 char **error_out);

int strappy_session_set_openrouter_model_allowed(const char *db_path,
                                                 const char *model_id,
                                                 int allowed,
                                                 char **error_out);

int strappy_session_refresh_openrouter_user_models(
  const char *api_endpoint,
  const char *api_token,
  const char *db_path,
  char **error_out);

int strappy_session_create_with_working_directory(
  const char *db_path,
  const char *working_directory,
  long long *session_id_out,
  char **error_out);

int strappy_session_delete(const char *db_path,
                           long long session_id,
                           char **error_out);

int strappy_session_update_model_request_include_in_context(
  const char *db_path,
  long long session_id,
  long long model_request_id,
  int include_in_context,
  char **error_out);

int strappy_session_validate_options_update(const char *resource_dir,
  const strappy_session_options *options, strappy_session_option_mask changed_fields,
  char **error_out);

int strappy_session_update_options(
  const char *db_path,
  long long session_id,
  const char *resource_dir,
  const strappy_session_options *options,
  strappy_session_option_mask changed_fields,
  strappy_session_options *saved_options_out,
  strappy_session_option_mask *actual_changed_fields_out,
  char **error_out);

int strappy_session_update_default_options(
  const char *db_path,
  const char *fallback_working_directory,
  const char *resource_dir,
  const strappy_session_options *options,
  strappy_session_option_mask changed_fields,
  strappy_session_options *saved_options_out,
  strappy_session_option_mask *actual_changed_fields_out,
  char **error_out);

int strappy_session_update_working_directory(
  const char *db_path,
  long long session_id,
  const char *working_directory,
  char **error_out);

int strappy_session_update_assistant_set(const char *db_path,
                                         long long session_id,
                                         const char *resource_dir,
                                         const char *assistant_set_id,
                                         char **error_out);

int strappy_session_send_prompt_with_events_and_load(
  const char *prompt,
  const char *api_endpoint,
  const char *api_token,
  const char *guidance_resource_dir,
  const char *db_path,
  long long session_id,
  strappy_responses_event_callback callback,
  void *callback_data,
  strappy_session_record *record,
  char **error_out);

int strappy_session_send_isolated_prompt_with_events_and_load(
  const char *prompt,
  const char *api_endpoint,
  const char *api_token,
  const char *guidance_resource_dir,
  const char *db_path,
  long long session_id,
  strappy_responses_event_callback callback,
  void *callback_data,
  strappy_session_record *record,
  char **error_out);

#endif
