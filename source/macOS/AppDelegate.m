#import "AppDelegate.h"
#import "SessionWindowController.h"
#import "StrappySession.h"
#import <AltivecCore/AltivecCore.h>

@interface AppDelegate (Private)
- (void)setupMenu;
- (void)showMainWindow;
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
  [self showMainWindow];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
  (void)sender;
  return YES;
}

- (void)showAboutWindow:(id)sender
{
  (void)sender;
  [NSApp orderFrontStandardAboutPanel:self];
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
  if (_windowController != nil) {
    [_windowController showWindow:self];
    return;
  }

  _windowController = [[SessionWindowController alloc] init];
  [_windowController showWindow:self];
}

- (void)dealloc
{
  [_windowController release];
  [super dealloc];
}

@end
