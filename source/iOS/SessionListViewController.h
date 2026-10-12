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
  BOOL pendingSelectionNotification_;
}

@property (nonatomic, assign) id<SessionListViewControllerDelegate> delegate;

- (void)reloadData;
- (void)reloadSessionIdentifier:(NSNumber *)sessionIdentifier
                         select:(BOOL)select;
- (void)selectSessionIdentifier:(NSNumber *)sessionIdentifier;
- (void)addSession:(id)sender;

@end
