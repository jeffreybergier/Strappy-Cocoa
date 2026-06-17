#import <AppKit/AppKit.h>
#import "AICookieCutterWindowController.h"
#import "XPAppKit.h"

@class SessionListViewController;

@protocol SessionListViewControllerDelegate
- (void)sessionListViewController:(SessionListViewController *)controller
                 didSelectSession:(NSDictionary *)session;
@end

@interface SessionListViewController : AIViewController
    <XPTableViewDataSource, XPTableViewDelegate> {
 @private
  NSScrollView *scrollView_;
  NSTableView  *tableView_;
  NSArray      *rows_;
  NSNumber     *selectedSessionId_;
  id<SessionListViewControllerDelegate> delegate_;
}

- (void)setDelegate:(id<SessionListViewControllerDelegate>)delegate;
- (id<SessionListViewControllerDelegate>)delegate;
- (void)reloadData;
- (void)selectSessionIdentifier:(NSNumber *)sessionIdentifier;

@end
