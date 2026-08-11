#import <AppKit/AppKit.h>

#import "AIViewController.h"
#import "StrappySession.h"
#import "XPAppKit.h"

@interface StrappySessionOptionsViewController : AIViewController
    <XPTextFieldDelegate> {
 @private
  NSScrollView *scrollView_;
  NSView *documentView_;
  NSTextField *titleLabel_;
  NSTextField *statusLabel_;
  NSBox *modelAssistantBox_;
  NSBox *toolsBox_;
  NSBox *limitsBox_;
  NSBox *searchProviderBox_;
  NSTextField *modelLabel_;
  NSTextField *assistantLabel_;
  NSPopUpButton *modelPopUpButton_;
  NSSegmentedControl *assistantSegmentedControl_;
  NSArray *assistantSegmentIdentifiers_;
  NSButton *webSearchButton_;
  NSButton *bashButton_;
  NSButton *limitToOneToolButton_;
  NSButton *answerQualityButton_;
  NSTextField *roundLimitLabel_;
  NSTextField *roundLimitField_;
  NSPopUpButton *searchProviderPopUpButton_;
  StrappySession *session_;
  NSString *statusText_;
  BOOL reloading_;
}

- (id)init;
- (void)reloadWithSession:(StrappySession *)session;
- (void)reloadOptions;

@end
