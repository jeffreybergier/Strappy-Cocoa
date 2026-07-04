#import "AppDelegate.h"
#import "PreferencesWindowController.h"
#import "SessionWindowController.h"
#import "StrappySession.h"
#import <AltivecCore/AltivecCore.h>

@interface AppDelegate (Private)
- (void)setupMenu;
- (void)showMainWindow;
- (void)populateModelMenu:(NSMenu *)menu;
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
  _terminateWhenInFlightSessionsFinish = NO;
  return NO;
}

- (BOOL)applicationShouldHandleReopen:(NSApplication *)sender
                    hasVisibleWindows:(BOOL)flag
{
  NSWindow *mainWindow;

  (void)sender;
  (void)flag;
  mainWindow = (_windowController != nil) ? [_windowController window] : nil;
  if ((mainWindow == nil) || ![mainWindow isVisible]) {
    [self showMainWindow];
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
  [appMenu addItem:[NSMenuItem separatorItem]];
  {
    NSMenuItem *preferences =
      [appMenu addItemWithTitle:NSLocalizedString(@"Preferences...", nil)
                         action:@selector(showPreferencesWindow:)
                  keyEquivalent:@","];
    [preferences setTarget:self];
  }
  [appMenu addItem:[NSMenuItem separatorItem]];
  {
    NSMenuItem *servicesItem =
      [appMenu addItemWithTitle:NSLocalizedString(@"Services", nil)
                         action:NULL
                  keyEquivalent:@""];
    NSMenu *servicesMenu = [[[NSMenu alloc]
        initWithTitle:NSLocalizedString(@"Services", nil)] autorelease];
    [appMenu setSubmenu:servicesMenu forItem:servicesItem];
    if ([NSApp respondsToSelector:@selector(setServicesMenu:)]) {
      [NSApp performSelector:@selector(setServicesMenu:)
                   withObject:servicesMenu];
    }
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

  NSMenuItem *chatMenuItem =
    [mainMenu addItemWithTitle:NSLocalizedString(@"Chat", nil)
                        action:NULL
                 keyEquivalent:@""];
  NSMenu *chatMenu = [[[NSMenu alloc]
      initWithTitle:NSLocalizedString(@"Chat", nil)] autorelease];
  [mainMenu setSubmenu:chatMenu forItem:chatMenuItem];
  {
    NSMenuItem *newSession =
      [chatMenu addItemWithTitle:NSLocalizedString(@"New Chat", nil)
                           action:@selector(newSession:)
                    keyEquivalent:@"n"];
    [newSession setTarget:nil];
  }
  {
    NSMenuItem *closeChat =
      [chatMenu addItemWithTitle:NSLocalizedString(@"Close Chat", nil)
                          action:@selector(closeCurrentChat:)
                   keyEquivalent:@"w"];
    [closeChat setKeyEquivalentModifierMask:(XPEventModifierFlagCommand |
                                             XPEventModifierFlagOption)];
    [closeChat setTarget:nil];
  }
  {
    NSMenuItem *deleteChat =
      [chatMenu addItemWithTitle:NSLocalizedString(@"Delete Chat...", nil)
                          action:@selector(deleteCurrentChat:)
                   keyEquivalent:[NSString stringWithFormat:@"%C",
                    (unichar)NSDeleteCharacter]];
    [deleteChat setKeyEquivalentModifierMask:XPEventModifierFlagCommand];
    [deleteChat setTarget:nil];
  }
  [chatMenu addItem:[NSMenuItem separatorItem]];
  {
    NSMenuItem *sendPrompt =
      [chatMenu addItemWithTitle:NSLocalizedString(@"Send Prompt", nil)
                           action:@selector(sendCurrentPrompt:)
                    keyEquivalent:@"\r"];
    [sendPrompt setKeyEquivalentModifierMask:XPEventModifierFlagCommand];
    [sendPrompt setTarget:nil];
  }
  {
    NSMenuItem *cancelPrompt =
      [chatMenu addItemWithTitle:NSLocalizedString(@"Cancel Prompt", nil)
                           action:@selector(cancelCurrentPrompt:)
                    keyEquivalent:@"."];
    [cancelPrompt setKeyEquivalentModifierMask:XPEventModifierFlagCommand];
    [cancelPrompt setTarget:nil];
  }
  [chatMenu addItem:[NSMenuItem separatorItem]];
  {
    NSMenuItem *modelItem =
      [chatMenu addItemWithTitle:NSLocalizedString(@"Model", nil)
                          action:NULL
                   keyEquivalent:@""];
    NSMenu *modelMenu = [[[NSMenu alloc]
        initWithTitle:NSLocalizedString(@"Model", nil)] autorelease];
    [chatMenu setSubmenu:modelMenu forItem:modelItem];
    [modelMenu setDelegate:self];
    [_modelMenu release];
    _modelMenu = [modelMenu retain];
    [self populateModelMenu:modelMenu];
  }
  {
    NSMenuItem *streaming =
      [chatMenu addItemWithTitle:NSLocalizedString(@"Streaming", nil)
                          action:@selector(toggleStreaming:)
                   keyEquivalent:@""];
    [streaming setTarget:nil];
  }
  [chatMenu addItem:[NSMenuItem separatorItem]];
  {
    NSMenuItem *pageSetup =
      [chatMenu addItemWithTitle:NSLocalizedString(@"Page Setup...", nil)
                          action:@selector(runPageLayout:)
                   keyEquivalent:@"p"];
    [pageSetup setKeyEquivalentModifierMask:(XPEventModifierFlagCommand |
                                             XPEventModifierFlagShift)];
    [pageSetup setTarget:nil];
  }
  {
    NSMenuItem *print =
      [chatMenu addItemWithTitle:NSLocalizedString(@"Print...", nil)
                          action:@selector(printCurrentChat:)
                   keyEquivalent:@"p"];
    [print setTarget:nil];
  }

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
  {
    NSMenuItem *pasteAndMatchStyle =
      [editMenu addItemWithTitle:NSLocalizedString(@"Paste and Match Style", nil)
                          action:@selector(pasteAsPlainText:)
                   keyEquivalent:@"v"];
    [pasteAndMatchStyle
      setKeyEquivalentModifierMask:(XPEventModifierFlagCommand |
                                    XPEventModifierFlagOption |
                                    XPEventModifierFlagShift)];
  }
  [editMenu addItemWithTitle:NSLocalizedString(@"Delete", nil)
                      action:@selector(delete:)
               keyEquivalent:@""];
  [editMenu addItemWithTitle:NSLocalizedString(@"Select All", nil)
                      action:@selector(selectAll:)
               keyEquivalent:@"a"];
  [editMenu addItem:[NSMenuItem separatorItem]];
  {
    NSMenuItem *findMenuItem =
      [editMenu addItemWithTitle:NSLocalizedString(@"Find", nil)
                          action:NULL
                   keyEquivalent:@""];
    NSMenu *findMenu = [[[NSMenu alloc]
        initWithTitle:NSLocalizedString(@"Find", nil)] autorelease];
    NSMenuItem *item;

    item = [findMenu addItemWithTitle:NSLocalizedString(@"Find...", nil)
                               action:@selector(performFindPanelAction:)
                        keyEquivalent:@"f"];
    [item setTag:1];
    item = [findMenu addItemWithTitle:NSLocalizedString(@"Find Next", nil)
                               action:@selector(performFindPanelAction:)
                        keyEquivalent:@"g"];
    [item setTag:2];
    item = [findMenu addItemWithTitle:NSLocalizedString(@"Find Previous", nil)
                               action:@selector(performFindPanelAction:)
                        keyEquivalent:@"G"];
    [item setTag:3];
    item = [findMenu addItemWithTitle:NSLocalizedString(@"Use Selection for Find", nil)
                               action:@selector(useSelectionForFind:)
                        keyEquivalent:@"e"];
    [item setTag:4];
    [findMenu addItemWithTitle:NSLocalizedString(@"Jump to Selection", nil)
                        action:@selector(jumpToSelection:)
                 keyEquivalent:@"j"];
    [editMenu setSubmenu:findMenu forItem:findMenuItem];
  }
  {
    NSMenuItem *spellingMenuItem =
      [editMenu addItemWithTitle:NSLocalizedString(@"Spelling and Grammar", nil)
                          action:NULL
                   keyEquivalent:@""];
    NSMenu *spellingMenu = [[[NSMenu alloc]
        initWithTitle:NSLocalizedString(@"Spelling and Grammar", nil)] autorelease];
    [spellingMenu addItemWithTitle:NSLocalizedString(@"Show Spelling and Grammar", nil)
                            action:@selector(showGuessPanel:)
                     keyEquivalent:@":"];
    [spellingMenu addItemWithTitle:NSLocalizedString(@"Check Document Now", nil)
                            action:@selector(checkSpelling:)
                     keyEquivalent:@";"];
    [spellingMenu addItem:[NSMenuItem separatorItem]];
    [spellingMenu addItemWithTitle:NSLocalizedString(@"Check Spelling While Typing", nil)
                            action:@selector(toggleContinuousSpellChecking:)
                     keyEquivalent:@""];
    [spellingMenu addItemWithTitle:NSLocalizedString(@"Check Grammar With Spelling", nil)
                            action:@selector(toggleGrammarChecking:)
                     keyEquivalent:@""];
    [spellingMenu addItemWithTitle:NSLocalizedString(@"Correct Spelling Automatically", nil)
                            action:@selector(toggleAutomaticSpellingCorrection:)
                     keyEquivalent:@""];
    [editMenu setSubmenu:spellingMenu forItem:spellingMenuItem];
  }
  {
    NSMenuItem *substitutionsMenuItem =
      [editMenu addItemWithTitle:NSLocalizedString(@"Substitutions", nil)
                          action:NULL
                   keyEquivalent:@""];
    NSMenu *substitutionsMenu = [[[NSMenu alloc]
        initWithTitle:NSLocalizedString(@"Substitutions", nil)] autorelease];
    [substitutionsMenu addItemWithTitle:NSLocalizedString(@"Show Substitutions", nil)
                                 action:@selector(orderFrontSubstitutionsPanel:)
                          keyEquivalent:@""];
    [substitutionsMenu addItem:[NSMenuItem separatorItem]];
    [substitutionsMenu addItemWithTitle:NSLocalizedString(@"Smart Copy/Paste", nil)
                                 action:@selector(toggleSmartInsertDelete:)
                          keyEquivalent:@""];
    [substitutionsMenu addItemWithTitle:NSLocalizedString(@"Smart Quotes", nil)
                                 action:@selector(toggleAutomaticQuoteSubstitution:)
                          keyEquivalent:@""];
    [substitutionsMenu addItemWithTitle:NSLocalizedString(@"Smart Dashes", nil)
                                 action:@selector(toggleAutomaticDashSubstitution:)
                          keyEquivalent:@""];
    [substitutionsMenu addItemWithTitle:NSLocalizedString(@"Smart Links", nil)
                                 action:@selector(toggleAutomaticLinkDetection:)
                          keyEquivalent:@""];
    [substitutionsMenu addItemWithTitle:NSLocalizedString(@"Data Detectors", nil)
                                 action:@selector(toggleAutomaticDataDetection:)
                          keyEquivalent:@""];
    [substitutionsMenu addItemWithTitle:NSLocalizedString(@"Text Replacement", nil)
                                 action:@selector(toggleAutomaticTextReplacement:)
                          keyEquivalent:@""];
    [editMenu setSubmenu:substitutionsMenu forItem:substitutionsMenuItem];
  }
  {
    NSMenuItem *transformationsMenuItem =
      [editMenu addItemWithTitle:NSLocalizedString(@"Transformations", nil)
                          action:NULL
                   keyEquivalent:@""];
    NSMenu *transformationsMenu = [[[NSMenu alloc]
        initWithTitle:NSLocalizedString(@"Transformations", nil)] autorelease];
    [transformationsMenu addItemWithTitle:NSLocalizedString(@"Make Upper Case", nil)
                                   action:@selector(uppercaseWord:)
                            keyEquivalent:@""];
    [transformationsMenu addItemWithTitle:NSLocalizedString(@"Make Lower Case", nil)
                                   action:@selector(lowercaseWord:)
                            keyEquivalent:@""];
    [transformationsMenu addItemWithTitle:NSLocalizedString(@"Capitalize", nil)
                                   action:@selector(capitalizeWord:)
                            keyEquivalent:@""];
    [editMenu setSubmenu:transformationsMenu forItem:transformationsMenuItem];
  }
  {
    NSMenuItem *speechMenuItem =
      [editMenu addItemWithTitle:NSLocalizedString(@"Speech", nil)
                          action:NULL
                   keyEquivalent:@""];
    NSMenu *speechMenu = [[[NSMenu alloc]
        initWithTitle:NSLocalizedString(@"Speech", nil)] autorelease];
    [speechMenu addItemWithTitle:NSLocalizedString(@"Start Speaking", nil)
                          action:@selector(startSpeaking:)
                   keyEquivalent:@""];
    [speechMenu addItemWithTitle:NSLocalizedString(@"Stop Speaking", nil)
                          action:@selector(stopSpeaking:)
                   keyEquivalent:@""];
    [editMenu setSubmenu:speechMenu forItem:speechMenuItem];
  }
  [editMenu addItem:[NSMenuItem separatorItem]];
  {
    NSMenuItem *emoji =
      [editMenu addItemWithTitle:NSLocalizedString(@"Emoji & Symbols", nil)
                          action:@selector(orderFrontCharacterPalette:)
                   keyEquivalent:@" "];
    [emoji setKeyEquivalentModifierMask:(XPEventModifierFlagCommand |
                                         XPEventModifierFlagControl)];
    [emoji setTarget:NSApp];
  }

  NSMenuItem *viewMenuItem = [mainMenu addItemWithTitle:NSLocalizedString(@"View", nil)
                                                 action:NULL
                                          keyEquivalent:@""];
  NSMenu *viewMenu = [[[NSMenu alloc]
      initWithTitle:NSLocalizedString(@"View", nil)] autorelease];
  [mainMenu setSubmenu:viewMenu forItem:viewMenuItem];
  {
    NSMenuItem *toggleSidebar =
      [viewMenu addItemWithTitle:NSLocalizedString(@"Hide Sidebar", nil)
                          action:@selector(toggleSidebar:)
                   keyEquivalent:@"s"];
    [toggleSidebar setKeyEquivalentModifierMask:(XPEventModifierFlagCommand |
                                                 XPEventModifierFlagOption)];
    [toggleSidebar setTarget:nil];
  }
  [viewMenu addItem:[NSMenuItem separatorItem]];
  {
    NSMenuItem *fullScreen =
      [viewMenu addItemWithTitle:NSLocalizedString(@"Enter Full Screen", nil)
                          action:@selector(toggleFullScreen:)
                   keyEquivalent:@"f"];
    [fullScreen setKeyEquivalentModifierMask:(XPEventModifierFlagCommand |
                                              XPEventModifierFlagControl)];
    [fullScreen setTarget:nil];
  }

  NSMenuItem *windowMenuItem = [mainMenu addItemWithTitle:NSLocalizedString(@"Window", nil)
                                                   action:NULL
                                            keyEquivalent:@""];
  NSMenu *windowMenu = [[[NSMenu alloc]
      initWithTitle:NSLocalizedString(@"Window", nil)] autorelease];
  [mainMenu setSubmenu:windowMenu forItem:windowMenuItem];
  [NSApp setWindowsMenu:windowMenu];
  [windowMenu addItemWithTitle:NSLocalizedString(@"Close Window", nil)
                        action:@selector(performClose:)
                 keyEquivalent:@"w"];
  {
    NSMenuItem *showMainWindow =
      [windowMenu addItemWithTitle:NSLocalizedString(@"Show Chat Window", nil)
                            action:@selector(showMainWindow:)
                     keyEquivalent:@""];
    [showMainWindow setTarget:self];
  }
  [windowMenu addItem:[NSMenuItem separatorItem]];
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

  NSMenuItem *helpMenuItem = [mainMenu addItemWithTitle:NSLocalizedString(@"Help", nil)
                                                 action:NULL
                                          keyEquivalent:@""];
  NSMenu *helpMenu = [[[NSMenu alloc]
      initWithTitle:NSLocalizedString(@"Help", nil)] autorelease];
  [mainMenu setSubmenu:helpMenu forItem:helpMenuItem];
  [helpMenu addItemWithTitle:NSLocalizedString(@"Strappy Help", nil)
                      action:@selector(showHelp:)
               keyEquivalent:@"?"];
  if ([NSApp respondsToSelector:@selector(setHelpMenu:)]) {
    [NSApp performSelector:@selector(setHelpMenu:)
                withObject:helpMenu];
  }

  [NSApp setMainMenu:mainMenu];
}

- (void)populateModelMenu:(NSMenu *)menu
{
  if (menu == nil) {
    return;
  }

  if (_windowController != nil) {
    [_windowController populateModelMenu:menu];
    return;
  }

  while ([menu numberOfItems] > 0) {
    [menu removeItemAtIndex:0];
  }
  {
    NSMenuItem *item;

    item = [menu addItemWithTitle:NSLocalizedString(@"Model", nil)
                           action:nil
                    keyEquivalent:@""];
    [item setEnabled:NO];
  }
}

- (void)menuNeedsUpdate:(NSMenu *)menu
{
  if (menu == _modelMenu) {
    [self populateModelMenu:menu];
  }
}

- (void)showMainWindow
{
  _terminateWhenInFlightSessionsFinish = NO;
  if (_windowController != nil) {
    [_windowController showWindow:self];
    [[_windowController window] makeKeyAndOrderFront:self];
    return;
  }

  _windowController = [[SessionWindowController alloc] init];
  [_windowController showWindow:self];
  [[_windowController window] makeKeyAndOrderFront:self];
}

- (void)showMainWindow:(id)sender
{
  (void)sender;
  [self showMainWindow];
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
  [_modelMenu setDelegate:nil];
  [_modelMenu release];
  [_windowController release];
  [_preferencesWindowController release];
  [_inFlightSessions release];
  [super dealloc];
}

@end
