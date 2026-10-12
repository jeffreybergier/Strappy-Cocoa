#import <AltivecCocoa/AICollectionTableViewController.h>
#import <UIKit/UIKit.h>

@class StrappyPreferencesStatusToolbarView;

@interface StrappyPreferencesDatabaseStudyViewController : AICollectionTableViewController
  <UIAlertViewDelegate, UIActionSheetDelegate> {
 @private
  NSArray *studyRows_;
  NSArray *studySections_;
  UIBarButtonItem *studyActionButton_;
  UIBarButtonItem *statusToolbarItem_;
  StrappyPreferencesStatusToolbarView *statusToolbarView_;
  NSDateFormatter *studyDateFormatter_;
  NSString *expandedDatabaseIdentifier_;
  NSString *measuredDatabaseIdentifier_;
  NSString *measuredStudyDetails_;
  CGFloat measuredStudyWidth_;
  CGFloat measuredStudyRowHeight_;
}

@end
