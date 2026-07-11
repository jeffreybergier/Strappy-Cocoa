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

static void XPUIKitInvokeBoolSetter(id target, SEL selector, BOOL value)
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

static UIScrollView *XPUIKitFindScrollView(UIView *view)
{
  NSArray *subviews;
  NSUInteger index;

  if ([view isKindOfClass:[UIScrollView class]]) {
    return (UIScrollView *)view;
  }

  subviews = [view subviews];
  for (index = 0U; index < [subviews count]; index++) {
    UIScrollView *scrollView;

    scrollView = XPUIKitFindScrollView([subviews objectAtIndex:index]);
    if (scrollView != nil) {
      return scrollView;
    }
  }
  return nil;
}

static UITextField *XPUIKitFindTextField(UIView *view)
{
  NSArray *subviews;
  NSUInteger index;

  if ([view isKindOfClass:[UITextField class]]) {
    return (UITextField *)view;
  }

  subviews = [view subviews];
  for (index = 0U; index < [subviews count]; index++) {
    UITextField *textField;

    textField = XPUIKitFindTextField([subviews objectAtIndex:index]);
    if (textField != nil) {
      return textField;
    }
  }
  return nil;
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

@end

@implementation UIScrollView (XPUIKit)

- (void)XP_setKeyboardDismissModeOnDrag
{
  if ([self respondsToSelector:@selector(setKeyboardDismissMode:)]) {
    [self setValue:[NSNumber numberWithInteger:1]
            forKey:@"keyboardDismissMode"];
  }
}

@end

@implementation UISearchBar (XPUIKit)

- (void)XP_enableSearchReturnKeyWhenEmpty
{
  UITextField *textField;

  XPUIKitInvokeIntegerSetter(self,
                             @selector(setReturnKeyType:),
                             (NSInteger)UIReturnKeySearch);
  XPUIKitInvokeBoolSetter(self,
                          @selector(setEnablesReturnKeyAutomatically:),
                          NO);

  textField = XPUIKitFindTextField(self);
  if (textField != nil) {
    [textField setReturnKeyType:UIReturnKeySearch];
    [textField setEnablesReturnKeyAutomatically:NO];
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
    return XPUIKitFindScrollView(self);
  }
  signature = [self methodSignatureForSelector:selector];
  if (signature == nil) {
    return XPUIKitFindScrollView(self);
  }

  invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:self];
  [invocation setSelector:selector];
  [invocation invoke];
  scrollView = nil;
  [invocation getReturnValue:&scrollView];
  return (scrollView != nil) ? scrollView : XPUIKitFindScrollView(self);
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
