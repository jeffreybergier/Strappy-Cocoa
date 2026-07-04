#import <AppKit/AppKit.h>
#import "AIWebViewController.h"
#import "PromptSendViewController.h"
#import "StrappySession.h"

@class MessageListViewController;

@protocol MessageListViewControllerDelegate
- (void)messageListViewController:(MessageListViewController *)controller
                  didUpdateSession:(NSDictionary *)session;
@end

@interface MessageListViewController : AIWebViewController
    <PromptSendViewControllerDelegate> {
 @private
  NSString                 *htmlDirectoryPath_;
  StrappySession           *session_;
  PromptSendViewController *sendController_;
  id<MessageListViewControllerDelegate> delegate_;
  NSString                 *statusText_;
  NSMutableString          *pendingStreamJavaScript_;
  NSTimer                  *streamEventFlushTimer_;
  NSUInteger                oldestRenderedMessageIndex_;
  BOOL                      sending_;
  BOOL                      cancelPromptRequested_;
}

- (id)init;
- (void)setDelegate:(id<MessageListViewControllerDelegate>)delegate;
- (id<MessageListViewControllerDelegate>)delegate;
- (void)reloadWithSession:(StrappySession *)session;
- (void)reloadData;
- (BOOL)canSendCurrentPrompt;
- (void)sendCurrentPrompt:(id)sender;
- (BOOL)canCancelCurrentPrompt;
- (void)cancelCurrentPrompt:(id)sender;
- (BOOL)canToggleStreaming;
- (BOOL)streamingEnabled;
- (void)toggleStreaming:(id)sender;

@end
