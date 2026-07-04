#import <UIKit/UIKit.h>

@class SessionListViewController;
@class StrappySession;

@protocol SessionListViewControllerDelegate <NSObject>
- (void)sessionListViewController:(SessionListViewController *)controller
                 didSelectSession:(StrappySession *)session;
@end

@interface SessionListViewController : UITableViewController

@property (nonatomic, assign) id<SessionListViewControllerDelegate> delegate;

- (void)reloadData;
- (void)reloadSessionIdentifier:(NSNumber *)sessionIdentifier
                         select:(BOOL)select;
- (void)selectSessionIdentifier:(NSNumber *)sessionIdentifier;
- (void)addSession:(id)sender;

@end
