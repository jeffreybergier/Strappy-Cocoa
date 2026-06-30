#import <AppKit/AppKit.h>

@interface StrappyPreferencesWhitelistView : NSView {
 @private
  NSView              *topAccessoryView_;
  NSSearchField       *searchField_;
  NSScrollView        *scrollView_;
  NSTableView         *tableView_;
  NSButton            *refreshButton_;
  NSProgressIndicator *progressIndicator_;
  NSTextField         *statusLabel_;
}

- (id)initWithFrame:(NSRect)frame
             target:(id)target
      refreshAction:(SEL)refreshAction
       searchAction:(SEL)searchAction
     refreshToolTip:(NSString *)refreshToolTip
         dataSource:(id)dataSource
           delegate:(id)delegate;

- (CGFloat)topAccessoryHeight;
- (CGFloat)topAccessoryTrailingControlWidth;
- (void)configureTopAccessoryView:(NSView *)view target:(id)target;
- (void)configureTableView:(NSTableView *)tableView;
- (void)addTableColumnsToTableView:(NSTableView *)tableView;
- (NSSortDescriptor *)requiredSortDescriptor;
- (NSSortDescriptor *)defaultPrimarySortDescriptor;
- (NSArray *)fallbackSortDescriptors;
- (NSString *)stableSortKey;
- (BOOL)sortKeyIsKnown:(NSString *)key;
- (NSComparisonResult)compareRow:(NSDictionary *)left
                             row:(NSDictionary *)right
                      forSortKey:(NSString *)key;
- (NSArray *)effectiveSortDescriptorsForSortDescriptors:(NSArray *)descriptors;
- (NSArray *)sortedRows:(NSArray *)rows;

- (NSView *)topAccessoryView;
- (NSSearchField *)searchField;
- (NSScrollView *)scrollView;
- (NSTableView *)tableView;
- (NSButton *)refreshButton;
- (NSProgressIndicator *)progressIndicator;
- (NSTextField *)statusLabel;

@end
