#import <AppKit/AppKit.h>

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 1060
  #define XPApplicationDelegate NSApplicationDelegate
#else
  @protocol XPApplicationDelegate @end
#endif

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 101200
  #define XPWindowStyleMaskTitled         NSWindowStyleMaskTitled
  #define XPWindowStyleMaskClosable       NSWindowStyleMaskClosable
  #define XPWindowStyleMaskMiniaturizable NSWindowStyleMaskMiniaturizable
  #define XPEventModifierFlagCommand      NSEventModifierFlagCommand
  #define XPEventModifierFlagOption       NSEventModifierFlagOption
#else
  #define XPWindowStyleMaskTitled         NSTitledWindowMask
  #define XPWindowStyleMaskClosable       NSClosableWindowMask
  #define XPWindowStyleMaskMiniaturizable NSMiniaturizableWindowMask
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

@interface NSWindow (XPAppKit)
- (void)XP_setTitle:(NSString *)title;
- (CGFloat)XP_backingScaleFactor;
@end
