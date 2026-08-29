#include "strappy_db.h"

#include "strappy_core.h"
#include "strappy_platform_profile.h"
#include "strappy_provider.h"

#include <emscripten/emscripten.h>
#include <stdlib.h>

#define STRAPPY_WEB_DATABASE_PATH "/strappy.sqlite"
#define STRAPPY_WEB_ACCOUNT_ID STRAPPY_PROVIDER_ACCOUNT_OPENROUTER

static char *strappy_web_database_last_error = NULL;
static long long strappy_web_database_last_created_session_id = 0LL;
static long strappy_web_database_last_count = 0L;
static const char *strappy_web_database_active_path = ":memory:";

void strappy_db_web_set_persistent_vfs_enabled(int enabled);

static void strappy_web_database_clear_error(void)
{
  free(strappy_web_database_last_error);
  strappy_web_database_last_error = NULL;
}

static int strappy_web_database_refresh_count(const char *db_path)
{
  strappy_session_record_list list;
  int ok;

  strappy_session_record_list_init(&list);
  ok = strappy_db_list_sessions(db_path,
                                &list,
                                &strappy_web_database_last_error);
  if (ok) {
    if (list.count > 2147483647U) {
      strappy_set_error(&strappy_web_database_last_error,
                        "The session list is too large.");
      ok = 0;
    } else {
      strappy_web_database_last_count = (long)list.count;
      strappy_web_database_last_created_session_id =
        (list.count > 0U) ? list.records[0].session_id : 0LL;
    }
  }
  strappy_session_record_list_destroy(&list);
  return ok;
}

static int strappy_web_database_enforce_assistant_set(const char *db_path)
{
  return (strappy_web_database_last_created_session_id <= 0LL) ||
    strappy_db_update_session_assistant_set(
      db_path,
      strappy_web_database_last_created_session_id,
      strappy_platform_default_assistant_set_id(),
      &strappy_web_database_last_error);
}

EMSCRIPTEN_KEEPALIVE
int strappy_web_database_initialize_temporary(void)
{
  strappy_web_database_clear_error();
  strappy_web_database_last_count = 0L;
  strappy_web_database_active_path = ":memory:";
  return strappy_db_initialize(strappy_web_database_active_path,
                               &strappy_web_database_last_error) &&
    strappy_db_restore_provider_account(
      strappy_web_database_active_path,
      STRAPPY_WEB_ACCOUNT_ID,
      STRAPPY_PROVIDER_OPENROUTER,
      "OpenRouter",
      STRAPPY_PROVIDER_OPENROUTER_RESPONSES_ENDPOINT,
      &strappy_web_database_last_error);
}

EMSCRIPTEN_KEEPALIVE
int strappy_web_database_initialize_persistent(void)
{
  strappy_web_database_clear_error();
  strappy_web_database_active_path = STRAPPY_WEB_DATABASE_PATH;
  strappy_web_database_last_created_session_id = 0LL;
  strappy_web_database_last_count = 0L;
  if (!strappy_db_initialize(STRAPPY_WEB_DATABASE_PATH,
                             &strappy_web_database_last_error) ||
      !strappy_db_restore_provider_account(
        STRAPPY_WEB_DATABASE_PATH,
        STRAPPY_WEB_ACCOUNT_ID,
        STRAPPY_PROVIDER_OPENROUTER,
        "OpenRouter",
        STRAPPY_PROVIDER_OPENROUTER_RESPONSES_ENDPOINT,
        &strappy_web_database_last_error)) {
    return 0;
  }
  return strappy_web_database_refresh_count(STRAPPY_WEB_DATABASE_PATH) &&
    strappy_web_database_enforce_assistant_set(STRAPPY_WEB_DATABASE_PATH);
}

EMSCRIPTEN_KEEPALIVE
void strappy_web_database_enable_persistence(void)
{
  strappy_db_web_set_persistent_vfs_enabled(1);
}

EMSCRIPTEN_KEEPALIVE
int strappy_web_database_create_session(void)
{
  strappy_web_database_clear_error();
  strappy_web_database_last_created_session_id = 0LL;
  if (!strappy_db_create_session_with_working_directory(
        strappy_web_database_active_path,
        "/",
        &strappy_web_database_last_created_session_id,
        &strappy_web_database_last_error)) {
    return 0;
  }
  return strappy_web_database_refresh_count(strappy_web_database_active_path) &&
    strappy_web_database_enforce_assistant_set(
      strappy_web_database_active_path);
}

const char *strappy_web_database_path(void)
{
  return strappy_web_database_active_path;
}

EMSCRIPTEN_KEEPALIVE
long strappy_web_database_session_count(void)
{
  return strappy_web_database_last_count;
}

EMSCRIPTEN_KEEPALIVE
long long strappy_web_database_last_session_id(void)
{
  return strappy_web_database_last_created_session_id;
}

EMSCRIPTEN_KEEPALIVE
const char *strappy_web_database_error(void)
{
  return (strappy_web_database_last_error != NULL) ?
    strappy_web_database_last_error : "";
}
