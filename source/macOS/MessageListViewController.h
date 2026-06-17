#import <AppKit/AppKit.h>
#import "AIWebViewController.h"
#import "PromptSendViewController.h"

@class MessageListViewController;

@protocol MessageListViewControllerDelegate
- (void)messageListViewController:(MessageListViewController *)controller
                 didCreateSession:(NSDictionary *)session;
@end

@interface MessageListViewController : AIWebViewController
    <PromptSendViewControllerDelegate> {
 @private
  NSString                 *htmlDirectoryPath_;
  NSNumber                 *sessionId_;
  PromptSendViewController *sendController_;
  id<MessageListViewControllerDelegate> delegate_;
  NSString                 *statusText_;
  BOOL                      sending_;
}

- (id)init;
- (void)setDelegate:(id<MessageListViewControllerDelegate>)delegate;
- (id<MessageListViewControllerDelegate>)delegate;
- (void)reloadWithSession:(NSDictionary *)session;
- (void)reloadData;

@end
