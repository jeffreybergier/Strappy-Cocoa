#import <AppKit/AppKit.h>
#import "AICookieCutterWindowController.h"
#import "StrappySession.h"
#import "XPAppKit.h"

@class PromptSendViewController;

@protocol PromptSendViewControllerDelegate
- (void)promptSendViewController:(PromptSendViewController *)controller
                 didSubmitPrompt:(NSString *)prompt;
- (NSArray *)allowedModelsForPromptSendViewController:
    (PromptSendViewController *)controller;
- (BOOL)promptSendViewController:(PromptSendViewController *)controller
            updateSessionOptions:(StrappySessionOptions *)options
                   changedFields:(StrappySessionOptionMask)changedFields;
- (void)promptSendViewControllerDidCancelPrompt:
    (PromptSendViewController *)controller;
- (void)promptSendViewControllerDidChangeHeight:
    (PromptSendViewController *)controller;
@end

@interface PromptSendViewController : AIViewController <XPTextViewDelegate> {
 @private
  NSView       *barView_;
  NSView       *bezelView_;
  NSScrollView *scrollView_;
  NSTextView   *textView_;
  NSSegmentedControl *actionSegmented_;
  NSMenu       *optionsMenu_;
  NSMenuItem    *webProviderMenuItem_;
  NSMenu        *webProviderMenu_;
  NSMenuItem    *streamingMenuItem_;
  id<PromptSendViewControllerDelegate> delegate_;
  BOOL          enabled_;
  BOOL          studyLocked_;
  BOOL          expanded_;
  BOOL          sending_;
  BOOL          cancellationRequested_;
  StrappySessionOptions *sessionOptions_;
}

- (void)setDelegate:(id<PromptSendViewControllerDelegate>)delegate;
- (id<PromptSendViewControllerDelegate>)delegate;
- (CGFloat)preferredHeight;
- (void)setEnabled:(BOOL)enabled;
- (void)setStudyLocked:(BOOL)studyLocked;
- (void)setSending:(BOOL)sending;
- (void)setCancellationRequested:(BOOL)requested;
- (void)setSessionOptions:(StrappySessionOptions *)options;
- (void)reloadOptionsMenu;
- (BOOL)canSendCurrentPrompt;
- (void)performSend:(id)sender;

@end
