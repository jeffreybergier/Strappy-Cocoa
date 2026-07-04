#import "XPUIKit.h"

static void XPUIKitInvokeIntegerSetter(id target, SEL selector, NSInteger value)
{
  NSMethodSignature *signature;
  NSInvocation *invocation;

  if ((target == nil) || ![target respondsToSelector:selector]) {
    return;
  }

  signature = [target methodSignatureForSelector:selector];
  if (signature == nil) {
    return;
  }

  invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:target];
  [invocation setSelector:selector];
  [invocation setArgument:&value atIndex:2];
  [invocation invoke];
}

@implementation UIColor (XPUIKit)

+ (UIColor *)messagesBackgroundColor
{
  return [UIColor colorWithRed:220.0f/255.0f
                         green:226.0f/255.0f
                          blue:236.0f/255.0f
                         alpha:1.0f];
}

@end

@implementation UILabel (XPUIKit)

- (void)XP_setTextAlignmentCenter
{
  XPUIKitInvokeIntegerSetter(self,
                             @selector(setTextAlignment:),
                             (NSInteger)UITextAlignmentCenter);
}

@end
