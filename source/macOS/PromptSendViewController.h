#import <AppKit/AppKit.h>
#import "AICookieCutterWindowController.h"
#import "XPAppKit.h"

@class PromptSendViewController;

@protocol PromptSendViewControllerDelegate
- (void)promptSendViewController:(PromptSendViewController *)controller
                 didSubmitPrompt:(NSString *)prompt;
- (NSArray *)allowedModelsForPromptSendViewController:
    (PromptSendViewController *)controller;
- (NSString *)selectedModelIdentifierForPromptSendViewController:
    (PromptSendViewController *)controller;
- (BOOL)promptSendViewController:(PromptSendViewController *)controller
        setSelectedModelIdentifier:(NSString *)modelIdentifier;
- (BOOL)webSearchEnabledForPromptSendViewController:
    (PromptSendViewController *)controller;
- (BOOL)paidWebSearchEnabledForPromptSendViewController:
    (PromptSendViewController *)controller;
- (BOOL)promptSendViewController:(PromptSendViewController *)controller
             setWebSearchEnabled:(BOOL)enabled;
- (BOOL)promptSendViewController:(PromptSendViewController *)controller
         setPaidWebSearchEnabled:(BOOL)enabled;
- (BOOL)promptSendViewController:(PromptSendViewController *)controller
              setStreamingEnabled:(BOOL)enabled;
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
  NSMenuItem    *webSearchMenuItem_;
  NSMenuItem    *paidWebSearchMenuItem_;
  NSMenuItem    *streamingMenuItem_;
  id<PromptSendViewControllerDelegate> delegate_;
  BOOL          enabled_;
  BOOL          expanded_;
  BOOL          sending_;
  BOOL          cancellationRequested_;
  BOOL          webSearchEnabled_;
  BOOL          paidWebSearchEnabled_;
  BOOL          streamingEnabled_;
}

- (void)setDelegate:(id<PromptSendViewControllerDelegate>)delegate;
- (id<PromptSendViewControllerDelegate>)delegate;
- (CGFloat)preferredHeight;
- (void)setEnabled:(BOOL)enabled;
- (void)setSending:(BOOL)sending;
- (void)setCancellationRequested:(BOOL)requested;
- (void)setWebSearchEnabled:(BOOL)enabled;
- (void)setPaidWebSearchEnabled:(BOOL)enabled;
- (void)setStreamingEnabled:(BOOL)enabled;
- (void)reloadOptionsMenu;
- (BOOL)canSendCurrentPrompt;
- (void)performSend:(id)sender;

@end
