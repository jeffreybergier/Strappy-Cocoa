#import "XPUIKit.h"

/* The legacy and modern UIKit text-alignment enums use the same ABI-stable
 * values. Naming either enum warns at one end of Strappy's supported range:
 * UITextAlignment is deprecated in the 8.4 SDK, while NSTextAlignment is not
 * available on the 4.3 deployment floor. Keep the compatibility values in the
 * XP layer as plain NSInteger constants. */
static const NSInteger XPUIKitTextAlignmentCenter = 1;
static const NSInteger XPUIKitTextAlignmentRight = 2;

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

static id XPUIKitAppearanceProxyForClass(Class appearanceClass)
{
  if (appearanceClass == Nil) {
    return nil;
  }
  if (![(id)appearanceClass respondsToSelector:@selector(appearance)]) {
    return nil;
  }
  return [(id)appearanceClass performSelector:@selector(appearance)];
}

static BOOL XPUIKitAppearanceProxyCanForwardSelector(
  id appearanceProxy,
  SEL selector)
{
  /* iOS 6 appearance proxies forward setters while reporting that they do
     not respond to them. Their method signatures expose the forwarding
     support reliably. */
  return ((appearanceProxy != nil) &&
          ([appearanceProxy methodSignatureForSelector:selector] != nil))
    ? YES : NO;
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

static id XPUIKitInvokeObjectGetter(id target, SEL selector)
{
  NSMethodSignature *signature;
  NSInvocation *invocation;
  __unsafe_unretained id value;

  if ((target == nil) || ![target respondsToSelector:selector]) {
    return nil;
  }
  signature = [target methodSignatureForSelector:selector];
  if ((signature == nil) || ([signature numberOfArguments] != 2U)) {
    return nil;
  }
  invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:target];
  [invocation setSelector:selector];
  [invocation invoke];
  value = nil;
  [invocation getReturnValue:&value];
  return value;
}

static void XPUIKitInvokeObjectSetter(id target, SEL selector, id value)
{
  NSMethodSignature *signature;
  NSInvocation *invocation;
  __unsafe_unretained id argument;

  if ((target == nil) || ![target respondsToSelector:selector]) {
    return;
  }
  signature = [target methodSignatureForSelector:selector];
  if ((signature == nil) || ([signature numberOfArguments] != 3U)) {
    return;
  }
  invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:target];
  [invocation setSelector:selector];
  argument = value;
  [invocation setArgument:&argument atIndex:2];
  [invocation invoke];
}

static NSUInteger XPUIKitInvokeUnsignedIntegerGetter(id target, SEL selector)
{
  NSMethodSignature *signature;
  NSInvocation *invocation;
  NSUInteger value;

  if ((target == nil) || ![target respondsToSelector:selector]) {
    return 0U;
  }
  signature = [target methodSignatureForSelector:selector];
  if ((signature == nil) || ([signature numberOfArguments] != 2U)) {
    return 0U;
  }
  invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:target];
  [invocation setSelector:selector];
  [invocation invoke];
  value = 0U;
  [invocation getReturnValue:&value];
  return value;
}

static id XPUIKitCreateNotificationSettings(Class settingsClass,
                                             NSUInteger types)
{
  SEL selector;
  NSMethodSignature *signature;
  NSInvocation *invocation;
  __unsafe_unretained id categoriesArgument;
  __unsafe_unretained id value;

  selector = @selector(settingsForTypes:categories:);
  if ((settingsClass == Nil) ||
      ![(id)settingsClass respondsToSelector:selector]) {
    return nil;
  }
  signature = [(id)settingsClass methodSignatureForSelector:selector];
  if ((signature == nil) || ([signature numberOfArguments] != 4U)) {
    return nil;
  }
  invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:settingsClass];
  [invocation setSelector:selector];
  categoriesArgument = nil;
  [invocation setArgument:&types atIndex:2];
  [invocation setArgument:&categoriesArgument atIndex:3];
  [invocation invoke];
  value = nil;
  [invocation getReturnValue:&value];
  return value;
}

static BOOL XPUIKitInvokePresentViewController(UIViewController *target,
                                               SEL selector,
                                               UIViewController *controller,
                                               BOOL animated,
                                               BOOL hasCompletion)
{
  NSMethodSignature *signature;
  NSInvocation *invocation;
  __unsafe_unretained id controllerArgument;

  if ((target == nil) || (controller == nil) ||
      ![target respondsToSelector:selector]) {
    return NO;
  }
  signature = [target methodSignatureForSelector:selector];
  if ((signature == nil) ||
      ([signature numberOfArguments] != (hasCompletion ? 5U : 4U))) {
    return NO;
  }
  invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:target];
  [invocation setSelector:selector];
  controllerArgument = controller;
  [invocation setArgument:&controllerArgument atIndex:2];
  [invocation setArgument:&animated atIndex:3];
  if (hasCompletion) {
    __unsafe_unretained id completionArgument;

    completionArgument = nil;
    [invocation setArgument:&completionArgument atIndex:4];
  }
  [invocation invoke];
  return YES;
}

static BOOL XPUIKitInvokeDismissViewController(UIViewController *target,
                                               SEL selector,
                                               BOOL animated,
                                               BOOL hasCompletion)
{
  NSMethodSignature *signature;
  NSInvocation *invocation;

  if ((target == nil) || ![target respondsToSelector:selector]) {
    return NO;
  }
  signature = [target methodSignatureForSelector:selector];
  if ((signature == nil) ||
      ([signature numberOfArguments] != (hasCompletion ? 4U : 3U))) {
    return NO;
  }
  invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:target];
  [invocation setSelector:selector];
  [invocation setArgument:&animated atIndex:2];
  if (hasCompletion) {
    __unsafe_unretained id completionArgument;

    completionArgument = nil;
    [invocation setArgument:&completionArgument atIndex:3];
  }
  [invocation invoke];
  return YES;
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
  return [UIColor colorWithRed:251.0f/255.0f
                         green:250.0f/255.0f
                          blue:252.0f/255.0f
                         alpha:1.0f];
}

@end

@implementation UIView (XPUIKit)

+ (void)XP_setAppearanceBarTintColorIfAvailable:(UIColor *)barTintColor
{
  id appearanceProxy;

  appearanceProxy = XPUIKitAppearanceProxyForClass(self);
  if (XPUIKitAppearanceProxyCanForwardSelector(
        appearanceProxy,
        @selector(setBarTintColor:))) {
    [appearanceProxy performSelector:@selector(setBarTintColor:)
                          withObject:barTintColor];
  }
}

+ (void)XP_setAppearanceTintColorIfAvailable:(UIColor *)tintColor
{
  id appearanceProxy;

  appearanceProxy = XPUIKitAppearanceProxyForClass(self);
  if (XPUIKitAppearanceProxyCanForwardSelector(
        appearanceProxy,
        @selector(setTintColor:))) {
    [appearanceProxy performSelector:@selector(setTintColor:)
                          withObject:tintColor];
  }
}

- (void)XP_setBackgroundTransparent
{
  [self setOpaque:NO];
  [self setBackgroundColor:[UIColor clearColor]];
}

- (void)XP_setTintColorIfAvailable:(UIColor *)tintColor
{
  if ([self respondsToSelector:@selector(setTintColor:)]) {
    [self performSelector:@selector(setTintColor:) withObject:tintColor];
  }
}

@end

@implementation UIBarButtonItem (XPUIKit)

+ (void)XP_setAppearanceTintColorIfAvailable:(UIColor *)tintColor
{
  id appearanceProxy;

  appearanceProxy = XPUIKitAppearanceProxyForClass(self);
  if (XPUIKitAppearanceProxyCanForwardSelector(
        appearanceProxy,
        @selector(setTintColor:))) {
    [appearanceProxy performSelector:@selector(setTintColor:)
                          withObject:tintColor];
  }
}

- (void)XP_setTintColorIfAvailable:(UIColor *)tintColor
{
  if ([self respondsToSelector:@selector(setTintColor:)]) {
    [self performSelector:@selector(setTintColor:) withObject:tintColor];
  }
}

@end

@implementation UISwitch (XPUIKit)

+ (void)XP_setAppearanceOnTintColorIfAvailable:(UIColor *)onTintColor
{
  id appearanceProxy;

  appearanceProxy = XPUIKitAppearanceProxyForClass(self);
  if (XPUIKitAppearanceProxyCanForwardSelector(
        appearanceProxy,
        @selector(setOnTintColor:))) {
    [appearanceProxy performSelector:@selector(setOnTintColor:)
                          withObject:onTintColor];
  }
}

@end

@implementation UITableView (XPUIKit)

+ (void)XP_setSectionHeaderFooterAppearanceTintColorIfAvailable:
  (UIColor *)tintColor
{
  Class headerFooterViewClass;
  id appearanceProxy;

  headerFooterViewClass = NSClassFromString(@"UITableViewHeaderFooterView");
  appearanceProxy = XPUIKitAppearanceProxyForClass(headerFooterViewClass);
  if (XPUIKitAppearanceProxyCanForwardSelector(
        appearanceProxy,
        @selector(setTintColor:))) {
    [appearanceProxy performSelector:@selector(setTintColor:)
                          withObject:tintColor];
  }
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

- (void)XP_presentViewController:(UIViewController *)viewController
                         animated:(BOOL)animated
{
  SEL selector;

  selector = @selector(presentViewController:animated:completion:);
  if (XPUIKitInvokePresentViewController(
        self, selector, viewController, animated, YES)) {
    return;
  }
  XPUIKitInvokePresentViewController(
    self,
    @selector(presentModalViewController:animated:),
    viewController,
    animated,
    NO);
}

- (void)XP_dismissViewControllerAnimated:(BOOL)animated
{
  SEL selector;

  selector = @selector(dismissViewControllerAnimated:completion:);
  if (XPUIKitInvokeDismissViewController(self, selector, animated, YES)) {
    return;
  }
  XPUIKitInvokeDismissViewController(
    self, @selector(dismissModalViewControllerAnimated:), animated, NO);
}

- (BOOL)XP_isMovingFromParentViewController
{
  return XPUIKitInvokeBoolGetter(self,
                                 @selector(isMovingFromParentViewController));
}

@end

@implementation UIDevice (XPUIKit)

- (BOOL)XP_isOperatingSystemAtLeastMajorVersion:(NSInteger)majorVersion
{
  NSString *systemVersion;

  systemVersion = [self systemVersion];
  return ([systemVersion integerValue] >= majorVersion) ? YES : NO;
}

@end

@implementation UILabel (XPUIKit)

- (void)XP_setTextAlignmentCenter
{
  XPUIKitInvokeIntegerSetter(self,
                             @selector(setTextAlignment:),
                             (NSInteger)XPUIKitTextAlignmentCenter);
}

- (void)XP_setLineBreakModeWordWrapping
{
  XPUIKitInvokeIntegerSetter(self, @selector(setLineBreakMode:), 0);
}

@end

@implementation UITextField (XPUIKit)

- (void)XP_setTextAlignmentRight
{
  XPUIKitInvokeIntegerSetter(self,
                             @selector(setTextAlignment:),
                             (NSInteger)XPUIKitTextAlignmentRight);
}

@end

@interface XPUserNotificationCenter ()
- (void)deliverTitle:(NSString *)title body:(NSString *)body;
@end

@implementation XPUserNotificationCenter

+ (XPUserNotificationCenter *)defaultCenter
{
  static XPUserNotificationCenter *instance = nil;

  if (instance == nil) {
    instance = [[XPUserNotificationCenter alloc] init];
  }
  return instance;
}

- (void)requestAuthorization
{
  @try {
    UIApplication *application;
    Class settingsClass;
    id settings;
    SEL registerSelector;

    application = [UIApplication sharedApplication];
    registerSelector = @selector(registerUserNotificationSettings:);
    if (![application respondsToSelector:registerSelector]) {
      return;
    }

    settingsClass = NSClassFromString(@"UIUserNotificationSettings");
    if (settingsClass == Nil) {
      return;
    }
    /* UIUserNotificationTypeSound (2) | UIUserNotificationTypeAlert (4). */
    settings = XPUIKitCreateNotificationSettings(settingsClass, (NSUInteger)6);
    if (settings != nil) {
      XPUIKitInvokeObjectSetter(application, registerSelector, settings);
    }
  } @catch (NSException *exception) {
    NSLog(@"StrappyNotifications authorization request failed: %@", exception);
  }
}

- (XPNotificationAuthStatus)authorizationStatus
{
  UIApplication *application;
  id settings;
  NSUInteger types;
  SEL currentSettingsSelector;

  application = [UIApplication sharedApplication];
  currentSettingsSelector = @selector(currentUserNotificationSettings);
  if (![application respondsToSelector:currentSettingsSelector]) {
    return XPNotificationAuthStatusAuthorized;
  }

  settings = XPUIKitInvokeObjectGetter(application, currentSettingsSelector);
  if (settings == nil) {
    return XPNotificationAuthStatusNotDetermined;
  }
  types = XPUIKitInvokeUnsignedIntegerGetter(settings, @selector(types));
  return (types != 0U) ? XPNotificationAuthStatusAuthorized :
                         XPNotificationAuthStatusDenied;
}

- (void)postNotificationWithTitle:(NSString *)title body:(NSString *)body
{
  if (![body isKindOfClass:[NSString class]] || ([body length] == 0U)) {
    return;
  }

  @try {
    [self deliverTitle:[title isKindOfClass:[NSString class]] ? title : @""
                  body:body];
  } @catch (NSException *exception) {
    NSLog(@"StrappyNotifications delivery failed: %@", exception);
  }
}

- (void)deliverTitle:(NSString *)title body:(NSString *)body
{
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
  UILocalNotification *notification;

  notification = [[UILocalNotification alloc] init];
  [notification setSoundName:UILocalNotificationDefaultSoundName];
  if (([title length] > 0U) &&
      [notification respondsToSelector:@selector(setAlertTitle:)]) {
    [notification setAlertBody:body];
    [notification setValue:title forKey:@"alertTitle"];
  } else {
    [notification setAlertBody:([title length] > 0U) ?
      [NSString stringWithFormat:@"%@: %@", title, body] : body];
  }
  /* TODO: UILocalNotification alerts do not display for non-sandboxed iOS
   * apps installed as system applications. SpringBoard accepts the request
   * but excludes the app from BulletinBoard.
   */
  [[UIApplication sharedApplication]
    presentLocalNotificationNow:notification];
#ifdef __clang__
#pragma clang diagnostic pop
#endif
}

@end
