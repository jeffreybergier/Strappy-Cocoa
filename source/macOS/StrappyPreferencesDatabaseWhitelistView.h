#import <AppKit/AppKit.h>

@interface StrappyPreferencesDatabaseWhitelistView : NSView {
 @private
  NSTableView         *tableView_;
  NSButton            *scanButton_;
  NSProgressIndicator *progressIndicator_;
}

- (id)initWithFrame:(NSRect)frame
             target:(id)target
         dataSource:(id)dataSource
           delegate:(id)delegate;
- (NSTableView *)tableView;
- (NSButton *)scanButton;
- (NSProgressIndicator *)progressIndicator;

@end
