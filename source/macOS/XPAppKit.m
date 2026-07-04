#import "XPAppKit.h"

static NSInvocation *XPAppKitInvocation(id target, SEL selector)
{
  NSMethodSignature *signature;
  NSInvocation *invocation;

  if ((target == nil) || ![target respondsToSelector:selector]) {
    return nil;
  }

  signature = [target methodSignatureForSelector:selector];
  if (signature == nil) {
    return nil;
  }

  invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:target];
  [invocation setSelector:selector];

  return invocation;
}

static void XPAppKitInvokeBOOLSetter(id target, SEL selector, BOOL value)
{
  NSInvocation *invocation;

  invocation = XPAppKitInvocation(target, selector);
  if (invocation == nil) {
    return;
  }

  [invocation setArgument:&value atIndex:2];
  [invocation invoke];
}

static void XPAppKitInvokeCGFloatSetter(id target, SEL selector, CGFloat value)
{
  NSInvocation *invocation;

  invocation = XPAppKitInvocation(target, selector);
  if (invocation == nil) {
    return;
  }

  [invocation setArgument:&value atIndex:2];
  [invocation invoke];
}

static void XPAppKitInvokeIntegerSetter(id target, SEL selector, NSInteger value)
{
  NSInvocation *invocation;

  invocation = XPAppKitInvocation(target, selector);
  if (invocation == nil) {
    return;
  }

  [invocation setArgument:&value atIndex:2];
  [invocation invoke];
}

static void XPAppKitInvokeCGColorSetter(id target,
                                        SEL selector,
                                        CGColorRef value)
{
  NSInvocation *invocation;

  invocation = XPAppKitInvocation(target, selector);
  if (invocation == nil) {
    return;
  }

  [invocation setArgument:&value atIndex:2];
  [invocation invoke];
}

static void XPAppKitInvokeObjectIntegerSetter(id target,
                                              SEL selector,
                                              id objectValue,
                                              NSInteger integerValue)
{
  NSInvocation *invocation;

  invocation = XPAppKitInvocation(target, selector);
  if (invocation == nil) {
    return;
  }

  [invocation setArgument:&objectValue atIndex:2];
  [invocation setArgument:&integerValue atIndex:3];
  [invocation invoke];
}

static CGColorRef XPAppKitInvokeCGColorGetter(id target, SEL selector)
{
  NSInvocation *invocation;
  CGColorRef result;

  invocation = XPAppKitInvocation(target, selector);
  if (invocation == nil) {
    return NULL;
  }

  result = NULL;
  [invocation invoke];
  [invocation getReturnValue:&result];

  return result;
}

@implementation NSWindow (XPAppKit)

- (void)XP_setTitle:(NSString *)title
{
  [self setTitle:(title ? title : @"")];
}

- (CGFloat)XP_titlebarHeight
{
  NSMethodSignature *signature;
  NSInvocation *invocation;
  NSRect layoutRect;

  if (AICCCurrentTier() < AICCTierMiddle) {
    return 0.0;
  }

  signature = [self methodSignatureForSelector:@selector(contentLayoutRect)];
  if (!signature) {
    return 0.0;
  }

  invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:self];
  [invocation setSelector:@selector(contentLayoutRect)];
  [invocation invoke];
  [invocation getReturnValue:&layoutRect];

  return NSHeight([[self contentView] bounds]) - NSHeight(layoutRect);
}

- (void)XP_setToolbarPreferenceStyle
{
  SEL selector;
  NSMethodSignature *signature;
  NSInvocation *invocation;
  NSInteger toolbarStyle;

  if (AICCCurrentTier() < AICCTierModern) {
    return;
  }

  selector = @selector(setToolbarStyle:);
  signature = [self methodSignatureForSelector:selector];
  if (!signature) {
    return;
  }

  toolbarStyle = XPWindowToolbarStylePreference;
  invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:self];
  [invocation setSelector:selector];
  [invocation setArgument:&toolbarStyle atIndex:2];
  [invocation invoke];
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

@implementation NSScrollView (XPAppKit)

- (void)XP_setAutomaticallyAdjustsContentInsets:(BOOL)flag
{
  if (AICCCurrentTier() < AICCTierMiddle) {
    return;
  }
  XPAppKitInvokeBOOLSetter(self,
                           @selector(setAutomaticallyAdjustsContentInsets:),
                           flag);
}

- (void)XP_setContentInsetsTop:(CGFloat)top
                          left:(CGFloat)left
                        bottom:(CGFloat)bottom
                         right:(CGFloat)right
{
  struct { CGFloat top, left, bottom, right; } insets;
  SEL selector;
  NSMethodSignature *signature;
  NSInvocation *invocation;

  if (AICCCurrentTier() < AICCTierMiddle) {
    return;
  }

  insets.top = top;
  insets.left = left;
  insets.bottom = bottom;
  insets.right = right;
  selector = @selector(setContentInsets:);
  signature = [self methodSignatureForSelector:selector];
  if (!signature) {
    return;
  }

  invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:self];
  [invocation setSelector:selector];
  [invocation setArgument:&insets atIndex:2];
  [invocation invoke];
}

@end

@implementation NSView (XPAppKitLayer)

- (void)XP_setWantsLayer:(BOOL)flag
{
  if (AICCCurrentTier() < AICCTierMiddle) {
    return;
  }
  XPAppKitInvokeBOOLSetter(self, @selector(setWantsLayer:), flag);
}

- (void)XP_setLayerCornerRadius:(CGFloat)radius
{
  id layer;

  if (AICCCurrentTier() < AICCTierMiddle) {
    return;
  }
  if (![self respondsToSelector:@selector(layer)]) {
    return;
  }
  layer = [self performSelector:@selector(layer)];
  if (!layer) {
    return;
  }
  XPAppKitInvokeCGFloatSetter(layer, @selector(setCornerRadius:), radius);
}

- (void)XP_setLayerMasksToBounds:(BOOL)flag
{
  id layer;

  if (AICCCurrentTier() < AICCTierMiddle) {
    return;
  }
  if (![self respondsToSelector:@selector(layer)]) {
    return;
  }
  layer = [self performSelector:@selector(layer)];
  if (!layer) {
    return;
  }
  XPAppKitInvokeBOOLSetter(layer, @selector(setMasksToBounds:), flag);
}

- (void)XP_setLayerBorderWidth:(CGFloat)width
{
  id layer;

  if (AICCCurrentTier() < AICCTierMiddle) {
    return;
  }
  if (![self respondsToSelector:@selector(layer)]) {
    return;
  }
  layer = [self performSelector:@selector(layer)];
  if (!layer) {
    return;
  }
  XPAppKitInvokeCGFloatSetter(layer, @selector(setBorderWidth:), width);
}

- (void)XP_setLayerBorderColor:(NSColor *)color
{
  id layer;
  CGColorRef cgColor;

  if (AICCCurrentTier() < AICCTierMiddle || color == nil) {
    return;
  }
  if (![self respondsToSelector:@selector(layer)]) {
    return;
  }
  layer = [self performSelector:@selector(layer)];
  if (!layer) {
    return;
  }
  cgColor = XPAppKitInvokeCGColorGetter(color, @selector(CGColor));
  if (!cgColor) {
    return;
  }
  XPAppKitInvokeCGColorSetter(layer, @selector(setBorderColor:), cgColor);
}

@end

@implementation NSView (XPAppKit)

- (void)XP_pinToWindowAppearance
{
  id appearance;
  NSWindow *window;

  if (AICCCurrentTier() < AICCTierModern) {
    return;
  }

  window = [self window];
  if (window == nil ||
      ![window respondsToSelector:@selector(effectiveAppearance)] ||
      ![self respondsToSelector:@selector(setAppearance:)]) {
    return;
  }

  appearance = [window performSelector:@selector(effectiveAppearance)];
  if (appearance == nil) {
    return;
  }
  [self performSelector:@selector(setAppearance:) withObject:appearance];
}

@end

@implementation NSSegmentedControl (XPAppKit)

- (void)XP_setToolbarSegmentStyle
{
  id cell;

  cell = [self cell];
  XPAppKitInvokeIntegerSetter(cell,
                              @selector(setSegmentStyle:),
                              XPSegmentStyleTexturedRounded);
}

- (void)XP_setToolTip:(NSString *)tip forSegment:(XPInteger)segment
{
  if (AICCCurrentTier() < AICCTierModern) {
    return;
  }
  XPAppKitInvokeObjectIntegerSetter(self,
                                    @selector(setToolTip:forSegment:),
                                    tip,
                                    segment);
}

@end

@implementation NSAlert (XPAppKit)

- (void)XP_beginSheetModalForWindow:(NSWindow *)window
                      modalDelegate:(id)delegate
                     didEndSelector:(SEL)didEndSelector
                        contextInfo:(void *)contextInfo
{
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
  [self beginSheetModalForWindow:window
                   modalDelegate:delegate
                  didEndSelector:didEndSelector
                     contextInfo:contextInfo];
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
}

@end

@implementation NSTableView (XPAppKit)

- (void)XP_setFloatsGroupRows:(BOOL)floats
{
  if (AICCCurrentTier() < AICCTierMiddle) {
    return;
  }
  XPAppKitInvokeBOOLSetter(self, @selector(setFloatsGroupRows:), floats);
}

- (void)XP_setSourceListStyle
{
  AICCTier tier;

  tier = AICCCurrentTier();
  if (tier == AICCTierModern) {
    NSInteger value;

    value = XPTableViewStyleSourceList;
    XPAppKitInvokeIntegerSetter(self, @selector(setStyle:), value);
  } else if (tier == AICCTierMiddle) {
    NSInteger value;

    value = NSTableViewSelectionHighlightStyleSourceList;
    XPAppKitInvokeIntegerSetter(self,
                                @selector(setSelectionHighlightStyle:),
                                value);
  }
}

@end
