#import "StrappyBottomToolbarView.h"
#import "XPAppKit.h"

@implementation StrappyBottomToolbarView

- (BOOL)isOpaque
{
  return YES;
}

- (void)drawRect:(NSRect)dirtyRect
{
  NSRect bounds;

  (void)dirtyRect;
  bounds = [self bounds];

  [XPColorWindowFrame set];
  NSRectFill(bounds);

  [XPColorControlHighlight set];
  NSRectFill(NSMakeRect(bounds.origin.x,
                        bounds.origin.y + bounds.size.height - 1.0,
                        bounds.size.width,
                        1.0));
}

@end
