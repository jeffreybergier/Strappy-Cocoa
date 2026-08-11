#import <AppKit/AppKit.h>

@interface StrappyPreferencesWhitelistView : NSView {
 @private
  NSView              *topAccessoryView_;
  NSView              *bottomAccessoryView_;
  NSSearchField       *searchField_;
  NSScrollView        *scrollView_;
  NSTableView         *tableView_;
  NSProgressIndicator *progressIndicator_;
  NSTextField         *statusLabel_;
}

- (id)initWithFrame:(NSRect)frame
             target:(id)target
         dataSource:(id)dataSource
           delegate:(id)delegate;

- (CGFloat)topAccessoryHeight;
- (CGFloat)topAccessoryTrailingControlWidth;
- (void)configureTopAccessoryView:(NSView *)view target:(id)target;
- (CGFloat)bottomAccessoryLeadingControlWidth;
- (void)configureBottomAccessoryView:(NSView *)view target:(id)target;
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
- (NSView *)bottomAccessoryView;
- (NSSearchField *)searchField;
- (NSScrollView *)scrollView;
- (NSTableView *)tableView;
- (NSProgressIndicator *)progressIndicator;
- (NSTextField *)statusLabel;

@end
