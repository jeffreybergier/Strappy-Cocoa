#import "strappy_keychain.h"

#import "XPFoundation.h"
#include "strappy_config.h"
#include "strappy_core.h"

#include <stdlib.h>

NSString * const StrappyKeychainDidChangeNotification =
  @"StrappyKeychainDidChangeNotification";

static NSString * const kStrappyKeychainAccount = @"apitoken";
static NSString * const kStrappyAPIEndpointEnvironmentName = @"APIENDPOINT";
static NSString * const kStrappyAPITokenEnvironmentName = @"APITOKEN";

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

char *strappy_keychain_copy_api_endpoint(void)
{
  NSAutoreleasePool *pool;
  NSString *apiEndpoint;
  char *copy;

  pool = [[NSAutoreleasePool alloc] init];
  apiEndpoint = [[StrappyKeychain sharedKeychain] apiEndpoint];
  copy = NULL;
  if ([apiEndpoint length] > 0U) {
    copy = strappy_string_duplicate([apiEndpoint UTF8String]);
  }
  [pool release];
  return copy;
}

char *strappy_keychain_copy_api_token(void)
{
  NSAutoreleasePool *pool;
  NSString *apiToken;
  char *copy;

  pool = [[NSAutoreleasePool alloc] init];
  apiToken = [[StrappyKeychain sharedKeychain] apiToken];
  copy = NULL;
  if ([apiToken length] > 0U) {
    copy = strappy_string_duplicate([apiToken UTF8String]);
  }
  [pool release];
  return copy;
}

int strappy_keychain_save_api_credentials(const char *api_endpoint,
                                          const char *api_token)
{
  NSAutoreleasePool *pool;
  NSString *apiEndpoint;
  NSString *apiToken;
  int ok;

  if ((api_endpoint == NULL) || (api_endpoint[0] == '\0') ||
      (api_token == NULL) || (api_token[0] == '\0')) {
    return 0;
  }

  pool = [[NSAutoreleasePool alloc] init];
  apiEndpoint = [NSString stringWithUTF8String:api_endpoint];
  apiToken = [NSString stringWithUTF8String:api_token];
  ok = [[StrappyKeychain sharedKeychain] saveAPIEndpoint:apiEndpoint
                                                   token:apiToken] ? 1 : 0;
  [pool release];
  return ok;
}
