#import <AppKit/AppKit.h>

@interface StrappyWindowAwareView : NSView {
 @private
  id  windowTarget_;
  SEL windowAction_;
}
- (void)setWindowChangeTarget:(id)target action:(SEL)action;
@end
