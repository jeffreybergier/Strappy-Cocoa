#import <UIKit/UIKit.h>

@interface StrappyPreferencesWhitelistTableViewController :
    UITableViewController <UISearchBarDelegate>

@property (nonatomic, copy) NSArray *allRows;
@property (nonatomic, copy) NSArray *rows;
@property (nonatomic, copy) NSString *statusMessage;
@property (nonatomic, strong) UISearchBar *searchBar;
@property (nonatomic, strong) UILabel *statusLabel;
@property (nonatomic, assign) BOOL working;

- (instancetype)initWithTitle:(NSString *)title;
- (void)reloadRows;
- (void)applyRows;
- (void)buildStatusToolbar;
- (void)refreshStatusToolbar;
- (NSArray *)loadAllRowsWithError:(NSError **)error;
- (NSArray *)preparedRowsForRows:(NSArray *)rows;
- (NSArray *)sortedRows:(NSArray *)rows;
- (BOOL)row:(NSDictionary *)row matchesSearchText:(NSString *)searchText;
- (BOOL)rowIsSelected:(NSDictionary *)row;
- (NSString *)currentSearchText;
- (NSString *)workingStatusText;
- (NSString *)statusText;
- (NSString *)emptyText;
- (NSString *)actionButtonAccessibilityLabel;
- (void)actionButtonPressed:(id)sender;
- (void)configureCell:(UITableViewCell *)cell withRow:(NSDictionary *)row;
- (void)useRow:(NSDictionary *)row atIndexPath:(NSIndexPath *)indexPath;
- (void)showError:(NSError *)error title:(NSString *)title;
- (void)showMessage:(NSString *)message title:(NSString *)title;

@end
