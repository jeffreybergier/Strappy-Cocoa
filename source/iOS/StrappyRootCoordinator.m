#import "StrappyRootCoordinator.h"

#import "MessageListViewController.h"
#import "SessionListViewController.h"
#import "StrappySession.h"

@interface StrappyRootCoordinator () <SessionListViewControllerDelegate,
                                      MessageListViewControllerDelegate>
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
    [self setNav:[[UINavigationController alloc] init]];
    [window setRootViewController:[self nav]];
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

  [self setSessionList:[[SessionListViewController alloc] init]];
  [[self sessionList] setDelegate:self];
  [[self nav] setViewControllers:[NSArray arrayWithObject:[self sessionList]]
                         animated:NO];
}

- (void)sessionListViewController:(SessionListViewController *)controller
                 didSelectSession:(StrappySession *)session
{
  MessageListViewController *messages;

  (void)controller;
  if (![session isKindOfClass:[StrappySession class]]) {
    return;
  }

  messages = [[MessageListViewController alloc] initWithSession:session];
  [messages setDelegate:self];
  [[self nav] pushViewController:messages animated:YES];
}

- (void)messageListViewController:(MessageListViewController *)controller
                 didUpdateSession:(NSDictionary *)session
{
  NSNumber *identifier;

  (void)controller;
  if (![session isKindOfClass:[NSDictionary class]]) {
    [[self sessionList] reloadData];
    return;
  }

  identifier = [session objectForKey:@"id"];
  [[self sessionList] reloadSessionIdentifier:identifier select:NO];
}

@end
