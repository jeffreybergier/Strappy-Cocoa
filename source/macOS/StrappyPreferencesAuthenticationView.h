#import <AppKit/AppKit.h>

#import "XPAppKit.h"

@interface StrappyPreferencesAuthenticationView : NSView
    <XPTableViewDataSource, XPTableViewDelegate> {
 @private
  NSScrollView *accountScrollView_;
  NSTableView *accountTableView_;
  NSView *dividerView_;
  NSView *rightPaneView_;
  NSArray *accounts_;
  NSArray *providers_;
  NSString *selectedAccountIdentifier_;
  BOOL suppressSelectionNotification_;
  BOOL creatingAccount_;

  NSTableView *providerTableView_;
  NSTextField *accountNameField_;
  NSTextField *endpointField_;
  NSSecureTextField *tokenField_;
  NSButton *saveButton_;
  NSButton *deleteButton_;

  NSTextField *chatGPTStatusLabel_;
  NSTextField *chatGPTURLField_;
  NSTextField *chatGPTCodeField_;
  NSButton *chatGPTActionButton_;
  NSButton *chatGPTCopyButton_;
  NSButton *chatGPTOpenButton_;
}

- (id)initWithFrame:(NSRect)frame target:(id)target;

@end
