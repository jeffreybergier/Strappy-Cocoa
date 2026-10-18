#import <UIKit/UIKit.h>
#import "AIFontAwesome.h"
#import "../../iOS/SessionListViewController.h"
#import "StrappySession.h"
#include <assert.h>
extern StrappySessionRows *StrappyCheckSessionRows(NSString *, NSString **);
@interface SessionListViewController (ScrollProbe)
- (void)sidebarLoaded:(NSDictionary *)result;
- (void)sessionPromptActivityDidChange:(NSNotification *)notification;
- (void)configureSessionCell:(UITableViewCell *)cell session:(NSDictionary *)row;
@end
@interface ScrollList : SessionListViewController
@end
@implementation ScrollList
- (void)reloadData {} /* Isolated fixture: no production store or daemon. */
@end
@interface ScrollFixture : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@property(nonatomic, strong) ScrollList *controller;
@property(nonatomic, copy) NSString *report;
@property(nonatomic, assign) NSUInteger tickCount;
@end
@implementation ScrollFixture
- (void)writeResult:(NSString *)text;
{
  [text writeToFile:[NSHomeDirectory() stringByAppendingPathComponent:
    @"Documents/session-scroll-result.txt"] atomically:YES
    encoding:NSUTF8StringEncoding error:NULL];
}
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options;
{
  (void)application; (void)options;
  [self writeResult:@"RUNNING"];
  [AIFontAwesome registerBundledFonts];
  NSString *report = nil;
  StrappySessionRows *rows = StrappyCheckSessionRows([[NSBundle mainBundle]
    pathForResource:@"sidebar-fixture" ofType:@"sqlite"], &report);
  [self setReport:report];
  ScrollList *owner = [[ScrollList alloc] init];
  [self setController:owner];
  (void)[owner view];
  [owner setValue:rows forKey:@"sessions"];
  [owner setValue:[StrappySessionSections sectionsForSessions:rows] forKey:@"sections"];
  [owner refreshCollection];
  [self setWindow:[[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]]];
  [[self window] setRootViewController:[[UINavigationController alloc]
    initWithRootViewController:owner]];
  [[self window] makeKeyAndVisible];
  UITableViewCell *cell = [[UITableViewCell alloc]
    initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:@"probe"];
  NSMutableDictionary *row = [[rows objectAtIndex:0] mutableCopy];
  [row setObject:@YES forKey:@"has_new_answers"];
  [owner configureSessionCell:cell session:row];
  assert([[cell accessibilityValue] isEqual:NSLocalizedString(@"Unread", nil)] && [cell accessoryView]);
  [row setObject:@YES forKey:@"prompt_in_flight"];
  [owner configureSessionCell:cell session:row];
  assert([[cell accessibilityValue] isEqual:NSLocalizedString(@"Processing", nil)] && [cell accessoryView]);
  [row setObject:@NO forKey:@"prompt_in_flight"];
  [row setObject:@NO forKey:@"has_new_answers"];
  [owner configureSessionCell:cell session:row];
  assert(![cell accessibilityValue] && ![cell accessoryView]);
  [self performSelector:@selector(tick) withObject:nil afterDelay:0.2];
  return YES;
}
- (void)tick;
{
  UITableView *table = [[self controller] tableView];
  NSUInteger jumps[] = {0, 19999, 1024, 64, 512, 0};
  NSUInteger tick = [self tickCount];
  if (tick == 6) {
    /* A replacement built from the older fixture must not erase pushed state. */
    StrappySessionRows *replacement = [[StrappySessionRows alloc]
      initWithDatabasePath:[[NSBundle mainBundle] pathForResource:
        @"sidebar-fixture" ofType:@"sqlite"] error:NULL];
    [[self controller] sidebarLoaded:@{@"sessions":replacement,
      @"sections":[StrappySessionSections sectionsForSessions:replacement]}];
    assert([[[replacement objectAtIndex:0] objectForKey:@"prompt_in_flight"] boolValue]);
    [self writeResult:[[self report] stringByAppendingString:
      @"; UIKit actual cells, processing/unread precedence, scroll jumps and scoped updates PASS\n"]];
    return;
  }
  [table scrollToRowAtIndexPath:[NSIndexPath indexPathForRow:(NSInteger)jumps[tick]
    inSection:0] atScrollPosition:UITableViewScrollPositionTop animated:NO];
  [table layoutIfNeeded];
  CGPoint before = [table contentOffset];
  NSIndexPath *visible = [[table indexPathsForVisibleRows] objectAtIndex:0];
  StrappySessionRows *rows = [[self controller] valueForKey:@"sessions"];
  NSDictionary *row = [rows objectAtIndex:(NSUInteger)[visible row]];
  [[self controller] sessionPromptActivityDidChange:[NSNotification
    notificationWithName:StrappySessionListActivityDidChangeNotification object:nil
    userInfo:@{[row objectForKey:@"id"]:@YES}]];
  [table layoutIfNeeded];
  assert(CGPointEqualToPoint(before, [table contentOffset]));
  assert([[[table cellForRowAtIndexPath:visible] accessibilityValue] isEqual:NSLocalizedString(@"Processing", nil)]);
  [self setTickCount:tick+1];
  [self performSelector:@selector(tick) withObject:nil afterDelay:0.2];
}
@end
int main(int argc, char **argv) {
  @autoreleasepool { return UIApplicationMain(argc, argv, nil, @"ScrollFixture"); }
}
