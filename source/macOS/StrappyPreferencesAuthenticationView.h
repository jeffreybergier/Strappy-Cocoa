#import <AppKit/AppKit.h>

@interface StrappyPreferencesAuthenticationView : NSView {
 @private
  NSTextField       *apiEndpointField_;
  NSSecureTextField *apiTokenField_;
  NSTextField       *statusLabel_;
  NSTextField       *chatGPTStatusLabel_;
  NSTextField       *chatGPTCodeLabel_;
  NSButton          *chatGPTSignInButton_;
  NSButton          *chatGPTCopyButton_;
  NSButton          *chatGPTOpenButton_;
  NSButton          *chatGPTCancelButton_;
  NSButton          *chatGPTRetryButton_;
  NSButton          *chatGPTSignOutButton_;
}

- (id)initWithFrame:(NSRect)frame target:(id)target;
- (NSTextField *)apiEndpointField;
- (NSSecureTextField *)apiTokenField;
- (NSTextField *)statusLabel;

@end
