#import "StrappyRootCoordinator.h"

#import "SessionListViewController.h"
#import "StrappySession.h"

@interface StrappyRootCoordinator () <SessionListViewControllerDelegate>
@property (nonatomic, strong) UINavigationController *nav;
@property (nonatomic, strong) SessionListViewController *sessionList;
@end

@implementation StrappyRootCoordinator

- (instancetype)initWithWindow:(UIWindow *)window
{
  if (window == nil) {
    @throw [NSException exceptionWithName:NSInvalidArgumentException
                                  reason:@"window is nil"
                                userInfo:nil];
  }

  if ((self = [super init])) {
    self.nav = [[UINavigationController alloc] init];
    window.rootViewController = self.nav;
  }
  return self;
}

- (void)start
{
  NSError *error;

  error = nil;
  if (![StrappySession initializeSessionStoreWithError:&error]) {
    NSLog(@"StrappyRootCoordinator.start initialize store failed: %@",
          [error localizedDescription]);
  }

  self.sessionList = [[SessionListViewController alloc] init];
  self.sessionList.delegate = self;
  [self.nav setViewControllers:[NSArray arrayWithObject:self.sessionList]
                       animated:NO];
}

- (void)sessionListViewController:(SessionListViewController *)controller
                 didSelectSession:(StrappySession *)session
{
  (void)controller;
  (void)session;
}

@end
