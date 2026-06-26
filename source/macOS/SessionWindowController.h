#import <AppKit/AppKit.h>
#import "AICookieCutterWindowController.h"
#import "SessionListViewController.h"
#import "MessageListViewController.h"

@interface SessionWindowController : AICookieCutterWindowController
    <SessionListViewControllerDelegate, MessageListViewControllerDelegate> {
 @private
  SessionListViewController *sessionsController_;
  MessageListViewController *messagesController_;
}

- (id)init;
- (void)newSession:(id)sender;
- (BOOL)canSendCurrentPrompt;
- (void)sendCurrentPrompt:(id)sender;
- (BOOL)canCancelCurrentPrompt;
- (void)cancelCurrentPrompt:(id)sender;
- (BOOL)canToggleStreaming;
- (BOOL)streamingEnabled;
- (void)toggleStreaming:(id)sender;

@end
