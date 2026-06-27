#import "XPFoundation.h"

#import <Security/Security.h>
#import <TargetConditionals.h>

#include <string.h>

#ifndef errSecSuccess
#define errSecSuccess 0
#endif

@implementation NSNumber (XPFoundation)

+ (NSNumber *)XP_numberWithInteger:(XPInteger)value
{
  SEL selector;
  NSMethodSignature *signature;
  NSInvocation *invocation;
  NSNumber *result;

  selector = @selector(numberWithInteger:);
  if ([(id)self respondsToSelector:selector]) {
    signature = [(id)self methodSignatureForSelector:selector];
    if (signature != nil) {
      invocation = [NSInvocation invocationWithMethodSignature:signature];
      [invocation setTarget:self];
      [invocation setSelector:selector];
      [invocation setArgument:&value atIndex:2];
      [invocation invoke];
      result = nil;
      [invocation getReturnValue:&result];
      return result;
    }
  }

  return [NSNumber numberWithLong:(long)value];
}

+ (NSNumber *)XP_numberWithUnsignedInteger:(XPUInteger)value
{
  SEL selector;
  NSMethodSignature *signature;
  NSInvocation *invocation;
  NSNumber *result;

  selector = @selector(numberWithUnsignedInteger:);
  if ([(id)self respondsToSelector:selector]) {
    signature = [(id)self methodSignatureForSelector:selector];
    if (signature != nil) {
      invocation = [NSInvocation invocationWithMethodSignature:signature];
      [invocation setTarget:self];
      [invocation setSelector:selector];
      [invocation setArgument:&value atIndex:2];
      [invocation invoke];
      result = nil;
      [invocation getReturnValue:&result];
      return result;
    }
  }

  return [NSNumber numberWithUnsignedLong:(unsigned long)value];
}

- (XPInteger)XP_integerValue
{
  SEL selector;
  NSMethodSignature *signature;
  NSInvocation *invocation;
  XPInteger result;

  selector = @selector(integerValue);
  if ([self respondsToSelector:selector]) {
    signature = [self methodSignatureForSelector:selector];
    if (signature != nil) {
      invocation = [NSInvocation invocationWithMethodSignature:signature];
      [invocation setTarget:self];
      [invocation setSelector:selector];
      [invocation invoke];
      result = 0;
      [invocation getReturnValue:&result];
      return result;
    }
  }

  return (XPInteger)[self longValue];
}

- (XPUInteger)XP_unsignedIntegerValue
{
  SEL selector;
  NSMethodSignature *signature;
  NSInvocation *invocation;
  XPUInteger result;

  selector = @selector(unsignedIntegerValue);
  if ([self respondsToSelector:selector]) {
    signature = [self methodSignatureForSelector:selector];
    if (signature != nil) {
      invocation = [NSInvocation invocationWithMethodSignature:signature];
      [invocation setTarget:self];
      [invocation setSelector:selector];
      [invocation invoke];
      result = 0U;
      [invocation getReturnValue:&result];
      return result;
    }
  }

  return (XPUInteger)[self unsignedLongValue];
}

@end

#if TARGET_OS_IPHONE

static NSString *xpkc_iosSchemeForProto(NSString *proto)
{
  return ([proto isEqual:(id)kSecAttrProtocolHTTP]) ? @"http" : @"https";
}

static NSString *xpkc_iosProtoForScheme(NSString *scheme)
{
  return [scheme isEqualToString:@"http"]
    ? (NSString *)kSecAttrProtocolHTTP
    : (NSString *)kSecAttrProtocolHTTPS;
}

@implementation XPKeychain

+ (BOOL)findInternetPasswordForAccount:(NSString *)account
                                outURL:(NSString **)outURL
                           outPassword:(NSString **)outPassword
{
  NSDictionary *query;
  CFTypeRef result;
  OSStatus status;
  NSDictionary *attributes;
  NSString *server;
  NSString *path;
  NSNumber *port;
  NSString *proto;
  NSData *passwordData;

  if ([account length] == 0U) {
    return NO;
  }

  query = [NSDictionary dictionaryWithObjectsAndKeys:
    (id)kSecClassInternetPassword, (id)kSecClass,
    account,                       (id)kSecAttrAccount,
    (id)kCFBooleanTrue,            (id)kSecReturnAttributes,
    (id)kCFBooleanTrue,            (id)kSecReturnData,
    (id)kSecMatchLimitOne,         (id)kSecMatchLimit, nil];
  result = NULL;
  status = SecItemCopyMatching((CFDictionaryRef)query, &result);
  if ((status != errSecSuccess) || (result == NULL)) {
    if (status != errSecItemNotFound) {
      NSLog(@"XPKeychain.findInternetPasswordForAccount status=%d account=%@",
            (int)status,
            account);
    }
    return NO;
  }

  attributes = (NSDictionary *)result;
  server = [attributes objectForKey:(id)kSecAttrServer];
  path = [attributes objectForKey:(id)kSecAttrPath];
  port = [attributes objectForKey:(id)kSecAttrPort];
  proto = [attributes objectForKey:(id)kSecAttrProtocol];
  passwordData = [attributes objectForKey:(id)kSecValueData];

  if (outURL != NULL) {
    NSMutableString *url;

    url = [NSMutableString stringWithFormat:@"%@://%@",
      xpkc_iosSchemeForProto(proto),
      server ? server : @""];
    if ([port intValue] > 0) {
      [url appendFormat:@":%d", [port intValue]];
    }
    if ([path length] > 0U) {
      [url appendString:path];
    }
    *outURL = url;
  }
  if (outPassword != NULL) {
    *outPassword = [[[NSString alloc] initWithData:passwordData
                                          encoding:NSUTF8StringEncoding]
      autorelease];
  }

  CFRelease(result);
  return YES;
}

+ (BOOL)deleteInternetPasswordForAccount:(NSString *)account
{
  NSDictionary *query;
  OSStatus status;

  if ([account length] == 0U) {
    return NO;
  }

  query = [NSDictionary dictionaryWithObjectsAndKeys:
    (id)kSecClassInternetPassword, (id)kSecClass,
    account,                       (id)kSecAttrAccount, nil];
  status = SecItemDelete((CFDictionaryRef)query);
  return ((status == errSecSuccess) || (status == errSecItemNotFound)) ? YES : NO;
}

+ (BOOL)setInternetPasswordForAccount:(NSString *)account
                                  URL:(NSString *)url
                             password:(NSString *)password
{
  NSURL *parsed;
  NSMutableDictionary *query;
  OSStatus status;

  if (([account length] == 0U) || ([url length] == 0U) || (password == nil)) {
    return NO;
  }

  parsed = [NSURL URLWithString:url];
  if ((parsed == nil) || ([[parsed host] length] == 0U)) {
    NSLog(@"XPKeychain.setInternetPasswordForAccount invalid URL: %@", url);
    return NO;
  }

  [self deleteInternetPasswordForAccount:account];

  query = [NSMutableDictionary dictionary];
  [query setObject:(id)kSecClassInternetPassword forKey:(id)kSecClass];
  [query setObject:account forKey:(id)kSecAttrAccount];
  [query setObject:xpkc_iosProtoForScheme([parsed scheme])
            forKey:(id)kSecAttrProtocol];
  [query setObject:[parsed host] forKey:(id)kSecAttrServer];
  if ([[parsed path] length] > 0U) {
    [query setObject:[parsed path] forKey:(id)kSecAttrPath];
  }
  if ([parsed port] != nil) {
    [query setObject:[parsed port] forKey:(id)kSecAttrPort];
  }
  [query setObject:[password dataUsingEncoding:NSUTF8StringEncoding]
            forKey:(id)kSecValueData];

  status = SecItemAdd((CFDictionaryRef)query, NULL);
  if (status != errSecSuccess) {
    NSLog(@"XPKeychain.setInternetPasswordForAccount SecItemAdd status=%d account=%@",
          (int)status,
          account);
    return NO;
  }
  return YES;
}

@end

#else

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

static SecProtocolType xpkc_macProtoForScheme(NSString *scheme)
{
  if ([scheme isEqualToString:@"http"]) {
    return kSecProtocolTypeHTTP;
  }
  return kSecProtocolTypeHTTPS;
}

static NSString *xpkc_macSchemeForProto(SecProtocolType proto)
{
  return (proto == kSecProtocolTypeHTTP) ? @"http" : @"https";
}

static NSString *xpkc_macAttrString(SecKeychainAttribute *attribute)
{
  if ((attribute == NULL) ||
      (attribute->data == NULL) ||
      (attribute->length == 0U)) {
    return @"";
  }
  return [[[NSString alloc] initWithBytes:attribute->data
                                   length:attribute->length
                                 encoding:NSUTF8StringEncoding] autorelease];
}

static NSString *xpkc_macReadURLForItem(SecKeychainItemRef item)
{
  UInt32 tags[4];
  UInt32 formats[4];
  SecKeychainAttributeInfo info;
  SecKeychainAttributeList *attributes;
  NSString *server;
  NSString *path;
  UInt16 port;
  SecProtocolType proto;
  UInt32 index;

  tags[0] = kSecServerItemAttr;
  tags[1] = kSecPathItemAttr;
  tags[2] = kSecPortItemAttr;
  tags[3] = kSecProtocolItemAttr;
  formats[0] = CSSM_DB_ATTRIBUTE_FORMAT_STRING;
  formats[1] = CSSM_DB_ATTRIBUTE_FORMAT_STRING;
  formats[2] = CSSM_DB_ATTRIBUTE_FORMAT_UINT32;
  formats[3] = CSSM_DB_ATTRIBUTE_FORMAT_UINT32;
  info.count = 4;
  info.tag = tags;
  info.format = formats;

  attributes = NULL;
  server = @"";
  path = @"";
  port = 0;
  proto = kSecProtocolTypeHTTPS;

  if (SecKeychainItemCopyAttributesAndData(item,
                                           &info,
                                           NULL,
                                           &attributes,
                                           NULL,
                                           NULL) != errSecSuccess) {
    return nil;
  }

  for (index = 0U; index < attributes->count; index++) {
    SecKeychainAttribute *attribute;

    attribute = &attributes->attr[index];
    if (attribute->tag == kSecServerItemAttr) {
      server = xpkc_macAttrString(attribute);
    } else if (attribute->tag == kSecPathItemAttr) {
      path = xpkc_macAttrString(attribute);
    } else if ((attribute->tag == kSecPortItemAttr) &&
               (attribute->length >= sizeof(UInt32))) {
      port = (UInt16)(*(UInt32 *)attribute->data);
    } else if ((attribute->tag == kSecProtocolItemAttr) &&
               (attribute->length >= sizeof(SecProtocolType))) {
      proto = *(SecProtocolType *)attribute->data;
    }
  }
  SecKeychainItemFreeAttributesAndData(attributes, NULL);

  {
    NSMutableString *url;

    url = [NSMutableString stringWithFormat:@"%@://%@",
      xpkc_macSchemeForProto(proto),
      server];
    if (port > 0U) {
      [url appendFormat:@":%d", (int)port];
    }
    if ([path length] > 0U) {
      [url appendString:path];
    }
    return url;
  }
}

@implementation XPKeychain

+ (BOOL)findInternetPasswordForAccount:(NSString *)account
                                outURL:(NSString **)outURL
                           outPassword:(NSString **)outPassword
{
  const char *accountCString;
  UInt32 accountLength;
  UInt32 passwordLength;
  void *passwordData;
  SecKeychainItemRef item;
  OSStatus status;

  if ([account length] == 0U) {
    return NO;
  }

  accountCString = [account UTF8String];
  accountLength = (UInt32)strlen(accountCString);
  passwordLength = 0U;
  passwordData = NULL;
  item = NULL;
  status = SecKeychainFindInternetPassword(NULL,
                                           0,
                                           NULL,
                                           0,
                                           NULL,
                                           accountLength,
                                           accountCString,
                                           0,
                                           NULL,
                                           0,
                                           0,
                                           0,
                                           &passwordLength,
                                           &passwordData,
                                           &item);
  if (status != errSecSuccess) {
    if (status != errSecItemNotFound) {
      NSLog(@"XPKeychain.findInternetPasswordForAccount status=%d account=%@",
            (int)status,
            account);
    }
    return NO;
  }

  if (outPassword != NULL) {
    *outPassword = [[[NSString alloc] initWithBytes:passwordData
                                             length:passwordLength
                                           encoding:NSUTF8StringEncoding]
      autorelease];
  }
  SecKeychainItemFreeContent(NULL, passwordData);

  if ((outURL != NULL) && (item != NULL)) {
    *outURL = xpkc_macReadURLForItem(item);
  }
  if (item != NULL) {
    CFRelease(item);
  }
  return YES;
}

+ (BOOL)deleteInternetPasswordForAccount:(NSString *)account
{
  const char *accountCString;
  UInt32 accountLength;
  SecKeychainItemRef item;
  OSStatus status;

  if ([account length] == 0U) {
    return NO;
  }

  accountCString = [account UTF8String];
  accountLength = (UInt32)strlen(accountCString);
  item = NULL;
  status = SecKeychainFindInternetPassword(NULL,
                                           0,
                                           NULL,
                                           0,
                                           NULL,
                                           accountLength,
                                           accountCString,
                                           0,
                                           NULL,
                                           0,
                                           0,
                                           0,
                                           NULL,
                                           NULL,
                                           &item);
  if (status == errSecItemNotFound) {
    return YES;
  }
  if ((status != errSecSuccess) || (item == NULL)) {
    NSLog(@"XPKeychain.deleteInternetPasswordForAccount find status=%d account=%@",
          (int)status,
          account);
    return NO;
  }

  status = SecKeychainItemDelete(item);
  CFRelease(item);
  if (status != errSecSuccess) {
    NSLog(@"XPKeychain.deleteInternetPasswordForAccount delete status=%d account=%@",
          (int)status,
          account);
    return NO;
  }
  return YES;
}

+ (BOOL)setInternetPasswordForAccount:(NSString *)account
                                  URL:(NSString *)url
                             password:(NSString *)password
{
  NSURL *parsed;
  const char *accountCString;
  const char *serverCString;
  const char *pathCString;
  const char *passwordCString;
  UInt32 accountLength;
  UInt32 serverLength;
  UInt32 pathLength;
  UInt32 passwordLength;
  UInt16 port;
  SecProtocolType proto;
  OSStatus status;

  if (([account length] == 0U) || ([url length] == 0U) || (password == nil)) {
    return NO;
  }

  parsed = [NSURL URLWithString:url];
  if ((parsed == nil) || ([[parsed host] length] == 0U)) {
    NSLog(@"XPKeychain.setInternetPasswordForAccount invalid URL: %@", url);
    return NO;
  }

  [self deleteInternetPasswordForAccount:account];

  accountCString = [account UTF8String];
  serverCString = [[parsed host] UTF8String];
  pathCString = ([[parsed path] length] > 0U) ? [[parsed path] UTF8String] : "";
  passwordCString = [password UTF8String];
  accountLength = (UInt32)strlen(accountCString);
  serverLength = (UInt32)strlen(serverCString);
  pathLength = (UInt32)strlen(pathCString);
  passwordLength = (UInt32)strlen(passwordCString);
  port = [[parsed port] unsignedShortValue];
  proto = xpkc_macProtoForScheme([parsed scheme]);

  status = SecKeychainAddInternetPassword(NULL,
                                          serverLength,
                                          serverCString,
                                          0,
                                          NULL,
                                          accountLength,
                                          accountCString,
                                          pathLength,
                                          pathCString,
                                          port,
                                          proto,
                                          0,
                                          passwordLength,
                                          passwordCString,
                                          NULL);
  if (status != errSecSuccess) {
    NSLog(@"XPKeychain.setInternetPasswordForAccount SecKeychainAddInternetPassword status=%d account=%@",
          (int)status,
          account);
    return NO;
  }
  return YES;
}

@end

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#endif
