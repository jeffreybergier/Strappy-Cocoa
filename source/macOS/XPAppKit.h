#import <AppKit/AppKit.h>

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
#else
  #define XPTextAlignmentCenter NSCenterTextAlignment
#endif

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 101000
  #define XPImageScaleAxesIndependently NSImageScaleAxesIndependently
#else
  #define XPImageScaleAxesIndependently NSScaleToFit
#endif

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 101400
  #define XPBezelStyleRounded        NSBezelStyleRounded
  #define XPButtonTypeMomentaryLight NSButtonTypeMomentaryLight
  #define XPProgressIndicatorStyleSpinning NSProgressIndicatorStyleSpinning
#else
  #define XPBezelStyleRounded        NSRoundedBezelStyle
  #define XPButtonTypeMomentaryLight NSMomentaryLightButton
  #define XPProgressIndicatorStyleSpinning NSProgressIndicatorSpinningStyle
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

@interface NSWindow (XPAppKit)
- (void)XP_setTitle:(NSString *)title;
- (CGFloat)XP_backingScaleFactor;
@end
