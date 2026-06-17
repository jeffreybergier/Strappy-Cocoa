#import "XPAppKit.h"

@implementation NSWindow (XPAppKit)

- (void)XP_setTitle:(NSString *)title
{
  [self setTitle:(title ? title : @"")];
}

- (CGFloat)XP_backingScaleFactor
{
  SEL selector = @selector(backingScaleFactor);
  NSMethodSignature *signature;
  NSInvocation *invocation;
  CGFloat scale = 1.0;

  if (![self respondsToSelector:selector])
    return 1.0;

  signature = [self methodSignatureForSelector:selector];
  if (!signature)
    return 1.0;

  invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:self];
  [invocation setSelector:selector];
  [invocation invoke];
  [invocation getReturnValue:&scale];

  return (scale > 0.0) ? scale : 1.0;
}

@end
