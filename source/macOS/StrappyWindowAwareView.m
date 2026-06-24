#import "StrappyWindowAwareView.h"

@implementation StrappyWindowAwareView

- (void)setWindowChangeTarget:(id)target action:(SEL)action
{
  windowTarget_ = target;
  windowAction_ = action;
}

- (void)viewDidMoveToWindow
{
  [super viewDidMoveToWindow];
  if (![self window] || !windowTarget_ || !windowAction_) {
    return;
  }
  if ([windowTarget_ respondsToSelector:windowAction_]) {
    [windowTarget_ performSelector:windowAction_ withObject:self];
  }
}

@end
