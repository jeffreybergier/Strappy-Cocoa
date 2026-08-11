#import <AppKit/AppKit.h>
#import "AICookieCutterWindowController.h"
#import "SessionListViewController.h"
#import "MessageListViewController.h"
#import "StrappySessionOptionsViewController.h"

@interface SessionWindowController : AICookieCutterWindowController
    <SessionListViewControllerDelegate, MessageListViewControllerDelegate> {
 @private
  SessionListViewController *sessionsController_;
  MessageListViewController *messagesController_;
  StrappySessionOptionsViewController *optionsController_;
}

- (id)init;
- (void)newSession:(id)sender;
- (BOOL)canCloseCurrentChat;
- (void)closeCurrentChat:(id)sender;
- (BOOL)canDeleteCurrentChat;
- (void)deleteCurrentChat:(id)sender;
- (BOOL)canSendCurrentPrompt;
- (void)sendCurrentPrompt:(id)sender;
- (BOOL)canCancelCurrentPrompt;
- (void)cancelCurrentPrompt:(id)sender;
- (void)populateModelMenu:(NSMenu *)menu;
- (void)selectCurrentModel:(id)sender;
- (BOOL)canPrintCurrentChat;
- (void)printCurrentChat:(id)sender;

@end
