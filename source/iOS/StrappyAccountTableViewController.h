#import <AltivecCocoa/AICollectionTableViewController.h>
#import <UIKit/UIKit.h>

@interface StrappyAccountTableViewController : AICollectionTableViewController

- (id)initWithProviderAccountIdentifier:(NSString *)identifier;
- (id)initWithProviderAccountIdentifier:(NSString *)identifier
                       presentedModally:(BOOL)presentedModally;

@end

@interface StrappyProviderPickerTableViewController : AICollectionTableViewController
@end
