#import "StrappyKeychain.h"

#import "XPKeychain.h"
#include "strappy_config.h"
#include "strappy_openai_oauth.h"

#include <stdlib.h>

NSString * const StrappyKeychainDidChangeNotification =
  @"StrappyKeychainDidChangeNotification";

static NSString * const kStrappyKeychainAccount = @"apitoken";
static NSString * const kStrappyChatGPTKeychainService =
  @"com.altivecintelligence.Strappy.openai-chatgpt";
static NSString * const kStrappyChatGPTKeychainAccount = @"oauth.v1";
static NSString * const kStrappyAPIEndpointEnvironmentName = @"APIENDPOINT";
static NSString * const kStrappyAPITokenEnvironmentName = @"APITOKEN";

static NSString * const kStrappyChatGPTFormatVersionKey = @"format_version";
static NSString * const kStrappyChatGPTAccessTokenKey = @"access_token";
static NSString * const kStrappyChatGPTRefreshTokenKey = @"refresh_token";
static NSString * const kStrappyChatGPTAccountIdentifierKey = @"account_id";
static NSString * const kStrappyChatGPTExpiresAtKey = @"expires_at_ms";

enum {
  kStrappyChatGPTCredentialFormatVersion =
    STRAPPY_OPENAI_OAUTH_CREDENTIAL_FORMAT_VERSION,
  kStrappyChatGPTCredentialMaximumBytes = 2 * 1024 * 1024
};

static NSString *StrappyEnvironmentValueOrNil(NSString *name)
{
  const char *value;

  value = getenv([name UTF8String]);
  if ((value == NULL) || (value[0] == '\0')) {
    return nil;
  }
  return [NSString stringWithUTF8String:value];
}

@implementation StrappyKeychain

+ (StrappyKeychain *)sharedKeychain
{
  static StrappyKeychain *instance = nil;

  if (instance == nil) {
    instance = [[StrappyKeychain alloc] init];
  }
  return instance;
}

+ (NSString *)defaultAPIEndpoint
{
  return [NSString stringWithUTF8String:STRAPPY_CONFIG_DEFAULT_API_ENDPOINT];
}

- (void)loadIfNeeded
{
  NSString *url;
  NSString *password;

  if (loaded_) {
    return;
  }

  url = nil;
  password = nil;
  if ([XPKeychain findInternetPasswordForAccount:kStrappyKeychainAccount
                                          outURL:&url
                                     outPassword:&password] &&
      (([url length] > 0U) || ([password length] > 0U))) {
    cachedAPIEndpoint_ = [url retain];
    cachedAPIToken_ = [password retain];
  }
  loaded_ = YES;
}

- (NSString *)apiEndpoint
{
  NSString *environmentEndpoint;

  environmentEndpoint =
    StrappyEnvironmentValueOrNil(kStrappyAPIEndpointEnvironmentName);
  if ([environmentEndpoint length] > 0U) {
    return environmentEndpoint;
  }

  [self loadIfNeeded];
  return cachedAPIEndpoint_;
}

- (NSString *)apiToken
{
  NSString *environmentToken;

  environmentToken = StrappyEnvironmentValueOrNil(kStrappyAPITokenEnvironmentName);
  if ([environmentToken length] > 0U) {
    return environmentToken;
  }

  [self loadIfNeeded];
  return cachedAPIToken_;
}

- (BOOL)hasAPICredentials
{
  return ([[self apiEndpoint] length] > 0U) && ([[self apiToken] length] > 0U);
}

- (BOOL)saveAPIEndpoint:(NSString *)apiEndpoint token:(NSString *)apiToken
{
  if (([apiEndpoint length] == 0U) || ([apiToken length] == 0U)) {
    return NO;
  }

  if (![XPKeychain setInternetPasswordForAccount:kStrappyKeychainAccount
                                             URL:apiEndpoint
                                        password:apiToken]) {
    return NO;
  }

  [self reload];
  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappyKeychainDidChangeNotification object:self];
  return YES;
}

- (BOOL)hasChatGPTCredentials
{
  return [self loadChatGPTAccessToken:NULL
                         refreshToken:NULL
                    accountIdentifier:NULL
                 expiresAtMilliseconds:NULL];
}

- (BOOL)loadChatGPTAccessToken:(NSString **)accessToken
                  refreshToken:(NSString **)refreshToken
             accountIdentifier:(NSString **)accountIdentifier
          expiresAtMilliseconds:(long long *)expiresAtMilliseconds
{
  NSData *data;
  NSString *errorDescription;
  NSPropertyListFormat format;
  id propertyList;
  NSDictionary *credential;
  id version;
  id access;
  id refresh;
  id account;
  id expiry;
  BOOL valid;

  data = nil;
  if (![XPKeychain
        findGenericPasswordDataForService:kStrappyChatGPTKeychainService
                                  account:kStrappyChatGPTKeychainAccount
                                  outData:&data] ||
      ([data length] == 0U) ||
      ([data length] > (NSUInteger)kStrappyChatGPTCredentialMaximumBytes)) {
    return NO;
  }

  errorDescription = nil;
  format = NSPropertyListOpenStepFormat;
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
  propertyList = [NSPropertyListSerialization
    propertyListFromData:data
         mutabilityOption:NSPropertyListImmutable
                   format:&format
         errorDescription:&errorDescription];
#ifdef __clang__
#pragma clang diagnostic pop
#endif
  [errorDescription release];
  if (![propertyList isKindOfClass:[NSDictionary class]] ||
      (format != NSPropertyListBinaryFormat_v1_0)) {
    return NO;
  }

  credential = propertyList;
  version = [credential objectForKey:kStrappyChatGPTFormatVersionKey];
  access = [credential objectForKey:kStrappyChatGPTAccessTokenKey];
  refresh = [credential objectForKey:kStrappyChatGPTRefreshTokenKey];
  account = [credential objectForKey:kStrappyChatGPTAccountIdentifierKey];
  expiry = [credential objectForKey:kStrappyChatGPTExpiresAtKey];
  valid = [version isKindOfClass:[NSNumber class]] &&
    ([version integerValue] == kStrappyChatGPTCredentialFormatVersion) &&
    [access isKindOfClass:[NSString class]] && ([access length] > 0U) &&
    [refresh isKindOfClass:[NSString class]] && ([refresh length] > 0U) &&
    [account isKindOfClass:[NSString class]] && ([account length] > 0U) &&
    [expiry isKindOfClass:[NSNumber class]] && ([expiry longLongValue] > 0LL);
  if (!valid) {
    return NO;
  }

  if (accessToken != NULL) {
    *accessToken = [[access copy] autorelease];
  }
  if (refreshToken != NULL) {
    *refreshToken = [[refresh copy] autorelease];
  }
  if (accountIdentifier != NULL) {
    *accountIdentifier = [[account copy] autorelease];
  }
  if (expiresAtMilliseconds != NULL) {
    *expiresAtMilliseconds = [expiry longLongValue];
  }
  return YES;
}

- (BOOL)saveChatGPTAccessToken:(NSString *)accessToken
                  refreshToken:(NSString *)refreshToken
             accountIdentifier:(NSString *)accountIdentifier
          expiresAtMilliseconds:(long long)expiresAtMilliseconds
{
  NSDictionary *credential;
  NSString *errorDescription;
  NSData *data;

  if (([accessToken length] == 0U) || ([refreshToken length] == 0U) ||
      ([accountIdentifier length] == 0U) ||
      (expiresAtMilliseconds <= 0LL)) {
    return NO;
  }

  credential = [NSDictionary dictionaryWithObjectsAndKeys:
    [NSNumber numberWithInteger:kStrappyChatGPTCredentialFormatVersion],
      kStrappyChatGPTFormatVersionKey,
    accessToken, kStrappyChatGPTAccessTokenKey,
    refreshToken, kStrappyChatGPTRefreshTokenKey,
    accountIdentifier, kStrappyChatGPTAccountIdentifierKey,
    [NSNumber numberWithLongLong:expiresAtMilliseconds],
      kStrappyChatGPTExpiresAtKey,
    nil];
  errorDescription = nil;
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
  data = [NSPropertyListSerialization
    dataFromPropertyList:credential
                  format:NSPropertyListBinaryFormat_v1_0
        errorDescription:&errorDescription];
#ifdef __clang__
#pragma clang diagnostic pop
#endif
  [errorDescription release];
  if ((data == nil) ||
      ([data length] > (NSUInteger)kStrappyChatGPTCredentialMaximumBytes) ||
      ![XPKeychain setGenericPasswordData:data
                                  service:kStrappyChatGPTKeychainService
                                  account:kStrappyChatGPTKeychainAccount]) {
    return NO;
  }

  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappyKeychainDidChangeNotification object:self];
  return YES;
}

- (BOOL)deleteChatGPTCredentials
{
  if (![XPKeychain
        deleteGenericPasswordForService:kStrappyChatGPTKeychainService
                                account:kStrappyChatGPTKeychainAccount]) {
    return NO;
  }
  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappyKeychainDidChangeNotification object:self];
  return YES;
}

- (void)reload
{
  [cachedAPIEndpoint_ release];
  cachedAPIEndpoint_ = nil;
  [cachedAPIToken_ release];
  cachedAPIToken_ = nil;
  loaded_ = NO;
}

- (void)dealloc
{
  [cachedAPIEndpoint_ release];
  [cachedAPIToken_ release];
  [super dealloc];
}

@end
