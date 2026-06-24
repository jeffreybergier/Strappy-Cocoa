#import "XPAppKit.h"
#import <objc/message.h>

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
  ((void (*)(id, SEL, BOOL))objc_msgSend)
    (self, @selector(setAutomaticallyAdjustsContentInsets:), flag);
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
  ((void (*)(id, SEL, BOOL))objc_msgSend)
    (self, @selector(setWantsLayer:), flag);
}

- (void)XP_setLayerCornerRadius:(CGFloat)radius
{
  id layer;

  if (AICCCurrentTier() < AICCTierMiddle) {
    return;
  }
  layer = ((id (*)(id, SEL))objc_msgSend)(self, @selector(layer));
  if (!layer) {
    return;
  }
  ((void (*)(id, SEL, CGFloat))objc_msgSend)
    (layer, @selector(setCornerRadius:), radius);
}

- (void)XP_setLayerMasksToBounds:(BOOL)flag
{
  id layer;

  if (AICCCurrentTier() < AICCTierMiddle) {
    return;
  }
  layer = ((id (*)(id, SEL))objc_msgSend)(self, @selector(layer));
  if (!layer) {
    return;
  }
  ((void (*)(id, SEL, BOOL))objc_msgSend)
    (layer, @selector(setMasksToBounds:), flag);
}

- (void)XP_setLayerBorderWidth:(CGFloat)width
{
  id layer;

  if (AICCCurrentTier() < AICCTierMiddle) {
    return;
  }
  layer = ((id (*)(id, SEL))objc_msgSend)(self, @selector(layer));
  if (!layer) {
    return;
  }
  ((void (*)(id, SEL, CGFloat))objc_msgSend)
    (layer, @selector(setBorderWidth:), width);
}

- (void)XP_setLayerBorderColor:(NSColor *)color
{
  id layer;
  CGColorRef cgColor;

  if (AICCCurrentTier() < AICCTierMiddle || color == nil) {
    return;
  }
  layer = ((id (*)(id, SEL))objc_msgSend)(self, @selector(layer));
  if (!layer) {
    return;
  }
  cgColor = ((CGColorRef (*)(id, SEL))objc_msgSend)(color, @selector(CGColor));
  if (!cgColor) {
    return;
  }
  ((void (*)(id, SEL, CGColorRef))objc_msgSend)
    (layer, @selector(setBorderColor:), cgColor);
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
  if (window == nil || ![self respondsToSelector:@selector(setAppearance:)]) {
    return;
  }

  appearance = ((id (*)(id, SEL))objc_msgSend)
    (window, @selector(effectiveAppearance));
  if (appearance == nil) {
    return;
  }
  ((void (*)(id, SEL, id))objc_msgSend)
    (self, @selector(setAppearance:), appearance);
}

@end

@implementation NSSegmentedControl (XPAppKit)

- (void)XP_setToolTip:(NSString *)tip forSegment:(XPInteger)segment
{
  if (AICCCurrentTier() < AICCTierModern) {
    return;
  }
  ((void (*)(id, SEL, NSString *, XPInteger))objc_msgSend)
    (self, @selector(setToolTip:forSegment:), tip, segment);
}

@end

@implementation NSTableView (XPAppKit)

- (void)XP_setFloatsGroupRows:(BOOL)floats
{
  if (AICCCurrentTier() < AICCTierMiddle) {
    return;
  }
  ((void (*)(id, SEL, BOOL))objc_msgSend)
    (self, @selector(setFloatsGroupRows:), floats);
}

- (void)XP_setSourceListStyle
{
  AICCTier tier;

  tier = AICCCurrentTier();
  if (tier == AICCTierModern) {
    NSInteger value;

    value = XPTableViewStyleSourceList;
    ((void (*)(id, SEL, NSInteger))objc_msgSend)
      (self, @selector(setStyle:), value);
  } else if (tier == AICCTierMiddle) {
    NSInteger value;

    value = NSTableViewSelectionHighlightStyleSourceList;
    ((void (*)(id, SEL, NSInteger))objc_msgSend)
      (self, @selector(setSelectionHighlightStyle:), value);
  }
}

@end
