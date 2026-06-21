#import "SessionListViewController.h"
#import "AIFontAwesome.h"
#import "StrappySession.h"
#import "XPFoundation.h"

static const CGFloat kStrappySessionRowHeight = 58.0;
static const CGFloat kStrappySectionRowHeight = 24.0;
static const CGFloat kStrappySessionToolbarHeight = 32.0;
static const CGFloat kStrappySessionToolbarPad = 4.0;
static const CGFloat kStrappyAvatarSize = 34.0;
static const CGFloat kStrappyPadLeft = 8.0;
static const CGFloat kStrappyPadRight = 8.0;
static const CGFloat kStrappyTextGap = 8.0;

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
  NSString *prompt;

  prompt = [session objectForKey:@"prompt"];
  if (![prompt isKindOfClass:[NSString class]] || ([prompt length] == 0U)) {
    return NSLocalizedString(@"Untitled Session", nil);
  }

  return prompt;
}

static NSString *StrappyRolePrefix(NSString *role)
{
  if ([role isEqualToString:@"assistant"]) {
    return NSLocalizedString(@"Agent", nil);
  }
  return NSLocalizedString(@"You", nil);
}

static NSString *StrappyPreviewText(NSDictionary *row)
{
  NSString *type;
  NSString *text;
  NSString *role;

  type = [row objectForKey:kStrappySessionRowTypeKey];
  text = [row objectForKey:@"last_message_text"];
  if (![text isKindOfClass:[NSString class]]) {
    text = @"";
  }
  if ([type isEqualToString:kStrappySessionRowTypeSession]) {
    role = [row objectForKey:@"last_message_role"];
    return [NSString stringWithFormat:@"%@: %@",
      StrappyRolePrefix(role),
      ([text length] ? text : NSLocalizedString(@"No messages", nil))];
  }
  return text;
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

@interface StrappySessionToolbarView : NSView
@end

@implementation StrappySessionToolbarView

- (BOOL)isOpaque
{
  return YES;
}

- (void)drawRect:(NSRect)dirtyRect
{
  NSRect bounds;

  (void)dirtyRect;
  bounds = [self bounds];

  [XPColorWindowFrame set];
  NSRectFill(bounds);

  [XPColorControlHighlight set];
  NSRectFill(NSMakeRect(bounds.origin.x,
                        bounds.origin.y + bounds.size.height - 1.0,
                        bounds.size.width,
                        1.0));
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

- (void)drawAvatarInRect:(NSRect)avatarRect
                     row:(NSDictionary *)row
                selected:(BOOL)selected
{
  NSString *state;
  NSColor *fillColor;
  NSDictionary *attrs;
  NSSize glyphSize;
  NSRect glyphRect;

  state = [row objectForKey:@"state"];
  fillColor = selected ? [NSColor alternateSelectedControlTextColor]
                       : [NSColor colorWithCalibratedWhite:0.72 alpha:1.0];

  [fillColor set];
  [[NSBezierPath bezierPathWithOvalInRect:avatarRect] fill];

  attrs = [NSDictionary dictionaryWithObjectsAndKeys:
    (selected ? [NSColor selectedControlColor] : [NSColor whiteColor]),
      NSForegroundColorAttributeName,
    [NSFont boldSystemFontOfSize:11.0],
      NSFontAttributeName,
    nil];
  glyphSize = [@"AI" sizeWithAttributes:attrs];
  glyphRect = NSMakeRect(NSMidX(avatarRect) - (glyphSize.width / 2.0),
                         NSMidY(avatarRect) - (glyphSize.height / 2.0),
                         glyphSize.width,
                         glyphSize.height);
  [@"AI" drawInRect:glyphRect withAttributes:attrs];

  if ([state isEqualToString:@"error"]) {
    NSRect dotRect;

    dotRect = NSMakeRect(NSMaxX(avatarRect) - 8.0,
                         NSMinY(avatarRect) + 1.0,
                         9.0,
                         9.0);
    [[NSColor colorWithCalibratedRed:0.82 green:0.12 blue:0.10 alpha:1.0] set];
    [[NSBezierPath bezierPathWithOvalInRect:dotRect] fill];
  }
}

- (void)drawSessionWithFrame:(NSRect)frame row:(NSDictionary *)row
{
  NSString *title;
  NSString *preview;
  NSString *timestamp;
  NSString *type;
  NSNumber *messageCount;
  BOOL selected;
  BOOL isEmpty;
  NSRect avatarRect;
  CGFloat textX;
  CGFloat textWidth;
  CGFloat timestampWidth;
  NSDictionary *titleAttrs;
  NSDictionary *previewAttrs;
  NSDictionary *metaAttrs;

  selected = [self isHighlighted];
  type = [row objectForKey:kStrappySessionRowTypeKey];
  isEmpty = [type isEqualToString:kStrappySessionRowTypeEmpty];

  avatarRect = NSMakeRect(NSMinX(frame) + kStrappyPadLeft,
                          NSMinY(frame) + floor((NSHeight(frame) - kStrappyAvatarSize) / 2.0),
                          kStrappyAvatarSize,
                          kStrappyAvatarSize);
  if (!isEmpty) {
    [self drawAvatarInRect:avatarRect row:row selected:selected];
  }

  textX = isEmpty ? (NSMinX(frame) + kStrappyPadLeft)
                  : (NSMaxX(avatarRect) + kStrappyTextGap);
  timestampWidth = 78.0;
  textWidth = NSMaxX(frame) - textX - kStrappyPadRight - timestampWidth;
  if (textWidth < 40.0) {
    textWidth = NSMaxX(frame) - textX - kStrappyPadRight;
    timestampWidth = 0.0;
  }

  title = StrappySessionPromptPreview(row);
  preview = StrappyPreviewText(row);
  timestamp = @"";
  if ([type isEqualToString:kStrappySessionRowTypeSession]) {
    timestamp = StrappyDisplayTimestamp([row objectForKey:@"last_message_at"]);
  }

  titleAttrs = StrappyTextAttributes([NSFont boldSystemFontOfSize:13.0],
                                     StrappySelectedTextColor(selected),
                                     NSLineBreakByTruncatingTail);
  previewAttrs = StrappyTextAttributes([NSFont systemFontOfSize:11.0],
                                       StrappySecondaryTextColor(selected),
                                       NSLineBreakByTruncatingTail);
  metaAttrs = StrappyTextAttributes([NSFont systemFontOfSize:10.0],
                                    StrappySecondaryTextColor(selected),
                                    NSLineBreakByTruncatingTail);

  [title drawInRect:NSMakeRect(textX,
                               NSMinY(frame) + 9.0,
                               textWidth,
                               17.0)
     withAttributes:titleAttrs];
  [preview drawInRect:NSMakeRect(textX,
                                 NSMinY(frame) + 29.0,
                                 NSMaxX(frame) - textX - kStrappyPadRight,
                                 15.0)
       withAttributes:previewAttrs];

  if ((timestampWidth > 0.0) && ([timestamp length] > 0U)) {
    [timestamp drawInRect:NSMakeRect(NSMaxX(frame) - kStrappyPadRight - timestampWidth,
                                     NSMinY(frame) + 10.0,
                                     timestampWidth,
                                     14.0)
           withAttributes:metaAttrs];
  }

  messageCount = [row objectForKey:@"message_count"];
  if ([messageCount isKindOfClass:[NSNumber class]] &&
      ([messageCount XP_unsignedIntegerValue] > 0UL)) {
    NSString *countText;

    countText = [NSString stringWithFormat:NSLocalizedString(@"%@ msgs", nil),
      messageCount];
    [countText drawInRect:NSMakeRect(NSMaxX(frame) - kStrappyPadRight - timestampWidth,
                                     NSMinY(frame) + 31.0,
                                     timestampWidth,
                                     13.0)
           withAttributes:metaAttrs];
  }
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
- (void)rebuildAddSegmentIcon;
- (void)addSessionSegmentClicked:(id)sender;
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
  StrappySessionToolbarView *toolbar;

  [super viewDidLoad];

  scrollView_ = [[NSScrollView alloc] initWithFrame:[[self view] bounds]];
  [scrollView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [scrollView_ setHasVerticalScroller:YES];
  [scrollView_ setHasHorizontalScroller:NO];
  [scrollView_ setAutohidesScrollers:YES];
  [scrollView_ setBorderType:NSNoBorder];

  tableView_ = [[StrappySessionTableView alloc] initWithFrame:[[self view] bounds]];
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

  toolbar = [[StrappySessionToolbarView alloc] initWithFrame:NSZeroRect];
  toolbarView_ = toolbar;
  [toolbarView_ setAutoresizingMask:NSViewWidthSizable | NSViewMaxYMargin];
  [[self view] addSubview:toolbarView_];

  addSegmented_ = [[NSSegmentedControl alloc] initWithFrame:NSZeroRect];
  [addSegmented_ setSegmentCount:1];
  [[addSegmented_ cell] setTrackingMode:NSSegmentSwitchTrackingMomentary];
  if ([[addSegmented_ cell] respondsToSelector:@selector(setSegmentStyle:)]) {
    [[addSegmented_ cell] setSegmentStyle:NSSegmentStyleTexturedRounded];
  }
  [addSegmented_ setTarget:self];
  [addSegmented_ setAction:@selector(addSessionSegmentClicked:)];
  [addSegmented_ setToolTip:NSLocalizedString(@"New Session", nil)];
  [self rebuildAddSegmentIcon];
  [addSegmented_ sizeToFit];
  [toolbarView_ addSubview:addSegmented_];

  [self reloadData];
  [tableView_ sizeLastColumnToFit];
  [self layoutSidebarViews];
}

- (void)viewDidLayout
{
  [self layoutSidebarViews];
  [super viewDidLayout];
}

- (void)layoutSidebarViews
{
  NSRect bounds;
  CGFloat scrollHeight;
  CGFloat segmentWidth;
  CGFloat segmentHeight;

  bounds = [[self view] bounds];
  scrollHeight = bounds.size.height - kStrappySessionToolbarHeight;
  if (scrollHeight < 0.0) {
    scrollHeight = 0.0;
  }

  [toolbarView_ setFrame:NSMakeRect(0.0,
                                    0.0,
                                    bounds.size.width,
                                    kStrappySessionToolbarHeight)];
  [scrollView_ setFrame:NSMakeRect(0.0,
                                   kStrappySessionToolbarHeight,
                                   bounds.size.width,
                                   scrollHeight)];

  segmentWidth = [addSegmented_ frame].size.width;
  segmentHeight = [addSegmented_ frame].size.height;
  if (segmentWidth < 28.0) {
    segmentWidth = 28.0;
  }
  [addSegmented_ setFrame:NSMakeRect(kStrappySessionToolbarPad,
                                     kStrappySessionToolbarPad,
                                     segmentWidth,
                                     segmentHeight)];
}

- (void)rebuildAddSegmentIcon
{
  CGFloat scale;
  NSImage *addImage;

  if (addSegmented_ == nil) {
    return;
  }

  scale = 1.0;
  if ([[toolbarView_ window] respondsToSelector:@selector(XP_backingScaleFactor)]) {
    scale = [[toolbarView_ window] XP_backingScaleFactor];
  }
  if (scale < 1.0) {
    scale = 1.0;
  }

  addImage = [AIFontAwesome imageForIcon:AIFACirclePlus
                                   style:AIFontAwesomeStyleSolid
                                iconSize:12.0
                              canvasSize:18.0
                                   scale:scale];
  if (addImage != nil) {
    [addSegmented_ setImage:addImage forSegment:0];
    [addSegmented_ setLabel:@"" forSegment:0];
  } else {
    [addSegmented_ setLabel:@"+" forSegment:0];
  }
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
    return;
  }

  session = [rows_ objectAtIndex:(NSUInteger)row];
  type = [session objectForKey:kStrappySessionRowTypeKey];
  if (delegate_ != nil) {
    if ([type isEqualToString:kStrappySessionRowTypeSession]) {
      [delegate_ sessionListViewController:self didSelectSession:session];
    }
  }
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
}

- (void)addSessionSegmentClicked:(id)sender
{
  (void)sender;
  [self addSession:sender];
}

- (void)addSession:(id)sender
{
  NSError *error;
  NSDictionary *session;
  NSNumber *identifier;

  (void)sender;
  if (creatingSession_) {
    return;
  }

  creatingSession_ = YES;
  [addSegmented_ setEnabled:NO];

  error = nil;
  session = [StrappySession createSessionWithError:&error];

  creatingSession_ = NO;
  [addSegmented_ setEnabled:YES];

  identifier = [session objectForKey:@"id"];
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
  [addSegmented_ release];
  [rows_ release];
  [selectedSessionId_ release];
  [super dealloc];
}

@end
