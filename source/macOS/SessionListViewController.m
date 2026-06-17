#import "SessionListViewController.h"
#import "StrappySession.h"

static const CGFloat kStrappySessionRowHeight = 52.0;
static NSString * const kStrappySessionRowTypeKey = @"row_type";
static NSString * const kStrappySessionRowTypeNew = @"new";

static NSDictionary *StrappyNewSessionRow(void)
{
  return [NSDictionary dictionaryWithObjectsAndKeys:
    kStrappySessionRowTypeNew, kStrappySessionRowTypeKey,
    NSLocalizedString(@"New Session", nil), @"prompt",
    @"", @"created_at",
    nil];
}

static NSString *StrappySessionPromptPreview(NSDictionary *session)
{
  NSString *prompt;

  if ([[session objectForKey:kStrappySessionRowTypeKey]
        isEqualToString:kStrappySessionRowTypeNew]) {
    return NSLocalizedString(@"New Session", nil);
  }

  prompt = [session objectForKey:@"prompt"];
  if (![prompt isKindOfClass:[NSString class]] || ([prompt length] == 0U)) {
    return NSLocalizedString(@"Untitled Session", nil);
  }

  return prompt;
}

@interface StrappySessionCell : NSCell
@end

@implementation StrappySessionCell

- (void)drawInteriorWithFrame:(NSRect)frame inView:(NSView *)view
{
  NSDictionary *session;
  NSString *title;
  NSString *subtitle;
  NSColor *textColor;
  NSDictionary *titleAttrs;
  NSDictionary *subtitleAttrs;
  NSRect titleRect;
  NSRect subtitleRect;
  BOOL isNewSession;

  session = [self objectValue];
  if (![session isKindOfClass:[NSDictionary class]]) {
    session = nil;
  }

  isNewSession = [[[session objectForKey:kStrappySessionRowTypeKey]
                    description] isEqualToString:kStrappySessionRowTypeNew];
  title = StrappySessionPromptPreview(session);
  subtitle = isNewSession ? NSLocalizedString(@"Start a new conversation", nil)
                          : [session objectForKey:@"created_at"];
  if (![subtitle isKindOfClass:[NSString class]]) {
    subtitle = @"";
  }

  textColor = [self isHighlighted] ? [NSColor alternateSelectedControlTextColor]
                                   : [NSColor controlTextColor];
  titleAttrs = [NSDictionary dictionaryWithObjectsAndKeys:
    textColor, NSForegroundColorAttributeName,
    [NSFont systemFontOfSize:13.0], NSFontAttributeName,
    nil];
  subtitleAttrs = [NSDictionary dictionaryWithObjectsAndKeys:
    ([self isHighlighted] ? [NSColor alternateSelectedControlTextColor]
                          : [NSColor disabledControlTextColor]),
      NSForegroundColorAttributeName,
    [NSFont systemFontOfSize:10.0], NSFontAttributeName,
    nil];

  titleRect = NSMakeRect(NSMinX(frame) + 10.0,
                         NSMinY(frame) + 8.0,
                         NSWidth(frame) - 20.0,
                         18.0);
  subtitleRect = NSMakeRect(NSMinX(frame) + 10.0,
                            NSMinY(frame) + 28.0,
                            NSWidth(frame) - 20.0,
                            14.0);

  [title drawInRect:titleRect withAttributes:titleAttrs];
  [subtitle drawInRect:subtitleRect withAttributes:subtitleAttrs];
  (void)view;
}

@end

@implementation SessionListViewController

- (void)setDelegate:(id<SessionListViewControllerDelegate>)delegate
{
  delegate_ = delegate;
}

- (id<SessionListViewControllerDelegate>)delegate
{
  return delegate_;
}

- (void)viewDidLoad
{
  NSTableColumn *column;

  [super viewDidLoad];

  scrollView_ = [[NSScrollView alloc] initWithFrame:[[self view] bounds]];
  [scrollView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [scrollView_ setHasVerticalScroller:YES];
  [scrollView_ setHasHorizontalScroller:NO];
  [scrollView_ setAutohidesScrollers:YES];
  [scrollView_ setBorderType:NSNoBorder];

  tableView_ = [[NSTableView alloc] initWithFrame:[[self view] bounds]];
  [tableView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [tableView_ setDataSource:self];
  [tableView_ setDelegate:self];
  [tableView_ setHeaderView:nil];
  [tableView_ setRowHeight:kStrappySessionRowHeight];
  [tableView_ setUsesAlternatingRowBackgroundColors:NO];
  [tableView_ setFocusRingType:NSFocusRingTypeNone];

  column = [[[NSTableColumn alloc] initWithIdentifier:@"session"] autorelease];
  [column setDataCell:[[[StrappySessionCell alloc] init] autorelease]];
  [tableView_ addTableColumn:column];

  [scrollView_ setDocumentView:tableView_];
  [[self view] addSubview:scrollView_];

  [self reloadData];
  [tableView_ sizeLastColumnToFit];
}

- (void)notifySelectedSession
{
  NSInteger row;
  NSDictionary *session;

  row = [tableView_ selectedRow];
  if ((row < 0) || (row >= (NSInteger)[rows_ count])) {
    if (delegate_ != nil) {
      [delegate_ sessionListViewController:self didSelectSession:nil];
    }
    return;
  }

  session = [rows_ objectAtIndex:(NSUInteger)row];
  if (delegate_ != nil) {
    if ([[session objectForKey:kStrappySessionRowTypeKey]
          isEqualToString:kStrappySessionRowTypeNew]) {
      [delegate_ sessionListViewController:self didSelectSession:nil];
    } else {
      [delegate_ sessionListViewController:self didSelectSession:session];
    }
  }
}

- (NSInteger)rowForSessionIdentifier:(NSNumber *)sessionIdentifier
{
  NSUInteger index;

  if (sessionIdentifier == nil) {
    return 0;
  }

  for (index = 0U; index < [rows_ count]; index++) {
    NSDictionary *session = [rows_ objectAtIndex:index];
    NSNumber *candidate = [session objectForKey:@"id"];
    if ([candidate isKindOfClass:[NSNumber class]] &&
        [candidate isEqualToNumber:sessionIdentifier]) {
      return (NSInteger)index;
    }
  }

  return -1;
}

- (void)reloadData
{
  NSError *error;
  NSArray *sessions;
  NSMutableArray *displayRows;
  NSInteger row;

  error = nil;
  sessions = [StrappySession sessionSummariesWithError:&error];
  if (sessions == nil) {
    sessions = [NSArray array];
  }

  displayRows = [NSMutableArray arrayWithCapacity:[sessions count] + 1U];
  [displayRows addObject:StrappyNewSessionRow()];
  [displayRows addObjectsFromArray:sessions];

  [rows_ release];
  rows_ = [displayRows copy];
  [tableView_ reloadData];

  row = [self rowForSessionIdentifier:selectedSessionId_];

  if (row >= 0) {
    [tableView_ selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row]
             byExtendingSelection:NO];
    [tableView_ scrollRowToVisible:row];
  } else {
    [tableView_ deselectAll:self];
    [self notifySelectedSession];
  }
}

- (void)selectSessionIdentifier:(NSNumber *)sessionIdentifier
{
  NSInteger row;

  if (selectedSessionId_ != sessionIdentifier) {
    [selectedSessionId_ release];
    selectedSessionId_ = [sessionIdentifier retain];
  }

  row = [self rowForSessionIdentifier:selectedSessionId_];
  if (row >= 0) {
    [tableView_ selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row]
             byExtendingSelection:NO];
    [tableView_ scrollRowToVisible:row];
  }
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView
{
  (void)tableView;
  return (NSInteger)[rows_ count];
}

- (id)tableView:(NSTableView *)tableView
    objectValueForTableColumn:(NSTableColumn *)tableColumn
                          row:(NSInteger)row
{
  (void)tableView;
  (void)tableColumn;
  if ((row < 0) || (row >= (NSInteger)[rows_ count])) {
    return nil;
  }
  return [rows_ objectAtIndex:(NSUInteger)row];
}

- (void)tableViewSelectionDidChange:(NSNotification *)notification
{
  NSInteger row;
  NSDictionary *session;

  (void)notification;
  row = [tableView_ selectedRow];
  if ((row >= 0) && (row < (NSInteger)[rows_ count])) {
    session = [rows_ objectAtIndex:(NSUInteger)row];
    [selectedSessionId_ release];
    selectedSessionId_ = nil;
    if ([[session objectForKey:@"id"] isKindOfClass:[NSNumber class]]) {
      selectedSessionId_ = [[session objectForKey:@"id"] retain];
    }
  }
  [self notifySelectedSession];
}

- (void)dealloc
{
  [scrollView_ release];
  [tableView_ release];
  [rows_ release];
  [selectedSessionId_ release];
  [super dealloc];
}

@end
