#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import "AICookieCutterWindowController.h"
#import "XPFoundation.h"

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 1060
  #define XPApplicationDelegate NSApplicationDelegate
  #define XPTableViewDataSource NSTableViewDataSource
  #define XPTableViewDelegate   NSTableViewDelegate
  #define XPTextViewDelegate    NSTextViewDelegate
#else
  @protocol XPApplicationDelegate @end
  @protocol XPTableViewDataSource @end
  @protocol XPTableViewDelegate   @end
  @protocol XPTextViewDelegate    @end
#endif

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 101200
  #define XPWindowStyleMaskTitled         NSWindowStyleMaskTitled
  #define XPWindowStyleMaskClosable       NSWindowStyleMaskClosable
  #define XPWindowStyleMaskMiniaturizable NSWindowStyleMaskMiniaturizable
  #define XPWindowStyleMaskResizable      NSWindowStyleMaskResizable
  #define XPEventModifierFlagCommand      NSEventModifierFlagCommand
  #define XPEventModifierFlagOption       NSEventModifierFlagOption
#else
  #define XPWindowStyleMaskTitled         NSTitledWindowMask
  #define XPWindowStyleMaskClosable       NSClosableWindowMask
  #define XPWindowStyleMaskMiniaturizable NSMiniaturizableWindowMask
  #define XPWindowStyleMaskResizable      NSResizableWindowMask
  #define XPEventModifierFlagCommand      NSCommandKeyMask
  #define XPEventModifierFlagOption       NSAlternateKeyMask
#endif

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 101200
  #define XPTextAlignmentCenter NSTextAlignmentCenter
  #define XPTextAlignmentRight  NSTextAlignmentRight
#else
  #define XPTextAlignmentCenter NSCenterTextAlignment
  #define XPTextAlignmentRight  NSRightTextAlignment
#endif

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 101000
  #define XPImageScaleAxesIndependently NSImageScaleAxesIndependently
#else
  #define XPImageScaleAxesIndependently NSScaleToFit
#endif

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 101400
  #define XPBezelStyleRounded        NSBezelStyleRounded
  #define XPBezelStyleTexturedSquare NSBezelStyleTexturedSquare
  #define XPButtonTypeMomentaryLight NSButtonTypeMomentaryLight
  #define XPButtonTypeSwitch         NSButtonTypeSwitch
  #define XPProgressIndicatorStyleSpinning NSProgressIndicatorStyleSpinning
#else
  #define XPBezelStyleRounded        NSRoundedBezelStyle
  #define XPBezelStyleTexturedSquare NSTexturedSquareBezelStyle
  #define XPButtonTypeMomentaryLight NSMomentaryLightButton
  #define XPButtonTypeSwitch         NSSwitchButton
  #define XPProgressIndicatorStyleSpinning NSProgressIndicatorSpinningStyle
#endif

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 101200
  #define XPControlStateValueOn      NSControlStateValueOn
  #define XPControlStateValueOff     NSControlStateValueOff
#else
  #define XPControlStateValueOn      NSOnState
  #define XPControlStateValueOff     NSOffState
#endif

#if defined(MAC_OS_X_VERSION_MIN_REQUIRED) && MAC_OS_X_VERSION_MIN_REQUIRED >= 110000
  #define XPColorWindowFrame [NSColor windowBackgroundColor]
#else
  #define XPColorWindowFrame [NSColor windowFrameColor]
#endif

#if defined(MAC_OS_X_VERSION_MIN_REQUIRED) && MAC_OS_X_VERSION_MIN_REQUIRED >= 101400
  #define XPColorControlHighlight [NSColor separatorColor]
#else
  #define XPColorControlHighlight [NSColor controlHighlightColor]
#endif

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 101200
  #define XPCompositingOperationSourceOver NSCompositingOperationSourceOver
  #define XPCompositingOperationSourceIn   NSCompositingOperationSourceIn
#else
  #define XPCompositingOperationSourceOver NSCompositeSourceOver
  #define XPCompositingOperationSourceIn   NSCompositeSourceIn
#endif

@interface NSWindow (XPAppKit)
- (void)XP_setTitle:(NSString *)title;
- (CGFloat)XP_titlebarHeight;
- (CGFloat)XP_backingScaleFactor;
@end

@interface NSScrollView (XPAppKit)
- (void)XP_setAutomaticallyAdjustsContentInsets:(BOOL)flag;
- (void)XP_setContentInsetsTop:(CGFloat)top
                          left:(CGFloat)left
                        bottom:(CGFloat)bottom
                         right:(CGFloat)right;
@end

@interface NSView (XPAppKitLayer)
- (void)XP_setWantsLayer:(BOOL)flag;
- (void)XP_setLayerCornerRadius:(CGFloat)radius;
- (void)XP_setLayerMasksToBounds:(BOOL)flag;
- (void)XP_setLayerBorderWidth:(CGFloat)width;
- (void)XP_setLayerBorderColor:(NSColor *)color;
@end

@interface NSView (XPAppKit)
- (void)XP_pinToWindowAppearance;
@end

@interface NSSegmentedControl (XPAppKit)
- (void)XP_setToolTip:(NSString *)tip forSegment:(XPInteger)segment;
@end

#if defined(MAC_OS_X_VERSION_MIN_REQUIRED) && MAC_OS_X_VERSION_MIN_REQUIRED >= 110000
  #define XPFontTextStyleBody       [NSFont preferredFontForTextStyle:NSFontTextStyleBody options:@{}]
  #define XPFontTextStyleBoldBody   [NSFont preferredFontForTextStyle:NSFontTextStyleHeadline options:@{}]
  #define XPFontTextStyleChatName   [NSFont preferredFontForTextStyle:NSFontTextStyleCallout options:@{}]
  #define XPFontTextStyleSmallLabel [NSFont preferredFontForTextStyle:NSFontTextStyleCaption2 options:@{}]
  #define XPFontTextStyleBoldSmall  [NSFont preferredFontForTextStyle:NSFontTextStyleFootnote options:@{}]
#else
  #define XPFontTextStyleBody       [NSFont systemFontOfSize:13.0]
  #define XPFontTextStyleBoldBody   [NSFont boldSystemFontOfSize:13.0]
  #define XPFontTextStyleChatName   [NSFont systemFontOfSize:14.0]
  #define XPFontTextStyleSmallLabel [NSFont systemFontOfSize:10.0]
  #define XPFontTextStyleBoldSmall  [NSFont boldSystemFontOfSize:11.0]
#endif

#define XPTableViewStyleSourceList 3

@interface NSTableView (XPAppKit)
- (void)XP_setFloatsGroupRows:(BOOL)floats;
- (void)XP_setSourceListStyle;
@end
