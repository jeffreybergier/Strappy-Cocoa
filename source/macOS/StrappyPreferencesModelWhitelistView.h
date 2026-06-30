#import <AppKit/AppKit.h>

@interface StrappyPreferencesModelWhitelistView : NSView {
 @private
  NSSearchField       *searchField_;
  NSPopUpButton       *defaultModelPopUpButton_;
  NSTableView         *tableView_;
  NSButton            *fetchButton_;
  NSProgressIndicator *progressIndicator_;
  NSTextField         *statusLabel_;
}

- (id)initWithFrame:(NSRect)frame
             target:(id)target
         dataSource:(id)dataSource
           delegate:(id)delegate;
- (NSSearchField *)searchField;
- (NSPopUpButton *)defaultModelPopUpButton;
- (NSTableView *)tableView;
- (NSButton *)fetchButton;
- (NSProgressIndicator *)progressIndicator;
- (NSTextField *)statusLabel;

@end
