#import <AppKit/AppKit.h>
#import "XPAppKit.h"

@interface StrappyModelProviderEditor : NSObject
    <XPTableViewDataSource, XPTableViewDelegate> {
 @private
  id                   target_;
  NSPanel              *sheet_;
  NSTableView          *providerTableView_;
  NSView               *detailView_;
  NSArray              *providers_;
  NSArray              *models_;
  NSArray              *bundledChatGPTModels_;
  NSArray              *otherModels_;
  NSMutableDictionary  *draftOtherModel_;
  NSString             *selectedProviderIdentifier_;
  NSTableView          *otherTableView_;
  NSButton             *fetchButton_;
  NSSegmentedControl   *otherModelActionsSegmented_;
  NSProgressIndicator  *progressIndicator_;
}

- (id)initWithTarget:(id)target;
- (void)beginSheetForWindow:(NSWindow *)window;

@end
