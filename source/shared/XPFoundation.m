#import "XPFoundation.h"
#import <objc/message.h>
#include <stdlib.h>

@implementation NSFileManager (XPFoundation)

- (BOOL)XP_createDirectoryAtPath:(NSString *)path
     withIntermediateDirectories:(BOOL)createIntermediates
                      attributes:(NSDictionary *)attributes
                           error:(NSError **)error
{
  SEL selector;
  NSMethodSignature *signature;
  NSInvocation *invocation;
  BOOL result;

  if (path == nil) {
    return NO;
  }

  selector =
    @selector(createDirectoryAtPath:withIntermediateDirectories:attributes:error:);
  if ([self respondsToSelector:selector]) {
    NSError **errorArgument;

    signature = [self methodSignatureForSelector:selector];
    if ((signature == nil) || ([signature numberOfArguments] != 6U)) {
      return NO;
    }
    invocation = [NSInvocation invocationWithMethodSignature:signature];
    [invocation setTarget:self];
    [invocation setSelector:selector];
    errorArgument = error;
    [invocation setArgument:&path atIndex:2];
    [invocation setArgument:&createIntermediates atIndex:3];
    [invocation setArgument:&attributes atIndex:4];
    [invocation setArgument:&errorArgument atIndex:5];
    [invocation invoke];
    result = NO;
    [invocation getReturnValue:&result];
    return result;
  }

  selector = @selector(createDirectoryAtPath:attributes:);
  if (![self respondsToSelector:selector]) {
    return NO;
  }
  signature = [self methodSignatureForSelector:selector];
  if ((signature == nil) || ([signature numberOfArguments] != 4U)) {
    return NO;
  }
  invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:self];
  [invocation setSelector:selector];
  [invocation setArgument:&path atIndex:2];
  [invocation setArgument:&attributes atIndex:3];
  [invocation invoke];
  result = NO;
  [invocation getReturnValue:&result];
  return result;
}

@end

@implementation NSString (XPFoundation)

- (long long)XP_longLongValue
{
  SEL selector;
  const char *value;

  selector = @selector(longLongValue);
  if ([self respondsToSelector:selector]) {
    return ((long long (*)(id, SEL))objc_msgSend)(self, selector);
  }

  value = [self UTF8String];
  return (value != NULL) ? strtoll(value, NULL, 10) : 0LL;
}

@end

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
