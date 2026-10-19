#import <AltivecCocoa/AICollectionController.h>
#import <AltivecCocoa/AICollectionTableViewController.h>
#import <UIKit/UIKit.h>

@class SessionListViewController;
@class StrappySession;

@protocol SessionListViewControllerDelegate <NSObject>
- (void)sessionListViewController:(SessionListViewController *)controller
                 didSelectSession:(StrappySession *)session;
@end

@interface SessionListViewController : AICollectionTableViewController {
  AICollectionController *sidebarCollection_;
  NSMutableDictionary *pendingPromptStates_;
  BOOL pendingSelectionNotification_;
  BOOL initialServiceWaitStarted_;
  BOOL initialServiceWaitExpired_;
}

@property (nonatomic, assign) id<SessionListViewControllerDelegate> delegate;

- (void)reloadData;
- (void)reloadSessionIdentifier:(NSNumber *)sessionIdentifier
                         select:(BOOL)select;
- (void)selectSessionIdentifier:(NSNumber *)sessionIdentifier;
- (void)addSession:(id)sender;

@end
