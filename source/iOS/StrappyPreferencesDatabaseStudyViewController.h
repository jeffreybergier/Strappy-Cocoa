#import <UIKit/UIKit.h>

@interface StrappyPreferencesDatabaseStudyViewController : UITableViewController
  <UIAlertViewDelegate, UIActionSheetDelegate> {
 @private
  NSArray *studyRows_;
  NSArray *studySections_;
  UIBarButtonItem *studyActionButton_;
  UILabel *statusLabel_;
  NSDateFormatter *studyDateFormatter_;
  NSString *expandedDatabaseIdentifier_;
  NSString *measuredDatabaseIdentifier_;
  NSString *measuredStudyDetails_;
  CGFloat measuredStudyWidth_;
  CGFloat measuredStudyRowHeight_;
}

@end
