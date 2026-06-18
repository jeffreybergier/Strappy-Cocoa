#import "SessionWindowController.h"

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
  (void)sender;
  [sessionsController_ selectSessionIdentifier:nil];
  [messagesController_ reloadWithSession:nil];
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
                 didSelectSession:(NSDictionary *)session
{
  (void)controller;
  [messagesController_ reloadWithSession:session];
}

- (void)messageListViewController:(MessageListViewController *)controller
                 didCreateSession:(NSDictionary *)session
{
  (void)controller;
  [sessionsController_ reloadSessionIdentifier:[session objectForKey:@"id"]
                                        select:YES];
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
  [sessionsController_ release];
  [messagesController_ release];
  [super dealloc];
}

@end
