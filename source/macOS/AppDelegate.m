#import "AppDelegate.h"
#import "AIFontAwesome.h"

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
  if (_window != nil) {
    [_window makeKeyAndOrderFront:self];
    return;
  }

  NSRect frame = NSMakeRect(0.0, 0.0, 480.0, 320.0);
  _window = [[NSWindow alloc]
      initWithContentRect:frame
                styleMask:(XPWindowStyleMaskTitled |
                           XPWindowStyleMaskClosable |
                           XPWindowStyleMaskMiniaturizable)
                  backing:NSBackingStoreBuffered
                    defer:NO];
  [_window XP_setTitle:NSLocalizedString(@"Strappy", nil)];
  [_window center];

  NSView *contentView = [_window contentView];
  {
    NSImage *image = [AIFontAwesome imageForIcon:AIFASeedling
                                           style:AIFontAwesomeStyleSolid
                                       pointSize:48.0
                                           scale:[_window XP_backingScaleFactor]];
    NSImageView *iconView = [[[NSImageView alloc]
        initWithFrame:NSMakeRect(216.0, 188.0, 48.0, 48.0)] autorelease];
    [iconView setImage:image];
    [iconView setImageScaling:XPImageScaleAxesIndependently];
    [iconView setAutoresizingMask:(NSViewMinXMargin | NSViewMaxXMargin |
                                   NSViewMinYMargin | NSViewMaxYMargin)];
    [contentView addSubview:iconView];
  }

  NSTextField *label = [[[NSTextField alloc]
      initWithFrame:NSMakeRect(32.0, 112.0, 416.0, 48.0)] autorelease];
  [label setStringValue:NSLocalizedString(@"Ready to build.", nil)];
  [label setAlignment:XPTextAlignmentCenter];
  [label setFont:[NSFont systemFontOfSize:24.0]];
  [label setBezeled:NO];
  [label setDrawsBackground:NO];
  [label setEditable:NO];
  [label setSelectable:NO];
  [label setAutoresizingMask:(NSViewWidthSizable | NSViewMinYMargin |
                              NSViewMaxYMargin)];
  [contentView addSubview:label];

  [_window makeKeyAndOrderFront:self];
}

- (void)dealloc
{
  [_window release];
  [super dealloc];
}

@end
