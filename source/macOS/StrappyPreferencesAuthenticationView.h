#import <AppKit/AppKit.h>

@interface StrappyPreferencesAuthenticationView : NSView {
 @private
  NSTextField       *apiEndpointField_;
  NSSecureTextField *apiTokenField_;
  NSTextField       *statusLabel_;
}

- (id)initWithFrame:(NSRect)frame target:(id)target;
- (NSTextField *)apiEndpointField;
- (NSSecureTextField *)apiTokenField;
- (NSTextField *)statusLabel;

@end
