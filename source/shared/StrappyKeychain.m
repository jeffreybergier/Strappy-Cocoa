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
static NSString * const kStrappyOpenRouterKeychainService =
  @"com.altivecintelligence.Strappy.openrouter";
static NSString * const kStrappyOtherKeychainService =
  @"com.altivecintelligence.Strappy.other";
static NSString * const kStrappyAPIEndpointEnvironmentName = @"APIENDPOINT";
static NSString * const kStrappyAPITokenEnvironmentName = @"APITOKEN";

static NSString * const kStrappyChatGPTFormatVersionKey = @"format_version";
static NSString * const kStrappyChatGPTAccessTokenKey = @"access_token";
static NSString * const kStrappyChatGPTRefreshTokenKey = @"refresh_token";
static NSString * const kStrappyChatGPTAccountIdentifierKey = @"account_id";
static NSString * const kStrappyChatGPTExpiresAtKey = @"expires_at_ms";
static NSString * const kStrappyBearerTokenKey = @"bearer_token";
static NSString * const kStrappyCredentialKindKey = @"credential_kind";

enum {
  kStrappyChatGPTCredentialFormatVersion =
    STRAPPY_OPENAI_OAUTH_CREDENTIAL_FORMAT_VERSION,
  kStrappyBearerCredentialFormatVersion = 1,
  kStrappyChatGPTCredentialMaximumBytes = 2 * 1024 * 1024
};

static NSString *StrappyKeychainServiceForProvider(NSString *provider)
{
  if ([provider isEqualToString:@"openrouter"]) {
    return kStrappyOpenRouterKeychainService;
  }
  if ([provider isEqualToString:@"openai_chatgpt"]) {
    return kStrappyChatGPTKeychainService;
  }
  if ([provider isEqualToString:@"other"]) {
    return kStrappyOtherKeychainService;
  }
  return nil;
}

static NSData *StrappyKeychainPropertyListData(NSDictionary *propertyList)
{
  NSString *errorDescription;
  NSData *data;

  errorDescription = nil;
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
  data = [NSPropertyListSerialization
    dataFromPropertyList:propertyList
                  format:NSPropertyListBinaryFormat_v1_0
        errorDescription:&errorDescription];
#ifdef __clang__
#pragma clang diagnostic pop
#endif
  [errorDescription release];
  if ([data length] > (NSUInteger)kStrappyChatGPTCredentialMaximumBytes) {
    return nil;
  }
  return data;
}

static NSDictionary *StrappyKeychainPropertyListFromData(NSData *data)
{
  NSString *errorDescription;
  NSPropertyListFormat format;
  id propertyList;

  if (([data length] == 0U) ||
      ([data length] > (NSUInteger)kStrappyChatGPTCredentialMaximumBytes)) {
    return nil;
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
  return ([propertyList isKindOfClass:[NSDictionary class]] &&
          (format == NSPropertyListBinaryFormat_v1_0)) ? propertyList : nil;
}

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

- (id)init
{
  if ((self = [super init])) {
    credentialLocks_ = [[NSMutableDictionary alloc] init];
  }
  return self;
}

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

  if (([designatedOpenRouterAccountIdentifier_ length] > 0U) &&
      ![self saveBearerToken:apiToken
       forProviderIdentifier:@"openrouter"
   providerAccountIdentifier:designatedOpenRouterAccountIdentifier_]) {
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

- (NSObject *)credentialLockForProviderIdentifier:(NSString *)providerIdentifier
                         providerAccountIdentifier:
                           (NSString *)providerAccountIdentifier
{
  NSString *key;
  NSObject *lock;

  if ((StrappyKeychainServiceForProvider(providerIdentifier) == nil) ||
      ([providerAccountIdentifier length] == 0U)) {
    return nil;
  }
  key = [NSString stringWithFormat:@"%@\n%@", providerIdentifier,
                                      providerAccountIdentifier];
  @synchronized(credentialLocks_) {
    lock = [credentialLocks_ objectForKey:key];
    if (lock == nil) {
      lock = [[[NSObject alloc] init] autorelease];
      [credentialLocks_ setObject:lock forKey:key];
    }
  }
  return lock;
}

- (BOOL)hasBearerTokenForProviderIdentifier:(NSString *)providerIdentifier
                   providerAccountIdentifier:
                     (NSString *)providerAccountIdentifier
{
  return [self loadBearerToken:NULL
         forProviderIdentifier:providerIdentifier
     providerAccountIdentifier:providerAccountIdentifier];
}

- (BOOL)loadBearerToken:(NSString **)bearerToken
  forProviderIdentifier:(NSString *)providerIdentifier
providerAccountIdentifier:(NSString *)providerAccountIdentifier
{
  NSString *service;
  NSData *data;
  NSDictionary *credential;
  id version;
  id kind;
  id token;

  service = StrappyKeychainServiceForProvider(providerIdentifier);
  if ((service == nil) || ([providerAccountIdentifier length] == 0U)) {
    return NO;
  }
  data = nil;
  if (![XPKeychain findGenericPasswordDataForService:service
                                              account:providerAccountIdentifier
                                              outData:&data]) {
    return NO;
  }
  credential = StrappyKeychainPropertyListFromData(data);
  version = [credential objectForKey:kStrappyChatGPTFormatVersionKey];
  kind = [credential objectForKey:kStrappyCredentialKindKey];
  token = [credential objectForKey:kStrappyBearerTokenKey];
  if (![version isKindOfClass:[NSNumber class]] ||
      ([version integerValue] != kStrappyBearerCredentialFormatVersion) ||
      ![kind isEqual:@"api_token"] ||
      ![token isKindOfClass:[NSString class]] || ([token length] == 0U)) {
    return NO;
  }
  if (bearerToken != NULL) {
    *bearerToken = [[token copy] autorelease];
  }
  return YES;
}

- (BOOL)saveBearerToken:(NSString *)bearerToken
  forProviderIdentifier:(NSString *)providerIdentifier
providerAccountIdentifier:(NSString *)providerAccountIdentifier
{
  NSString *service;
  NSDictionary *credential;
  NSData *data;

  service = StrappyKeychainServiceForProvider(providerIdentifier);
  if ((service == nil) || ([providerAccountIdentifier length] == 0U) ||
      ([bearerToken length] == 0U) ||
      [providerIdentifier isEqualToString:@"openai_chatgpt"]) {
    return NO;
  }
  credential = [NSDictionary dictionaryWithObjectsAndKeys:
    [NSNumber numberWithInteger:kStrappyBearerCredentialFormatVersion],
      kStrappyChatGPTFormatVersionKey,
    @"api_token", kStrappyCredentialKindKey,
    bearerToken, kStrappyBearerTokenKey,
    nil];
  data = StrappyKeychainPropertyListData(credential);
  if ((data == nil) ||
      ![XPKeychain setGenericPasswordData:data service:service
                                  account:providerAccountIdentifier]) {
    return NO;
  }
  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappyKeychainDidChangeNotification object:self];
  return YES;
}

- (BOOL)deleteBearerTokenForProviderIdentifier:(NSString *)providerIdentifier
                      providerAccountIdentifier:
                        (NSString *)providerAccountIdentifier
{
  NSString *service;

  service = StrappyKeychainServiceForProvider(providerIdentifier);
  if ((service == nil) || ([providerAccountIdentifier length] == 0U) ||
      [providerIdentifier isEqualToString:@"openai_chatgpt"] ||
      ![XPKeychain deleteGenericPasswordForService:service
                                           account:providerAccountIdentifier]) {
    return NO;
  }
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
  return [self loadChatGPTAccessToken:accessToken
                         refreshToken:refreshToken
                    accountIdentifier:accountIdentifier
                 expiresAtMilliseconds:expiresAtMilliseconds
            providerAccountIdentifier:kStrappyChatGPTKeychainAccount];
}

- (BOOL)hasChatGPTCredentialsForProviderAccountIdentifier:
  (NSString *)providerAccountIdentifier
{
  return [self loadChatGPTAccessToken:NULL
                         refreshToken:NULL
                    accountIdentifier:NULL
                 expiresAtMilliseconds:NULL
            providerAccountIdentifier:providerAccountIdentifier];
}

- (BOOL)loadChatGPTAccessToken:(NSString **)accessToken
                  refreshToken:(NSString **)refreshToken
             accountIdentifier:(NSString **)accountIdentifier
          expiresAtMilliseconds:(long long *)expiresAtMilliseconds
     providerAccountIdentifier:(NSString *)providerAccountIdentifier
{
  NSData *data;
  NSDictionary *credential;
  id version;
  id access;
  id refresh;
  id account;
  id expiry;
  BOOL valid;

  data = nil;
  if ([providerAccountIdentifier length] == 0U) {
    return NO;
  }
  if (![XPKeychain
        findGenericPasswordDataForService:kStrappyChatGPTKeychainService
                                  account:providerAccountIdentifier
                                  outData:&data] ||
      ([data length] == 0U) ||
      ([data length] > (NSUInteger)kStrappyChatGPTCredentialMaximumBytes)) {
    return NO;
  }

  credential = StrappyKeychainPropertyListFromData(data);
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
  return [self saveChatGPTAccessToken:accessToken
                         refreshToken:refreshToken
                    accountIdentifier:accountIdentifier
                 expiresAtMilliseconds:expiresAtMilliseconds
            providerAccountIdentifier:kStrappyChatGPTKeychainAccount];
}

- (BOOL)saveChatGPTAccessToken:(NSString *)accessToken
                  refreshToken:(NSString *)refreshToken
             accountIdentifier:(NSString *)accountIdentifier
          expiresAtMilliseconds:(long long)expiresAtMilliseconds
     providerAccountIdentifier:(NSString *)providerAccountIdentifier
{
  NSDictionary *credential;
  NSData *data;

  if (([accessToken length] == 0U) || ([refreshToken length] == 0U) ||
      ([accountIdentifier length] == 0U) ||
      ([providerAccountIdentifier length] == 0U) ||
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
  data = StrappyKeychainPropertyListData(credential);
  if ((data == nil) ||
      ([data length] > (NSUInteger)kStrappyChatGPTCredentialMaximumBytes) ||
      ![XPKeychain setGenericPasswordData:data
                                  service:kStrappyChatGPTKeychainService
                                  account:providerAccountIdentifier]) {
    return NO;
  }

  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappyKeychainDidChangeNotification object:self];
  return YES;
}

- (BOOL)deleteChatGPTCredentials
{
  return [self deleteChatGPTCredentialsForProviderAccountIdentifier:
    kStrappyChatGPTKeychainAccount];
}

- (BOOL)deleteChatGPTCredentialsForProviderAccountIdentifier:
  (NSString *)providerAccountIdentifier
{
  if ([providerAccountIdentifier length] == 0U) {
    return NO;
  }
  if (![XPKeychain
        deleteGenericPasswordForService:kStrappyChatGPTKeychainService
                                account:providerAccountIdentifier]) {
    return NO;
  }
  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappyKeychainDidChangeNotification object:self];
  return YES;
}

- (BOOL)migrateLegacyOpenRouterCredentialToProviderAccountIdentifier:
          (NSString *)providerAccountIdentifier
                                                        endpoint:
          (NSString **)endpoint
{
  NSString *legacyEndpoint;
  NSString *legacyToken;

  if ([providerAccountIdentifier length] == 0U) {
    return NO;
  }
  [designatedOpenRouterAccountIdentifier_ release];
  designatedOpenRouterAccountIdentifier_ = [providerAccountIdentifier copy];
  if ([self hasBearerTokenForProviderIdentifier:@"openrouter"
                       providerAccountIdentifier:providerAccountIdentifier]) {
    return YES;
  }
  legacyEndpoint = nil;
  legacyToken = nil;
  if (![XPKeychain findInternetPasswordForAccount:kStrappyKeychainAccount
                                           outURL:&legacyEndpoint
                                      outPassword:&legacyToken] ||
      ([legacyToken length] == 0U)) {
    return YES;
  }
  if (![self saveBearerToken:legacyToken
       forProviderIdentifier:@"openrouter"
   providerAccountIdentifier:providerAccountIdentifier]) {
    return NO;
  }
  if (endpoint != NULL) {
    *endpoint = [[legacyEndpoint copy] autorelease];
  }
  return YES;
}

- (BOOL)migrateLegacyChatGPTCredentialToProviderAccountIdentifier:
  (NSString *)providerAccountIdentifier
{
  NSString *accessToken;
  NSString *refreshToken;
  NSString *accountIdentifier;
  long long expiresAtMilliseconds;

  if ([providerAccountIdentifier length] == 0U) {
    return NO;
  }
  if ([self hasChatGPTCredentialsForProviderAccountIdentifier:
        providerAccountIdentifier]) {
    return YES;
  }
  accessToken = nil;
  refreshToken = nil;
  accountIdentifier = nil;
  expiresAtMilliseconds = 0LL;
  if (![self loadChatGPTAccessToken:&accessToken
                       refreshToken:&refreshToken
                  accountIdentifier:&accountIdentifier
               expiresAtMilliseconds:&expiresAtMilliseconds]) {
    return YES;
  }
  if (![self saveChatGPTAccessToken:accessToken
                       refreshToken:refreshToken
                  accountIdentifier:accountIdentifier
               expiresAtMilliseconds:expiresAtMilliseconds
          providerAccountIdentifier:providerAccountIdentifier]) {
    return NO;
  }
  return [XPKeychain
    deleteGenericPasswordForService:kStrappyChatGPTKeychainService
                             account:kStrappyChatGPTKeychainAccount];
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
  [credentialLocks_ release];
  [designatedOpenRouterAccountIdentifier_ release];
  [super dealloc];
}

@end
