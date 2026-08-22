#import "StrappyPreferencesAuthenticationView.h"

#import "AIFontAwesome.h"
#import "StrappyAuthentication.h"
#import "StrappyKeychain.h"
#import "StrappySession.h"

#include <errno.h>
#include <stdlib.h>

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 101400
  #define StrappyAccountsPasteboardStringType NSPasteboardTypeString
#else
  #define StrappyAccountsPasteboardStringType NSStringPboardType
#endif

static const CGFloat kStrappyAccountsSidebarWidth = 218.0;
static const CGFloat kStrappyAccountsMinimumSidebarWidth = 190.0;
static const CGFloat kStrappyAccountsRowHeight = 54.0;
static const CGFloat kStrappyProviderRowHeight = 70.0;
static const CGFloat kStrappyAccountsControlHeight = 24.0;
static const CGFloat kStrappyAccountsInset = 16.0;
static NSString * const kStrappyAccountsAddRowKey = @"add_account";

static NSDictionary *StrappyAccountsAddRow(void)
{
  static NSDictionary *row = nil;

  if (row == nil) {
    row = [[NSDictionary alloc] initWithObjectsAndKeys:
      NSLocalizedString(@"Add Account", nil), @"name",
      [NSNumber numberWithBool:YES], kStrappyAccountsAddRowKey,
      nil];
  }
  return row;
}

static NSString *StrappyAccountsProviderDisplayName(NSString *provider)
{
  if ([provider isEqualToString:@"openrouter"]) {
    return @"OpenRouter";
  }
  if ([provider isEqualToString:@"openai_chatgpt"]) {
    return @"ChatGPT";
  }
  return NSLocalizedString(@"Custom", nil);
}

static NSString *StrappyAccountsProviderSubtitle(NSString *provider)
{
  if ([provider isEqualToString:@"openrouter"]) {
    return NSLocalizedString(@"API token", nil);
  }
  if ([provider isEqualToString:@"openai_chatgpt"]) {
    return NSLocalizedString(@"Sign in with ChatGPT", nil);
  }
  return NSLocalizedString(@"Endpoint and optional bearer token", nil);
}

static AIFontAwesomeIcon StrappyAccountsProviderIcon(NSString *provider)
{
  if ([provider isEqualToString:@"openai_chatgpt"]) {
    return AIFARobot;
  }
  if ([provider isEqualToString:@"other"]) {
    return AIFAServer;
  }
  return AIFAGlobe;
}

static NSDictionary *StrappyAccountsTextAttributes(NSFont *font,
                                                    NSColor *color,
                                                    NSLineBreakMode mode)
{
  NSMutableParagraphStyle *style;

  style = [[[NSMutableParagraphStyle alloc] init] autorelease];
  [style setLineBreakMode:mode];
  return [NSDictionary dictionaryWithObjectsAndKeys:
    font, NSFontAttributeName,
    color, NSForegroundColorAttributeName,
    style, NSParagraphStyleAttributeName,
    nil];
}

static CGFloat StrappyAccountsBackingScale(NSView *view)
{
  NSWindow *window;
  CGFloat scale;

  window = [view window];
  scale = 1.0;
  if ([window respondsToSelector:@selector(XP_backingScaleFactor)]) {
    scale = [window XP_backingScaleFactor];
  }
  return (scale > 1.0) ? scale : 1.0;
}

static void StrappyAccountsDrawTintedImage(NSImage *image,
                                           NSRect rect,
                                           NSColor *color)
{
  NSImage *tinted;
  NSRect source;

  if ((image == nil) || (color == nil)) {
    return;
  }
  source = NSMakeRect(0.0, 0.0, [image size].width, [image size].height);
  tinted = [[[NSImage alloc] initWithSize:[image size]] autorelease];
  [tinted lockFocus];
  [image drawAtPoint:NSZeroPoint
            fromRect:source
           operation:XPCompositingOperationSourceOver
            fraction:1.0];
  [color set];
  NSRectFillUsingOperation(source, XPCompositingOperationSourceIn);
  [tinted unlockFocus];
  [tinted drawInRect:rect
            fromRect:source
           operation:XPCompositingOperationSourceOver
            fraction:1.0];
}

static NSTextField *StrappyAccountsLabel(NSRect frame,
                                         NSString *text,
                                         NSFont *font)
{
  NSTextField *label;

  label = [[[NSTextField alloc] initWithFrame:frame] autorelease];
  [label setStringValue:(text != nil) ? text : @""];
  [label setBezeled:NO];
  [label setDrawsBackground:NO];
  [label setEditable:NO];
  [label setSelectable:NO];
  [label setFont:font];
  return label;
}

static NSButton *StrappyAccountsButton(NSRect frame,
                                       NSString *title,
                                       id target,
                                       SEL action)
{
  NSButton *button;

  button = [[[NSButton alloc] initWithFrame:frame] autorelease];
  [button setTitle:title];
  [button setBezelStyle:XPBezelStyleRounded];
  [button setButtonType:XPButtonTypeMomentaryLight];
  [button setTarget:target];
  [button setAction:action];
  return button;
}

@interface StrappyAccountsDividerView : NSView
@end

@implementation StrappyAccountsDividerView

- (void)drawRect:(NSRect)dirtyRect
{
  (void)dirtyRect;
  [[NSColor gridColor] set];
  NSRectFill([self bounds]);
}

@end

@interface StrappyAccountCell : NSCell
@end

@implementation StrappyAccountCell

- (void)drawInteriorWithFrame:(NSRect)frame inView:(NSView *)view
{
  NSDictionary *account;
  NSString *name;
  NSString *provider;
  BOOL addsAccount;
  BOOL selected;
  NSColor *primaryColor;
  NSColor *secondaryColor;
  NSImage *icon;
  NSRect iconRect;
  NSRect nameRect;
  NSRect providerRect;

  account = [self objectValue];
  if (![account isKindOfClass:[NSDictionary class]]) {
    return;
  }
  name = [account objectForKey:@"name"];
  provider = [account objectForKey:@"provider_id"];
  addsAccount = [[account objectForKey:kStrappyAccountsAddRowKey] boolValue];
  selected = [self isHighlighted];
  primaryColor = selected ? [NSColor alternateSelectedControlTextColor] :
    [NSColor controlTextColor];
  secondaryColor = selected ? [NSColor alternateSelectedControlTextColor] :
    [NSColor disabledControlTextColor];
  icon = [AIFontAwesome imageForIcon:(addsAccount ? AIFACirclePlus :
                                      StrappyAccountsProviderIcon(provider))
                                style:AIFontAwesomeStyleSolid
                             iconSize:20.0
                           canvasSize:26.0
                                scale:StrappyAccountsBackingScale(view)];
  iconRect = NSMakeRect(NSMinX(frame) + 8.0,
                        NSMinY(frame) + ((NSHeight(frame) - 26.0) / 2.0),
                        26.0,
                        26.0);
  StrappyAccountsDrawTintedImage(icon, iconRect, primaryColor);
  nameRect = NSMakeRect(NSMinX(frame) + 42.0,
                        NSMinY(frame) + (addsAccount ?
                          ((NSHeight(frame) - 18.0) / 2.0) : 10.0),
                        NSWidth(frame) - 50.0,
                        18.0);
  providerRect = NSMakeRect(NSMinX(frame) + 42.0,
                            NSMinY(frame) + 29.0,
                            NSWidth(frame) - 50.0,
                            15.0);
  [(name != nil ? name : @"") drawInRect:nameRect
    withAttributes:StrappyAccountsTextAttributes(
      [NSFont boldSystemFontOfSize:12.0], primaryColor,
      NSLineBreakByTruncatingTail)];
  if (!addsAccount) {
    [StrappyAccountsProviderDisplayName(provider) drawInRect:providerRect
      withAttributes:StrappyAccountsTextAttributes(
        [NSFont systemFontOfSize:10.0], secondaryColor,
        NSLineBreakByTruncatingTail)];
  }
}

@end

@interface StrappyProviderCell : NSCell
@end

@implementation StrappyProviderCell

- (void)drawInteriorWithFrame:(NSRect)frame inView:(NSView *)view
{
  NSDictionary *providerRecord;
  NSString *provider;
  NSString *name;
  NSString *subtitle;
  BOOL selected;
  BOOL available;
  NSColor *primaryColor;
  NSColor *secondaryColor;
  NSImage *icon;
  NSRect iconRect;
  NSRect nameRect;
  NSRect subtitleRect;

  providerRecord = [self objectValue];
  if (![providerRecord isKindOfClass:[NSDictionary class]]) {
    return;
  }
  provider = [providerRecord objectForKey:@"id"];
  name = StrappyAccountsProviderDisplayName(provider);
  subtitle = StrappyAccountsProviderSubtitle(provider);
  available = [[providerRecord objectForKey:@"available"] boolValue];
  selected = [self isHighlighted];
  primaryColor = available ?
    (selected ? [NSColor alternateSelectedControlTextColor] :
      [NSColor controlTextColor]) : [NSColor disabledControlTextColor];
  secondaryColor = selected && available ?
    [NSColor alternateSelectedControlTextColor] :
    [NSColor disabledControlTextColor];
  icon = [AIFontAwesome imageForIcon:StrappyAccountsProviderIcon(provider)
                                style:AIFontAwesomeStyleSolid
                             iconSize:28.0
                           canvasSize:36.0
                                scale:StrappyAccountsBackingScale(view)];
  iconRect = NSMakeRect(NSMinX(frame) + 18.0,
                        NSMinY(frame) + ((NSHeight(frame) - 36.0) / 2.0),
                        36.0,
                        36.0);
  StrappyAccountsDrawTintedImage(icon, iconRect, primaryColor);
  nameRect = NSMakeRect(NSMinX(frame) + 68.0,
                        NSMinY(frame) + 15.0,
                        NSWidth(frame) - 82.0,
                        20.0);
  subtitleRect = NSMakeRect(NSMinX(frame) + 68.0,
                            NSMinY(frame) + 38.0,
                            NSWidth(frame) - 82.0,
                            16.0);
  [name drawInRect:nameRect
    withAttributes:StrappyAccountsTextAttributes(
      [NSFont boldSystemFontOfSize:14.0], primaryColor,
      NSLineBreakByTruncatingTail)];
  [subtitle drawInRect:subtitleRect
    withAttributes:StrappyAccountsTextAttributes(
      [NSFont systemFontOfSize:11.0], secondaryColor,
      NSLineBreakByTruncatingTail)];
}

@end

@interface StrappyPreferencesAuthenticationView ()
- (void)buildView;
- (void)layoutAccountsView;
- (void)reloadAccountsPreservingSelection;
- (NSDictionary *)selectedAccount;
- (NSInteger)rowForAccountIdentifier:(NSString *)identifier;
- (void)selectAccountIdentifier:(NSString *)identifier;
- (void)clearRightPane;
- (void)configureKeyViewLoopForProvider:(NSString *)provider;
- (void)showProviderChooser;
- (void)showSelectedAccount;
- (void)showError:(NSError *)error title:(NSString *)title;
- (void)toggleMaxOutputTokens:(id)sender;
- (void)saveAccount:(id)sender;
- (void)deleteAccount:(id)sender;
- (void)deleteAccountAlertDidEnd:(NSAlert *)alert
                      returnCode:(NSInteger)returnCode
                     contextInfo:(void *)contextInfo;
- (void)authenticationDidChange:(NSNotification *)notification;
- (StrappyAuthentication *)selectedChatGPTAuthentication;
- (void)reloadChatGPTState;
- (void)performChatGPTAction:(id)sender;
- (void)copyChatGPTCode:(id)sender;
- (void)openChatGPTVerificationURL:(id)sender;
@end

@implementation StrappyPreferencesAuthenticationView

- (id)initWithFrame:(NSRect)frame
{
  return [self initWithFrame:frame target:nil];
}

- (id)initWithFrame:(NSRect)frame target:(id)target
{
  (void)target;
  if ((self = [super initWithFrame:frame])) {
    [self setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [self setAutoresizesSubviews:YES];
    providers_ = [[StrappySession providerCatalog] copy];
    [self buildView];
    [self reloadAccountsPreservingSelection];
    [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(authenticationDidChange:)
             name:StrappyAuthenticationDidChangeNotification
           object:nil];
  }
  return self;
}

- (void)buildView
{
  NSTableColumn *column;
  StrappyAccountsDividerView *divider;

  accountScrollView_ = [[NSScrollView alloc] initWithFrame:NSZeroRect];
  [accountScrollView_ setHasVerticalScroller:YES];
  [accountScrollView_ setHasHorizontalScroller:NO];
  [accountScrollView_ setAutohidesScrollers:YES];
  [accountScrollView_ setBorderType:NSNoBorder];
  accountTableView_ = [[NSTableView alloc] initWithFrame:NSZeroRect];
  [accountTableView_ setDataSource:self];
  [accountTableView_ setDelegate:self];
  [accountTableView_ setHeaderView:nil];
  [accountTableView_ setRowHeight:kStrappyAccountsRowHeight];
  [accountTableView_ setAllowsMultipleSelection:NO];
  [accountTableView_ setAllowsEmptySelection:NO];
  [accountTableView_ setUsesAlternatingRowBackgroundColors:NO];
  [accountTableView_ XP_setSourceListStyle];
  column = [[[NSTableColumn alloc] initWithIdentifier:@"account"] autorelease];
  [column setDataCell:[[[StrappyAccountCell alloc] init] autorelease]];
  [accountTableView_ addTableColumn:column];
  [accountScrollView_ setDocumentView:accountTableView_];
  [self addSubview:accountScrollView_];

  divider = [[[StrappyAccountsDividerView alloc] initWithFrame:NSZeroRect]
    autorelease];
  [self addSubview:divider];
  dividerView_ = divider;

  rightPaneView_ = [[NSView alloc] initWithFrame:NSZeroRect];
  [rightPaneView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [self addSubview:rightPaneView_];
  [self layoutAccountsView];
}

- (void)resizeSubviewsWithOldSize:(NSSize)oldSize
{
  [super resizeSubviewsWithOldSize:oldSize];
  [self layoutAccountsView];
}

- (void)layoutAccountsView
{
  NSRect bounds;
  CGFloat sidebarWidth;

  bounds = [self bounds];
  sidebarWidth = kStrappyAccountsSidebarWidth;
  if (NSWidth(bounds) < 560.0) {
    sidebarWidth = kStrappyAccountsMinimumSidebarWidth;
  }
  [accountScrollView_ setFrame:NSMakeRect(0.0,
                                          0.0,
                                          sidebarWidth,
                                          NSHeight(bounds))];
  [dividerView_ setFrame:NSMakeRect(sidebarWidth, 0.0, 1.0,
                                    NSHeight(bounds))];
  [rightPaneView_ setFrame:NSMakeRect(sidebarWidth + 1.0,
                                      0.0,
                                      MAX(0.0, NSWidth(bounds) -
                                        sidebarWidth - 1.0),
                                      NSHeight(bounds))];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView
{
  if (tableView == accountTableView_) {
    return (NSInteger)[accounts_ count] + 1;
  }
  if (tableView == providerTableView_) {
    return (NSInteger)[providers_ count];
  }
  return 0;
}

- (id)tableView:(NSTableView *)tableView
    objectValueForTableColumn:(NSTableColumn *)tableColumn
                          row:(NSInteger)row
{
  NSArray *rows;

  (void)tableColumn;
  if ((tableView == accountTableView_) &&
      (row == (NSInteger)[accounts_ count])) {
    return StrappyAccountsAddRow();
  }
  rows = (tableView == accountTableView_) ? accounts_ : providers_;
  if ((row < 0) || (row >= (NSInteger)[rows count])) {
    return nil;
  }
  return [rows objectAtIndex:(NSUInteger)row];
}

- (BOOL)tableView:(NSTableView *)tableView shouldSelectRow:(NSInteger)row
{
  if (tableView == providerTableView_) {
    NSDictionary *provider;

    if ((row < 0) || (row >= (NSInteger)[providers_ count])) {
      return NO;
    }
    provider = [providers_ objectAtIndex:(NSUInteger)row];
    return [[provider objectForKey:@"available"] boolValue] ? YES : NO;
  }
  return ((row >= 0) && (row <= (NSInteger)[accounts_ count])) ? YES : NO;
}

- (void)tableViewSelectionDidChange:(NSNotification *)notification
{
  NSTableView *tableView;
  NSInteger row;

  tableView = [notification object];
  if (suppressSelectionNotification_) {
    return;
  }
  row = [tableView selectedRow];
  if (tableView == accountTableView_) {
    [selectedAccountIdentifier_ release];
    selectedAccountIdentifier_ = nil;
    if ((row >= 0) && (row < (NSInteger)[accounts_ count])) {
      selectedAccountIdentifier_ = [[[accounts_ objectAtIndex:(NSUInteger)row]
        objectForKey:@"id"] copy];
      [self showSelectedAccount];
    } else {
      [self showProviderChooser];
    }
    return;
  }
  if ((tableView == providerTableView_) && (row >= 0) &&
      (row < (NSInteger)[providers_ count]) && !creatingAccount_) {
    NSDictionary *provider;
    NSDictionary *account;
    NSError *error;

    [tableView retain];
    provider = [providers_ objectAtIndex:(NSUInteger)row];
    creatingAccount_ = YES;
    error = nil;
    account = [StrappySession
      createProviderAccountForProviderIdentifier:[provider objectForKey:@"id"]
                                           error:&error];
    creatingAccount_ = NO;
    suppressSelectionNotification_ = YES;
    [providerTableView_ deselectAll:self];
    suppressSelectionNotification_ = NO;
    if (account == nil) {
      [self showError:error
                title:NSLocalizedString(@"Could Not Add Account", nil)];
      [tableView release];
      return;
    }
    [self reloadAccountsPreservingSelection];
    [self selectAccountIdentifier:[account objectForKey:@"id"]];
    [tableView release];
  }
}

- (NSInteger)rowForAccountIdentifier:(NSString *)identifier
{
  NSUInteger index;

  if ([identifier length] == 0U) {
    return -1;
  }
  for (index = 0U; index < [accounts_ count]; index++) {
    if ([[[accounts_ objectAtIndex:index] objectForKey:@"id"]
          isEqualToString:identifier]) {
      return (NSInteger)index;
    }
  }
  return -1;
}

- (NSDictionary *)selectedAccount
{
  NSInteger row;

  row = [self rowForAccountIdentifier:selectedAccountIdentifier_];
  return (row >= 0) ? [accounts_ objectAtIndex:(NSUInteger)row] : nil;
}

- (void)reloadAccountsPreservingSelection
{
  NSArray *accounts;
  NSError *error;
  NSInteger row;

  error = nil;
  accounts = [StrappySession verifiedProviderAccountCatalogWithError:&error];
  if (accounts == nil) {
    accounts = [NSArray array];
    [self showError:error
              title:NSLocalizedString(@"Could Not Load Accounts", nil)];
  }
  [accounts_ release];
  accounts_ = [accounts copy];
  [accountTableView_ reloadData];
  row = [self rowForAccountIdentifier:selectedAccountIdentifier_];
  suppressSelectionNotification_ = YES;
  if (row >= 0) {
    [accountTableView_ selectRowIndexes:
      [NSIndexSet indexSetWithIndex:(NSUInteger)row]
                      byExtendingSelection:NO];
  } else {
    [selectedAccountIdentifier_ release];
    selectedAccountIdentifier_ = nil;
    [accountTableView_ selectRowIndexes:
      [NSIndexSet indexSetWithIndex:[accounts_ count]]
                      byExtendingSelection:NO];
  }
  suppressSelectionNotification_ = NO;
  if (row >= 0) {
    [self showSelectedAccount];
  } else {
    [self showProviderChooser];
  }
}

- (void)selectAccountIdentifier:(NSString *)identifier
{
  NSInteger row;

  row = [self rowForAccountIdentifier:identifier];
  [selectedAccountIdentifier_ release];
  selectedAccountIdentifier_ = (row >= 0) ? [identifier copy] : nil;
  suppressSelectionNotification_ = YES;
  if (row >= 0) {
    [accountTableView_ selectRowIndexes:
      [NSIndexSet indexSetWithIndex:(NSUInteger)row]
                      byExtendingSelection:NO];
    [accountTableView_ scrollRowToVisible:row];
  } else {
    [accountTableView_ selectRowIndexes:
      [NSIndexSet indexSetWithIndex:[accounts_ count]]
                      byExtendingSelection:NO];
    [accountTableView_ scrollRowToVisible:(NSInteger)[accounts_ count]];
  }
  suppressSelectionNotification_ = NO;
  if (row >= 0) {
    [self showSelectedAccount];
  } else {
    [self showProviderChooser];
  }
}

- (void)clearRightPane
{
  NSArray *subviews;
  NSUInteger index;

  if (accountNameField_ != nil) {
    [[NSNotificationCenter defaultCenter]
      removeObserver:self
                name:NSControlTextDidBeginEditingNotification
              object:accountNameField_];
  }
  if (endpointField_ != nil) {
    [[NSNotificationCenter defaultCenter]
      removeObserver:self
                name:NSControlTextDidBeginEditingNotification
              object:endpointField_];
  }
  if (tokenField_ != nil) {
    [[NSNotificationCenter defaultCenter]
      removeObserver:self
                name:NSControlTextDidBeginEditingNotification
              object:tokenField_];
  }
  if (maxOutputTokensField_ != nil) {
    [[NSNotificationCenter defaultCenter]
      removeObserver:self
                name:NSControlTextDidBeginEditingNotification
              object:maxOutputTokensField_];
  }
  subviews = [[rightPaneView_ subviews] copy];
  for (index = 0U; index < [subviews count]; index++) {
    [[subviews objectAtIndex:index] removeFromSuperview];
  }
  [subviews release];
  providerTableView_ = nil;
  accountNameField_ = nil;
  endpointField_ = nil;
  tokenField_ = nil;
  maxOutputTokensButton_ = nil;
  maxOutputTokensField_ = nil;
  saveButton_ = nil;
  deleteButton_ = nil;
  chatGPTStatusLabel_ = nil;
  chatGPTURLField_ = nil;
  chatGPTCodeField_ = nil;
  chatGPTActionButton_ = nil;
  chatGPTCopyButton_ = nil;
  chatGPTOpenButton_ = nil;
}

- (void)configureKeyViewLoopForProvider:(NSString *)provider
{
  [accountTableView_ setNextKeyView:(providerTableView_ != nil) ?
    (NSView *)providerTableView_ : (NSView *)accountNameField_];
  if (providerTableView_ != nil) {
    [providerTableView_ setNextKeyView:accountTableView_];
  } else if ([provider isEqualToString:@"openrouter"]) {
    [accountNameField_ setNextKeyView:tokenField_];
    [tokenField_ setNextKeyView:maxOutputTokensButton_];
    [maxOutputTokensButton_ setNextKeyView:maxOutputTokensField_];
    [maxOutputTokensField_ setNextKeyView:accountTableView_];
  } else if ([provider isEqualToString:@"other"]) {
    [accountNameField_ setNextKeyView:endpointField_];
    [endpointField_ setNextKeyView:tokenField_];
    [tokenField_ setNextKeyView:maxOutputTokensButton_];
    [maxOutputTokensButton_ setNextKeyView:maxOutputTokensField_];
    [maxOutputTokensField_ setNextKeyView:accountTableView_];
  } else {
    [accountNameField_ setNextKeyView:accountTableView_];
  }
}

- (void)showProviderChooser
{
  NSRect bounds;
  NSTextField *title;
  NSTextField *instruction;
  NSScrollView *scrollView;
  NSTableColumn *column;
  CGFloat panelWidth;
  CGFloat panelHeight;

  [self clearRightPane];
  bounds = [rightPaneView_ bounds];
  title = StrappyAccountsLabel(
    NSMakeRect(kStrappyAccountsInset,
               NSHeight(bounds) - 58.0,
               NSWidth(bounds) - (2.0 * kStrappyAccountsInset),
               28.0),
    NSLocalizedString(@"Add an Account", nil),
    [NSFont boldSystemFontOfSize:20.0]);
  [title setAlignment:XPTextAlignmentCenter];
  [title setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
  [rightPaneView_ addSubview:title];

  panelWidth = MAX(180.0, NSWidth(bounds) -
    (2.0 * kStrappyAccountsInset));
  panelHeight = MIN(NSHeight(bounds) - 140.0,
                    kStrappyProviderRowHeight * (CGFloat)[providers_ count] +
                      2.0);
  if (panelHeight < kStrappyProviderRowHeight) {
    panelHeight = kStrappyProviderRowHeight;
  }
  scrollView = [[[NSScrollView alloc] initWithFrame:NSMakeRect(
    kStrappyAccountsInset,
    NSHeight(bounds) - 82.0 - panelHeight,
    panelWidth,
    panelHeight)] autorelease];
  [scrollView setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
  [scrollView setHasVerticalScroller:NO];
  [scrollView setHasHorizontalScroller:NO];
  [scrollView setBorderType:NSBezelBorder];
  providerTableView_ = [[[NSTableView alloc] initWithFrame:[scrollView bounds]]
    autorelease];
  [providerTableView_ setDataSource:self];
  [providerTableView_ setDelegate:self];
  [providerTableView_ setHeaderView:nil];
  [providerTableView_ setRowHeight:kStrappyProviderRowHeight];
  [providerTableView_ setAllowsMultipleSelection:NO];
  [providerTableView_ setAllowsEmptySelection:YES];
  [providerTableView_ setUsesAlternatingRowBackgroundColors:NO];
  column = [[[NSTableColumn alloc] initWithIdentifier:@"provider"] autorelease];
  [column setDataCell:[[[StrappyProviderCell alloc] init] autorelease]];
  [providerTableView_ addTableColumn:column];
  [scrollView setDocumentView:providerTableView_];
  [rightPaneView_ addSubview:scrollView];
  [providerTableView_ sizeLastColumnToFit];

  instruction = StrappyAccountsLabel(
    NSMakeRect(kStrappyAccountsInset,
               22.0,
               NSWidth(bounds) - (2.0 * kStrappyAccountsInset),
               18.0),
    NSLocalizedString(@"Select a provider to add an account.", nil),
    [NSFont systemFontOfSize:11.0]);
  [instruction setTextColor:[NSColor disabledControlTextColor]];
  [instruction setAlignment:XPTextAlignmentCenter];
  [instruction setAutoresizingMask:NSViewWidthSizable | NSViewMaxYMargin];
  [rightPaneView_ addSubview:instruction];
  [self configureKeyViewLoopForProvider:nil];
}

- (void)showSelectedAccount
{
  NSDictionary *account;
  NSString *provider;
  NSString *providerName;
  NSString *token;
  NSRect bounds;
  CGFloat width;
  CGFloat top;
  NSTextField *label;
  NSTextField *title;
  long long maxOutputTokens;

  account = [self selectedAccount];
  if (account == nil) {
    [self showProviderChooser];
    return;
  }
  [self clearRightPane];
  provider = [account objectForKey:@"provider_id"];
  providerName = StrappyAccountsProviderDisplayName(provider);
  maxOutputTokens = [[account objectForKey:@"max_output_tokens"] longLongValue];
  bounds = [rightPaneView_ bounds];
  width = MAX(120.0, NSWidth(bounds) - (2.0 * kStrappyAccountsInset));
  top = NSHeight(bounds) - 42.0;

  title = StrappyAccountsLabel(
    NSMakeRect(kStrappyAccountsInset, top, width, 24.0),
    [NSString stringWithFormat:NSLocalizedString(@"%@ Account", nil),
                               providerName],
    [NSFont boldSystemFontOfSize:18.0]);
  [title setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
  [rightPaneView_ addSubview:title];
  top -= 34.0;
  label = StrappyAccountsLabel(
    NSMakeRect(kStrappyAccountsInset, top, width, 16.0),
    NSLocalizedString(@"Account Name", nil),
    [NSFont systemFontOfSize:10.0]);
  [label setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
  [rightPaneView_ addSubview:label];
  top -= 28.0;
  accountNameField_ = [[[NSTextField alloc] initWithFrame:NSMakeRect(
    kStrappyAccountsInset, top, width, kStrappyAccountsControlHeight)]
    autorelease];
  [accountNameField_ setStringValue:[account objectForKey:@"name"]];
  [accountNameField_ setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
  [rightPaneView_ addSubview:accountNameField_];
  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(controlTextDidBeginEditing:)
           name:NSControlTextDidBeginEditingNotification
         object:accountNameField_];
  top -= 40.0;

  if ([provider isEqualToString:@"openrouter"]) {
    label = StrappyAccountsLabel(
      NSMakeRect(kStrappyAccountsInset, top, width, 16.0),
      NSLocalizedString(@"API Key", nil),
      [NSFont systemFontOfSize:10.0]);
    [label setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
    [rightPaneView_ addSubview:label];
    top -= 28.0;
    tokenField_ = [[[NSSecureTextField alloc] initWithFrame:NSMakeRect(
      kStrappyAccountsInset, top, width, kStrappyAccountsControlHeight)]
      autorelease];
    token = nil;
    [[StrappyKeychain sharedKeychain]
      loadBearerToken:&token
      forProviderIdentifier:provider
      providerAccountIdentifier:selectedAccountIdentifier_];
    [tokenField_ setStringValue:(token != nil) ? token : @""];
    [[tokenField_ cell] setPlaceholderString:
      NSLocalizedString(@"Paste API key", nil)];
    [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(controlTextDidBeginEditing:)
             name:NSControlTextDidBeginEditingNotification
           object:tokenField_];
    [tokenField_ setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
    [rightPaneView_ addSubview:tokenField_];
    top -= 38.0;
    maxOutputTokensButton_ = [[[NSButton alloc] initWithFrame:NSMakeRect(
      kStrappyAccountsInset, top, MAX(40.0, width - 122.0),
      kStrappyAccountsControlHeight)] autorelease];
    [maxOutputTokensButton_ setButtonType:XPButtonTypeSwitch];
    [maxOutputTokensButton_ setTitle:
      NSLocalizedString(@"Limit maximum output tokens", nil)];
    [maxOutputTokensButton_ setTarget:self];
    [maxOutputTokensButton_ setAction:@selector(toggleMaxOutputTokens:)];
    [maxOutputTokensButton_ setState:(maxOutputTokens > 0LL) ?
      XPControlStateValueOn : XPControlStateValueOff];
    [maxOutputTokensButton_ setAutoresizingMask:NSViewWidthSizable |
      NSViewMinYMargin];
    [rightPaneView_ addSubview:maxOutputTokensButton_];
    maxOutputTokensField_ = [[[NSTextField alloc] initWithFrame:NSMakeRect(
      NSWidth(bounds) - kStrappyAccountsInset - 112.0, top, 112.0,
      kStrappyAccountsControlHeight)] autorelease];
    [maxOutputTokensField_ setStringValue:(maxOutputTokens > 0LL) ?
      [NSString stringWithFormat:@"%lld", maxOutputTokens] : @""];
    [[maxOutputTokensField_ cell] setPlaceholderString:@"14286"];
    [maxOutputTokensField_ setEnabled:(maxOutputTokens > 0LL)];
    [maxOutputTokensField_ setAutoresizingMask:NSViewMinXMargin |
      NSViewMinYMargin];
    [rightPaneView_ addSubview:maxOutputTokensField_];
    [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(controlTextDidBeginEditing:)
             name:NSControlTextDidBeginEditingNotification
           object:maxOutputTokensField_];
    top -= 32.0;
    saveButton_ = StrappyAccountsButton(
      NSMakeRect(NSWidth(bounds) - kStrappyAccountsInset - 88.0,
                 top - 3.0,
                 88.0,
                 kStrappyAccountsControlHeight),
      NSLocalizedString(@"Save", nil), self, @selector(saveAccount:));
    [saveButton_ setAutoresizingMask:NSViewMinXMargin | NSViewMinYMargin];
    [saveButton_ setKeyEquivalent:@"\r"];
    [saveButton_ setEnabled:NO];
    [rightPaneView_ addSubview:saveButton_];
  } else if ([provider isEqualToString:@"other"]) {
    label = StrappyAccountsLabel(
      NSMakeRect(kStrappyAccountsInset, top, width, 16.0),
      NSLocalizedString(@"Responses Endpoint", nil),
      [NSFont systemFontOfSize:10.0]);
    [label setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
    [rightPaneView_ addSubview:label];
    top -= 28.0;
    endpointField_ = [[[NSTextField alloc] initWithFrame:NSMakeRect(
      kStrappyAccountsInset, top, width, kStrappyAccountsControlHeight)]
      autorelease];
    [endpointField_ setStringValue:[account objectForKey:@"responses_endpoint"]];
    [[endpointField_ cell] setPlaceholderString:
      NSLocalizedString(@"https://example.com/v1/responses", nil)];
    [endpointField_ setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
    [rightPaneView_ addSubview:endpointField_];
    [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(controlTextDidBeginEditing:)
             name:NSControlTextDidBeginEditingNotification
           object:endpointField_];
    top -= 40.0;
    label = StrappyAccountsLabel(
      NSMakeRect(kStrappyAccountsInset, top, width, 16.0),
      NSLocalizedString(@"Bearer Token (Optional)", nil),
      [NSFont systemFontOfSize:10.0]);
    [label setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
    [rightPaneView_ addSubview:label];
    top -= 28.0;
    tokenField_ = [[[NSSecureTextField alloc] initWithFrame:NSMakeRect(
      kStrappyAccountsInset, top, width, kStrappyAccountsControlHeight)]
      autorelease];
    token = nil;
    [[StrappyKeychain sharedKeychain]
      loadBearerToken:&token
      forProviderIdentifier:provider
      providerAccountIdentifier:selectedAccountIdentifier_];
    [tokenField_ setStringValue:(token != nil) ? token : @""];
    [tokenField_ setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
    [rightPaneView_ addSubview:tokenField_];
    [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(controlTextDidBeginEditing:)
             name:NSControlTextDidBeginEditingNotification
           object:tokenField_];
    top -= 38.0;
    maxOutputTokensButton_ = [[[NSButton alloc] initWithFrame:NSMakeRect(
      kStrappyAccountsInset, top, MAX(40.0, width - 122.0),
      kStrappyAccountsControlHeight)] autorelease];
    [maxOutputTokensButton_ setButtonType:XPButtonTypeSwitch];
    [maxOutputTokensButton_ setTitle:
      NSLocalizedString(@"Limit maximum output tokens", nil)];
    [maxOutputTokensButton_ setTarget:self];
    [maxOutputTokensButton_ setAction:@selector(toggleMaxOutputTokens:)];
    [maxOutputTokensButton_ setState:(maxOutputTokens > 0LL) ?
      XPControlStateValueOn : XPControlStateValueOff];
    [maxOutputTokensButton_ setAutoresizingMask:NSViewWidthSizable |
      NSViewMinYMargin];
    [rightPaneView_ addSubview:maxOutputTokensButton_];
    maxOutputTokensField_ = [[[NSTextField alloc] initWithFrame:NSMakeRect(
      NSWidth(bounds) - kStrappyAccountsInset - 112.0, top, 112.0,
      kStrappyAccountsControlHeight)] autorelease];
    [maxOutputTokensField_ setStringValue:(maxOutputTokens > 0LL) ?
      [NSString stringWithFormat:@"%lld", maxOutputTokens] : @""];
    [[maxOutputTokensField_ cell] setPlaceholderString:@"14286"];
    [maxOutputTokensField_ setEnabled:(maxOutputTokens > 0LL)];
    [maxOutputTokensField_ setAutoresizingMask:NSViewMinXMargin |
      NSViewMinYMargin];
    [rightPaneView_ addSubview:maxOutputTokensField_];
    [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(controlTextDidBeginEditing:)
             name:NSControlTextDidBeginEditingNotification
           object:maxOutputTokensField_];
    top -= 32.0;
    saveButton_ = StrappyAccountsButton(
      NSMakeRect(NSWidth(bounds) - kStrappyAccountsInset - 88.0,
                 top - 3.0,
                 88.0,
                 kStrappyAccountsControlHeight),
      NSLocalizedString(@"Save", nil), self, @selector(saveAccount:));
    [saveButton_ setAutoresizingMask:NSViewMinXMargin | NSViewMinYMargin];
    [saveButton_ setKeyEquivalent:@"\r"];
    [saveButton_ setEnabled:NO];
    [rightPaneView_ addSubview:saveButton_];
  } else {
    saveButton_ = StrappyAccountsButton(
      NSMakeRect(NSWidth(bounds) - kStrappyAccountsInset - 88.0,
                 top,
                 88.0,
                 kStrappyAccountsControlHeight),
      NSLocalizedString(@"Save", nil), self, @selector(saveAccount:));
    [saveButton_ setAutoresizingMask:NSViewMinXMargin | NSViewMinYMargin];
    [saveButton_ setKeyEquivalent:@"\r"];
    [saveButton_ setEnabled:NO];
    [rightPaneView_ addSubview:saveButton_];
    top -= 38.0;
    chatGPTStatusLabel_ = StrappyAccountsLabel(
      NSMakeRect(kStrappyAccountsInset, top, width, 34.0), @"",
      [NSFont systemFontOfSize:11.0]);
    [chatGPTStatusLabel_ setSelectable:YES];
    [[chatGPTStatusLabel_ cell] setWraps:YES];
    [chatGPTStatusLabel_ setAutoresizingMask:NSViewWidthSizable |
      NSViewMinYMargin];
    [rightPaneView_ addSubview:chatGPTStatusLabel_];
    top -= 38.0;
    chatGPTActionButton_ = StrappyAccountsButton(
      NSMakeRect(kStrappyAccountsInset, top, 86.0,
                 kStrappyAccountsControlHeight),
      NSLocalizedString(@"Sign In", nil), self,
      @selector(performChatGPTAction:));
    [chatGPTActionButton_ setAutoresizingMask:NSViewMaxXMargin |
      NSViewMinYMargin];
    [rightPaneView_ addSubview:chatGPTActionButton_];
    top -= 38.0;
    chatGPTURLField_ = [[[NSTextField alloc] initWithFrame:NSMakeRect(
      kStrappyAccountsInset, top, width - 78.0,
      kStrappyAccountsControlHeight)] autorelease];
    [chatGPTURLField_ setEditable:NO];
    [chatGPTURLField_ setSelectable:YES];
    [chatGPTURLField_ setAutoresizingMask:NSViewWidthSizable |
      NSViewMinYMargin];
    chatGPTOpenButton_ = StrappyAccountsButton(
      NSMakeRect(NSWidth(bounds) - kStrappyAccountsInset - 70.0, top, 70.0,
                 kStrappyAccountsControlHeight),
      NSLocalizedString(@"Open", nil), self,
      @selector(openChatGPTVerificationURL:));
    [chatGPTOpenButton_ setAutoresizingMask:NSViewMinXMargin |
      NSViewMinYMargin];
    [rightPaneView_ addSubview:chatGPTURLField_];
    [rightPaneView_ addSubview:chatGPTOpenButton_];
    top -= 32.0;
    chatGPTCodeField_ = [[[NSTextField alloc] initWithFrame:NSMakeRect(
      kStrappyAccountsInset, top, width - 78.0,
      kStrappyAccountsControlHeight)] autorelease];
    [chatGPTCodeField_ setEditable:NO];
    [chatGPTCodeField_ setSelectable:YES];
    [chatGPTCodeField_ setFont:[NSFont boldSystemFontOfSize:13.0]];
    [chatGPTCodeField_ setAlignment:XPTextAlignmentCenter];
    [chatGPTCodeField_ setAutoresizingMask:NSViewWidthSizable |
      NSViewMinYMargin];
    chatGPTCopyButton_ = StrappyAccountsButton(
      NSMakeRect(NSWidth(bounds) - kStrappyAccountsInset - 70.0, top, 70.0,
                 kStrappyAccountsControlHeight),
      NSLocalizedString(@"Copy", nil), self,
      @selector(copyChatGPTCode:));
    [chatGPTCopyButton_ setAutoresizingMask:NSViewMinXMargin |
      NSViewMinYMargin];
    [rightPaneView_ addSubview:chatGPTCodeField_];
    [rightPaneView_ addSubview:chatGPTCopyButton_];
    [[self selectedChatGPTAuthentication] refreshChatGPTCredentialsIfNeeded];
    [self reloadChatGPTState];
  }

  deleteButton_ = StrappyAccountsButton(
    NSMakeRect(kStrappyAccountsInset,
               20.0,
               138.0,
               kStrappyAccountsControlHeight),
    NSLocalizedString(@"Delete Account…", nil), self,
    @selector(deleteAccount:));
  [deleteButton_ setAutoresizingMask:NSViewMaxXMargin | NSViewMaxYMargin];
  [deleteButton_ setEnabled:![StrappySession hasInFlightSessions]];
  [rightPaneView_ addSubview:deleteButton_];
  [self configureKeyViewLoopForProvider:provider];
}

- (void)controlTextDidBeginEditing:(NSNotification *)notification
{
  id field;

  field = [notification object];
  if (((field == tokenField_) || (field == accountNameField_) ||
       (field == endpointField_) || (field == maxOutputTokensField_)) &&
      (saveButton_ != nil)) {
    [saveButton_ setEnabled:YES];
  }
}

- (void)toggleMaxOutputTokens:(id)sender
{
  BOOL enabled;

  (void)sender;
  enabled = [maxOutputTokensButton_ state] == XPControlStateValueOn;
  [maxOutputTokensField_ setEnabled:enabled];
  if (enabled) {
    [[self window] makeFirstResponder:maxOutputTokensField_];
  }
  [saveButton_ setEnabled:YES];
}

- (void)showError:(NSError *)error title:(NSString *)title
{
  NSAlert *alert;
  NSString *message;
  NSWindow *window;

  message = [error localizedDescription];
  if ([message length] == 0U) {
    message = NSLocalizedString(@"The request failed.", nil);
  }
  alert = [[[NSAlert alloc] init] autorelease];
  [alert setMessageText:title];
  [alert setInformativeText:message];
  [alert addButtonWithTitle:NSLocalizedString(@"OK", nil)];
  window = [self window];
  if (window == nil) {
    NSBeep();
    return;
  }
  [alert XP_beginSheetModalForWindow:window
                       modalDelegate:nil
                      didEndSelector:NULL
                         contextInfo:NULL];
}

- (void)saveAccount:(id)sender
{
  NSDictionary *account;
  NSString *provider;
  NSString *name;
  NSString *endpoint;
  NSString *token;
  NSString *maxOutputText;
  const char *maxOutputUTF8;
  char *maxOutputEnd;
  long long maxOutputTokens;
  NSError *error;
  StrappyKeychain *keychain;
  NSObject *credentialLock;
  BOOL credentialSaved;

  (void)sender;
  account = [self selectedAccount];
  provider = [account objectForKey:@"provider_id"];
  name = [[accountNameField_ stringValue]
    stringByTrimmingCharactersInSet:
      [NSCharacterSet whitespaceAndNewlineCharacterSet]];
  endpoint = (endpointField_ != nil) ? [[endpointField_ stringValue]
    stringByTrimmingCharactersInSet:
      [NSCharacterSet whitespaceAndNewlineCharacterSet]] : @"";
  token = (tokenField_ != nil) ? [[tokenField_ stringValue]
    stringByTrimmingCharactersInSet:
      [NSCharacterSet whitespaceAndNewlineCharacterSet]] : @"";
  maxOutputTokens = 0LL;
  maxOutputText = @"";
  if ((maxOutputTokensButton_ != nil) &&
      ([maxOutputTokensButton_ state] == XPControlStateValueOn)) {
    maxOutputText = [[maxOutputTokensField_ stringValue]
      stringByTrimmingCharactersInSet:
        [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    maxOutputUTF8 = [maxOutputText UTF8String];
    maxOutputEnd = NULL;
    errno = 0;
    maxOutputTokens = (maxOutputUTF8 != NULL) ?
      strtoll(maxOutputUTF8, &maxOutputEnd, 10) : 0LL;
    if (([maxOutputText length] == 0U) || (errno == ERANGE) ||
        (maxOutputEnd == maxOutputUTF8) || (maxOutputEnd == NULL) ||
        (*maxOutputEnd != '\0') || (maxOutputTokens <= 0LL)) {
      [self showError:nil title:NSLocalizedString(
        @"Maximum Output Tokens Must Be a Positive Integer", nil)];
      return;
    }
  }
  if ([name length] == 0U) {
    [self showError:nil
              title:NSLocalizedString(@"Account Name Is Required", nil)];
    return;
  }
  if ([provider isEqualToString:@"openrouter"] && ([token length] == 0U)) {
    [self showError:nil
              title:NSLocalizedString(@"API Key Is Required", nil)];
    return;
  }
  if ([provider isEqualToString:@"other"] && ([endpoint length] == 0U)) {
    [self showError:nil
              title:NSLocalizedString(@"Responses Endpoint Is Required", nil)];
    return;
  }
  error = nil;
  if (![StrappySession
        updateProviderAccountIdentifier:selectedAccountIdentifier_
                            displayName:name
                      responsesEndpoint:endpoint
                        maxOutputTokens:maxOutputTokens
                                  error:&error]) {
    [self showError:error
              title:NSLocalizedString(@"Could Not Save Account", nil)];
    return;
  }
  credentialSaved = YES;
  if (![provider isEqualToString:@"openai_chatgpt"]) {
    keychain = [StrappyKeychain sharedKeychain];
    credentialLock = [keychain
      credentialLockForProviderIdentifier:provider
      providerAccountIdentifier:selectedAccountIdentifier_];
    @synchronized(credentialLock) {
      if ([token length] > 0U) {
        credentialSaved = [keychain saveBearerToken:token
          forProviderIdentifier:provider
          providerAccountIdentifier:selectedAccountIdentifier_];
      } else {
        credentialSaved = [keychain
          deleteBearerTokenForProviderIdentifier:provider
          providerAccountIdentifier:selectedAccountIdentifier_];
      }
    }
  }
  if (!credentialSaved) {
    [self showError:nil
              title:NSLocalizedString(@"Could Not Save Credential", nil)];
    return;
  }
  [self reloadAccountsPreservingSelection];
}

- (void)deleteAccount:(id)sender
{
  NSDictionary *account;
  NSAlert *alert;
  NSWindow *window;

  (void)sender;
  account = [self selectedAccount];
  if (account == nil) {
    return;
  }
  if ([StrappySession hasInFlightSessions]) {
    [self showError:nil
              title:NSLocalizedString(
                @"Wait for active requests before deleting this account.",
                nil)];
    return;
  }
  window = [self window];
  if (window == nil) {
    NSBeep();
    return;
  }
  alert = [[[NSAlert alloc] init] autorelease];
  [alert setMessageText:NSLocalizedString(@"Delete Account?", nil)];
  [alert setInformativeText:[NSString stringWithFormat:NSLocalizedString(
    @"The credential for \"%@\" will be removed. Existing conversations and history will be preserved, but they cannot send new requests with this account.",
    nil), [account objectForKey:@"name"]]];
  [alert addButtonWithTitle:NSLocalizedString(@"Delete Account", nil)];
  [alert addButtonWithTitle:NSLocalizedString(@"Cancel", nil)];
  [alert XP_beginSheetModalForWindow:window
                       modalDelegate:self
                      didEndSelector:@selector(deleteAccountAlertDidEnd:returnCode:contextInfo:)
                         contextInfo:[selectedAccountIdentifier_ retain]];
}

- (void)deleteAccountAlertDidEnd:(NSAlert *)alert
                      returnCode:(NSInteger)returnCode
                     contextInfo:(void *)contextInfo
{
  NSString *accountIdentifier;
  NSError *error;

  (void)alert;
  accountIdentifier = (NSString *)contextInfo;
  if (returnCode != NSAlertFirstButtonReturn) {
    [accountIdentifier release];
    return;
  }
  error = nil;
  if (![StrappySession
        archiveProviderAccountIdentifier:accountIdentifier
                                    error:&error]) {
    [self showError:error
              title:NSLocalizedString(@"Could Not Delete Account", nil)];
    [accountIdentifier release];
    return;
  }
  [accountIdentifier release];
  [selectedAccountIdentifier_ release];
  selectedAccountIdentifier_ = nil;
  [self reloadAccountsPreservingSelection];
}

- (StrappyAuthentication *)selectedChatGPTAuthentication
{
  NSDictionary *account;

  account = [self selectedAccount];
  if (![[account objectForKey:@"provider_id"]
        isEqualToString:@"openai_chatgpt"]) {
    return nil;
  }
  return [StrappyAuthentication
    authenticationForProviderAccountIdentifier:selectedAccountIdentifier_];
}

- (void)authenticationDidChange:(NSNotification *)notification
{
  StrappyAuthentication *authentication;

  authentication = [self selectedChatGPTAuthentication];
  if ((authentication != nil) && ([notification object] == authentication)) {
    [self reloadChatGPTState];
  }
}

- (void)reloadChatGPTState
{
  StrappyAuthentication *authentication;
  StrappyAuthenticationState state;
  NSString *message;
  NSString *verificationURL;
  NSString *code;
  BOOL signingIn;
  BOOL inFlight;
  BOOL hasCredentials;
  BOOL providerEnabled;

  authentication = [self selectedChatGPTAuthentication];
  if ((authentication == nil) || (chatGPTStatusLabel_ == nil)) {
    return;
  }
  state = [authentication state];
  verificationURL = [authentication verificationURL];
  code = [authentication userCode];
  signingIn = (state == StrappyAuthenticationStateRequestingCode) ||
    (state == StrappyAuthenticationStateAwaitingUser);
  inFlight = [authentication isOperationInFlight];
  hasCredentials = [authentication hasStoredCredentials];
  providerEnabled = [StrappyAuthentication isChatGPTProviderEnabled];
  if (!providerEnabled) {
    message = NSLocalizedString(
      @"Disabled by the experimental provider kill switch", nil);
  } else switch (state) {
    case StrappyAuthenticationStateRequestingCode:
      message = NSLocalizedString(@"Requesting a device code…", nil);
      break;
    case StrappyAuthenticationStateAwaitingUser:
      message = NSLocalizedString(
        @"Open the verification page and enter this code.", nil);
      break;
    case StrappyAuthenticationStateSignedIn:
      message = NSLocalizedString(@"Signed in to ChatGPT.", nil);
      break;
    case StrappyAuthenticationStateRefreshing:
      message = NSLocalizedString(@"Refreshing ChatGPT credentials…", nil);
      break;
    case StrappyAuthenticationStateError:
      message = [authentication errorMessage];
      if ([message length] == 0U) {
        message = NSLocalizedString(@"ChatGPT authentication failed.", nil);
      }
      break;
    case StrappyAuthenticationStateCancelled:
      message = NSLocalizedString(@"ChatGPT sign-in was cancelled.", nil);
      break;
    case StrappyAuthenticationStateSignedOut:
    default:
      message = NSLocalizedString(@"Not signed in to ChatGPT.", nil);
      break;
  }
  [chatGPTStatusLabel_ setStringValue:(message != nil) ? message : @""];
  [chatGPTURLField_ setStringValue:
    (verificationURL != nil) ? verificationURL : @""];
  [chatGPTCodeField_ setStringValue:(code != nil) ? code : @""];
  [chatGPTURLField_ setEnabled:signingIn];
  [chatGPTCodeField_ setEnabled:signingIn];
  if (signingIn) {
    [chatGPTActionButton_ setTitle:NSLocalizedString(@"Cancel", nil)];
  } else if (hasCredentials) {
    [chatGPTActionButton_ setTitle:NSLocalizedString(@"Sign Out", nil)];
  } else {
    [chatGPTActionButton_ setTitle:NSLocalizedString(@"Sign In", nil)];
  }
  [chatGPTActionButton_ setEnabled:providerEnabled &&
    (signingIn || !inFlight)];
  [chatGPTCopyButton_ setEnabled:providerEnabled && signingIn &&
    ([code length] > 0U)];
  [chatGPTOpenButton_ setEnabled:providerEnabled && signingIn &&
    ([verificationURL length] > 0U)];
}

- (void)performChatGPTAction:(id)sender
{
  StrappyAuthentication *authentication;
  StrappyAuthenticationState state;

  (void)sender;
  authentication = [self selectedChatGPTAuthentication];
  state = [authentication state];
  if ((state == StrappyAuthenticationStateRequestingCode) ||
      (state == StrappyAuthenticationStateAwaitingUser)) {
    [authentication cancelChatGPTDeviceLogin];
  } else if ([authentication hasStoredCredentials]) {
    if (![authentication signOutChatGPT]) {
      NSBeep();
    }
  } else if (![authentication startChatGPTDeviceLogin]) {
    NSBeep();
  }
}

- (void)copyChatGPTCode:(id)sender
{
  NSString *code;
  NSPasteboard *pasteboard;

  (void)sender;
  code = [[self selectedChatGPTAuthentication] userCode];
  if ([code length] == 0U) {
    NSBeep();
    return;
  }
  pasteboard = [NSPasteboard generalPasteboard];
  [pasteboard declareTypes:[NSArray arrayWithObject:
    StrappyAccountsPasteboardStringType] owner:nil];
  [pasteboard setString:code forType:StrappyAccountsPasteboardStringType];
}

- (void)openChatGPTVerificationURL:(id)sender
{
  NSString *verificationURL;
  NSURL *url;

  (void)sender;
  verificationURL = [[self selectedChatGPTAuthentication] verificationURL];
  url = ([verificationURL length] > 0U) ?
    [NSURL URLWithString:verificationURL] : nil;
  if ((url == nil) || ![[NSWorkspace sharedWorkspace] openURL:url]) {
    NSBeep();
  }
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [accountTableView_ setDataSource:nil];
  [accountTableView_ setDelegate:nil];
  [accountTableView_ release];
  [accountScrollView_ release];
  [rightPaneView_ release];
  [accounts_ release];
  [providers_ release];
  [selectedAccountIdentifier_ release];
  [super dealloc];
}

@end
