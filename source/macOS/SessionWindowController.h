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

@end
