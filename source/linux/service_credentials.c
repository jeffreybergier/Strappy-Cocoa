/* Linux offline daemon fixture: synthetic credentials, no Keychain or cache. */
#include "../daemon/strappy_execution.h"
#include "../shared/strappy_responses.h"
#include "../shared/strappy_core.h"
#include <string.h>
static int credentials(const char *provider, const char *account, int refresh,
    char **token, char **identity, void *context, char **error)
{
    (void)refresh; (void)context; (void)error;
    *token = NULL; *identity = NULL;
    (void)account;
    if (strcmp(provider, "other")) return 0;
    *token = strappy_string_duplicate("offline-fixture");
    return *token != NULL;
}
int strappy_service_credentials_configure(void)
{
    strappy_responses_set_provider_credentials_callback(credentials, NULL); return 1;
}
void *strappy_service_worker_enter(void) { return 0; }
void strappy_service_worker_leave(void *pool) { (void)pool; }

int strappy_service_credentials_probe(const char *database) { (void)database; return 1; }

#include "../daemon/strappy_mutations.h"
#include "../shared/strappy_db.h"
#include "../shared/strappy_model_catalog.h"
int strappy_service_scanner_profile(void) { return 0; }
int strappy_service_account_command(const char *db, const cJSON *c, char **result)
{
    const char *action = strappy_command_text(c,"action");
    (void)result;
    if (!strcmp(action,"archive_account"))
        return strappy_db_archive_provider_account(db,strappy_command_text(c,"id"),NULL);
    if (!strcmp(action,"update_account"))
        return strappy_db_update_provider_account(db,strappy_command_text(c,"id"),strappy_command_text(c,"name"),
            strappy_command_text(c,"endpoint"),strappy_command_number(c,"output"),NULL);
    return 0;
}
#include <stdlib.h>
static int catalog_cancel(void *context) { (void)context; return strappy_service_should_cancel(); }
int strappy_service_refresh_catalog(const char *db, const char *account)
{
    strappy_provider_account_record record;
    strappy_config config;
    char *json = NULL;
    long status = 0;
    int ok;
    strappy_provider_account_record_init(&record);
    strappy_config_init(&config);
    ok = strappy_db_get_provider_account(db,account,&record,NULL);
    if (ok) {
        config.api_endpoint = strappy_string_duplicate(record.responses_endpoint);
        config.api_token = strappy_string_duplicate("offline-fixture");
        ok = strappy_client_fetch_openrouter_user_models_json_with_cancel(&config,&json,&status,
            catalog_cancel,NULL,NULL) && strappy_db_save_account_models_json(db,account,json,NULL);
    }
    free(json); strappy_config_destroy(&config); strappy_provider_account_record_destroy(&record);
    return ok;
}
