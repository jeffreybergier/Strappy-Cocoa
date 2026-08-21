#import "StrappyKeychain.h"

#import "XPFoundation.h"
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
static NSString * const kStrappyDisplayNameKey = @"display_name";

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

static NSMutableDictionary *StrappyKeychainMutableCredential(
  NSString *service,
  NSString *providerAccountIdentifier)
{
  NSData *data;
  NSDictionary *credential;

  data = nil;
  if (![XPKeychain findGenericPasswordDataForService:service
                                              account:providerAccountIdentifier
                                              outData:&data]) {
    return [NSMutableDictionary dictionary];
  }
  credential = StrappyKeychainPropertyListFromData(data);
  return [credential isKindOfClass:[NSDictionary class]] ?
    [[credential mutableCopy] autorelease] : nil;
}

static NSString *StrappyKeychainNormalizedBearerToken(NSString *token)
{
  NSMutableString *normalized;
  NSUInteger index;

  if (![token isKindOfClass:[NSString class]]) {
    return @"";
  }
  normalized = [NSMutableString stringWithCapacity:[token length]];
  for (index = 0U; index < [token length]; index++) {
    unichar character;

    character = [token characterAtIndex:index];
    if ((character < 0x20U) || (character == 0x7fU)) {
      continue;
    }
    [normalized appendFormat:@"%C", character];
  }
  return [normalized stringByTrimmingCharactersInSet:
    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
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
  NSString *normalizedToken;

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
  normalizedToken = StrappyKeychainNormalizedBearerToken(token);
  if (![version isKindOfClass:[NSNumber class]] ||
      ([version XP_integerValue] !=
        kStrappyBearerCredentialFormatVersion) ||
      ![kind isEqual:@"api_token"] ||
      ([normalizedToken length] == 0U)) {
    return NO;
  }
  if (bearerToken != NULL) {
    *bearerToken = [[normalizedToken copy] autorelease];
  }
  return YES;
}

- (BOOL)saveBearerToken:(NSString *)bearerToken
  forProviderIdentifier:(NSString *)providerIdentifier
providerAccountIdentifier:(NSString *)providerAccountIdentifier
{
  NSString *service;
  NSString *normalizedToken;
  NSMutableDictionary *credential;
  NSData *data;

  service = StrappyKeychainServiceForProvider(providerIdentifier);
  normalizedToken = StrappyKeychainNormalizedBearerToken(bearerToken);
  if ((service == nil) || ([providerAccountIdentifier length] == 0U) ||
      ([normalizedToken length] == 0U) ||
      [providerIdentifier isEqualToString:@"openai_chatgpt"]) {
    return NO;
  }
  credential = StrappyKeychainMutableCredential(
    service, providerAccountIdentifier);
  if (credential == nil) {
    return NO;
  }
  [credential setObject:[NSNumber XP_numberWithInteger:
    (XPInteger)kStrappyBearerCredentialFormatVersion]
                 forKey:kStrappyChatGPTFormatVersionKey];
  [credential setObject:@"api_token" forKey:kStrappyCredentialKindKey];
  [credential setObject:normalizedToken forKey:kStrappyBearerTokenKey];
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

- (NSArray *)credentialProviderAccountIdentifiersForProviderIdentifier:
  (NSString *)providerIdentifier
{
  NSString *service;
  NSArray *candidates;
  NSMutableArray *identifiers;
  NSUInteger index;

  service = StrappyKeychainServiceForProvider(providerIdentifier);
  if (service == nil) {
    return [NSArray array];
  }
  candidates = [XPKeychain genericPasswordAccountsForService:service];
  identifiers = [NSMutableArray array];
  for (index = 0U; index < [candidates count]; index++) {
    NSString *identifier;
    BOOL valid;

    identifier = [candidates objectAtIndex:index];
    if (![identifier isKindOfClass:[NSString class]] ||
        ![identifier hasPrefix:@"acct_"]) {
      continue;
    }
    if ([providerIdentifier isEqualToString:@"openai_chatgpt"]) {
      valid = [self hasChatGPTCredentialsForProviderAccountIdentifier:
        identifier];
    } else {
      valid = [self hasBearerTokenForProviderIdentifier:providerIdentifier
                               providerAccountIdentifier:identifier];
    }
    if (valid && ![identifiers containsObject:identifier]) {
      [identifiers addObject:identifier];
    }
  }
  return identifiers;
}

- (BOOL)loadDisplayName:(NSString **)displayName
  forProviderIdentifier:(NSString *)providerIdentifier
providerAccountIdentifier:(NSString *)providerAccountIdentifier
{
  NSString *service;
  NSData *data;
  NSDictionary *credential;
  id storedName;

  if (displayName != NULL) {
    *displayName = nil;
  }
  service = StrappyKeychainServiceForProvider(providerIdentifier);
  data = nil;
  if ((service == nil) || ([providerAccountIdentifier length] == 0U) ||
      ![XPKeychain findGenericPasswordDataForService:service
                                              account:providerAccountIdentifier
                                              outData:&data]) {
    return NO;
  }
  credential = StrappyKeychainPropertyListFromData(data);
  storedName = [credential objectForKey:kStrappyDisplayNameKey];
  if (![storedName isKindOfClass:[NSString class]] ||
      ([storedName length] == 0U)) {
    return NO;
  }
  if (displayName != NULL) {
    *displayName = [[storedName copy] autorelease];
  }
  return YES;
}

- (BOOL)saveDisplayName:(NSString *)displayName
  forProviderIdentifier:(NSString *)providerIdentifier
providerAccountIdentifier:(NSString *)providerAccountIdentifier
{
  NSString *service;
  NSMutableDictionary *credential;
  NSData *data;

  service = StrappyKeychainServiceForProvider(providerIdentifier);
  if ((service == nil) || ([providerAccountIdentifier length] == 0U) ||
      ([displayName length] == 0U)) {
    return NO;
  }
  credential = StrappyKeychainMutableCredential(
    service, providerAccountIdentifier);
  if (credential == nil) {
    return NO;
  }
  [credential setObject:displayName forKey:kStrappyDisplayNameKey];
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
    ([version XP_integerValue] ==
      kStrappyChatGPTCredentialFormatVersion) &&
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
  NSMutableDictionary *credential;
  NSData *data;

  if (([accessToken length] == 0U) || ([refreshToken length] == 0U) ||
      ([accountIdentifier length] == 0U) ||
      ([providerAccountIdentifier length] == 0U) ||
      (expiresAtMilliseconds <= 0LL)) {
    return NO;
  }

  credential = StrappyKeychainMutableCredential(
    kStrappyChatGPTKeychainService, providerAccountIdentifier);
  if (credential == nil) {
    return NO;
  }
  [credential setObject:[NSNumber XP_numberWithInteger:
    (XPInteger)kStrappyChatGPTCredentialFormatVersion]
                 forKey:kStrappyChatGPTFormatVersionKey];
  [credential setObject:accessToken forKey:kStrappyChatGPTAccessTokenKey];
  [credential setObject:refreshToken forKey:kStrappyChatGPTRefreshTokenKey];
  [credential setObject:accountIdentifier
                 forKey:kStrappyChatGPTAccountIdentifierKey];
  [credential setObject:[NSNumber numberWithLongLong:expiresAtMilliseconds]
                 forKey:kStrappyChatGPTExpiresAtKey];
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
    if (![XPKeychain deleteInternetPasswordForAccount:
          kStrappyKeychainAccount]) {
      return NO;
    }
    [self reload];
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
  if (![XPKeychain deleteInternetPasswordForAccount:
        kStrappyKeychainAccount]) {
    return NO;
  }
  [self reload];
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
    return [XPKeychain
      deleteGenericPasswordForService:kStrappyChatGPTKeychainService
                               account:kStrappyChatGPTKeychainAccount];
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
