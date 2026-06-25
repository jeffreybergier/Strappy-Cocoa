#import "SessionListViewController.h"
#import "AIFontAwesome.h"
#import "StrappyBottomToolbarView.h"
#import "StrappySession.h"
#import "XPFoundation.h"

static const CGFloat kStrappySessionRowHeight = 80.0;
static const CGFloat kStrappySectionRowHeight = 24.0;
static const CGFloat kStrappySessionToolbarHeight = 32.0;
static const CGFloat kStrappySessionToolbarPad = 4.0;
static const CGFloat kStrappyPadLeft = 8.0;
static const CGFloat kStrappyPadRight = 8.0;
static const CGFloat kStrappyTimestampTop = 8.0;
static const CGFloat kStrappyTitleLineHeight = 17.0;
static const CGFloat kStrappyTitleLines = 3.0;
static const CGFloat kStrappyTimestampGap = 3.0;
static const CGFloat kStrappyTimestampHeight = 14.0;
static const AIFontAwesomeIcon kStrappySessionToolbarPlusIcon =
  (AIFontAwesomeIcon)0xF067;

enum {
  kStrappySessionToolbarClose = 0,
  kStrappySessionToolbarNew = 1
};

static NSString * const kStrappySessionRowTypeKey = @"row_type";
static NSString * const kStrappySessionRowTypeSection = @"section";
static NSString * const kStrappySessionRowTypeSession = @"session";
static NSString * const kStrappySessionRowTypeEmpty = @"empty";

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 101400
  #define StrappyPasteboardStringType NSPasteboardTypeString
#else
  #define StrappyPasteboardStringType NSStringPboardType
#endif

static NSDictionary *StrappySectionRow(NSString *title)
{
  return [NSDictionary dictionaryWithObjectsAndKeys:
    kStrappySessionRowTypeSection, kStrappySessionRowTypeKey,
    (title ? title : @""), @"section_title",
    nil];
}

static NSDictionary *StrappyEmptySessionRow(void)
{
  return [NSDictionary dictionaryWithObjectsAndKeys:
    kStrappySessionRowTypeEmpty, kStrappySessionRowTypeKey,
    NSLocalizedString(@"No conversations yet", nil), @"prompt",
    NSLocalizedString(@"Create a conversation to begin.", nil), @"last_message_text",
    nil];
}

static NSDictionary *StrappySessionDisplayRow(NSDictionary *session)
{
  NSMutableDictionary *row;

  row = [NSMutableDictionary dictionaryWithDictionary:session];
  [row setObject:kStrappySessionRowTypeSession forKey:kStrappySessionRowTypeKey];
  return row;
}

static NSString *StrappySessionPromptPreview(NSDictionary *session)
{
  NSString *name;
  NSString *prompt;

  name = [session objectForKey:@"name"];
  if ([name isKindOfClass:[NSString class]] && ([name length] > 0U)) {
    return name;
  }

  prompt = [session objectForKey:@"prompt"];
  if (![prompt isKindOfClass:[NSString class]] || ([prompt length] == 0U)) {
    return NSLocalizedString(@"Untitled Session", nil);
  }

  return prompt;
}

static NSString *StrappyDisplayTimestamp(NSString *timestamp)
{
  static NSDateFormatter *inputFormatter = nil;
  static NSDateFormatter *displayFormatter = nil;
  NSDate *date;

  if (![timestamp isKindOfClass:[NSString class]] || ([timestamp length] == 0U)) {
    return @"";
  }

  if (inputFormatter == nil) {
    inputFormatter = [[NSDateFormatter alloc] init];
    [inputFormatter setFormatterBehavior:NSDateFormatterBehavior10_4];
    [inputFormatter setDateFormat:@"yyyy-MM-dd'T'HH:mm:ss.SSS'Z'"];
    [inputFormatter setTimeZone:[NSTimeZone timeZoneForSecondsFromGMT:0]];
  }
  if (displayFormatter == nil) {
    displayFormatter = [[NSDateFormatter alloc] init];
    [displayFormatter setFormatterBehavior:NSDateFormatterBehavior10_4];
    [displayFormatter setDateStyle:NSDateFormatterShortStyle];
    [displayFormatter setTimeStyle:NSDateFormatterShortStyle];
  }

  date = [inputFormatter dateFromString:timestamp];
  if (date == nil) {
    return timestamp;
  }
  return [displayFormatter stringFromDate:date];
}

static NSDictionary *StrappyTextAttributes(NSFont *font,
                                           NSColor *color,
                                           NSLineBreakMode breakMode)
{
  NSMutableParagraphStyle *style;

  style = [[[NSMutableParagraphStyle alloc] init] autorelease];
  [style setLineBreakMode:breakMode];
  return [NSDictionary dictionaryWithObjectsAndKeys:
    font, NSFontAttributeName,
    color, NSForegroundColorAttributeName,
    style, NSParagraphStyleAttributeName,
    nil];
}

static void StrappyDrawMultilineString(NSString *text,
                                       NSRect rect,
                                       NSDictionary *attrs)
{
  NSStringDrawingOptions options;

  if (![text isKindOfClass:[NSString class]]) {
    text = @"";
  }

  options = NSStringDrawingUsesLineFragmentOrigin |
            NSStringDrawingTruncatesLastVisibleLine;
  [text drawWithRect:rect options:options attributes:attrs];
}

static NSColor *StrappySelectedTextColor(BOOL selected)
{
  return selected ? [NSColor alternateSelectedControlTextColor]
                  : [NSColor controlTextColor];
}

static NSColor *StrappySecondaryTextColor(BOOL selected)
{
  return selected ? [NSColor alternateSelectedControlTextColor]
                  : [NSColor disabledControlTextColor];
}

@protocol StrappySessionTableViewMenu <NSObject>
- (NSMenu *)contextMenuForRow:(NSInteger)row;
@end

@interface StrappySessionTableView : NSTableView
@end

@implementation StrappySessionTableView

- (NSMenu *)menuForEvent:(NSEvent *)event
{
  NSPoint point;
  NSInteger row;
  id<StrappySessionTableViewMenu> source;
  NSMenu *menu;

  point = [self convertPoint:[event locationInWindow] fromView:nil];
  row = [self rowAtPoint:point];
  if (row < 0) {
    return nil;
  }

  source = (id<StrappySessionTableViewMenu>)[self dataSource];
  if ([source respondsToSelector:@selector(contextMenuForRow:)]) {
    menu = [source contextMenuForRow:row];
    if (menu != nil) {
      [self selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row]
          byExtendingSelection:NO];
    }
    return menu;
  }
  return nil;
}

@end

@interface StrappySessionCell : NSCell
@end

@implementation StrappySessionCell

- (void)drawSectionWithFrame:(NSRect)frame row:(NSDictionary *)row
{
  NSString *title;
  NSDictionary *attrs;
  NSRect rect;

  title = [row objectForKey:@"section_title"];
  if (![title isKindOfClass:[NSString class]]) {
    title = @"";
  }

  attrs = StrappyTextAttributes([NSFont boldSystemFontOfSize:11.0],
                                [NSColor disabledControlTextColor],
                                NSLineBreakByTruncatingTail);
  rect = NSMakeRect(NSMinX(frame) + kStrappyPadLeft,
                    NSMinY(frame) + 5.0,
                    NSWidth(frame) - (kStrappyPadLeft + kStrappyPadRight),
                    14.0);
  [title drawInRect:rect withAttributes:attrs];
}

- (void)drawSessionWithFrame:(NSRect)frame row:(NSDictionary *)row
{
  NSString *title;
  NSString *timestamp;
  NSString *type;
  BOOL selected;
  CGFloat titleHeight;
  CGFloat titleY;
  NSRect titleRect;
  NSRect timestampRect;
  NSDictionary *titleAttrs;
  NSDictionary *metaAttrs;

  selected = [self isHighlighted];
  type = [row objectForKey:kStrappySessionRowTypeKey];

  title = StrappySessionPromptPreview(row);
  timestamp = @"";
  if ([type isEqualToString:kStrappySessionRowTypeSession]) {
    timestamp = StrappyDisplayTimestamp([row objectForKey:@"last_message_at"]);
  }

  titleAttrs = StrappyTextAttributes([NSFont systemFontOfSize:13.0],
                                     StrappySelectedTextColor(selected),
                                     NSLineBreakByWordWrapping);
  metaAttrs = StrappyTextAttributes([NSFont systemFontOfSize:10.0],
                                    StrappySecondaryTextColor(selected),
                                    NSLineBreakByTruncatingTail);

  titleHeight = kStrappyTitleLineHeight * kStrappyTitleLines;
  titleY = NSMinY(frame) + kStrappyTimestampTop;
  if ([timestamp length] > 0U) {
    timestampRect = NSMakeRect(NSMinX(frame) + kStrappyPadLeft,
                               NSMinY(frame) + kStrappyTimestampTop,
                               NSWidth(frame) - (kStrappyPadLeft + kStrappyPadRight),
                               kStrappyTimestampHeight);
    [timestamp drawInRect:timestampRect withAttributes:metaAttrs];

    titleY += kStrappyTimestampHeight + kStrappyTimestampGap;
  }

  titleRect = NSMakeRect(NSMinX(frame) + kStrappyPadLeft,
                         titleY,
                         NSWidth(frame) - (kStrappyPadLeft + kStrappyPadRight),
                         titleHeight);
  StrappyDrawMultilineString(title, titleRect, titleAttrs);
}

- (void)drawInteriorWithFrame:(NSRect)frame inView:(NSView *)view
{
  NSDictionary *row;
  NSString *type;

  (void)view;
  row = [self objectValue];
  if (![row isKindOfClass:[NSDictionary class]]) {
    return;
  }

  type = [row objectForKey:kStrappySessionRowTypeKey];
  if ([type isEqualToString:kStrappySessionRowTypeSection]) {
    [self drawSectionWithFrame:frame row:row];
    return;
  }

  [self drawSessionWithFrame:frame row:row];
}

@end

@interface SessionListViewController ()
- (void)rebuildToolbarSegmentIcons;
- (void)updateToolbarSegments;
- (void)toolbarDidMoveToWindow:(id)sender;
- (void)toolbarSegmentClicked:(id)sender;
- (void)closeActiveSession:(id)sender;
- (void)layoutSidebarViews;
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
  StrappyBottomToolbarView *toolbar;

  [super viewDidLoad];

  scrollView_ = [[NSScrollView alloc] initWithFrame:[[self view] bounds]];
  [scrollView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [scrollView_ setHasVerticalScroller:YES];
  [scrollView_ setHasHorizontalScroller:NO];
  [scrollView_ setAutohidesScrollers:YES];
  [scrollView_ setBorderType:NSNoBorder];
  if (AICCCurrentTier() >= AICCTierMiddle) {
    [scrollView_ setDrawsBackground:NO];
    [[scrollView_ contentView] setDrawsBackground:NO];
  }
  [scrollView_ XP_setAutomaticallyAdjustsContentInsets:NO];

  tableView_ = [[StrappySessionTableView alloc] initWithFrame:[[self view] bounds]];
  [tableView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [tableView_ setDataSource:self];
  [tableView_ setDelegate:self];
  [tableView_ setHeaderView:nil];
  [tableView_ setRowHeight:kStrappySessionRowHeight];
  [tableView_ setUsesAlternatingRowBackgroundColors:NO];
  [tableView_ setFocusRingType:NSFocusRingTypeNone];
  if (AICCCurrentTier() >= AICCTierMiddle) {
    [tableView_ setBackgroundColor:[NSColor clearColor]];
  }
  [tableView_ XP_setSourceListStyle];
  [tableView_ XP_setFloatsGroupRows:YES];
  [scrollView_ setFocusRingType:NSFocusRingTypeNone];

  column = [[[NSTableColumn alloc] initWithIdentifier:@"session"] autorelease];
  [column setDataCell:[[[StrappySessionCell alloc] init] autorelease]];
  [tableView_ addTableColumn:column];

  [scrollView_ setDocumentView:tableView_];
  [[self view] addSubview:scrollView_];

  toolbar = [[StrappyBottomToolbarView alloc] initWithFrame:NSZeroRect];
  toolbarView_ = toolbar;
  [toolbarView_ setAutoresizingMask:NSViewWidthSizable | NSViewMaxYMargin];
  [toolbar setWindowChangeTarget:self action:@selector(toolbarDidMoveToWindow:)];
  [[self view] addSubview:toolbarView_];

  toolbarSegmented_ = [[NSSegmentedControl alloc] initWithFrame:NSZeroRect];
  [toolbarSegmented_ setSegmentCount:2];
  [[toolbarSegmented_ cell] setTrackingMode:NSSegmentSwitchTrackingMomentary];
  if ([[toolbarSegmented_ cell] respondsToSelector:@selector(setSegmentStyle:)]) {
    [[toolbarSegmented_ cell] setSegmentStyle:NSSegmentStyleTexturedRounded];
  }
  [toolbarSegmented_ setTarget:self];
  [toolbarSegmented_ setAction:@selector(toolbarSegmentClicked:)];
  [toolbarSegmented_ setAutoresizingMask:NSViewMinXMargin | NSViewMaxYMargin];
  [self rebuildToolbarSegmentIcons];
  [toolbarView_ addSubview:toolbarSegmented_];

  [self reloadData];
  [tableView_ sizeLastColumnToFit];
  [self layoutSidebarViews];
}

- (void)viewDidLayout
{
  [super viewDidLayout];
  [self layoutSidebarViews];
}

- (void)layoutSidebarViews
{
  NSRect bounds;
  CGFloat segmentWidth;
  CGFloat segmentHeight;

  bounds = [[self view] bounds];

  [toolbarView_ setFrame:NSMakeRect(0.0,
                                    0.0,
                                    bounds.size.width,
                                    kStrappySessionToolbarHeight)];
  if (AICCCurrentTier() >= AICCTierMiddle) {
    CGFloat top;

    [scrollView_ setFrame:NSMakeRect(0.0,
                                     0.0,
                                     bounds.size.width,
                                     bounds.size.height)];
    top = [[[self view] window] XP_titlebarHeight];
    [scrollView_ XP_setContentInsetsTop:top
                                   left:0.0
                                 bottom:kStrappySessionToolbarHeight
                                  right:0.0];
  } else {
    CGFloat scrollHeight;

    scrollHeight = bounds.size.height - kStrappySessionToolbarHeight;
    if (scrollHeight < 0.0) {
      scrollHeight = 0.0;
    }
    [scrollView_ setFrame:NSMakeRect(0.0,
                                     kStrappySessionToolbarHeight,
                                     bounds.size.width,
                                     scrollHeight)];
  }

  segmentWidth = [toolbarSegmented_ frame].size.width;
  segmentHeight = [toolbarSegmented_ frame].size.height;
  [toolbarSegmented_ setFrame:NSMakeRect(bounds.size.width -
                                         kStrappySessionToolbarPad -
                                         segmentWidth,
                                         kStrappySessionToolbarPad,
                                         segmentWidth,
                                         segmentHeight)];
}

- (void)toolbarDidMoveToWindow:(id)sender
{
  (void)sender;
  [toolbarView_ XP_pinToWindowAppearance];
  [self rebuildToolbarSegmentIcons];
  [self updateToolbarSegments];
}

- (void)rebuildToolbarSegmentIcons
{
  CGFloat scale;
  NSImage *closeImage;
  NSImage *newImage;

  if (toolbarSegmented_ == nil) {
    return;
  }

  scale = 1.0;
  if ([[toolbarView_ window] respondsToSelector:@selector(XP_backingScaleFactor)]) {
    scale = [[toolbarView_ window] XP_backingScaleFactor];
  }
  if (scale < 1.0) {
    scale = 1.0;
  }

  closeImage = [AIFontAwesome imageForIcon:AIFAXmark
                                     style:AIFontAwesomeStyleSolid
                                  iconSize:14.0
                                canvasSize:20.0
                                     scale:scale];
  newImage = [AIFontAwesome imageForIcon:kStrappySessionToolbarPlusIcon
                                   style:AIFontAwesomeStyleSolid
                                iconSize:14.0
                              canvasSize:20.0
                                   scale:scale];

  if (closeImage != nil) {
    [toolbarSegmented_ setImage:closeImage
                     forSegment:kStrappySessionToolbarClose];
    [toolbarSegmented_ setLabel:@""
                     forSegment:kStrappySessionToolbarClose];
  } else {
    [toolbarSegmented_ setLabel:NSLocalizedString(@"Close", nil)
                     forSegment:kStrappySessionToolbarClose];
  }

  if (newImage != nil) {
    [toolbarSegmented_ setImage:newImage
                     forSegment:kStrappySessionToolbarNew];
    [toolbarSegmented_ setLabel:@""
                     forSegment:kStrappySessionToolbarNew];
  } else {
    [toolbarSegmented_ setLabel:NSLocalizedString(@"New", nil)
                     forSegment:kStrappySessionToolbarNew];
  }

  [toolbarSegmented_ XP_setToolTip:NSLocalizedString(@"Close Chat", nil)
                         forSegment:kStrappySessionToolbarClose];
  [toolbarSegmented_ XP_setToolTip:NSLocalizedString(@"New Chat", nil)
                         forSegment:kStrappySessionToolbarNew];
  [toolbarSegmented_ sizeToFit];
}

- (void)updateToolbarSegments
{
  BOOL hasOpenSession;

  if (toolbarSegmented_ == nil) {
    return;
  }

  hasOpenSession = ([tableView_ selectedRow] >= 0) ? YES : NO;
  [toolbarSegmented_ setEnabled:hasOpenSession
                     forSegment:kStrappySessionToolbarClose];
  [toolbarSegmented_ setEnabled:(creatingSession_ ? NO : YES)
                     forSegment:kStrappySessionToolbarNew];
}

- (void)notifySelectedSession
{
  NSInteger row;
  NSDictionary *session;
  NSString *type;

  row = [tableView_ selectedRow];
  if ((row < 0) || (row >= (NSInteger)[rows_ count])) {
    if (delegate_ != nil) {
      [delegate_ sessionListViewController:self didSelectSession:nil];
    }
    [self updateToolbarSegments];
    return;
  }

  session = [rows_ objectAtIndex:(NSUInteger)row];
  type = [session objectForKey:kStrappySessionRowTypeKey];
  if (delegate_ != nil) {
    if ([type isEqualToString:kStrappySessionRowTypeSession]) {
      [delegate_ sessionListViewController:self
                          didSelectSession:[StrappySession sessionWithSummary:session]];
    }
  }
  [self updateToolbarSegments];
}

- (NSInteger)rowForSessionIdentifier:(NSNumber *)sessionIdentifier
{
  NSUInteger index;

  if (sessionIdentifier == nil) {
    return -1;
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

- (NSUInteger)firstSessionInsertIndexForRows:(NSArray *)rows
{
  NSUInteger index;

  for (index = 0U; index < [rows count]; index++) {
    NSDictionary *row = [rows objectAtIndex:index];
    if ([[row objectForKey:kStrappySessionRowTypeKey]
          isEqualToString:kStrappySessionRowTypeSection]) {
      return index + 1U;
    }
  }
  return [rows count];
}

- (void)reloadData
{
  NSError *error;
  NSArray *sessions;
  NSMutableArray *displayRows;
  NSInteger row;
  NSUInteger index;

  error = nil;
  sessions = [StrappySession sessionSummariesWithError:&error];
  if (sessions == nil) {
    sessions = [NSArray array];
  }

  displayRows = [NSMutableArray arrayWithCapacity:[sessions count] + 1U];
  [displayRows addObject:StrappySectionRow(NSLocalizedString(@"Conversations", nil))];
  if ([sessions count] == 0U) {
    [displayRows addObject:StrappyEmptySessionRow()];
  } else {
    for (index = 0U; index < [sessions count]; index++) {
      [displayRows addObject:StrappySessionDisplayRow([sessions objectAtIndex:index])];
    }
  }

  [rows_ release];
  rows_ = [displayRows copy];
  [tableView_ reloadData];

  row = [self rowForSessionIdentifier:selectedSessionId_];

  if (row >= 0) {
    suppressSelectionNotification_ = YES;
    [tableView_ selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row]
             byExtendingSelection:NO];
    suppressSelectionNotification_ = NO;
    [tableView_ scrollRowToVisible:row];
  } else {
    [tableView_ deselectAll:self];
    [self notifySelectedSession];
  }
  [self updateToolbarSegments];
}

- (void)reloadSessionIdentifier:(NSNumber *)sessionIdentifier select:(BOOL)select
{
  NSError *error;
  NSDictionary *summary;
  NSMutableArray *mutableRows;
  NSDictionary *displayRow;
  NSInteger row;
  NSUInteger index;
  NSUInteger insertIndex;

  if (select) {
    [selectedSessionId_ release];
    selectedSessionId_ = [sessionIdentifier retain];
  }

  if (sessionIdentifier == nil) {
    [self reloadData];
    return;
  }

  error = nil;
  summary = [StrappySession sessionSummaryForSessionIdentifier:sessionIdentifier
                                                        error:&error];
  if (summary == nil) {
    [self reloadData];
    return;
  }

  mutableRows = [[rows_ mutableCopy] autorelease];
  displayRow = StrappySessionDisplayRow(summary);
  row = [self rowForSessionIdentifier:sessionIdentifier];
  if (row >= 0) {
    [mutableRows replaceObjectAtIndex:(NSUInteger)row withObject:displayRow];
  } else {
    for (index = 0U; index < [mutableRows count]; index++) {
      NSDictionary *candidate = [mutableRows objectAtIndex:index];
      if ([[candidate objectForKey:kStrappySessionRowTypeKey]
            isEqualToString:kStrappySessionRowTypeEmpty]) {
        [mutableRows removeObjectAtIndex:index];
        break;
      }
    }

    insertIndex = [self firstSessionInsertIndexForRows:mutableRows];
    while (insertIndex < [mutableRows count]) {
      NSDictionary *candidate;
      NSNumber *candidateId;

      candidate = [mutableRows objectAtIndex:insertIndex];
      if (![[candidate objectForKey:kStrappySessionRowTypeKey]
             isEqualToString:kStrappySessionRowTypeSession]) {
        break;
      }
      candidateId = [candidate objectForKey:@"id"];
      if (![candidateId isKindOfClass:[NSNumber class]] ||
          ([candidateId longLongValue] < [sessionIdentifier longLongValue])) {
        break;
      }
      insertIndex++;
    }
    [mutableRows insertObject:displayRow atIndex:insertIndex];
  }

  [rows_ release];
  rows_ = [mutableRows copy];
  [tableView_ reloadData];
  [self selectSessionIdentifier:selectedSessionId_];
  [self updateToolbarSegments];
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
    suppressSelectionNotification_ = YES;
    [tableView_ selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row]
             byExtendingSelection:NO];
    suppressSelectionNotification_ = NO;
    [tableView_ scrollRowToVisible:row];
  } else {
    suppressSelectionNotification_ = YES;
    [tableView_ deselectAll:self];
    suppressSelectionNotification_ = NO;
  }
  [self updateToolbarSegments];
}

- (void)toolbarSegmentClicked:(id)sender
{
  NSInteger selectedSegment;

  selectedSegment = [(NSSegmentedControl *)sender selectedSegment];
  if (selectedSegment == kStrappySessionToolbarClose) {
    [self closeActiveSession:sender];
  } else if (selectedSegment == kStrappySessionToolbarNew) {
    [self addSession:sender];
  }
}

- (void)closeActiveSession:(id)sender
{
  (void)sender;
  if ([tableView_ selectedRow] < 0) {
    return;
  }

  [selectedSessionId_ release];
  selectedSessionId_ = nil;
  suppressSelectionNotification_ = YES;
  [tableView_ deselectAll:self];
  suppressSelectionNotification_ = NO;
  [self notifySelectedSession];
  [self updateToolbarSegments];
}

- (void)addSession:(id)sender
{
  NSError *error;
  StrappySession *session;
  NSNumber *identifier;

  (void)sender;
  if (creatingSession_) {
    return;
  }

  creatingSession_ = YES;
  [self updateToolbarSegments];

  error = nil;
  session = [StrappySession createSessionWithError:&error];

  creatingSession_ = NO;
  [self updateToolbarSegments];

  identifier = [session sessionIdentifier];
  if (![identifier isKindOfClass:[NSNumber class]]) {
    NSBeep();
    return;
  }

  [self reloadSessionIdentifier:identifier select:YES];
  [self notifySelectedSession];
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

- (CGFloat)tableView:(NSTableView *)tableView heightOfRow:(NSInteger)row
{
  NSDictionary *rowData;

  (void)tableView;
  if ((row < 0) || (row >= (NSInteger)[rows_ count])) {
    return kStrappySessionRowHeight;
  }

  rowData = [rows_ objectAtIndex:(NSUInteger)row];
  if ([[rowData objectForKey:kStrappySessionRowTypeKey]
        isEqualToString:kStrappySessionRowTypeSection]) {
    return kStrappySectionRowHeight;
  }
  return kStrappySessionRowHeight;
}

- (BOOL)tableView:(NSTableView *)tableView shouldSelectRow:(NSInteger)row
{
  NSDictionary *rowData;
  NSString *type;

  (void)tableView;
  if ((row < 0) || (row >= (NSInteger)[rows_ count])) {
    return NO;
  }

  rowData = [rows_ objectAtIndex:(NSUInteger)row];
  type = [rowData objectForKey:kStrappySessionRowTypeKey];
  return [type isEqualToString:kStrappySessionRowTypeSession] ? YES : NO;
}

- (void)tableViewSelectionDidChange:(NSNotification *)notification
{
  NSInteger row;
  NSDictionary *session;

  (void)notification;
  row = [tableView_ selectedRow];
  if (suppressSelectionNotification_) {
    return;
  }

  [selectedSessionId_ release];
  selectedSessionId_ = nil;
  if ((row >= 0) && (row < (NSInteger)[rows_ count])) {
    session = [rows_ objectAtIndex:(NSUInteger)row];
    if ([[session objectForKey:@"id"] isKindOfClass:[NSNumber class]]) {
      selectedSessionId_ = [[session objectForKey:@"id"] retain];
    }
  }
  [self notifySelectedSession];
}

- (NSMenu *)contextMenuForRow:(NSInteger)row
{
  NSDictionary *rowData;
  NSString *type;
  NSMenu *menu;
  NSMenuItem *item;

  if ((row < 0) || (row >= (NSInteger)[rows_ count])) {
    return nil;
  }

  rowData = [rows_ objectAtIndex:(NSUInteger)row];
  type = [rowData objectForKey:kStrappySessionRowTypeKey];
  menu = [[[NSMenu alloc] initWithTitle:@""] autorelease];

  if (![type isEqualToString:kStrappySessionRowTypeSession]) {
    return nil;
  }

  item = [[[NSMenuItem alloc] initWithTitle:NSLocalizedString(@"Copy Title", nil)
                                     action:@selector(contextCopyTitle:)
                              keyEquivalent:@""] autorelease];
  [item setTarget:self];
  [item setRepresentedObject:rowData];
  [menu addItem:item];

  item = [[[NSMenuItem alloc] initWithTitle:NSLocalizedString(@"Copy Last Message", nil)
                                     action:@selector(contextCopyLastMessage:)
                              keyEquivalent:@""] autorelease];
  [item setTarget:self];
  [item setRepresentedObject:rowData];
  [menu addItem:item];

  return menu;
}

- (void)copyStringToPasteboard:(NSString *)string
{
  NSPasteboard *pasteboard;

  if (![string isKindOfClass:[NSString class]]) {
    string = @"";
  }

  pasteboard = [NSPasteboard generalPasteboard];
  [pasteboard declareTypes:[NSArray arrayWithObject:StrappyPasteboardStringType]
                     owner:nil];
  [pasteboard setString:string forType:StrappyPasteboardStringType];
}

- (void)contextCopyTitle:(id)sender
{
  [self copyStringToPasteboard:
    StrappySessionPromptPreview([sender representedObject])];
}

- (void)contextCopyLastMessage:(id)sender
{
  NSDictionary *rowData;
  NSString *text;

  rowData = [sender representedObject];
  text = [rowData objectForKey:@"last_message_text"];
  [self copyStringToPasteboard:text];
}

- (void)dealloc
{
  [scrollView_ release];
  [tableView_ release];
  [toolbarView_ release];
  [toolbarSegmented_ release];
  [rows_ release];
  [selectedSessionId_ release];
  [super dealloc];
}

@end
