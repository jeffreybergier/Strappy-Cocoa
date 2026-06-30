#import <AppKit/AppKit.h>

@interface StrappyPreferencesSystemPromptsView : NSView {
 @private
  NSTextView *textView_;
}

- (NSTextView *)textView;

@end
