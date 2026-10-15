#import "XPKeychain.h"
#import <AltivecCocoa/AIKeychainStore.h>

/* Keep credential schema and legacy first-match behavior in the app facade. */
@implementation XPKeychain

+ (BOOL)findInternetPasswordForAccount:(NSString *)account
                              outURL:(NSString **)outURL
                         outPassword:(NSString **)outPassword;
{
  AIKeychainStore *store = [AIKeychainStore internetPasswordStoreForKey:account
                                                              error:NULL];
  NSString *password;
  if (outURL) *outURL = nil;
  if (outPassword) *outPassword = nil;
  if (!store) return NO;
  password = [store stringForKey:account error:NULL];
  if (!password) return NO;
  if (outURL) *outURL = [store serverURLString];
  if (outPassword) *outPassword = password;
  return YES;
}

+ (BOOL)deleteInternetPasswordForAccount:(NSString *)account;
{
  unsigned int attempt;
  for (attempt = 0; attempt < 128; attempt++) {
    NSError *error = nil;
    AIKeychainStore *store = [AIKeychainStore internetPasswordStoreForKey:account
                                                                error:&error];
    if (!store) return [error code] == errSecItemNotFound;
    if (![store removeItemForKey:account error:&error] &&
        [error code] != errSecItemNotFound) return NO;
#if !TARGET_OS_IPHONE
    /* Preserve the old Mac first-match policy; iOS removed all account matches. */
    return YES;
#endif
  }
  return NO; /* Bound concurrent delete/recreate races. */
}

+ (BOOL)setInternetPasswordForAccount:(NSString *)account
                                URL:(NSString *)url
                           password:(NSString *)password;
{
  NSURL *parsed;
  AIKeychainAuthentication authentication;
  AIKeychainStore *store;
  if (![account length] || ![url length] || !password) return NO;
  parsed = [NSURL URLWithString:url];
  if (![[parsed host] length]) return NO;
#if TARGET_OS_IPHONE
  authentication = AIKeychainAuthenticationDefault;
#else
  authentication = AIKeychainAuthenticationUnspecified;
#endif
  store = [[[AIKeychainStore alloc] initWithServer:parsed
      protocolType:[[parsed scheme] isEqualToString:@"http"] ?
          AIKeychainProtocolHTTP : AIKeychainProtocolHTTPS
      authenticationType:authentication securityDomain:nil] autorelease];
  if (!store) return NO;
  /* URL replacement is the existing app policy, not a library migration.
   * Keep exact-scope updates in place whenever the account already lives here. */
  if ([store containsKey:account error:NULL])
    return [store setString:password forKey:account error:NULL];
  if (![self deleteInternetPasswordForAccount:account]) return NO;
  return [store setString:password forKey:account error:NULL];
}

+ (BOOL)configureBackgroundAccess;
{
#if TARGET_OS_IPHONE
  return YES;
#else
  return [AIKeychainStore disableUserInteractionWithError:NULL];
#endif
}

+ (BOOL)findGenericPasswordDataForService:(NSString *)service
                                account:(NSString *)account
                                outData:(NSData **)outData;
{
  AIKeychainStore *store = [[[AIKeychainStore alloc]
      initWithService:service] autorelease];
  NSData *data = [store dataForKey:account error:NULL];
  if (outData) *outData = data;
  return data != nil;
}

+ (NSArray *)genericPasswordAccountsForService:(NSString *)service;
{
  AIKeychainStore *store = [[[AIKeychainStore alloc]
      initWithService:service] autorelease];
  NSArray *keys = [store allKeysWithError:NULL];
  return keys ? keys : [NSArray array];
}

+ (BOOL)deleteGenericPasswordForService:(NSString *)service
                              account:(NSString *)account;
{
  NSError *error = nil;
  AIKeychainStore *store = [[[AIKeychainStore alloc]
      initWithService:service] autorelease];
  return [store removeItemForKey:account error:&error] ||
      [error code] == errSecItemNotFound;
}

+ (BOOL)setGenericPasswordData:(NSData *)data
                      service:(NSString *)service
                      account:(NSString *)account;
{
  AIKeychainStore *store = [[[AIKeychainStore alloc]
      initWithService:service] autorelease];
#if TARGET_OS_IPHONE
  [store setAccessibility:AIKeychainAccessibilityWhenUnlockedThisDeviceOnly];
#endif
  return [store setData:data forKey:account error:NULL];
}
@end
