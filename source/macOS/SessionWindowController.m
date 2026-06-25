#import "SessionWindowController.h"
#import "StrappySession.h"

@interface SessionWindowController ()
- (void)strappySessionDidUpdate:(NSNotification *)notification;
@end

@implementation SessionWindowController

- (id)init
{
  if ((self = [super initWithTitle:NSLocalizedString(@"Strappy", nil)
                      autosaveName:@"StrappySessions"])) {
    sessionsController_ = [[SessionListViewController alloc] init];
    messagesController_ = [[MessageListViewController alloc] init];

    [sessionsController_ setDelegate:self];
    [messagesController_ setDelegate:self];

    [self setSidebarViewController:sessionsController_];
    [self setDetailViewController:messagesController_];
    [self setSidebarWidthLimits:AIMinMidMaxMake(180.0, 260.0, 360.0)];
    [self setSplitViewAutosaveName:@"StrappySessionSplit"];

    [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(strappySessionDidUpdate:)
             name:StrappySessionDidUpdateNotification
           object:nil];
  }
  return self;
}

- (void)windowDidLoad
{
  [super windowDidLoad];
  [[self window] setTitle:NSLocalizedString(@"Strappy", nil)];
}

- (void)newSession:(id)sender
{
  [sessionsController_ addSession:sender];
}

- (BOOL)canSendCurrentPrompt
{
  return [messagesController_ canSendCurrentPrompt];
}

- (void)sendCurrentPrompt:(id)sender
{
  [messagesController_ sendCurrentPrompt:sender];
}

- (void)sessionListViewController:(SessionListViewController *)controller
                 didSelectSession:(StrappySession *)session
{
  (void)controller;
  [messagesController_ reloadWithSession:session];
}

- (void)strappySessionDidUpdate:(NSNotification *)notification
{
  NSDictionary *session;
  NSNumber *identifier;

  session = [[notification userInfo] objectForKey:@"session"];
  if (![session isKindOfClass:[NSDictionary class]]) {
    return;
  }

  identifier = [session objectForKey:@"id"];
  if (![identifier isKindOfClass:[NSNumber class]]) {
    return;
  }

  [sessionsController_ reloadSessionIdentifier:identifier select:NO];
}

- (void)messageListViewController:(MessageListViewController *)controller
                  didUpdateSession:(NSDictionary *)session
{
  (void)controller;
  [sessionsController_ reloadSessionIdentifier:[session objectForKey:@"id"]
                                        select:YES];
}

- (BOOL)validateMenuItem:(NSMenuItem *)item
{
  SEL action;

  action = [item action];
  if (action == @selector(sendCurrentPrompt:)) {
    return [self canSendCurrentPrompt];
  }
  return YES;
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [sessionsController_ release];
  [messagesController_ release];
  [super dealloc];
}

@end
