#import "StrappyPreferencesModelWhitelistView.h"

#import "XPAppKit.h"

static const CGFloat kStrappyPreferencesInset = 12.0;

@interface StrappyPreferencesModelWhitelistView ()
- (void)buildViewWithTarget:(id)target
                 dataSource:(id)dataSource
                   delegate:(id)delegate;
@end

@implementation StrappyPreferencesModelWhitelistView

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
  NSScrollView *scrollView;
  NSTableColumn *allowedColumn;
  NSTableColumn *nameColumn;
  NSTableColumn *idColumn;
  NSTableColumn *contextColumn;
  NSTableColumn *promptColumn;
  NSTableColumn *completionColumn;
  NSButtonCell *allowedCell;
  NSTextFieldCell *textCell;
  NSTextFieldCell *rightCell;
  CGFloat topY;
  CGFloat searchY;
  CGFloat defaultModelPopupWidth;
  CGFloat searchWidth;

  topY = NSMaxY([self bounds]) - kStrappyPreferencesInset - 24.0;

  fetchButton_ = [[NSButton alloc]
      initWithFrame:NSMakeRect(kStrappyPreferencesInset, topY, 116.0, 24.0)];
  [fetchButton_ setAutoresizingMask:NSViewMaxXMargin | NSViewMinYMargin];
  [fetchButton_ setTitle:NSLocalizedString(@"Fetch Models", nil)];
  [fetchButton_ setBezelStyle:XPBezelStyleRounded];
  [fetchButton_ setButtonType:XPButtonTypeMomentaryLight];
  [fetchButton_ setToolTip:
    NSLocalizedString(@"Refresh the OpenRouter model list.", nil)];
  [fetchButton_ setTarget:target];
  [fetchButton_ setAction:@selector(refreshModels:)];
  [self addSubview:fetchButton_];

  progressIndicator_ = [[NSProgressIndicator alloc]
      initWithFrame:NSMakeRect(NSMaxX([fetchButton_ frame]) + 10.0,
                               topY + 2.0,
                               20.0,
                               20.0)];
  [progressIndicator_ setAutoresizingMask:NSViewMaxXMargin | NSViewMinYMargin];
  [progressIndicator_ setStyle:XPProgressIndicatorStyleSpinning];
  [progressIndicator_ setIndeterminate:YES];
  [progressIndicator_ setDisplayedWhenStopped:NO];
  [self addSubview:progressIndicator_];

  statusLabel_ =
    [[NSTextField alloc] initWithFrame:NSMakeRect(NSMaxX([progressIndicator_ frame]) + 8.0,
                                                  topY + 3.0,
                                                  NSWidth([self bounds]) -
                                                    NSMaxX([progressIndicator_ frame]) -
                                                    20.0,
                                                  20.0)];
  [statusLabel_ setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
  [statusLabel_ setBezeled:NO];
  [statusLabel_ setDrawsBackground:NO];
  [statusLabel_ setEditable:NO];
  [statusLabel_ setSelectable:NO];
  [statusLabel_ setFont:[NSFont systemFontOfSize:11.0]];
  [statusLabel_ setTextColor:[NSColor disabledControlTextColor]];
  [self addSubview:statusLabel_];

  searchY = topY - 34.0;
  defaultModelPopupWidth = 220.0;
  searchWidth = NSWidth([self bounds]) - (kStrappyPreferencesInset * 2.0) -
    defaultModelPopupWidth - 8.0;
  if (searchWidth < 140.0) {
    searchWidth = 140.0;
  }
  searchField_ =
    [[NSSearchField alloc] initWithFrame:NSMakeRect(kStrappyPreferencesInset,
                                                    searchY,
                                                    searchWidth,
                                                    24.0)];
  [searchField_ setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
  [searchField_ setTarget:target];
  [searchField_ setAction:@selector(modelSearchChanged:)];
  [self addSubview:searchField_];

  defaultModelPopUpButton_ =
    [[NSPopUpButton alloc] initWithFrame:NSMakeRect(NSWidth([self bounds]) -
                                                      kStrappyPreferencesInset -
                                                      defaultModelPopupWidth,
                                                    searchY,
                                                    defaultModelPopupWidth,
                                                    24.0)
                               pullsDown:NO];
  [defaultModelPopUpButton_ setAutoresizingMask:NSViewMinXMargin | NSViewMinYMargin];
  [defaultModelPopUpButton_ setBezelStyle:XPBezelStyleRounded];
  [defaultModelPopUpButton_ setToolTip:
    NSLocalizedString(@"Default model for new chats", nil)];
  [defaultModelPopUpButton_ setTarget:target];
  [defaultModelPopUpButton_ setAction:@selector(defaultModelPopUpButtonChanged:)];
  [[defaultModelPopUpButton_ menu] setAutoenablesItems:NO];
  [self addSubview:defaultModelPopUpButton_];

  scrollView = [[[NSScrollView alloc]
      initWithFrame:NSMakeRect(kStrappyPreferencesInset,
                               kStrappyPreferencesInset,
                               NSWidth([self bounds]) - (kStrappyPreferencesInset * 2.0),
                               searchY - (kStrappyPreferencesInset * 2.0))]
      autorelease];
  [scrollView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [scrollView setBorderType:NSBezelBorder];
  [scrollView setHasVerticalScroller:YES];
  [scrollView setHasHorizontalScroller:YES];
  [scrollView setAutohidesScrollers:YES];

  tableView_ =
    [[NSTableView alloc] initWithFrame:[[scrollView contentView] bounds]];
  [tableView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [tableView_ setDataSource:dataSource];
  [tableView_ setDelegate:delegate];
  [tableView_ setAllowsMultipleSelection:NO];
  [tableView_ setUsesAlternatingRowBackgroundColors:YES];
  [tableView_ setRowHeight:22.0];
  [tableView_ setColumnAutoresizingStyle:NSTableViewSequentialColumnAutoresizingStyle];

  allowedColumn =
    [[[NSTableColumn alloc] initWithIdentifier:@"model_allowed"] autorelease];
  [[allowedColumn headerCell] setStringValue:NSLocalizedString(@"Use", nil)];
  [allowedColumn setWidth:44.0];
  [allowedColumn setMinWidth:40.0];
  [allowedColumn setMaxWidth:48.0];
  [allowedColumn setEditable:YES];
  [allowedColumn setSortDescriptorPrototype:
    [[[NSSortDescriptor alloc] initWithKey:@"model_allowed"
                                 ascending:NO] autorelease]];
  allowedCell = [[[NSButtonCell alloc] init] autorelease];
  [allowedCell setButtonType:XPButtonTypeSwitch];
  [allowedCell setTitle:@""];
  [allowedCell setAlignment:XPTextAlignmentCenter];
  [allowedColumn setDataCell:allowedCell];
  [tableView_ addTableColumn:allowedColumn];

  nameColumn = [[[NSTableColumn alloc] initWithIdentifier:@"model_name"] autorelease];
  [[nameColumn headerCell] setStringValue:NSLocalizedString(@"Model", nil)];
  [nameColumn setWidth:148.0];
  [nameColumn setMinWidth:100.0];
  [nameColumn setEditable:NO];
  [nameColumn setSortDescriptorPrototype:
    [[[NSSortDescriptor alloc] initWithKey:@"model_name"
                                 ascending:YES] autorelease]];
  textCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [textCell setLineBreakMode:NSLineBreakByTruncatingTail];
  [nameColumn setDataCell:textCell];
  [tableView_ addTableColumn:nameColumn];

  idColumn = [[[NSTableColumn alloc] initWithIdentifier:@"model_id"] autorelease];
  [[idColumn headerCell] setStringValue:NSLocalizedString(@"ID", nil)];
  [idColumn setWidth:174.0];
  [idColumn setMinWidth:120.0];
  [idColumn setEditable:NO];
  [idColumn setSortDescriptorPrototype:
    [[[NSSortDescriptor alloc] initWithKey:@"model_id"
                                 ascending:YES] autorelease]];
  textCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [textCell setLineBreakMode:NSLineBreakByTruncatingMiddle];
  [textCell setTextColor:[NSColor disabledControlTextColor]];
  [idColumn setDataCell:textCell];
  [tableView_ addTableColumn:idColumn];

  contextColumn =
    [[[NSTableColumn alloc] initWithIdentifier:@"model_context"] autorelease];
  [[contextColumn headerCell] setStringValue:NSLocalizedString(@"Context", nil)];
  [[contextColumn headerCell] setAlignment:XPTextAlignmentRight];
  [contextColumn setWidth:58.0];
  [contextColumn setMinWidth:50.0];
  [contextColumn setEditable:NO];
  [contextColumn setSortDescriptorPrototype:
    [[[NSSortDescriptor alloc] initWithKey:@"model_context"
                                 ascending:NO] autorelease]];
  rightCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [rightCell setAlignment:XPTextAlignmentRight];
  [contextColumn setDataCell:rightCell];
  [tableView_ addTableColumn:contextColumn];

  promptColumn =
    [[[NSTableColumn alloc] initWithIdentifier:@"model_prompt_price"] autorelease];
  [[promptColumn headerCell] setStringValue:NSLocalizedString(@"Cost In (1M)", nil)];
  [[promptColumn headerCell] setAlignment:XPTextAlignmentRight];
  [promptColumn setWidth:88.0];
  [promptColumn setMinWidth:76.0];
  [promptColumn setEditable:NO];
  [promptColumn setSortDescriptorPrototype:
    [[[NSSortDescriptor alloc] initWithKey:@"model_prompt_price"
                                 ascending:YES] autorelease]];
  rightCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [rightCell setAlignment:XPTextAlignmentRight];
  [promptColumn setDataCell:rightCell];
  [tableView_ addTableColumn:promptColumn];

  completionColumn =
    [[[NSTableColumn alloc] initWithIdentifier:@"model_completion_price"] autorelease];
  [[completionColumn headerCell] setStringValue:NSLocalizedString(@"Cost Out (1M)", nil)];
  [[completionColumn headerCell] setAlignment:XPTextAlignmentRight];
  [completionColumn setWidth:94.0];
  [completionColumn setMinWidth:82.0];
  [completionColumn setEditable:NO];
  [completionColumn setSortDescriptorPrototype:
    [[[NSSortDescriptor alloc] initWithKey:@"model_completion_price"
                                 ascending:YES] autorelease]];
  rightCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [rightCell setAlignment:XPTextAlignmentRight];
  [completionColumn setDataCell:rightCell];
  [tableView_ addTableColumn:completionColumn];

  [tableView_ setSortDescriptors:[NSArray arrayWithObjects:
    [[[NSSortDescriptor alloc] initWithKey:@"model_id"
                                 ascending:YES] autorelease],
    nil]];

  [scrollView setDocumentView:tableView_];
  [self addSubview:scrollView];
}

- (NSSearchField *)searchField
{
  return searchField_;
}

- (NSPopUpButton *)defaultModelPopUpButton
{
  return defaultModelPopUpButton_;
}

- (NSTableView *)tableView
{
  return tableView_;
}

- (NSButton *)fetchButton
{
  return fetchButton_;
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
  [searchField_ release];
  [defaultModelPopUpButton_ release];
  [tableView_ release];
  [fetchButton_ release];
  [progressIndicator_ release];
  [statusLabel_ release];
  [super dealloc];
}

@end
