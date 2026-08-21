#import <UIKit/UIKit.h>

#import "StrappySession.h"

@class StrappySessionOptionsTableViewController;

@protocol StrappySessionOptionsTableViewControllerDelegate <NSObject>
- (NSArray *)currentProviderAccounts;
- (NSArray *)currentAllowedModels;
- (NSArray *)currentAssistantSets;
- (StrappySessionOptions *)sessionOptions;
- (BOOL)updateSessionOptions:(StrappySessionOptions *)options
               changedFields:(StrappySessionOptionMask)changedFields;
- (void)dismissOptionsControllerAnimated:(BOOL)animated;
@end

@interface StrappySessionOptionsTableViewController : UITableViewController

- (instancetype)initWithOptionsDelegate:
    (id<StrappySessionOptionsTableViewControllerDelegate>)optionsDelegate
                       presentedModally:(BOOL)presentedModally;
- (void)reloadOptionsFromDelegate;

@end
