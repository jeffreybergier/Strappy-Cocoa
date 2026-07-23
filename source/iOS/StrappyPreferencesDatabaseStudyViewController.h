#import <UIKit/UIKit.h>

@interface StrappyPreferencesDatabaseStudyViewController : UITableViewController
  <UIAlertViewDelegate, UIActionSheetDelegate> {
 @private
  NSArray *allStudyRows_;
  NSArray *studySections_;
  BOOL showsUnstudiedOnly_;
  UIBarButtonItem *filterButton_;
  UILabel *statusLabel_;
  NSDateFormatter *studyDateFormatter_;
}

@end
