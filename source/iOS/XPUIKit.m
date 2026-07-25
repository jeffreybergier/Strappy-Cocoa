#import "XPUIKit.h"

@implementation NSString (XPUIKit)

- (CGSize)XP_sizeWithFont:(UIFont *)font constrainedToSize:(CGSize)size
{
  SEL selector;
  NSMethodSignature *signature;
  NSInvocation *invocation;
  __unsafe_unretained UIFont *fontArgument;
  CGSize measuredSize;

  if ((font == nil) || (size.width <= 0.0f) || (size.height <= 0.0f)) {
    return CGSizeZero;
  }

  selector =
    NSSelectorFromString(@"boundingRectWithSize:options:attributes:context:");
  signature = [self respondsToSelector:selector] ?
    [self methodSignatureForSelector:selector] : nil;
  if (signature != nil) {
    CGRect measuredRect;
    NSInteger options;
    NSDictionary *attributes;
    __unsafe_unretained NSDictionary *attributesArgument;
    __unsafe_unretained id contextArgument;

    invocation = [NSInvocation invocationWithMethodSignature:signature];
    [invocation setTarget:self];
    [invocation setSelector:selector];
    /* UsesLineFragmentOrigin | UsesFontLeading. */
    options = (NSInteger)((1U << 0) | (1U << 1));
    attributes = [NSDictionary dictionaryWithObject:font forKey:@"NSFont"];
    attributesArgument = attributes;
    contextArgument = nil;
    [invocation setArgument:&size atIndex:2];
    [invocation setArgument:&options atIndex:3];
    [invocation setArgument:&attributesArgument atIndex:4];
    [invocation setArgument:&contextArgument atIndex:5];
    [invocation invoke];
    measuredRect = CGRectZero;
    [invocation getReturnValue:&measuredRect];
    return CGSizeMake(CGRectGetWidth(measuredRect),
                      CGRectGetHeight(measuredRect));
  }

  selector = NSSelectorFromString(
    @"sizeWithFont:constrainedToSize:lineBreakMode:");
  signature = [self respondsToSelector:selector] ?
    [self methodSignatureForSelector:selector] : nil;
  if (signature == nil) {
    return CGSizeZero;
  }

  invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:self];
  [invocation setSelector:selector];
  fontArgument = font;
  {
    NSInteger lineBreakMode;

    /* NSLineBreakByWordWrapping. */
    lineBreakMode = 0;
    [invocation setArgument:&fontArgument atIndex:2];
    [invocation setArgument:&size atIndex:3];
    [invocation setArgument:&lineBreakMode atIndex:4];
  }
  [invocation invoke];
  measuredSize = CGSizeZero;
  [invocation getReturnValue:&measuredSize];
  return measuredSize;
}

@end

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

- (void)XP_setVisibleFrame:(CGRect)frame
{
  UIEdgeInsets contentInset;
  UIEdgeInsets scrollIndicatorInsets;
  UIScrollView *scrollView;
  CGFloat bottomInset;

  if (![self respondsToSelector:@selector(scrollView)]) {
    [self setFrame:frame];
    return;
  }

  scrollView = [self XP_scrollView];
  if (scrollView == nil) {
    [self setFrame:frame];
    return;
  }

  bottomInset = CGRectGetMaxY([self frame]) - CGRectGetMaxY(frame);
  if (bottomInset < 0.0f) {
    bottomInset = 0.0f;
  }

  contentInset = [scrollView contentInset];
  contentInset.bottom = bottomInset;
  [scrollView setContentInset:contentInset];

  scrollIndicatorInsets = [scrollView scrollIndicatorInsets];
  scrollIndicatorInsets.bottom = bottomInset;
  [scrollView setScrollIndicatorInsets:scrollIndicatorInsets];
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

- (void)XP_setLineBreakModeWordWrapping
{
  XPUIKitInvokeIntegerSetter(self, @selector(setLineBreakMode:), 0);
}

@end
