#include "strappy_assistant.h"

#include "strappy_config.h"
#include "strappy_core.h"
#include "strappy_client.h"
#include "strappy_db.h"

static char *strappy_assistant_send_prompt_internal(const char *prompt,
                                                    const char *env_path,
                                                    const char *session_db_path,
                                                    int should_store,
                                                    char **error_out)
{
  strappy_config config;
  strappy_chat_result result;
  char *response;

  if ((prompt == NULL) || (prompt[0] == '\0')) {
    strappy_set_error(error_out, "Prompt is empty.");
    return NULL;
  }

  strappy_config_init(&config);
  strappy_chat_result_init(&result);

  if (!strappy_config_load(&config, env_path, error_out)) {
    return NULL;
  }

  if (!strappy_client_send_prompt(&config, prompt, &result, error_out)) {
    strappy_config_destroy(&config);
    return NULL;
  }

  if (should_store &&
      !strappy_db_save_exchange(session_db_path,
                                prompt,
                                result.response_text,
                                result.model,
                                result.http_status,
                                error_out)) {
    strappy_chat_result_destroy(&result);
    strappy_config_destroy(&config);
    return NULL;
  }

  response = result.response_text;
  result.response_text = NULL;

  strappy_chat_result_destroy(&result);
  strappy_config_destroy(&config);
  return response;
}

char *strappy_assistant_send_prompt(const char *prompt,
                                    const char *env_path,
                                    char **error_out)
{
  return strappy_assistant_send_prompt_internal(prompt,
                                               env_path,
                                               NULL,
                                               0,
                                               error_out);
}

char *strappy_assistant_send_prompt_and_store(const char *prompt,
                                             const char *env_path,
                                             const char *session_db_path,
                                             char **error_out)
{
  return strappy_assistant_send_prompt_internal(prompt,
                                               env_path,
                                               session_db_path,
                                               1,
                                               error_out);
}
