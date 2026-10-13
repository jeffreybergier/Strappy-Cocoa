/* Isolated UIKit regression: synthetic settings only, no database or daemon.
 * The main-run-loop completion marker catches accessory layout livelocks. */
#import <UIKit/UIKit.h>
#import "../../iOS/StrappySessionOptionsTableViewController.h"
#include <assert.h>
#import "../../iOS/StrappyPreferencesModelWhitelistTableViewController.h"
#import <AIPagedRows.h>
@interface StrappyPreferencesModelWhitelistTableViewController (LoadingFixture)
- (NSDictionary *)modelRowAtIndexPath:(NSIndexPath *)path;
@end
@interface ModelFixtureRows : AIPagedRows
@property(nonatomic, assign) NSUInteger reads;
@end
@implementation ModelFixtureRows
- (NSUInteger)count { return 100000; }
- (id)copyWithZone:(NSZone *)zone { (void)zone; return self; }
- (NSArray *)pageAtOffset:(NSUInteger)offset error:(NSError **)error {
  (void)error; [self setReads:[self reads]+1];
  NSMutableArray *page=[NSMutableArray array];
  for(NSUInteger i=offset;i<MIN(offset+32,[self count]);++i)
    [page addObject:@{@"id":[NSString stringWithFormat:@"model-%lu",(unsigned long)i],@"name":@"Complete model text"}];
  return page;
}
@end
static void CheckModelLoading(void) {
  ModelFixtureRows *rows=[[ModelFixtureRows alloc] init];
  StrappyPreferencesModelWhitelistTableViewController *controller=[[StrappyPreferencesModelWhitelistTableViewController alloc] init];
  [controller setRows:rows];
  [controller setValue:@[@{@"offset":@0,@"count":@100000}] forKey:@"modelSections"];
  assert([rows count]==100000 && [rows reads]==0);
  NSUInteger jumps[]={0,99999,128,512,1024,4096,0};
  for(NSUInteger i=0;i<7;++i) {
    NSDictionary *row=[controller modelRowAtIndexPath:[NSIndexPath indexPathForRow:jumps[i] inSection:0]];
    NSString *identity=[NSString stringWithFormat:@"model-%lu",(unsigned long)jumps[i]];
    assert([[row objectForKey:@"id"] isEqual:identity]);
    assert([[row objectForKey:@"name"] isEqual:@"Complete model text"]);
    assert([rows reads]==i+1);
    if(i==5) assert([rows cachedObjectAtIndex:0]==nil);
  }
}


@interface SettingsFixture : UIResponder
    <UIApplicationDelegate, StrappySessionOptionsTableViewControllerDelegate>
@property(nonatomic, strong) UIWindow *window;
@property(nonatomic, strong) UINavigationController *navigation;
@property(nonatomic, strong) StrappySessionOptions *options;
@property(nonatomic, strong) StrappySessionOptionsTableViewController *controller;
@property(nonatomic, assign) NSUInteger refreshes;
@property(nonatomic, assign) NSUInteger optionReads;
@end

@implementation SettingsFixture
- (NSArray *)currentProviderAccounts;
{
  return @[@{@"id":@"fixture", @"name":@"Fixture", @"provider_id":@"openrouter"}];
}
- (NSArray *)currentAllowedModels;
{ return @[]; }
- (NSArray *)currentAssistantSets;
{
  return @[@{@"id":@"personal", @"name":@"Personal", @"available":@YES},
           @{@"id":@"world", @"name":@"World", @"available":@YES}];
}
- (StrappySessionOptions *)sessionOptions;
{ [self setOptionReads:[self optionReads]+1]; return [self options]; }
- (BOOL)updateSessionOptions:(StrappySessionOptions *)options
              changedFields:(StrappySessionOptionMask)fields;
{ (void)fields; [self setOptions:options]; return YES; }
- (void)dismissOptionsControllerAnimated:(BOOL)animated;
{ (void)animated; }
- (void)writeResult:(NSString *)result;
{
  [result writeToFile:[NSHomeDirectory()
      stringByAppendingPathComponent:@"Documents/settings-result.txt"]
      atomically:YES encoding:NSUTF8StringEncoding error:NULL];
}
- (void)push;
{
  [self setController:[[StrappySessionOptionsTableViewController alloc]
      initWithOptionsDelegate:self presentedModally:NO]];
  [[self navigation] pushViewController:[self controller] animated:YES];
  [self performSelector:@selector(checkAccessories) withObject:nil afterDelay:1];
}
- (void)checkAccessories;
{
  NSUInteger reads=[self optionReads];
  [[self controller] viewWillAppear:NO];
  assert([self optionReads]==reads);
  UITableView *table = [[self controller] tableView];
  NSUInteger controls = 0;
  /* Force two simultaneously live cells for every controller-owned control,
   * exactly as UITableView does while replacing a visible row. */
  for (NSInteger section = 0; section < [[self controller] numberOfSectionsInTableView:table]; ++section) {
    for (NSInteger row = 0; row < [[self controller] tableView:table numberOfRowsInSection:section]; ++row) {
      NSIndexPath *path = [NSIndexPath indexPathForRow:row inSection:section];
      UITableViewCell *old = [[self controller] tableView:table cellForRowAtIndexPath:path];
      UIView *accessory = [old accessoryView];
      if (!accessory) continue;
      ++controls;
      [old layoutIfNeeded];
      UITableViewCell *next = [[self controller] tableView:table cellForRowAtIndexPath:path];
      assert(next != old);
      assert([next accessoryView] == accessory);
      assert([old accessoryView] == nil);
      [next layoutIfNeeded];
    }
  }
  assert(controls == 5);
  [self tick];
}
- (void)tick;
{
  if ([self refreshes] == 6) {
    [self writeResult:@"PASS: complete bounded model rows; appearance does not reload options; five controls transfer ownership; repeated refreshes remain responsive\n"];
    return;
  }
  [self setRefreshes:[self refreshes] + 1];
  [[self controller] refreshOptionValues];
  /* Exercise the bottom slider rows as well as the initial visible rows. */
  if ([self refreshes] == 2)
    [[[self controller] tableView] scrollToRowAtIndexPath:
        [NSIndexPath indexPathForRow:2 inSection:4]
        atScrollPosition:UITableViewScrollPositionBottom animated:NO];
  [self performSelector:@selector(tick) withObject:nil afterDelay:0.3];
}
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions;
{
  (void)application; (void)launchOptions;
  [self writeResult:@"RUNNING\n"];
  CheckModelLoading();
  [self setOptions:[[StrappySessionOptions alloc] init]];
  [[self options] setProviderAccountIdentifier:@"fixture"];
  [self setWindow:[[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]]];
  [self setNavigation:[[UINavigationController alloc]
      initWithRootViewController:[[UIViewController alloc] init]]];
  [[self window] setRootViewController:[self navigation]];
  [[self window] makeKeyAndVisible];
  [self performSelector:@selector(push) withObject:nil afterDelay:0.3];
  return YES;
}
@end
int main(int argc, char **argv)
{
  @autoreleasepool { return UIApplicationMain(argc, argv, nil, @"SettingsFixture"); }
}
