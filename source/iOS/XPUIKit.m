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

static BOOL XPUIKitInvokeBoolGetter(id target, SEL selector)
{
  NSMethodSignature *signature;
  NSInvocation *invocation;
  BOOL value;

  if ((target == nil) || ![target respondsToSelector:selector]) {
    return NO;
  }

  signature = [target methodSignatureForSelector:selector];
  if (signature == nil) {
    return NO;
  }

  invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:target];
  [invocation setSelector:selector];
  [invocation invoke];
  value = NO;
  [invocation getReturnValue:&value];
  return value ? YES : NO;
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

@implementation UIView (XPUIKit)

- (void)XP_setBackgroundTransparent
{
  [self setOpaque:NO];
  [self setBackgroundColor:[UIColor clearColor]];
}

- (void)XP_removeShadow
{
  NSArray *subviews;
  NSUInteger index;

  subviews = [self subviews];
  for (index = 0U; index < [subviews count]; index++) {
    UIView *view;

    view = [subviews objectAtIndex:index];
    if (![view isKindOfClass:[UIImageView class]]) {
      break;
    }
    [view setHidden:YES];
  }
}

@end

@implementation UIWebView (XPUIKit)

- (UIScrollView *)XP_scrollView
{
  SEL selector;
  NSMethodSignature *signature;
  NSInvocation *invocation;
  __unsafe_unretained UIScrollView *scrollView;

  selector = @selector(scrollView);
  if (![self respondsToSelector:selector]) {
    return nil;
  }
  signature = [self methodSignatureForSelector:selector];
  if (signature == nil) {
    return nil;
  }

  invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:self];
  [invocation setSelector:selector];
  [invocation invoke];
  scrollView = nil;
  [invocation getReturnValue:&scrollView];
  return scrollView;
}

@end

@implementation UIViewController (XPUIKit)

- (BOOL)XP_isMovingFromParentViewController
{
  return XPUIKitInvokeBoolGetter(self,
                                 @selector(isMovingFromParentViewController));
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
