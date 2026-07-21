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
- (NSString *)webProviderForPromptSendViewController:
    (PromptSendViewController *)controller;
- (BOOL)promptSendViewController:(PromptSendViewController *)controller
                  setWebProvider:(NSString *)webProvider;
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
  NSMenuItem    *webProviderMenuItem_;
  NSMenu        *webProviderMenu_;
  NSMenuItem    *streamingMenuItem_;
  id<PromptSendViewControllerDelegate> delegate_;
  BOOL          enabled_;
  BOOL          expanded_;
  BOOL          sending_;
  BOOL          cancellationRequested_;
  NSString     *webProvider_;
  BOOL          streamingEnabled_;
}

- (void)setDelegate:(id<PromptSendViewControllerDelegate>)delegate;
- (id<PromptSendViewControllerDelegate>)delegate;
- (CGFloat)preferredHeight;
- (void)setEnabled:(BOOL)enabled;
- (void)setSending:(BOOL)sending;
- (void)setCancellationRequested:(BOOL)requested;
- (void)setWebProvider:(NSString *)webProvider;
- (void)setStreamingEnabled:(BOOL)enabled;
- (void)reloadOptionsMenu;
- (BOOL)canSendCurrentPrompt;
- (void)performSend:(id)sender;

@end
