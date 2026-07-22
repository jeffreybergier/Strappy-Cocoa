#import <AppKit/AppKit.h>

@interface StrappyPreferencesDatabaseStudyView : NSView {
 @private
  NSTextView *textView_;
  NSButton   *resetButton_;
  NSButton   *studyButton_;
}

- (id)initWithFrame:(NSRect)frame target:(id)target;
- (NSTextView *)textView;

@end
