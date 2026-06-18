#import "XPFoundation.h"

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
