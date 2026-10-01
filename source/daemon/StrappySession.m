/* Daemon-only Objective-C/C session credential boundary. */
#import "../shared/StrappyKeychain.h"
#import "../shared/XPFoundation.h"
#import "../shared/XPKeychain.h"
#include "../shared/strappy_core.h"
#include "../shared/strappy_provider.h"
#include "../shared/strappy_openai_oauth.h"
#include "../shared/strappy_responses.h"
#include "strappy_execution.h"
#include <string.h>
#include <stdio.h>
#include "../shared/strappy_db.h"
static int StrappyServiceCancelRefresh(void *context)
{
    (void)context;
    return strappy_service_should_cancel();
}
static int StrappyServiceCopyCredentials(
  const char *providerID,
  const char *providerAccountID,
  int forceRefresh,
  char **accessTokenOut,
  char **accountIdentifierOut,
  void *userData,
  char **errorOut)
{
  static const long long refreshLeewayMilliseconds = 5LL * 60LL * 1000LL;
  StrappyKeychain *keychain;
  NSString *accessToken;
  NSString *refreshToken;
  NSString *accountIdentifier;
  long long expiresAtMilliseconds;
  long long nowMilliseconds;
  BOOL credentialReady;
  int ok;

  (void)userData;
  if ((providerID == NULL) || (providerID[0] == '\0') ||
      (providerAccountID == NULL) || (providerAccountID[0] == '\0') ||
      (accessTokenOut == NULL) || (accountIdentifierOut == NULL)) {
    strappy_set_error(errorOut, "Account credential inputs are missing.");
    return 0;
  }
  *accessTokenOut = NULL;
  *accountIdentifierOut = NULL;
  keychain = [StrappyKeychain sharedKeychain];
  accessToken = nil;
  refreshToken = nil;
  accountIdentifier = nil;
  expiresAtMilliseconds = 0LL;
  ok = 0;

  if (strcmp(providerID, STRAPPY_PROVIDER_OPENAI_CHATGPT) != 0) {
    NSString *providerIdentifier;
    NSString *providerAccountIdentifier;
    NSString *bearerToken;
    NSObject *credentialLock;

    providerIdentifier = [NSString stringWithUTF8String:providerID];
    providerAccountIdentifier = [NSString stringWithUTF8String:
      providerAccountID];
    bearerToken = nil;
    credentialLock = [keychain
      credentialLockForProviderIdentifier:providerIdentifier
      providerAccountIdentifier:providerAccountIdentifier];
    @synchronized(credentialLock) {
      BOOL loaded;

      loaded = [keychain loadBearerToken:&bearerToken
                   forProviderIdentifier:providerIdentifier
               providerAccountIdentifier:providerAccountIdentifier];
      if (loaded) {
        *accessTokenOut = strappy_string_duplicate([bearerToken UTF8String]);
        if (*accessTokenOut != NULL) {
          return 1;
        }
      }
    }
    strappy_set_error(errorOut, "The selected account has no bearer credential.");
    return 0;
  }

  {
    NSString *providerAccountIdentifier;
    NSObject *credentialLock;

    providerAccountIdentifier = [NSString stringWithUTF8String:
      providerAccountID];
    credentialLock = [keychain
      credentialLockForProviderIdentifier:@"openai_chatgpt"
      providerAccountIdentifier:providerAccountIdentifier];
  @synchronized(credentialLock) {
    BOOL loaded;

    loaded = [keychain loadChatGPTAccessToken:&accessToken
                                  refreshToken:&refreshToken
                             accountIdentifier:&accountIdentifier
                          expiresAtMilliseconds:&expiresAtMilliseconds
                     providerAccountIdentifier:providerAccountIdentifier];
    if (!loaded &&
        [keychain
          migrateLegacyChatGPTCredentialToProviderAccountIdentifier:
            providerAccountIdentifier]) {
      loaded = [keychain loadChatGPTAccessToken:&accessToken
                                    refreshToken:&refreshToken
                               accountIdentifier:&accountIdentifier
                            expiresAtMilliseconds:&expiresAtMilliseconds
                       providerAccountIdentifier:providerAccountIdentifier];
    }
    if (!loaded) {
      strappy_set_error(errorOut, "Sign in to ChatGPT before sending a prompt.");
    } else {
      credentialReady = YES;
      nowMilliseconds = strappy_unix_milliseconds();
      if (forceRefresh || (nowMilliseconds <= 0LL) ||
          ((expiresAtMilliseconds - nowMilliseconds) <=
           refreshLeewayMilliseconds)) {
        strappy_openai_oauth_configuration configuration;
        strappy_openai_oauth_credentials credentials;
        NSString *nextAccessToken;
        NSString *nextRefreshToken;
        NSString *nextAccountIdentifier;

        strappy_openai_oauth_default_configuration(&configuration);
        strappy_openai_oauth_credentials_init(&credentials);
        credentialReady = NO;
        if (strappy_openai_oauth_refresh_credentials(
              &configuration,
              [refreshToken UTF8String],
              &credentials,
              StrappyServiceCancelRefresh,
              NULL,
              errorOut)) {
          nextAccessToken = [NSString stringWithUTF8String:
            credentials.access_token];
          nextRefreshToken = [NSString stringWithUTF8String:
            credentials.refresh_token];
          nextAccountIdentifier = [NSString stringWithUTF8String:
            credentials.account_id];
          if ((nextAccessToken == nil) || (nextRefreshToken == nil) ||
              (nextAccountIdentifier == nil) ||
              ![nextAccountIdentifier isEqualToString:accountIdentifier]) {
            strappy_set_error(
              errorOut,
              "Refreshed ChatGPT credentials changed account identity.");
          } else if (![keychain
                       saveChatGPTAccessToken:nextAccessToken
                       refreshToken:nextRefreshToken
                       accountIdentifier:nextAccountIdentifier
                       expiresAtMilliseconds:
                         credentials.expires_at_milliseconds
                  providerAccountIdentifier:providerAccountIdentifier]) {
            strappy_set_error(
              errorOut,
              "The Keychain refused the refreshed ChatGPT credential.");
          } else {
            accessToken = nextAccessToken;
            refreshToken = nextRefreshToken;
            accountIdentifier = nextAccountIdentifier;
            expiresAtMilliseconds = credentials.expires_at_milliseconds;
            credentialReady = YES;
          }
        }
        strappy_openai_oauth_credentials_destroy(&credentials);
      }
      if (credentialReady && ((errorOut == NULL) || (*errorOut == NULL))) {
        *accessTokenOut = strappy_string_duplicate([accessToken UTF8String]);
        *accountIdentifierOut =
          strappy_string_duplicate([accountIdentifier UTF8String]);
        if ((*accessTokenOut == NULL) || (*accountIdentifierOut == NULL)) {
          strappy_secure_free_string(*accessTokenOut);
          strappy_secure_free_string(*accountIdentifierOut);
          *accessTokenOut = NULL;
          *accountIdentifierOut = NULL;
          strappy_set_error(errorOut,
                            "Could not allocate ChatGPT credential snapshot.");
        } else {
          ok = 1;
        }
      }
    }
  }
  }
  return ok;
}

int strappy_service_credentials_configure(void)
{
    if (![XPKeychain configureBackgroundAccess]) return 0;
    strappy_responses_set_provider_credentials_callback(StrappyServiceCopyCredentials, NULL);
    return 1;
}
void *strappy_service_worker_enter(void) { return [[NSAutoreleasePool alloc] init]; }
void strappy_service_worker_leave(void *pool) { [(NSAutoreleasePool *)pool release]; }

/* Read-only diagnostic: no network, no secret/identity output, no prompts.
 * It checks the daemon executable's Keychain access independently of the UI. */
int strappy_service_credentials_probe(const char *database)
{
  strappy_provider_account_record_list accounts;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  size_t i;
  int available = 0;
  strappy_provider_account_record_list_init(&accounts);
  if (!strappy_service_credentials_configure() ||
      !strappy_db_list_provider_accounts(database, NULL, 0, &accounts, NULL)) {
    [pool release]; return 1;
  }
  for (i = 0; i < accounts.count; ++i) {
    strappy_provider_account_record *account = &accounts.records[i];
    NSString *identifier = [NSString stringWithUTF8String:account->account_id];
    NSString *token = nil;
    BOOL loaded;
    if (strcmp(account->provider_id, STRAPPY_PROVIDER_OPENAI_CHATGPT) == 0) {
      NSString *refresh = nil, *identity = nil;
      long long expiry = 0;
      loaded = [[StrappyKeychain sharedKeychain] loadChatGPTAccessToken:&token
          refreshToken:&refresh accountIdentifier:&identity expiresAtMilliseconds:&expiry
          providerAccountIdentifier:identifier];
    } else {
      loaded = [[StrappyKeychain sharedKeychain] loadBearerToken:&token
          forProviderIdentifier:[NSString stringWithUTF8String:account->provider_id]
          providerAccountIdentifier:identifier];
    }
    if (loaded && [token length] > 0) ++available;
  }
  printf("Keychain probe: accounts=%lu accessible=%d; no refresh or provider request\n",
      (unsigned long)accounts.count, available);
  strappy_provider_account_record_list_destroy(&accounts);
  [pool release]; return 0;
}

#include "strappy_mutations.h"
#include "../shared/strappy_model_catalog.h"
#include "../shared/strappy_file_scanner.h"
#include <stdlib.h>

static NSString *StrappyCommandString(const cJSON *c, const char *key)
{
    return [NSString stringWithUTF8String:strappy_command_text(c,key)];
}
int strappy_service_scanner_profile(void)
{
    XPPlatformFamily family = [[NSProcessInfo processInfo] XP_platformFamily];
    return family == XPPlatformFamilyIOS ? STRAPPY_FILE_SCANNER_PLATFORM_IOS :
        (family == XPPlatformFamilyMacOS ? STRAPPY_FILE_SCANNER_PLATFORM_MACOS : STRAPPY_FILE_SCANNER_PLATFORM_GENERIC);
}
int strappy_service_refresh_catalog(const char *db, const char *account)
{
    strappy_provider_account_record record;
    char *token = NULL, *identity = NULL, *error = NULL;
    int ok;
    strappy_provider_account_record_init(&record);
    ok = strappy_db_get_provider_account(db,account,&record,&error) &&
        !strcmp(record.provider_id,STRAPPY_PROVIDER_OPENROUTER) &&
        StrappyServiceCopyCredentials(record.provider_id,account,0,&token,&identity,NULL,&error);
    if (ok) {
        strappy_config config;
        char *json = NULL;
        long http_status = 0;
        strappy_config_init(&config);
        config.api_endpoint = strappy_string_duplicate(record.responses_endpoint);
        config.api_token = strappy_string_duplicate(token);
        ok = strappy_client_fetch_openrouter_user_models_json_with_cancel(&config,&json,&http_status,
            StrappyServiceCancelRefresh,NULL,&error) && strappy_db_save_account_models_json(db,account,json,&error);
        free(json); strappy_config_destroy(&config);
    }
    strappy_secure_free_string(token); strappy_secure_free_string(identity); free(error);
    strappy_provider_account_record_destroy(&record); return ok;
}
int strappy_service_account_command(const char *db, const cJSON *c, char **result)
{
    const char *action = strappy_command_text(c,"action"), *account = strappy_command_text(c,"id");
    strappy_provider_account_record record;
    StrappyKeychain *keychain = [StrappyKeychain sharedKeychain];
    NSString *identifier = StrappyCommandString(c,"id"), *provider;
    NSString *previous = nil;
    char *error = NULL;
    int ok = 0;
    strappy_provider_account_record_init(&record);
    if (!strappy_db_get_provider_account(db,account,&record,&error) || !strcmp(record.lifecycle_state,STRAPPY_PROVIDER_ACCOUNT_ARCHIVED)) goto done;
    provider = [NSString stringWithUTF8String:record.provider_id];
    if (!strcmp(action,"refresh_oauth")) {
        char *token = NULL, *identity = NULL;
        NSString *current = nil;
        if (![provider isEqualToString:@"openai_chatgpt"] ||
            ![keychain loadChatGPTAccessToken:NULL refreshToken:NULL accountIdentifier:&current
                expiresAtMilliseconds:NULL providerAccountIdentifier:identifier] ||
            ![current isEqualToString:StrappyCommandString(c,"identity")]) goto done;
        ok = StrappyServiceCopyCredentials(record.provider_id,account,1,&token,&identity,NULL,&error);
        if (ok) *result = strappy_string_duplicate(identity);
        strappy_secure_free_string(token); strappy_secure_free_string(identity);
    } else if (!strcmp(action,"delete_oauth")) {
        ok = [provider isEqualToString:@"openai_chatgpt"] &&
            [keychain deleteChatGPTCredentialsForProviderAccountIdentifier:identifier];
    } else if (!strcmp(action,"save_oauth")) {
        if ([provider isEqualToString:@"openai_chatgpt"] &&
            *strappy_command_text(c,"access") && *strappy_command_text(c,"refresh") &&
            *strappy_command_text(c,"identity") && strappy_command_number(c,"expiry") > 0)
            ok = [keychain saveChatGPTAccessToken:StrappyCommandString(c,"access")
                refreshToken:StrappyCommandString(c,"refresh") accountIdentifier:StrappyCommandString(c,"identity")
                expiresAtMilliseconds:strappy_command_number(c,"expiry") providerAccountIdentifier:identifier];
    } else if (!strcmp(action,"archive_account")) {
        /* Archive first: a credential deletion failure must never leave an active
         * account whose credential was silently destroyed. */
        ok = strappy_db_archive_provider_account(db,account,&error);
        if (ok) {
            if ([provider isEqualToString:@"openai_chatgpt"])
                [keychain deleteChatGPTCredentialsForProviderAccountIdentifier:identifier];
            else [keychain deleteBearerTokenForProviderIdentifier:provider providerAccountIdentifier:identifier];
        }
    } else if (!strcmp(action,"update_account")) {
        NSString *name = [StrappyCommandString(c,"name") stringByTrimmingCharactersInSet:
            [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        NSString *endpoint = [StrappyCommandString(c,"endpoint") stringByTrimmingCharactersInSet:
            [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        NSString *token = [StrappyCommandString(c,"token") stringByTrimmingCharactersInSet:
            [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        BOOL replace = cJSON_GetObjectItemCaseSensitive(c,"token") != NULL &&
            ![provider isEqualToString:@"openai_chatgpt"];
        BOOL hadPrevious = NO;
        if (![name length] || strappy_command_number(c,"output") < 0 ||
            (replace && [provider isEqualToString:@"openrouter"] && ![token length])) goto done;
        if (replace) {
            hadPrevious = [keychain loadBearerToken:&previous forProviderIdentifier:provider providerAccountIdentifier:identifier];
            ok = [token length] ? [keychain saveBearerToken:token forProviderIdentifier:provider providerAccountIdentifier:identifier] :
                [keychain deleteBearerTokenForProviderIdentifier:provider providerAccountIdentifier:identifier];
            if (!ok) goto done;
        }
        ok = strappy_db_update_provider_account(db,account,[name UTF8String],[endpoint UTF8String],
            strappy_command_number(c,"output"),&error);
        if (!ok && replace) {
            if (hadPrevious) [keychain saveBearerToken:previous forProviderIdentifier:provider providerAccountIdentifier:identifier];
            else [keychain deleteBearerTokenForProviderIdentifier:provider providerAccountIdentifier:identifier];
        }
        if (ok) [keychain saveDisplayName:name forProviderIdentifier:provider providerAccountIdentifier:identifier];
    }
done:
    free(error); strappy_provider_account_record_destroy(&record); return ok;
}
