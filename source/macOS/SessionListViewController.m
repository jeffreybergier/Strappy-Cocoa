#import "SessionListViewController.h"
#import "AIFontAwesome.h"
#import "StrappyBottomToolbarView.h"
#import "StrappySession.h"
#import "XPFoundation.h"

static const CGFloat kStrappySessionRowHeight = 68.0;
static const CGFloat kStrappySectionRowHeight = 24.0;
static const CGFloat kStrappySessionToolbarHeight = 32.0;
static const CGFloat kStrappySessionToolbarPad = 4.0;
static const CGFloat kStrappyPadLeft = 8.0;
static const CGFloat kStrappyPadRight = 8.0;
static const CGFloat kStrappyTimestampTop = 8.0;
static const CGFloat kStrappyTitleLineHeight = 17.0;
static const CGFloat kStrappyTitleLines = 2.0;
static const CGFloat kStrappyTimestampGap = 4.0;
static const CGFloat kStrappyTimestampHeight = 14.0;
static const CGFloat kStrappyPromptIconCanvasSize = 14.0;
static const CGFloat kStrappyPromptIconSize = 10.0;
static const CGFloat kStrappyPromptIconGap = 4.0;
static const AIFontAwesomeIcon kStrappySessionToolbarPlusIcon =
  (AIFontAwesomeIcon)0xF067;
static const AIFontAwesomeIcon kStrappySessionToolbarMinusIcon =
  AIFAMinus;
static const AIFontAwesomeIcon kStrappySessionPromptActiveIcon =
  AIFAArrowsRotate;

enum {
  kStrappySessionToolbarClose = 0,
  kStrappySessionToolbarDelete = 1,
  kStrappySessionToolbarNew = 2
};

static NSString * const kStrappySessionRowTypeKey = @"row_type";
static NSString * const kStrappySessionRowTypeSection = @"section";
static NSString * const kStrappySessionRowTypeSession = @"session";
static NSString * const kStrappySessionRowTypeEmpty = @"empty";
static NSString * const kStrappySessionPromptInFlightKey =
  @"prompt_in_flight";

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
    NSLocalizedString(@"No conversations yet", nil), @"name",
    NSLocalizedString(@"Create a conversation to begin.", nil), @"last_message_text",
    nil];
}

static NSDictionary *StrappySessionDisplayRow(NSDictionary *session)
{
  NSMutableDictionary *row;
  NSNumber *sessionIdentifier;
  BOOL inFlight;

  row = [NSMutableDictionary dictionaryWithDictionary:session];
  [row setObject:kStrappySessionRowTypeSession forKey:kStrappySessionRowTypeKey];
  sessionIdentifier = [session objectForKey:@"id"];
  inFlight =
    [StrappySession isPromptInFlightForSessionIdentifier:sessionIdentifier];
  [row setObject:[NSNumber numberWithBool:inFlight]
          forKey:kStrappySessionPromptInFlightKey];
  return row;
}

static NSString *StrappySessionPromptPreview(NSDictionary *session)
{
  NSString *name;

  name = [session objectForKey:@"name"];
  if ([name isKindOfClass:[NSString class]] && ([name length] > 0U)) {
    return name;
  }
  return NSLocalizedString(@"Untitled Session", nil);
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

static BOOL StrappyRowIsPromptInFlight(NSDictionary *row)
{
  NSNumber *inFlight;

  inFlight = [row objectForKey:kStrappySessionPromptInFlightKey];
  return ([inFlight isKindOfClass:[NSNumber class]] &&
          [inFlight boolValue]) ? YES : NO;
}

static CGFloat StrappyBackingScaleForView(NSView *view)
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

static void StrappyDrawTintedImage(NSImage *image,
                                   NSRect rect,
                                   NSColor *color)
{
  NSImage *tintedImage;
  NSRect imageRect;

  if ((image == nil) || (color == nil)) {
    return;
  }

  imageRect = NSMakeRect(0.0, 0.0, [image size].width, [image size].height);
  tintedImage = [[[NSImage alloc] initWithSize:[image size]] autorelease];
  [tintedImage lockFocus];
  [image drawAtPoint:NSZeroPoint
            fromRect:imageRect
           operation:XPCompositingOperationSourceOver
            fraction:1.0];
  [color set];
  NSRectFillUsingOperation(imageRect, XPCompositingOperationSourceIn);
  [tintedImage unlockFocus];

  [tintedImage drawInRect:rect
                 fromRect:imageRect
                operation:XPCompositingOperationSourceOver
                 fraction:1.0];
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

- (void)drawSessionWithFrame:(NSRect)frame
                         row:(NSDictionary *)row
                        view:(NSView *)view
{
  NSString *title;
  NSString *timestamp;
  NSString *modelName;
  NSString *type;
  BOOL selected;
  BOOL promptInFlight;
  CGFloat scale;
  CGFloat titleHeight;
  CGFloat titleY;
  CGFloat timestampWidth;
  NSRect titleRect;
  NSRect timestampRect;
  NSRect promptIconRect;
  NSDictionary *titleAttrs;
  NSDictionary *metaAttrs;

  selected = [self isHighlighted];
  type = [row objectForKey:kStrappySessionRowTypeKey];
  promptInFlight =
    ([type isEqualToString:kStrappySessionRowTypeSession] &&
     StrappyRowIsPromptInFlight(row)) ? YES : NO;
  scale = StrappyBackingScaleForView(view);

  title = StrappySessionPromptPreview(row);
  timestamp = @"";
  if ([type isEqualToString:kStrappySessionRowTypeSession]) {
    timestamp = StrappyDisplayTimestamp([row objectForKey:@"last_message_at"]);
    modelName = [row objectForKey:@"model_name"];
    if (![modelName isKindOfClass:[NSString class]]) {
      modelName = @"";
    }
    if (([timestamp length] > 0U) && ([modelName length] > 0U)) {
      timestamp = [NSString stringWithFormat:@"%@, %@", timestamp, modelName];
    } else if ([timestamp length] == 0U) {
      timestamp = modelName;
    }
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
    timestampWidth = NSWidth(frame) -
      (kStrappyPadLeft + kStrappyPadRight +
       kStrappyPromptIconCanvasSize + kStrappyPromptIconGap);
    if (timestampWidth < 0.0) {
      timestampWidth = 0.0;
    }
    timestampRect = NSMakeRect(NSMinX(frame) + kStrappyPadLeft,
                               NSMinY(frame) + kStrappyTimestampTop,
                               timestampWidth,
                               kStrappyTimestampHeight);
    [timestamp drawInRect:timestampRect withAttributes:metaAttrs];

    titleY += kStrappyTimestampHeight + kStrappyTimestampGap;
  }
  if (promptInFlight) {
    NSImage *image;

    promptIconRect =
      NSMakeRect(NSMaxX(frame) - kStrappyPadRight -
                   kStrappyPromptIconCanvasSize,
                 NSMinY(frame) + kStrappyTimestampTop +
                   ((kStrappyTimestampHeight -
                     kStrappyPromptIconCanvasSize) / 2.0),
                 kStrappyPromptIconCanvasSize,
                 kStrappyPromptIconCanvasSize);
    image = [AIFontAwesome imageForIcon:kStrappySessionPromptActiveIcon
                                  style:AIFontAwesomeStyleSolid
                               iconSize:kStrappyPromptIconSize
                             canvasSize:kStrappyPromptIconCanvasSize
                                  scale:scale];
    StrappyDrawTintedImage(image,
                           promptIconRect,
                           StrappySecondaryTextColor(selected));
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

  [self drawSessionWithFrame:frame row:row view:view];
}

@end

@interface SessionListViewController ()
- (void)rebuildToolbarSegmentIcons;
- (void)updateToolbarSegments;
- (NSDictionary *)selectedSessionRow;
- (NSInteger)rowForSessionIdentifier:(NSNumber *)sessionIdentifier;
- (void)sessionPromptActivityDidChange:(NSNotification *)notification;
- (void)modelCatalogDidChange:(NSNotification *)notification;
- (void)toolbarDidMoveToWindow:(id)sender;
- (void)toolbarSegmentClicked:(id)sender;
- (void)layoutSidebarViews;
- (void)deleteChatAlertDidEnd:(NSAlert *)alert
                    returnCode:(NSInteger)returnCode
                   contextInfo:(void *)contextInfo;
- (void)deleteSessionIdentifier:(NSNumber *)sessionIdentifier;
- (void)showDeleteError:(NSError *)error;
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
  [toolbarSegmented_ setSegmentCount:3];
  [[toolbarSegmented_ cell] setTrackingMode:NSSegmentSwitchTrackingMomentary];
  [toolbarSegmented_ XP_setToolbarSegmentStyle];
  [toolbarSegmented_ setTarget:self];
  [toolbarSegmented_ setAction:@selector(toolbarSegmentClicked:)];
  [toolbarSegmented_ setAutoresizingMask:NSViewMinXMargin | NSViewMaxYMargin];
  [self rebuildToolbarSegmentIcons];
  [toolbarView_ addSubview:toolbarSegmented_];

  [self reloadData];
  [tableView_ sizeLastColumnToFit];
  [self layoutSidebarViews];

  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(sessionPromptActivityDidChange:)
           name:StrappySessionPromptDidStartNotification
         object:nil];
  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(sessionPromptActivityDidChange:)
           name:StrappySessionPromptDidFinishNotification
         object:nil];
  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(modelCatalogDidChange:)
           name:StrappySessionModelCatalogDidChangeNotification
         object:nil];
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
  NSImage *deleteImage;
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
  deleteImage = [AIFontAwesome imageForIcon:kStrappySessionToolbarMinusIcon
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

  if (deleteImage != nil) {
    [toolbarSegmented_ setImage:deleteImage
                     forSegment:kStrappySessionToolbarDelete];
    [toolbarSegmented_ setLabel:@""
                     forSegment:kStrappySessionToolbarDelete];
  } else {
    [toolbarSegmented_ setLabel:NSLocalizedString(@"Delete", nil)
                     forSegment:kStrappySessionToolbarDelete];
  }

  [toolbarSegmented_ XP_setToolTip:NSLocalizedString(@"Close Chat", nil)
                         forSegment:kStrappySessionToolbarClose];
  [toolbarSegmented_ XP_setToolTip:NSLocalizedString(@"Delete Chat", nil)
                         forSegment:kStrappySessionToolbarDelete];
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

  hasOpenSession = [self canCloseActiveSession];
  [toolbarSegmented_ setEnabled:hasOpenSession
                     forSegment:kStrappySessionToolbarClose];
  [toolbarSegmented_ setEnabled:[self canDeleteActiveSession]
                     forSegment:kStrappySessionToolbarDelete];
  [toolbarSegmented_ setEnabled:(creatingSession_ ? NO : YES)
                     forSegment:kStrappySessionToolbarNew];
}

- (void)sessionPromptActivityDidChange:(NSNotification *)notification
{
  StrappySession *session;
  NSNumber *identifier;
  NSDictionary *summary;

  session = [notification object];
  identifier = nil;
  if ([session isKindOfClass:[StrappySession class]]) {
    identifier = [session sessionIdentifier];
  }
  if (![identifier isKindOfClass:[NSNumber class]]) {
    summary = [[notification userInfo] objectForKey:@"session"];
    if ([summary isKindOfClass:[NSDictionary class]]) {
      identifier = [summary objectForKey:@"id"];
    }
  }
  if (![identifier isKindOfClass:[NSNumber class]]) {
    return;
  }

  {
    NSInteger row;
    NSMutableArray *mutableRows;
    NSMutableDictionary *displayRow;

    row = [self rowForSessionIdentifier:identifier];
    if (row < 0) {
      return;
    }
    mutableRows = [[rows_ mutableCopy] autorelease];
    displayRow = [NSMutableDictionary dictionaryWithDictionary:
      [mutableRows objectAtIndex:(NSUInteger)row]];
    [displayRow setObject:[NSNumber numberWithBool:
      [StrappySession isPromptInFlightForSessionIdentifier:identifier]]
                   forKey:kStrappySessionPromptInFlightKey];
    [mutableRows replaceObjectAtIndex:(NSUInteger)row withObject:displayRow];
    [rows_ release];
    rows_ = [mutableRows copy];
    [tableView_ reloadData];
    [self updateToolbarSegments];
  }
}

- (void)modelCatalogDidChange:(NSNotification *)notification
{
  (void)notification;
  [self reloadData];
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
  NSMutableArray *summaries;
  NSMutableArray *displayRows;
  NSArray *sortDescriptors;
  NSUInteger index;
  BOOL replaced;

  if (select) {
    [selectedSessionId_ release];
    selectedSessionId_ = [sessionIdentifier retain];
  }

  if (sessionIdentifier == nil) {
    [self reloadData];
    return;
  }

  error = nil;
  summary =
    [StrappySession sessionListSummaryForSessionIdentifier:sessionIdentifier
                                                     error:&error];
  if (summary == nil) {
    [self reloadData];
    return;
  }

  summaries = [NSMutableArray array];
  replaced = NO;
  for (index = 0U; index < [rows_ count]; index++) {
    NSDictionary *candidate;
    NSNumber *candidateId;

    candidate = [rows_ objectAtIndex:index];
    if (![[candidate objectForKey:kStrappySessionRowTypeKey]
           isEqualToString:kStrappySessionRowTypeSession]) {
      continue;
    }
    candidateId = [candidate objectForKey:@"id"];
    if ([candidateId isEqualToNumber:sessionIdentifier]) {
      [summaries addObject:summary];
      replaced = YES;
    } else {
      [summaries addObject:candidate];
    }
  }
  if (!replaced) {
    [summaries addObject:summary];
  }
  sortDescriptors = [NSArray arrayWithObjects:
    [[[NSSortDescriptor alloc] initWithKey:@"last_activity_at_ms"
                                 ascending:NO] autorelease],
    [[[NSSortDescriptor alloc] initWithKey:@"id"
                                 ascending:NO] autorelease],
    nil];
  [summaries sortUsingDescriptors:sortDescriptors];

  displayRows = [NSMutableArray arrayWithCapacity:[summaries count] + 1U];
  [displayRows addObject:
    StrappySectionRow(NSLocalizedString(@"Conversations", nil))];
  for (index = 0U; index < [summaries count]; index++) {
    [displayRows addObject:
      StrappySessionDisplayRow([summaries objectAtIndex:index])];
  }

  [rows_ release];
  rows_ = [displayRows copy];
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
  } else if (selectedSegment == kStrappySessionToolbarDelete) {
    [self deleteActiveSession:sender];
  } else if (selectedSegment == kStrappySessionToolbarNew) {
    [self addSession:sender];
  }
}

- (NSDictionary *)selectedSessionRow
{
  NSInteger row;
  NSDictionary *rowData;
  NSString *type;

  row = [tableView_ selectedRow];
  if ((row < 0) || (row >= (NSInteger)[rows_ count])) {
    return nil;
  }

  rowData = [rows_ objectAtIndex:(NSUInteger)row];
  type = [rowData objectForKey:kStrappySessionRowTypeKey];
  return [type isEqualToString:kStrappySessionRowTypeSession] ? rowData : nil;
}

- (BOOL)canCloseActiveSession
{
  return ([self selectedSessionRow] != nil) ? YES : NO;
}

- (void)closeActiveSession:(id)sender
{
  (void)sender;
  if (![self canCloseActiveSession]) {
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

- (BOOL)canDeleteActiveSession
{
  NSDictionary *rowData;
  NSNumber *sessionIdentifier;

  rowData = [self selectedSessionRow];
  if (rowData == nil) {
    return NO;
  }

  sessionIdentifier = [rowData objectForKey:@"id"];
  if (![sessionIdentifier isKindOfClass:[NSNumber class]]) {
    return NO;
  }

  if (StrappyRowIsPromptInFlight(rowData) ||
      [StrappySession isPromptInFlightForSessionIdentifier:sessionIdentifier]) {
    return NO;
  }

  return YES;
}

- (void)deleteActiveSession:(id)sender
{
  NSDictionary *rowData;
  NSNumber *sessionIdentifier;
  NSString *title;
  NSString *message;
  NSWindow *window;
  NSAlert *alert;

  (void)sender;
  if (![self canDeleteActiveSession]) {
    NSBeep();
    return;
  }

  rowData = [self selectedSessionRow];
  sessionIdentifier = [rowData objectForKey:@"id"];
  title = StrappySessionPromptPreview(rowData);
  message = [NSString stringWithFormat:
    NSLocalizedString(@"This will permanently delete \"%@\" and all of its messages. This cannot be undone.", nil),
    title];

  window = [[self view] window];
  if (window == nil) {
    NSBeep();
    return;
  }

  alert = [[[NSAlert alloc] init] autorelease];
  [alert setMessageText:NSLocalizedString(@"Delete Chat?", nil)];
  [alert setInformativeText:message];
  [alert addButtonWithTitle:NSLocalizedString(@"Delete", nil)];
  [alert addButtonWithTitle:NSLocalizedString(@"Cancel", nil)];
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 101000
  [alert setAlertStyle:NSAlertStyleWarning];
#else
  [alert setAlertStyle:NSWarningAlertStyle];
#endif
  [alert XP_beginSheetModalForWindow:window
                       modalDelegate:self
                      didEndSelector:@selector(deleteChatAlertDidEnd:returnCode:contextInfo:)
                         contextInfo:[sessionIdentifier retain]];
}

- (void)deleteChatAlertDidEnd:(NSAlert *)alert
                    returnCode:(NSInteger)returnCode
                   contextInfo:(void *)contextInfo
{
  NSNumber *sessionIdentifier;

  (void)alert;
  sessionIdentifier = (NSNumber *)contextInfo;
  if (returnCode == NSAlertFirstButtonReturn) {
    [self deleteSessionIdentifier:sessionIdentifier];
  }
  [sessionIdentifier release];
}

- (void)deleteSessionIdentifier:(NSNumber *)sessionIdentifier
{
  NSError *error;

  if (![sessionIdentifier isKindOfClass:[NSNumber class]]) {
    NSBeep();
    return;
  }
  if ([StrappySession isPromptInFlightForSessionIdentifier:sessionIdentifier]) {
    NSBeep();
    return;
  }

  error = nil;
  if (![StrappySession deleteSessionWithIdentifier:sessionIdentifier
                                             error:&error]) {
    [self showDeleteError:error];
    return;
  }

  if ([selectedSessionId_ isEqualToNumber:sessionIdentifier]) {
    [selectedSessionId_ release];
    selectedSessionId_ = nil;
  }
  [self reloadData];
}

- (void)showDeleteError:(NSError *)error
{
  NSString *message;
  NSAlert *alert;
  NSWindow *window;

  message = [error localizedDescription];
  if ([message length] == 0U) {
    message = NSLocalizedString(@"The chat could not be deleted.", nil);
  }

  alert = [[[NSAlert alloc] init] autorelease];
  [alert setMessageText:NSLocalizedString(@"Could Not Delete Chat", nil)];
  [alert setInformativeText:message];
  window = [[self view] window];
  if (window != nil) {
    [alert XP_beginSheetModalForWindow:window
                         modalDelegate:nil
                        didEndSelector:NULL
                           contextInfo:NULL];
  } else {
    [alert runModal];
  }
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
  NSNumber *identifier;
  StrappySession *session;
  NSArray *messages;
  NSDictionary *message;
  NSString *text;

  rowData = [sender representedObject];
  identifier = [rowData objectForKey:@"id"];
  session = [StrappySession sessionWithIdentifier:identifier];
  messages = [session messagesWithError:nil];
  message = ([messages count] > 0U) ? [messages lastObject] : nil;
  text = [message objectForKey:@"text"];
  [self copyStringToPasteboard:text];
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [scrollView_ release];
  [tableView_ release];
  [toolbarView_ release];
  [toolbarSegmented_ release];
  [rows_ release];
  [selectedSessionId_ release];
  [super dealloc];
}

@end
