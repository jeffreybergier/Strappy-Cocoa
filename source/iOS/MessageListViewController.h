#import <UIKit/UIKit.h>

@class MessageListViewController;
@class StrappySession;

@protocol MessageListViewControllerDelegate <NSObject>
- (void)messageListViewController:(MessageListViewController *)controller
                 didUpdateSession:(NSDictionary *)session;
@end

@interface MessageListViewController : UIViewController

@property (nonatomic, assign) id<MessageListViewControllerDelegate> delegate;

- (instancetype)initWithSession:(StrappySession *)session;
- (void)reloadWithSession:(StrappySession *)session;
- (BOOL)canSendCurrentPrompt;
- (void)sendCurrentPrompt:(id)sender;
- (BOOL)canCancelCurrentPrompt;
- (void)cancelCurrentPrompt:(id)sender;
- (NSArray *)availableModels;
- (NSArray *)availableAssistantSets;
- (NSString *)selectedAssistantSetIdentifier;
- (BOOL)setSelectedAssistantSetIdentifier:(NSString *)assistantSetIdentifier;
- (NSString *)selectedModelIdentifier;
- (BOOL)canSelectModel;
- (BOOL)setSelectedModelIdentifier:(NSString *)modelIdentifier;

@end
