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
