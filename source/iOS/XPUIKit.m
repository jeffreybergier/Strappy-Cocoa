#import "XPUIKit.h"
#import <objc/message.h>

@implementation UIColor (XPUIKit)

+ (UIColor *)messagesBackgroundColor
{
  return [UIColor colorWithRed:220.0f/255.0f
                         green:226.0f/255.0f
                          blue:236.0f/255.0f
                         alpha:1.0f];
}

@end

@implementation UILabel (XPUIKit)

- (void)XP_setTextAlignmentCenter
{
  ((void (*)(id, SEL, NSInteger))objc_msgSend)
    (self, @selector(setTextAlignment:), (NSInteger)UITextAlignmentCenter);
}

@end
