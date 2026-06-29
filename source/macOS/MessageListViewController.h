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
  NSString                 *pendingMessageIdentifier_;
  NSString                 *pendingAssistantMessageIdentifier_;
  NSString                 *pendingToolActivityIdentifier_;
  NSString                 *pendingHarnessMessageIdentifier_;
  NSString                 *pendingHarnessAssistantMessageIdentifier_;
  NSString                 *pendingHarnessToolActivityIdentifier_;
  NSString                 *pendingHarnessPrompt_;
  NSString                 *pendingPromptGroupKey_;
  NSString                 *pendingPrompt_;
  NSString                 *pendingErrorText_;
  NSNumber                 *sendingSessionId_;
  NSMutableString          *streamingAssistantText_;
  NSMutableString          *streamingReasoningText_;
  NSMutableString          *streamingToolActivityText_;
  NSMutableString          *streamingHarnessAssistantText_;
  NSMutableString          *streamingHarnessReasoningText_;
  NSMutableString          *streamingHarnessToolActivityText_;
  NSMutableString          *pendingAssistantTextDelta_;
  NSMutableString          *pendingReasoningTextDelta_;
  NSMutableString          *pendingToolActivityTextDelta_;
  NSMutableString          *pendingHarnessAssistantTextDelta_;
  NSMutableString          *pendingHarnessReasoningTextDelta_;
  NSMutableString          *pendingHarnessToolActivityTextDelta_;
  NSTimer                  *streamFlushTimer_;
  NSTimer                  *databaseReloadTimer_;
  NSTimeInterval            pendingStartedAt_;
  NSTimeInterval            pendingHarnessStartedAt_;
  long long                 lastKnownMessageIdentifier_;
  NSUInteger                oldestRenderedMessageIndex_;
  NSUInteger                renderedMessageCount_;
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
