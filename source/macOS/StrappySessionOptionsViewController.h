#import <AppKit/AppKit.h>

#import "AIViewController.h"
#import "StrappySession.h"
#import "XPAppKit.h"

@interface StrappySessionOptionsViewController : AIViewController {
 @private
  NSScrollView *scrollView_;
  NSView *documentView_;
  NSTextField *titleLabel_;
  NSTextField *statusLabel_;
  NSBox *modelAssistantBox_;
  NSBox *toolsBox_;
  NSBox *limitsBox_;
  NSBox *searchProviderBox_;
  NSTextField *accountLabel_;
  NSTextField *modelLabel_;
  NSTextField *assistantLabel_;
  NSTextField *searchProviderLabel_;
  NSPopUpButton *accountPopUpButton_;
  NSPopUpButton *modelPopUpButton_;
  NSSegmentedControl *assistantSegmentedControl_;
  NSArray *assistantSegmentIdentifiers_;
  NSButton *webSearchButton_;
  NSButton *bashButton_;
  NSButton *limitToOneToolButton_;
  NSButton *answerQualityButton_;
  NSTextField *roundLimitLabel_;
  NSTextField *roundLimitValueLabel_;
  NSTextField *roundLimitMinimumLabel_;
  NSSlider *roundLimitSlider_;
  NSTextField *roundLimitMaximumLabel_;
  NSPopUpButton *searchProviderPopUpButton_;
  StrappySession *session_;
  StrappySessionOptions *defaultOptions_;
  NSArray *accountRows_;
  NSArray *modelRows_;
  NSString *statusText_;
  BOOL editsSessionDefaults_;
  BOOL layingOut_;
  BOOL reloading_;
  BOOL catalogsDirty_;
  BOOL savingOptions_;
}

- (id)init;
- (id)initForSessionDefaults;
- (void)reloadWithSession:(StrappySession *)session;
- (void)reloadOptions;

@end
