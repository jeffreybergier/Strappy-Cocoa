#import "StrappyPreferencesWhitelistView.h"

#import "AIFontAwesome.h"
#import "XPAppKit.h"

static const CGFloat kStrappyPreferencesInset = 12.0;
static const CGFloat kStrappyWhitelistControlHeight = 24.0;
static const CGFloat kStrappyWhitelistBottomGap = 8.0;
static const CGFloat kStrappyWhitelistTopControlGap = 8.0;
static const CGFloat kStrappyWhitelistRefreshButtonWidth = 28.0;
static const CGFloat kStrappyWhitelistProgressSize = 20.0;

@interface StrappyPreferencesWhitelistTableView : NSTableView
@end

@implementation StrappyPreferencesWhitelistTableView

- (void)keyDown:(NSEvent *)event
{
  NSString *characters;
  id delegate;
  SEL selector;

  characters = [event charactersIgnoringModifiers];
  if (([characters length] == 1U) &&
      ([characters characterAtIndex:0] == ' ') &&
      ([[self selectedRowIndexes] count] > 0U)) {
    delegate = [self delegate];
    selector = @selector(whitelistTableViewDidPressSpace:);
    if ([delegate respondsToSelector:selector]) {
      [delegate performSelector:selector withObject:self];
      return;
    }
  }

  [super keyDown:event];
}

@end

static NSSortDescriptor *StrappyWhitelistSortDescriptorWithKey(NSString *key,
                                                               BOOL ascending)
{
  return [[[NSSortDescriptor alloc] initWithKey:key
                                      ascending:ascending] autorelease];
}

static NSSortDescriptor *StrappyWhitelistSortDescriptorForKey(NSArray *descriptors,
                                                              NSString *key)
{
  NSUInteger index;

  if ([key length] == 0U) {
    return nil;
  }

  for (index = 0U; index < [descriptors count]; index++) {
    NSSortDescriptor *descriptor;

    descriptor = [descriptors objectAtIndex:index];
    if (![descriptor isKindOfClass:[NSSortDescriptor class]]) {
      continue;
    }
    if ([[descriptor key] isEqualToString:key]) {
      return descriptor;
    }
  }

  return nil;
}

static BOOL StrappyWhitelistSortDescriptorListHasKey(NSArray *descriptors,
                                                     NSString *key)
{
  return (StrappyWhitelistSortDescriptorForKey(descriptors, key) != nil) ?
    YES : NO;
}

static void StrappyWhitelistAddSortDescriptorIfMissing(
  NSMutableArray *descriptors,
  NSSortDescriptor *descriptor)
{
  if (![descriptor isKindOfClass:[NSSortDescriptor class]] ||
      ([[descriptor key] length] == 0U) ||
      StrappyWhitelistSortDescriptorListHasKey(descriptors, [descriptor key])) {
    return;
  }

  [descriptors addObject:StrappyWhitelistSortDescriptorWithKey(
    [descriptor key],
    [descriptor ascending])];
}

static NSSortDescriptor *StrappyWhitelistPrimarySortDescriptor(
  StrappyPreferencesWhitelistView *view,
  NSArray *descriptors,
  NSString *requiredKey)
{
  NSUInteger index;

  for (index = 0U; index < [descriptors count]; index++) {
    NSSortDescriptor *descriptor;
    NSString *key;

    descriptor = [descriptors objectAtIndex:index];
    if (![descriptor isKindOfClass:[NSSortDescriptor class]]) {
      continue;
    }

    key = [descriptor key];
    if ([view sortKeyIsKnown:key] && ![key isEqualToString:requiredKey]) {
      return descriptor;
    }
  }

  return nil;
}

typedef struct {
  StrappyPreferencesWhitelistView *view;
  NSArray *sortDescriptors;
  NSString *stableSortKey;
} StrappyWhitelistSortContext;

static NSComparisonResult StrappyWhitelistCompareRows(id leftObject,
                                                      id rightObject,
                                                      void *context)
{
  StrappyWhitelistSortContext *sortContext;
  StrappyPreferencesWhitelistView *view;
  NSArray *sortDescriptors;
  NSString *stableSortKey;
  NSUInteger index;

  sortContext = (StrappyWhitelistSortContext *)context;
  if ((sortContext == NULL) || (sortContext->view == nil)) {
    return NSOrderedSame;
  }

  view = sortContext->view;
  if (![leftObject isKindOfClass:[NSDictionary class]] ||
      ![rightObject isKindOfClass:[NSDictionary class]]) {
    return NSOrderedSame;
  }

  sortDescriptors = sortContext->sortDescriptors;
  for (index = 0U; index < [sortDescriptors count]; index++) {
    NSSortDescriptor *sortDescriptor;
    NSComparisonResult result;

    sortDescriptor = [sortDescriptors objectAtIndex:index];
    if (![sortDescriptor isKindOfClass:[NSSortDescriptor class]]) {
      continue;
    }

    result = [view compareRow:leftObject
                          row:rightObject
                   forSortKey:[sortDescriptor key]];
    if (result != NSOrderedSame) {
      return [sortDescriptor ascending] ? result : -result;
    }
  }

  stableSortKey = sortContext->stableSortKey;
  if ([stableSortKey length] > 0U) {
    return [view compareRow:leftObject
                        row:rightObject
                 forSortKey:stableSortKey];
  }

  return NSOrderedSame;
}

@interface StrappyPreferencesWhitelistView ()
- (void)buildViewWithTarget:(id)target
              refreshAction:(SEL)refreshAction
               searchAction:(SEL)searchAction
             refreshToolTip:(NSString *)refreshToolTip
                 dataSource:(id)dataSource
                   delegate:(id)delegate;
- (NSImage *)refreshButtonImage;
@end

@implementation StrappyPreferencesWhitelistView

- (id)initWithFrame:(NSRect)frame
{
  return [self initWithFrame:frame
                      target:nil
               refreshAction:NULL
                searchAction:NULL
              refreshToolTip:nil
                  dataSource:nil
                    delegate:nil];
}

- (id)initWithFrame:(NSRect)frame
             target:(id)target
      refreshAction:(SEL)refreshAction
       searchAction:(SEL)searchAction
     refreshToolTip:(NSString *)refreshToolTip
         dataSource:(id)dataSource
           delegate:(id)delegate
{
  if ((self = [super initWithFrame:frame])) {
    [self setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [self buildViewWithTarget:target
                refreshAction:refreshAction
                 searchAction:searchAction
               refreshToolTip:refreshToolTip
                   dataSource:dataSource
                     delegate:delegate];
  }
  return self;
}

- (void)buildViewWithTarget:(id)target
              refreshAction:(SEL)refreshAction
               searchAction:(SEL)searchAction
             refreshToolTip:(NSString *)refreshToolTip
                 dataSource:(id)dataSource
                   delegate:(id)delegate
{
  NSRect bounds;
  NSRect scrollFrame;
  CGFloat topAccessoryHeight;
  CGFloat tableTop;
  CGFloat tableBottom;
  CGFloat trailingControlWidth;
  CGFloat searchWidth;
  CGFloat statusX;
  CGFloat statusWidth;
  NSImage *refreshImage;

  bounds = [self bounds];
  topAccessoryHeight = [self topAccessoryHeight];
  tableTop = NSMaxY(bounds) - kStrappyPreferencesInset;

  if (topAccessoryHeight > 0.0) {
    topAccessoryView_ =
      [[NSView alloc] initWithFrame:NSMakeRect(kStrappyPreferencesInset,
                                               tableTop - topAccessoryHeight,
                                               NSWidth(bounds) -
                                                 (kStrappyPreferencesInset * 2.0),
                                               topAccessoryHeight)];
    [topAccessoryView_ setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
    trailingControlWidth = [self topAccessoryTrailingControlWidth];
    searchWidth = NSWidth([topAccessoryView_ bounds]);
    if (trailingControlWidth > 0.0) {
      searchWidth = searchWidth - trailingControlWidth -
        kStrappyWhitelistTopControlGap;
    }
    if (searchWidth < 140.0) {
      searchWidth = 140.0;
    }
    searchField_ =
      [[NSSearchField alloc] initWithFrame:NSMakeRect(0.0,
                                                      NSHeight([topAccessoryView_ bounds]) -
                                                        kStrappyWhitelistControlHeight,
                                                      searchWidth,
                                                      kStrappyWhitelistControlHeight)];
    [searchField_ setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
    if (searchAction != NULL) {
      [searchField_ setTarget:target];
      [searchField_ setAction:searchAction];
    }
    [[searchField_ cell] setPlaceholderString:NSLocalizedString(@"Search", nil)];
    [topAccessoryView_ addSubview:searchField_];
    [self configureTopAccessoryView:topAccessoryView_ target:target];
    [self addSubview:topAccessoryView_];
    tableTop = NSMinY([topAccessoryView_ frame]) - kStrappyWhitelistBottomGap;
  }

  tableBottom = kStrappyPreferencesInset +
    kStrappyWhitelistControlHeight + kStrappyWhitelistBottomGap;
  if (tableTop < tableBottom) {
    tableTop = tableBottom;
  }

  scrollFrame = NSMakeRect(kStrappyPreferencesInset,
                           tableBottom,
                           NSWidth(bounds) - (kStrappyPreferencesInset * 2.0),
                           tableTop - tableBottom);
  scrollView_ = [[NSScrollView alloc] initWithFrame:scrollFrame];
  [scrollView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [scrollView_ setBorderType:NSBezelBorder];
  [scrollView_ setHasVerticalScroller:YES];
  [scrollView_ setHasHorizontalScroller:YES];
  [scrollView_ setAutohidesScrollers:YES];

  tableView_ =
    [[StrappyPreferencesWhitelistTableView alloc]
      initWithFrame:[[scrollView_ contentView] bounds]];
  [tableView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [tableView_ setDataSource:dataSource];
  [tableView_ setDelegate:delegate];
  [tableView_ setAllowsMultipleSelection:YES];
  [tableView_ setUsesAlternatingRowBackgroundColors:YES];
  [tableView_ setRowHeight:22.0];
  [tableView_ setColumnAutoresizingStyle:NSTableViewSequentialColumnAutoresizingStyle];
  [self configureTableView:tableView_];
  [self addTableColumnsToTableView:tableView_];

  [scrollView_ setDocumentView:tableView_];
  [self addSubview:scrollView_];

  refreshButton_ =
    [[NSButton alloc] initWithFrame:NSMakeRect(kStrappyPreferencesInset,
                                               kStrappyPreferencesInset,
                                               kStrappyWhitelistRefreshButtonWidth,
                                               kStrappyWhitelistControlHeight)];
  [refreshButton_ setAutoresizingMask:NSViewMaxXMargin | NSViewMaxYMargin];
  [refreshButton_ setBezelStyle:XPBezelStyleSmallSquare];
  [refreshButton_ setButtonType:XPButtonTypeMomentaryLight];
  [refreshButton_ setToolTip:(refreshToolTip != nil) ? refreshToolTip :
    NSLocalizedString(@"Refresh", nil)];
  [refreshButton_ setTarget:target];
  [refreshButton_ setAction:refreshAction];
  refreshImage = [self refreshButtonImage];
  if (refreshImage != nil) {
    [refreshButton_ setImage:refreshImage];
    [refreshButton_ setImagePosition:NSImageOnly];
    [refreshButton_ setTitle:@""];
  } else {
    [refreshButton_ setTitle:@"R"];
    [refreshButton_ setFont:[NSFont boldSystemFontOfSize:12.0]];
  }
  [self addSubview:refreshButton_];

  progressIndicator_ = [[NSProgressIndicator alloc]
      initWithFrame:NSMakeRect(NSMaxX([refreshButton_ frame]) + 8.0,
                               kStrappyPreferencesInset + 2.0,
                               kStrappyWhitelistProgressSize,
                               kStrappyWhitelistProgressSize)];
  [progressIndicator_ setAutoresizingMask:NSViewMaxXMargin | NSViewMaxYMargin];
  [progressIndicator_ setStyle:XPProgressIndicatorStyleSpinning];
  [progressIndicator_ setIndeterminate:YES];
  [progressIndicator_ setDisplayedWhenStopped:NO];
  [self addSubview:progressIndicator_];

  statusX = NSMaxX([progressIndicator_ frame]);
  statusWidth = NSWidth(bounds) - statusX - kStrappyPreferencesInset;
  if (statusWidth < 0.0) {
    statusWidth = 0.0;
  }
  statusLabel_ =
    [[NSTextField alloc] initWithFrame:NSMakeRect(statusX,
                                                  kStrappyPreferencesInset,
                                                  statusWidth,
                                                  20.0)];
  [statusLabel_ setAutoresizingMask:NSViewWidthSizable | NSViewMaxYMargin];
  [statusLabel_ setBezeled:NO];
  [statusLabel_ setDrawsBackground:NO];
  [statusLabel_ setEditable:NO];
  [statusLabel_ setSelectable:NO];
  [statusLabel_ setAlignment:XPTextAlignmentRight];
  [statusLabel_ setFont:[NSFont systemFontOfSize:11.0]];
  [statusLabel_ setTextColor:[NSColor disabledControlTextColor]];
  [self addSubview:statusLabel_];
}

- (CGFloat)topAccessoryHeight
{
  return kStrappyWhitelistControlHeight;
}

- (CGFloat)topAccessoryTrailingControlWidth
{
  return 0.0;
}

- (void)configureTopAccessoryView:(NSView *)view target:(id)target
{
  (void)view;
  (void)target;
}

- (void)configureTableView:(NSTableView *)tableView
{
  (void)tableView;
}

- (void)addTableColumnsToTableView:(NSTableView *)tableView
{
  (void)tableView;
}

- (NSSortDescriptor *)requiredSortDescriptor
{
  return nil;
}

- (NSSortDescriptor *)defaultPrimarySortDescriptor
{
  return nil;
}

- (NSArray *)fallbackSortDescriptors
{
  return [NSArray array];
}

- (NSString *)stableSortKey
{
  return nil;
}

- (BOOL)sortKeyIsKnown:(NSString *)key
{
  (void)key;
  return NO;
}

- (NSComparisonResult)compareRow:(NSDictionary *)left
                             row:(NSDictionary *)right
                      forSortKey:(NSString *)key
{
  (void)left;
  (void)right;
  (void)key;
  return NSOrderedSame;
}

- (NSArray *)effectiveSortDescriptorsForSortDescriptors:(NSArray *)descriptors
{
  NSMutableArray *effective;
  NSSortDescriptor *requiredDescriptor;
  NSSortDescriptor *useRequiredDescriptor;
  NSSortDescriptor *primaryDescriptor;
  NSArray *fallbackDescriptors;
  NSString *requiredKey;
  NSUInteger index;

  if (![descriptors isKindOfClass:[NSArray class]]) {
    descriptors = [NSArray array];
  }

  effective = [NSMutableArray arrayWithCapacity:4U];
  requiredDescriptor = [self requiredSortDescriptor];
  requiredKey = [requiredDescriptor key];
  if ([requiredKey length] > 0U) {
    useRequiredDescriptor =
      StrappyWhitelistSortDescriptorForKey(descriptors, requiredKey);
    if (useRequiredDescriptor == nil) {
      useRequiredDescriptor = requiredDescriptor;
    }
    [effective addObject:StrappyWhitelistSortDescriptorWithKey(
      requiredKey,
      [useRequiredDescriptor ascending])];
  }

  primaryDescriptor =
    StrappyWhitelistPrimarySortDescriptor(self, descriptors, requiredKey);
  if (primaryDescriptor != nil) {
    [effective addObject:StrappyWhitelistSortDescriptorWithKey(
      [primaryDescriptor key],
      [primaryDescriptor ascending])];
  } else {
    StrappyWhitelistAddSortDescriptorIfMissing(
      effective,
      [self defaultPrimarySortDescriptor]);
  }

  fallbackDescriptors = [self fallbackSortDescriptors];
  for (index = 0U; index < [fallbackDescriptors count]; index++) {
    StrappyWhitelistAddSortDescriptorIfMissing(
      effective,
      [fallbackDescriptors objectAtIndex:index]);
  }

  return effective;
}

- (NSArray *)sortedRows:(NSArray *)rows
{
  StrappyWhitelistSortContext sortContext;

  if (![rows isKindOfClass:[NSArray class]]) {
    return [NSArray array];
  }

  sortContext.view = self;
  sortContext.sortDescriptors =
    [self effectiveSortDescriptorsForSortDescriptors:
      [[self tableView] sortDescriptors]];
  sortContext.stableSortKey = [self stableSortKey];

  return [rows sortedArrayUsingFunction:StrappyWhitelistCompareRows
                                context:&sortContext];
}

- (NSImage *)refreshButtonImage
{
  return [AIFontAwesome imageForIcon:AIFAArrowsRotate
                               style:AIFontAwesomeStyleSolid
                            iconSize:13.0
                          canvasSize:18.0
                               scale:1.0];
}

- (NSView *)topAccessoryView
{
  return topAccessoryView_;
}

- (NSSearchField *)searchField
{
  return searchField_;
}

- (NSScrollView *)scrollView
{
  return scrollView_;
}

- (NSTableView *)tableView
{
  return tableView_;
}

- (NSButton *)refreshButton
{
  return refreshButton_;
}

- (NSProgressIndicator *)progressIndicator
{
  return progressIndicator_;
}

- (NSTextField *)statusLabel
{
  return statusLabel_;
}

- (void)dealloc
{
  [topAccessoryView_ release];
  [searchField_ release];
  [scrollView_ release];
  [tableView_ release];
  [refreshButton_ release];
  [progressIndicator_ release];
  [statusLabel_ release];
  [super dealloc];
}

@end
