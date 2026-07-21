#import "SessionWindowController.h"
#import "StrappySession.h"

static NSString *StrappyModelStringForRow(NSDictionary *row, NSString *key)
{
  id value;

  if (![row isKindOfClass:[NSDictionary class]]) {
    return @"";
  }

  value = [row objectForKey:key];
  if (![value isKindOfClass:[NSString class]]) {
    return @"";
  }
  return value;
}

static NSString *StrappyModelDisplayNameForRow(NSDictionary *row)
{
  NSString *name;
  NSString *modelId;

  name = StrappyModelStringForRow(row, @"name");
  if ([name length] > 0U) {
    return name;
  }

  modelId = StrappyModelStringForRow(row, @"id");
  return ([modelId length] > 0U) ? modelId : NSLocalizedString(@"Model", nil);
}

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

- (BOOL)canCloseCurrentChat
{
  return [sessionsController_ canCloseActiveSession];
}

- (void)closeCurrentChat:(id)sender
{
  [sessionsController_ closeActiveSession:sender];
}

- (BOOL)canDeleteCurrentChat
{
  return [sessionsController_ canDeleteActiveSession];
}

- (void)deleteCurrentChat:(id)sender
{
  [sessionsController_ deleteActiveSession:sender];
}

- (BOOL)canSendCurrentPrompt
{
  return [messagesController_ canSendCurrentPrompt];
}

- (void)sendCurrentPrompt:(id)sender
{
  [messagesController_ sendCurrentPrompt:sender];
}

- (BOOL)canCancelCurrentPrompt
{
  return [messagesController_ canCancelCurrentPrompt];
}

- (void)cancelCurrentPrompt:(id)sender
{
  [messagesController_ cancelCurrentPrompt:sender];
}

- (BOOL)canToggleStreaming
{
  return [messagesController_ canToggleStreaming];
}

- (BOOL)streamingEnabled
{
  return [messagesController_ streamingEnabled];
}

- (void)toggleStreaming:(id)sender
{
  [messagesController_ toggleStreaming:sender];
  if ([sender isKindOfClass:[NSMenuItem class]]) {
    [(NSMenuItem *)sender setState:([self streamingEnabled] ?
      XPControlStateValueOn : XPControlStateValueOff)];
  }
}

- (void)populateModelMenu:(NSMenu *)menu
{
  NSArray *models;
  NSString *selectedModelIdentifier;
  NSUInteger index;
  BOOL canSelect;
  BOOL foundSelectedModel;

  if (menu == nil) {
    return;
  }

  while ([menu numberOfItems] > 0) {
    [menu removeItemAtIndex:0];
  }

  models = [messagesController_ availableModels];
  if (![models isKindOfClass:[NSArray class]]) {
    models = [NSArray array];
  }

  selectedModelIdentifier = [messagesController_ selectedModelIdentifier];
  if (![selectedModelIdentifier isKindOfClass:[NSString class]]) {
    selectedModelIdentifier = @"";
  }

  canSelect = [messagesController_ canSelectModel];
  foundSelectedModel = NO;

  for (index = 0U; index < [models count]; index++) {
    NSDictionary *model;
    NSString *modelIdentifier;
    NSMenuItem *item;

    model = [models objectAtIndex:index];
    modelIdentifier = StrappyModelStringForRow(model, @"id");
    if ([modelIdentifier length] == 0U) {
      continue;
    }

    item = [menu addItemWithTitle:StrappyModelDisplayNameForRow(model)
                           action:@selector(selectCurrentModel:)
                    keyEquivalent:@""];
    [item setTarget:self];
    [item setRepresentedObject:modelIdentifier];
    [item setEnabled:canSelect];
    if ([modelIdentifier isEqualToString:selectedModelIdentifier]) {
      [item setState:XPControlStateValueOn];
      foundSelectedModel = YES;
    } else {
      [item setState:XPControlStateValueOff];
    }
  }

  if (!foundSelectedModel && ([selectedModelIdentifier length] > 0U)) {
    NSMenuItem *item;

    item = [menu addItemWithTitle:selectedModelIdentifier
                           action:nil
                    keyEquivalent:@""];
    [item setEnabled:NO];
    [item setRepresentedObject:selectedModelIdentifier];
    [item setState:XPControlStateValueOn];
  }

  if ([menu numberOfItems] == 0) {
    NSMenuItem *item;

    item = [menu addItemWithTitle:NSLocalizedString(@"Model", nil)
                           action:nil
                    keyEquivalent:@""];
    [item setEnabled:NO];
  }
}

- (void)selectCurrentModel:(id)sender
{
  NSString *modelIdentifier;

  if (![sender respondsToSelector:@selector(representedObject)]) {
    return;
  }

  modelIdentifier = [sender representedObject];
  if (![modelIdentifier isKindOfClass:[NSString class]] ||
      ([modelIdentifier length] == 0U)) {
    return;
  }

  if (![messagesController_ canSelectModel] ||
      ![messagesController_ setSelectedModelIdentifier:modelIdentifier]) {
    NSBeep();
  }
}

- (BOOL)canPrintCurrentChat
{
  return [messagesController_ canPrintCurrentChat];
}

- (void)printCurrentChat:(id)sender
{
  [messagesController_ printCurrentChat:sender];
}

- (void)sessionListViewController:(SessionListViewController *)controller
                 didSelectSession:(StrappySession *)session
{
  (void)controller;
  [messagesController_ reloadWithSession:session];
}

- (void)strappySessionDidUpdate:(NSNotification *)notification
{
  NSString *changeKind;
  NSDictionary *session;
  NSNumber *identifier;

  changeKind = [[notification userInfo] objectForKey:StrappySessionChangeKindKey];
  if ([changeKind isEqualToString:StrappySessionChangeKindWebProvider] ||
      [changeKind isEqualToString:StrappySessionChangeKindBash] ||
      [changeKind isEqualToString:StrappySessionChangeKindStreaming]) {
    return;
  }
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
  if (action == @selector(closeCurrentChat:)) {
    return [self canCloseCurrentChat];
  } else if (action == @selector(deleteCurrentChat:)) {
    return [self canDeleteCurrentChat];
  } else if (action == @selector(sendCurrentPrompt:)) {
    return [self canSendCurrentPrompt];
  } else if (action == @selector(cancelCurrentPrompt:)) {
    return [self canCancelCurrentPrompt];
  } else if (action == @selector(toggleStreaming:)) {
    [item setState:([self streamingEnabled] ?
      XPControlStateValueOn : XPControlStateValueOff)];
    return [self canToggleStreaming];
  } else if (action == @selector(toggleSidebar:)) {
    [item setTitle:([self isSidebarCollapsed] ?
      NSLocalizedString(@"Show Sidebar", nil) :
      NSLocalizedString(@"Hide Sidebar", nil))];
    return YES;
  } else if (action == @selector(selectCurrentModel:)) {
    NSString *modelIdentifier;
    NSString *selectedModelIdentifier;

    modelIdentifier = [item representedObject];
    selectedModelIdentifier = [messagesController_ selectedModelIdentifier];
    [item setState:([modelIdentifier isKindOfClass:[NSString class]] &&
                    [selectedModelIdentifier isKindOfClass:[NSString class]] &&
                    [modelIdentifier isEqualToString:selectedModelIdentifier]) ?
      XPControlStateValueOn : XPControlStateValueOff];
    return [messagesController_ canSelectModel];
  } else if (action == @selector(printCurrentChat:)) {
    return [self canPrintCurrentChat];
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
