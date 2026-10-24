#import "XPKeychain.h"
#import <AltivecCocoa/AIKeychainStore.h>

/* Log only fixed operation names and numeric status, never NSError text,
 * identities, URLs, credential bytes, or tokens. Missing items are logged too
 * so a lookup miss can be distinguished from denied access during diagnosis. */
static void XPKeychainLogFailure(NSString *operation, NSError *error)
{
  NSLog(@"[StrappyKeychain] operation=%@ failed status=%ld error_present=%d",
        operation, (long)[error code], error != nil);
}

/* Keep credential schema and legacy first-match behavior in the app facade. */
@implementation XPKeychain

+ (BOOL)findInternetPasswordForAccount:(NSString *)account
                              outURL:(NSString **)outURL
                         outPassword:(NSString **)outPassword;
{
  NSError *error = nil;
  AIKeychainStore *store = [AIKeychainStore internetPasswordStoreForKey:account
                                                              error:&error];
  NSString *password;
  if (outURL) *outURL = nil;
  if (outPassword) *outPassword = nil;
  if (!store) {
    XPKeychainLogFailure(@"internet-discover", error);
    return NO;
  }
  password = [store stringForKey:account error:&error];
  if (!password) {
    XPKeychainLogFailure(@"internet-read", error);
    return NO;
  }
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
    if (!store) {
      XPKeychainLogFailure(@"internet-delete-discover", error);
      return [error code] == errSecItemNotFound;
    }
    if (![store removeItemForKey:account error:&error]) {
      XPKeychainLogFailure(@"internet-delete", error);
      if ([error code] != errSecItemNotFound) return NO;
    }
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
  NSError *error = nil;
  BOOL exists;
  BOOL ok;
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
  exists = [store containsKey:account error:&error];
  if (!exists && error) XPKeychainLogFailure(@"internet-contains", error);
  if (!exists && ![self deleteInternetPasswordForAccount:account]) return NO;
  ok = [store setString:password forKey:account error:&error];
  if (!ok) XPKeychainLogFailure(@"internet-write", error);
  return ok;
}

+ (BOOL)configureBackgroundAccess;
{
#if TARGET_OS_IPHONE
  return YES;
#else
  NSError *error = nil;
  BOOL ok = [AIKeychainStore disableUserInteractionWithError:&error];
  if (!ok) XPKeychainLogFailure(@"disable-interaction", error);
  return ok;
#endif
}

+ (BOOL)findGenericPasswordDataForService:(NSString *)service
                                account:(NSString *)account
                                outData:(NSData **)outData;
{
  AIKeychainStore *store = [[[AIKeychainStore alloc]
      initWithService:service] autorelease];
  NSError *error = nil;
  NSData *data = [store dataForKey:account error:&error];
  if (!data) XPKeychainLogFailure(@"generic-read", error);
  if (outData) *outData = data;
  return data != nil;
}

+ (NSArray *)genericPasswordAccountsForService:(NSString *)service;
{
  AIKeychainStore *store = [[[AIKeychainStore alloc]
      initWithService:service] autorelease];
  NSError *error = nil;
  NSArray *keys = [store allKeysWithError:&error];
  if (!keys) XPKeychainLogFailure(@"generic-enumerate", error);
  return keys ? keys : [NSArray array];
}

+ (BOOL)deleteGenericPasswordForService:(NSString *)service
                              account:(NSString *)account;
{
  NSError *error = nil;
  AIKeychainStore *store = [[[AIKeychainStore alloc]
      initWithService:service] autorelease];
  BOOL ok = [store removeItemForKey:account error:&error];
  if (!ok) XPKeychainLogFailure(@"generic-delete", error);
  return ok || [error code] == errSecItemNotFound;
}

+ (BOOL)setGenericPasswordData:(NSData *)data
                      service:(NSString *)service
                      account:(NSString *)account;
{
  AIKeychainStore *store = [[[AIKeychainStore alloc]
      initWithService:service] autorelease];
  NSError *error = nil;
  BOOL ok;
#if TARGET_OS_IPHONE
  [store setAccessibility:AIKeychainAccessibilityWhenUnlockedThisDeviceOnly];
#endif
  ok = [store setData:data forKey:account error:&error];
  if (!ok) {
    XPKeychainLogFailure(@"generic-write", error);
  } else {
    /* Diagnostic only: preserve write semantics even if read-back fails. */
    NSData *readBack = [store dataForKey:account error:&error];
    if (!readBack) XPKeychainLogFailure(@"generic-write-readback", error);
    else NSLog(@"[StrappyKeychain] operation=generic-write-readback "
               @"readable=1 matches=%d", [readBack isEqualToData:data]);
  }
  return ok;
}
@end
