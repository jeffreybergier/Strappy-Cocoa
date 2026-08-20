#import <AppKit/AppKit.h>
#import "AICookieCutterWindowController.h"
#import "XPAppKit.h"

@class PromptSendViewController;

@protocol PromptSendViewControllerDelegate
- (void)promptSendViewController:(PromptSendViewController *)controller
                 didSubmitPrompt:(NSString *)prompt;
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
  NSSegmentedControl *sidebarSegmented_;
  NSSegmentedControl *actionSegmented_;
  id<PromptSendViewControllerDelegate> delegate_;
  BOOL          enabled_;
  BOOL          studyLocked_;
  BOOL          expanded_;
  BOOL          sending_;
  BOOL          cancellationRequested_;
  BOOL          sidebarStateKnown_;
  BOOL          sidebarCollapsed_;
}

- (void)setDelegate:(id<PromptSendViewControllerDelegate>)delegate;
- (id<PromptSendViewControllerDelegate>)delegate;
- (CGFloat)preferredHeight;
- (void)setEnabled:(BOOL)enabled;
- (void)setStudyLocked:(BOOL)studyLocked;
- (void)setSending:(BOOL)sending;
- (void)setCancellationRequested:(BOOL)requested;
- (BOOL)canSendCurrentPrompt;
- (void)performSend:(id)sender;

@end
