#import "StrappyPreferencesDatabaseWhitelistView.h"

#import "XPAppKit.h"

static const CGFloat kStrappyPreferencesInset = 12.0;

@interface StrappyDatabaseTableView : NSTableView
@end

@implementation StrappyDatabaseTableView

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
    selector = @selector(databaseTableViewDidPressSpace:);
    if ([delegate respondsToSelector:selector]) {
      [delegate performSelector:selector withObject:self];
      return;
    }
  }

  [super keyDown:event];
}

@end

@interface StrappyPreferencesDatabaseWhitelistView ()
- (void)buildViewWithTarget:(id)target
                 dataSource:(id)dataSource
                   delegate:(id)delegate;
@end

@implementation StrappyPreferencesDatabaseWhitelistView

- (id)initWithFrame:(NSRect)frame
{
  return [self initWithFrame:frame
                      target:nil
                  dataSource:nil
                    delegate:nil];
}

- (id)initWithFrame:(NSRect)frame
             target:(id)target
         dataSource:(id)dataSource
           delegate:(id)delegate
{
  if ((self = [super initWithFrame:frame])) {
    [self setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [self buildViewWithTarget:target
                   dataSource:dataSource
                     delegate:delegate];
  }
  return self;
}

- (void)buildViewWithTarget:(id)target
                 dataSource:(id)dataSource
                   delegate:(id)delegate
{
  NSTableColumn *allowedColumn;
  NSTableColumn *nameColumn;
  NSTableColumn *locationColumn;
  NSTableColumn *sizeColumn;
  NSButtonCell *allowedCell;
  NSTextFieldCell *nameCell;
  NSTextFieldCell *locationCell;
  NSTextFieldCell *sizeCell;
  NSScrollView *scrollView;
  CGFloat topY;

  topY = NSMaxY([self bounds]) - kStrappyPreferencesInset - 24.0;

  scanButton_ = [[NSButton alloc]
      initWithFrame:NSMakeRect(kStrappyPreferencesInset, topY, 128.0, 24.0)];
  [scanButton_ setAutoresizingMask:NSViewMaxXMargin | NSViewMinYMargin];
  [scanButton_ setTitle:NSLocalizedString(@"Scan Databases", nil)];
  [scanButton_ setBezelStyle:XPBezelStyleRounded];
  [scanButton_ setButtonType:XPButtonTypeMomentaryLight];
  [scanButton_ setToolTip:
    NSLocalizedString(@"Scan your home folder for SQLite databases.", nil)];
  [scanButton_ setTarget:target];
  [scanButton_ setAction:@selector(scanDatabases:)];
  [self addSubview:scanButton_];

  progressIndicator_ = [[NSProgressIndicator alloc]
      initWithFrame:NSMakeRect(NSMaxX([scanButton_ frame]) + 10.0,
                               topY + 2.0,
                               20.0,
                               20.0)];
  [progressIndicator_ setAutoresizingMask:NSViewMaxXMargin | NSViewMinYMargin];
  [progressIndicator_ setStyle:XPProgressIndicatorStyleSpinning];
  [progressIndicator_ setIndeterminate:YES];
  [progressIndicator_ setDisplayedWhenStopped:NO];
  [self addSubview:progressIndicator_];

  scrollView = [[[NSScrollView alloc]
      initWithFrame:NSMakeRect(kStrappyPreferencesInset,
                               kStrappyPreferencesInset,
                               NSWidth([self bounds]) - (kStrappyPreferencesInset * 2.0),
                               topY - (kStrappyPreferencesInset * 2.0))]
      autorelease];
  [scrollView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [scrollView setBorderType:NSBezelBorder];
  [scrollView setHasVerticalScroller:YES];
  [scrollView setHasHorizontalScroller:NO];
  [scrollView setAutohidesScrollers:YES];

  tableView_ =
    [[StrappyDatabaseTableView alloc] initWithFrame:[[scrollView contentView] bounds]];
  [tableView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [tableView_ setDataSource:dataSource];
  [tableView_ setDelegate:delegate];
  [tableView_ setAllowsMultipleSelection:YES];
  [tableView_ setUsesAlternatingRowBackgroundColors:YES];
  [tableView_ setRowHeight:22.0];
  [tableView_ setColumnAutoresizingStyle:NSTableViewSequentialColumnAutoresizingStyle];

  allowedColumn =
    [[[NSTableColumn alloc] initWithIdentifier:@"allowed"] autorelease];
  [[allowedColumn headerCell] setStringValue:NSLocalizedString(@"Use", nil)];
  [allowedColumn setWidth:48.0];
  [allowedColumn setMinWidth:44.0];
  [allowedColumn setMaxWidth:54.0];
  [allowedColumn setEditable:YES];
  allowedCell = [[[NSButtonCell alloc] init] autorelease];
  [allowedCell setButtonType:XPButtonTypeSwitch];
  [allowedCell setTitle:@""];
  [allowedCell setAlignment:XPTextAlignmentCenter];
  [allowedColumn setDataCell:allowedCell];
  [tableView_ addTableColumn:allowedColumn];

  nameColumn = [[[NSTableColumn alloc] initWithIdentifier:@"name"] autorelease];
  [[nameColumn headerCell] setStringValue:NSLocalizedString(@"Database", nil)];
  [nameColumn setWidth:210.0];
  [nameColumn setMinWidth:120.0];
  [nameColumn setEditable:NO];
  nameCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [nameCell setLineBreakMode:NSLineBreakByTruncatingTail];
  [nameColumn setDataCell:nameCell];
  [tableView_ addTableColumn:nameColumn];

  locationColumn =
    [[[NSTableColumn alloc] initWithIdentifier:@"location"] autorelease];
  [[locationColumn headerCell] setStringValue:NSLocalizedString(@"Location", nil)];
  [locationColumn setWidth:270.0];
  [locationColumn setMinWidth:160.0];
  [locationColumn setEditable:NO];
  locationCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [locationCell setLineBreakMode:NSLineBreakByTruncatingMiddle];
  [locationCell setTextColor:[NSColor disabledControlTextColor]];
  [locationColumn setDataCell:locationCell];
  [tableView_ addTableColumn:locationColumn];

  sizeColumn = [[[NSTableColumn alloc] initWithIdentifier:@"size"] autorelease];
  [[sizeColumn headerCell] setStringValue:NSLocalizedString(@"Size", nil)];
  [sizeColumn setWidth:76.0];
  [sizeColumn setMinWidth:66.0];
  [sizeColumn setEditable:NO];
  sizeCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [sizeCell setAlignment:XPTextAlignmentRight];
  [sizeColumn setDataCell:sizeCell];
  [tableView_ addTableColumn:sizeColumn];

  [scrollView setDocumentView:tableView_];
  [self addSubview:scrollView];
}

- (NSTableView *)tableView
{
  return tableView_;
}

- (NSButton *)scanButton
{
  return scanButton_;
}

- (NSProgressIndicator *)progressIndicator
{
  return progressIndicator_;
}

- (void)dealloc
{
  [tableView_ release];
  [scanButton_ release];
  [progressIndicator_ release];
  [super dealloc];
}

@end
