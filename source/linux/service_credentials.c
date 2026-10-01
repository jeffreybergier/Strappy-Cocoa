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
    if (strcmp(provider, "other") || strcmp(account, "daemon-offline")) return 0;
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
