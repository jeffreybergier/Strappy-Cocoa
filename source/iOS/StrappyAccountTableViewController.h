#import <UIKit/UIKit.h>

@interface StrappyAccountTableViewController : UITableViewController

- (id)initWithProviderAccountIdentifier:(NSString *)identifier;
- (id)initWithProviderAccountIdentifier:(NSString *)identifier
                       presentedModally:(BOOL)presentedModally;

@end

@interface StrappyProviderPickerTableViewController : UITableViewController
@end
