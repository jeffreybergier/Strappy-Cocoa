#import "AppDelegate.h"
#import "PreferencesWindowController.h"
#import "SessionWindowController.h"
#import "StrappySession.h"
#import <AltivecCore/AltivecCore.h>

@interface AppDelegate (Private)
- (void)setupMenu;
- (void)showMainWindow;
- (void)strappySessionPromptDidStart:(NSNotification *)notification;
- (void)strappySessionPromptDidFinish:(NSNotification *)notification;
- (void)releaseFinishedPromptSession:(StrappySession *)session;
- (void)terminateIfPendingInFlightSessionsFinished;
@end

@implementation AppDelegate

- (void)applicationWillFinishLaunching:(NSNotification *)notification
{
  (void)notification;
  [self setupMenu];
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
  (void)notification;
  {
    NSString *cacert = [AltivecCore certPath];
    NSParameterAssert(cacert);
    [StrappySession bootstrapProcessWithCACertPath:cacert];
  }
  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(strappySessionPromptDidStart:)
           name:StrappySessionPromptDidStartNotification
         object:nil];
  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(strappySessionPromptDidFinish:)
           name:StrappySessionPromptDidFinishNotification
         object:nil];
  [self showMainWindow];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
  (void)sender;
  if ([StrappySession hasInFlightSessions] || ([_inFlightSessions count] > 0U)) {
    _terminateWhenInFlightSessionsFinish = YES;
    return NO;
  }
  return YES;
}

- (void)showAboutWindow:(id)sender
{
  (void)sender;
  [NSApp orderFrontStandardAboutPanel:self];
}

- (void)showPreferencesWindow:(id)sender
{
  (void)sender;
  if (_preferencesWindowController == nil) {
    _preferencesWindowController = [[PreferencesWindowController alloc] init];
  }

  [_preferencesWindowController showWindow:self];
  [[_preferencesWindowController window] makeKeyAndOrderFront:self];
}

- (void)setupMenu
{
  NSMenu *mainMenu = [[[NSMenu alloc] initWithTitle:@"MainMenu"] autorelease];

  NSMenuItem *appMenuItem = [mainMenu addItemWithTitle:@""
                                                action:NULL
                                         keyEquivalent:@""];
  NSMenu *appMenu = [[[NSMenu alloc] initWithTitle:@""] autorelease];
  [mainMenu setSubmenu:appMenu forItem:appMenuItem];

  if ([NSApp respondsToSelector:@selector(setAppleMenu:)]) {
    [NSApp performSelector:@selector(setAppleMenu:) withObject:appMenu];
  }

  [appMenu addItemWithTitle:NSLocalizedString(@"About Strappy", nil)
                     action:@selector(showAboutWindow:)
              keyEquivalent:@""];
  {
    NSMenuItem *preferences =
      [appMenu addItemWithTitle:NSLocalizedString(@"Preferences...", nil)
                         action:@selector(showPreferencesWindow:)
                  keyEquivalent:@","];
    [preferences setTarget:self];
  }
  [appMenu addItem:[NSMenuItem separatorItem]];
  [appMenu addItemWithTitle:NSLocalizedString(@"Hide Strappy", nil)
                     action:@selector(hide:)
              keyEquivalent:@"h"];
  {
    NSMenuItem *hideOthers =
      [appMenu addItemWithTitle:NSLocalizedString(@"Hide Others", nil)
                         action:@selector(hideOtherApplications:)
                  keyEquivalent:@"h"];
    [hideOthers setKeyEquivalentModifierMask:(XPEventModifierFlagCommand |
                                              XPEventModifierFlagOption)];
  }
  [appMenu addItemWithTitle:NSLocalizedString(@"Show All", nil)
                     action:@selector(unhideAllApplications:)
              keyEquivalent:@""];
  [appMenu addItem:[NSMenuItem separatorItem]];
  [appMenu addItemWithTitle:NSLocalizedString(@"Quit Strappy", nil)
                     action:@selector(terminate:)
              keyEquivalent:@"q"];

  NSMenuItem *editMenuItem = [mainMenu addItemWithTitle:NSLocalizedString(@"Edit", nil)
                                                 action:NULL
                                          keyEquivalent:@""];
  NSMenu *editMenu = [[[NSMenu alloc]
      initWithTitle:NSLocalizedString(@"Edit", nil)] autorelease];
  [mainMenu setSubmenu:editMenu forItem:editMenuItem];
  [editMenu addItemWithTitle:NSLocalizedString(@"Undo", nil)
                      action:@selector(undo:)
               keyEquivalent:@"z"];
  [editMenu addItemWithTitle:NSLocalizedString(@"Redo", nil)
                      action:@selector(redo:)
               keyEquivalent:@"Z"];
  [editMenu addItem:[NSMenuItem separatorItem]];
  [editMenu addItemWithTitle:NSLocalizedString(@"Cut", nil)
                      action:@selector(cut:)
               keyEquivalent:@"x"];
  [editMenu addItemWithTitle:NSLocalizedString(@"Copy", nil)
                      action:@selector(copy:)
               keyEquivalent:@"c"];
  [editMenu addItemWithTitle:NSLocalizedString(@"Paste", nil)
                      action:@selector(paste:)
               keyEquivalent:@"v"];
  [editMenu addItemWithTitle:NSLocalizedString(@"Select All", nil)
                      action:@selector(selectAll:)
               keyEquivalent:@"a"];

  NSMenuItem *conversationMenuItem =
    [mainMenu addItemWithTitle:NSLocalizedString(@"Conversation", nil)
                        action:NULL
                 keyEquivalent:@""];
  NSMenu *conversationMenu = [[[NSMenu alloc]
      initWithTitle:NSLocalizedString(@"Conversation", nil)] autorelease];
  [mainMenu setSubmenu:conversationMenu forItem:conversationMenuItem];
  {
    NSMenuItem *newSession =
      [conversationMenu addItemWithTitle:NSLocalizedString(@"New Session", nil)
                                  action:@selector(newSession:)
                           keyEquivalent:@"n"];
    [newSession setTarget:nil];
  }
  {
    NSMenuItem *sendPrompt =
      [conversationMenu addItemWithTitle:NSLocalizedString(@"Send Prompt", nil)
                                  action:@selector(sendCurrentPrompt:)
                           keyEquivalent:@"\r"];
    [sendPrompt setKeyEquivalentModifierMask:XPEventModifierFlagCommand];
    [sendPrompt setTarget:nil];
  }

  NSMenuItem *viewMenuItem = [mainMenu addItemWithTitle:NSLocalizedString(@"View", nil)
                                                 action:NULL
                                          keyEquivalent:@""];
  NSMenu *viewMenu = [[[NSMenu alloc]
      initWithTitle:NSLocalizedString(@"View", nil)] autorelease];
  [mainMenu setSubmenu:viewMenu forItem:viewMenuItem];
  {
    NSMenuItem *toggleSidebar =
      [viewMenu addItemWithTitle:NSLocalizedString(@"Toggle Sidebar", nil)
                          action:@selector(toggleSidebar:)
                   keyEquivalent:@"s"];
    [toggleSidebar setKeyEquivalentModifierMask:(XPEventModifierFlagCommand |
                                                 XPEventModifierFlagOption)];
    [toggleSidebar setTarget:nil];
  }

  NSMenuItem *windowMenuItem = [mainMenu addItemWithTitle:NSLocalizedString(@"Window", nil)
                                                   action:NULL
                                            keyEquivalent:@""];
  NSMenu *windowMenu = [[[NSMenu alloc]
      initWithTitle:NSLocalizedString(@"Window", nil)] autorelease];
  [mainMenu setSubmenu:windowMenu forItem:windowMenuItem];
  [NSApp setWindowsMenu:windowMenu];
  [windowMenu addItemWithTitle:NSLocalizedString(@"Minimize", nil)
                        action:@selector(performMiniaturize:)
                 keyEquivalent:@"m"];
  [windowMenu addItemWithTitle:NSLocalizedString(@"Zoom", nil)
                        action:@selector(performZoom:)
                 keyEquivalent:@""];
  [windowMenu addItem:[NSMenuItem separatorItem]];
  [windowMenu addItemWithTitle:NSLocalizedString(@"Bring All to Front", nil)
                        action:@selector(arrangeInFront:)
                 keyEquivalent:@""];

  [NSApp setMainMenu:mainMenu];
}

- (void)showMainWindow
{
  _terminateWhenInFlightSessionsFinish = NO;
  if (_windowController != nil) {
    [_windowController showWindow:self];
    return;
  }

  _windowController = [[SessionWindowController alloc] init];
  [_windowController showWindow:self];
}

- (void)strappySessionPromptDidFinish:(NSNotification *)notification
{
  StrappySession *session;

  session = [notification object];
  if ([session isKindOfClass:[StrappySession class]]) {
    [self performSelectorOnMainThread:@selector(releaseFinishedPromptSession:)
                           withObject:session
                        waitUntilDone:NO];
    return;
  }
  [self terminateIfPendingInFlightSessionsFinished];
}

- (void)strappySessionPromptDidStart:(NSNotification *)notification
{
  StrappySession *session;
  NSNumber *identifier;

  session = [notification object];
  if (![session isKindOfClass:[StrappySession class]]) {
    return;
  }

  identifier = [session sessionIdentifier];
  if (![identifier isKindOfClass:[NSNumber class]]) {
    return;
  }

  if (_inFlightSessions == nil) {
    _inFlightSessions = [[NSMutableDictionary alloc] init];
  }
  [_inFlightSessions setObject:session forKey:identifier];
}

- (void)releaseFinishedPromptSession:(StrappySession *)session
{
  NSNumber *identifier;

  if ([session isKindOfClass:[StrappySession class]]) {
    identifier = [session sessionIdentifier];
    if ([identifier isKindOfClass:[NSNumber class]] &&
        ([_inFlightSessions objectForKey:identifier] == session)) {
      [_inFlightSessions removeObjectForKey:identifier];
    }
  }

  [self terminateIfPendingInFlightSessionsFinished];
}

- (void)terminateIfPendingInFlightSessionsFinished
{
  if (!_terminateWhenInFlightSessionsFinish) {
    return;
  }
  if ([StrappySession hasInFlightSessions]) {
    return;
  }
  if ([_inFlightSessions count] > 0U) {
    return;
  }
  [NSApp terminate:self];
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [_windowController release];
  [_preferencesWindowController release];
  [_inFlightSessions release];
  [super dealloc];
}

@end
