#include "strappy_session_execution.h"
#include "strappy_core.h"
#include "strappy_model_catalog.h"
#include "strappy_responses.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static int strappy_session_ensure_directory_component(const char *path,
                                                      char **error_out)
{
  struct stat directory_stat;

  errno = 0;
  if (mkdir(path, 0755) == 0) {
    return 1;
  }
  if (errno != EEXIST) {
    strappy_set_formatted_error(error_out,
                                "Could not create working directory %s: %s.",
                                path,
                                strerror(errno));
    return 0;
  }
  errno = 0;
  if (stat(path, &directory_stat) != 0) {
    strappy_set_formatted_error(error_out,
                                "Could not inspect working directory %s: %s.",
                                path,
                                strerror(errno));
    return 0;
  }
  if (!S_ISDIR(directory_stat.st_mode)) {
    strappy_set_formatted_error(error_out,
                                "Working directory path is not a directory: %s.",
                                path);
    return 0;
  }
  return 1;
}

static int strappy_session_ensure_working_directory(
  const char *working_directory,
  char **error_out)
{
  char *path;
  char *cursor;
  size_t length;
  int ok;

  if ((working_directory == NULL) || (working_directory[0] == '\0')) {
    strappy_set_error(error_out, "Session working directory is empty.");
    return 0;
  }
  if (working_directory[0] != '/') {
    strappy_set_error(error_out,
                      "Session working directory must be an absolute path.");
    return 0;
  }

  path = strappy_string_duplicate(working_directory);
  if (path == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate the session working directory.");
    return 0;
  }
  length = strlen(path);
  while ((length > 1U) && (path[length - 1U] == '/')) {
    path[--length] = '\0';
  }

  ok = 1;
  cursor = path + 1;
  while (ok) {
    char saved;

    while ((*cursor != '\0') && (*cursor != '/')) {
      cursor++;
    }
    saved = *cursor;
    *cursor = '\0';
    if (path[1] != '\0') {
      ok = strappy_session_ensure_directory_component(path, error_out);
    }
    *cursor = saved;
    if (saved == '\0') {
      break;
    }
    cursor++;
    while (*cursor == '/') {
      cursor++;
    }
  }
  free(path);
  return ok;
}

int strappy_session_initialize_store(const char *db_path,
                                     char **error_out)
{
  return strappy_db_initialize(db_path, error_out);
}

int strappy_session_set_default_model(const char *db_path,
                                      const char *model_id,
                                      char **error_out)
{
  return strappy_db_set_default_model(db_path, model_id, error_out);
}

int strappy_session_set_model_allowed(const char *db_path,
                                      const char *model_id,
                                      int allowed,
                                      char **error_out)
{
  return strappy_db_set_model_allowed(db_path,
                                      model_id,
                                      allowed,
                                      error_out);
}

int strappy_session_set_default_openrouter_model(const char *db_path,
                                                 const char *model_id,
                                                 char **error_out)
{
  return strappy_session_set_default_model(db_path, model_id, error_out);
}

int strappy_session_set_openrouter_model_allowed(const char *db_path,
                                                 const char *model_id,
                                                 int allowed,
                                                 char **error_out)
{
  return strappy_session_set_model_allowed(db_path,
                                           model_id,
                                           allowed,
                                           error_out);
}

int strappy_session_refresh_openrouter_user_models(
  const char *api_endpoint,
  const char *api_token,
  const char *db_path,
  char **error_out)
{
  return strappy_model_catalog_refresh_openrouter_user_models(NULL,
                                                             api_endpoint,
                                                             api_token,
                                                             db_path,
                                                             error_out);
}

int strappy_session_create_with_working_directory(
  const char *db_path,
  const char *working_directory,
  long long *session_id_out,
  char **error_out)
{
  if (session_id_out != NULL) {
    *session_id_out = 0LL;
  }
  if (!strappy_session_ensure_working_directory(working_directory,
                                                error_out)) {
    return 0;
  }
  return strappy_db_create_session_with_working_directory(db_path,
                                                          working_directory,
                                                          session_id_out,
                                                          error_out);
}

int strappy_session_delete(const char *db_path,
                           long long session_id,
                           char **error_out)
{
  return strappy_db_delete_session(db_path, session_id, error_out);
}

int strappy_session_update_model_request_include_in_context(
  const char *db_path,
  long long session_id,
  long long model_request_id,
  int include_in_context,
  char **error_out)
{
  return strappy_db_update_model_request_include_in_context(
    db_path,
    session_id,
    model_request_id,
    include_in_context,
    error_out);
}

int strappy_session_validate_options_update(
  const char *resource_dir,
  const strappy_session_options *options,
  strappy_session_option_mask changed_fields,
  char **error_out)
{
  strappy_assistant_set_profile profile;

  if (options == NULL) {
    strappy_set_error(error_out, "Session options are missing.");
    return 0;
  }
  if ((changed_fields & STRAPPY_SESSION_OPTION_WORKING_DIRECTORY) != 0U) {
    if (!strappy_session_ensure_working_directory(options->working_directory,
                                                  error_out)) {
      return 0;
    }
  }
  if ((changed_fields & STRAPPY_SESSION_OPTION_ASSISTANT_SET) != 0U) {
    if ((options->assistant_set_id == NULL) ||
        (options->assistant_set_id[0] == '\0')) {
      strappy_set_error(error_out, "Session assistant set is not selected.");
      return 0;
    }
    if ((resource_dir == NULL) || (resource_dir[0] == '\0')) {
      strappy_set_error(error_out,
                        "Assistant resource directory is missing.");
      return 0;
    }
    strappy_assistant_set_profile_init(&profile);
    if (!strappy_assistant_sets_load_profile(resource_dir,
                                             options->assistant_set_id,
                                             &profile,
                                             error_out)) {
      return 0;
    }
    if (!strappy_assistant_set_profile_is_available(&profile)) {
      strappy_set_formatted_error(error_out,
                                  "Assistant set is not available: %s",
                                  options->assistant_set_id);
      strappy_assistant_set_profile_destroy(&profile);
      return 0;
    }
    strappy_assistant_set_profile_destroy(&profile);
  }
  return 1;
}

int strappy_session_update_options(
  const char *db_path,
  long long session_id,
  const char *resource_dir,
  const strappy_session_options *options,
  strappy_session_option_mask changed_fields,
  strappy_session_options *saved_options_out,
  strappy_session_option_mask *actual_changed_fields_out,
  char **error_out)
{
  if (!strappy_session_validate_options_update(resource_dir,
                                               options,
                                               changed_fields,
                                               error_out)) {
    return 0;
  }
  return strappy_db_update_session_options(db_path,
                                           session_id,
                                           options,
                                           changed_fields,
                                           saved_options_out,
                                           actual_changed_fields_out,
                                           error_out);
}

int strappy_session_update_default_options(
  const char *db_path,
  const char *fallback_working_directory,
  const char *resource_dir,
  const strappy_session_options *options,
  strappy_session_option_mask changed_fields,
  strappy_session_options *saved_options_out,
  strappy_session_option_mask *actual_changed_fields_out,
  char **error_out)
{
  if (!strappy_session_validate_options_update(resource_dir,
                                               options,
                                               changed_fields,
                                               error_out)) {
    return 0;
  }
  return strappy_db_update_default_session_options(
    db_path,
    fallback_working_directory,
    options,
    changed_fields,
    saved_options_out,
    actual_changed_fields_out,
    error_out);
}

int strappy_session_update_working_directory(
  const char *db_path,
  long long session_id,
  const char *working_directory,
  char **error_out)
{
  if (!strappy_session_ensure_working_directory(working_directory,
                                                error_out)) {
    return 0;
  }
  return strappy_db_update_session_working_directory(db_path,
                                                     session_id,
                                                     working_directory,
                                                     error_out);
}

int strappy_session_update_assistant_set(const char *db_path,
                                         long long session_id,
                                         const char *resource_dir,
                                         const char *assistant_set_id,
                                         char **error_out)
{
  strappy_assistant_set_profile profile;
  int ok;

  strappy_assistant_set_profile_init(&profile);
  if (!strappy_assistant_sets_load_profile(resource_dir,
                                           assistant_set_id,
                                           &profile,
                                           error_out)) {
    return 0;
  }
  if (!strappy_assistant_set_profile_is_available(&profile)) {
    strappy_set_formatted_error(error_out,
                                "Assistant set is not available: %s",
                                assistant_set_id);
    strappy_assistant_set_profile_destroy(&profile);
    return 0;
  }
  ok = strappy_db_update_session_assistant_set(db_path,
                                               session_id,
                                               assistant_set_id,
                                               error_out);
  strappy_assistant_set_profile_destroy(&profile);
  return ok;
}

static int strappy_session_load_after_prompt(const char *db_path,
                                             long long session_id,
                                             char *response,
                                             strappy_session_record *record,
                                             char **error_out)
{
  if (response == NULL) {
    return 0;
  }

  strappy_free_string(response);
  return strappy_session_load_record(db_path, session_id, record, error_out);
}

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
  char **error_out)
{
  char *response;

  response = strappy_responses_send_prompt_for_session_and_store_with_events(
    prompt,
    NULL,
    api_endpoint,
    api_token,
    guidance_resource_dir,
    db_path,
    session_id,
    callback,
    callback_data,
    error_out);

  return strappy_session_load_after_prompt(db_path,
                                           session_id,
                                           response,
                                           record,
                                           error_out);
}

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
  char **error_out)
{
  char *response;

  response =
    strappy_responses_send_isolated_prompt_for_session_and_store_with_events(
      prompt,
      NULL,
      api_endpoint,
      api_token,
      guidance_resource_dir,
      db_path,
      session_id,
      callback,
      callback_data,
      error_out);
  return strappy_session_load_after_prompt(db_path,
                                           session_id,
                                           response,
                                           record,
                                           error_out);
}
