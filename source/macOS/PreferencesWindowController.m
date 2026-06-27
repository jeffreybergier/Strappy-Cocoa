#import "PreferencesWindowController.h"

#import "FileScanner.h"
#import "StrappySession.h"
#import "strappy_config.h"
#import "strappy_keychain.h"

static const CGFloat kStrappyPreferencesWidth = 720.0;
static const CGFloat kStrappyPreferencesHeight = 480.0;
static const CGFloat kStrappyPreferencesInset = 12.0;
static NSString * const kStrappyPreferencesFrameAutosaveName =
  @"StrappyPreferencesWindow";

static NSString *StrappyByteCountString(NSNumber *sizeNumber)
{
  unsigned long long size;
  double value;
  NSArray *units;
  NSUInteger unitIndex;

  if (![sizeNumber isKindOfClass:[NSNumber class]]) {
    return @"";
  }

  size = [sizeNumber unsignedLongLongValue];
  value = (double)size;
  units = [NSArray arrayWithObjects:@"B", @"KB", @"MB", @"GB", @"TB", nil];
  unitIndex = 0U;

  while ((value >= 1024.0) && ((unitIndex + 1U) < [units count])) {
    value = value / 1024.0;
    unitIndex++;
  }

  if (unitIndex == 0U) {
    return [NSString stringWithFormat:@"%llu %@",
      size,
      [units objectAtIndex:unitIndex]];
  }
  return [NSString stringWithFormat:@"%.1f %@",
    value,
    [units objectAtIndex:unitIndex]];
}

static NSString *StrappyDatabasePathForRow(NSDictionary *row)
{
  NSString *path;

  path = [row objectForKey:@"path"];
  if (![path isKindOfClass:[NSString class]]) {
    return @"";
  }
  return path;
}

static NSString *StrappyDatabaseNameForRow(NSDictionary *row)
{
  NSString *path;
  NSString *name;

  path = StrappyDatabasePathForRow(row);
  name = [path lastPathComponent];
  if ([name length] == 0U) {
    return path;
  }
  return name;
}

static NSString *StrappyDatabaseLocationForRow(NSDictionary *row)
{
  NSString *path;
  NSString *directory;
  NSString *homeDirectory;
  NSUInteger homeLength;

  path = StrappyDatabasePathForRow(row);
  directory = [path stringByDeletingLastPathComponent];
  if (([directory length] == 0U) || [directory isEqualToString:path]) {
    return @"";
  }

  homeDirectory = NSHomeDirectory();
  homeLength = [homeDirectory length];
  if ((homeLength > 0U) && [directory hasPrefix:homeDirectory]) {
    if ([directory length] == homeLength) {
      return @"~";
    }
    if ([directory characterAtIndex:homeLength] == '/') {
      return [@"~" stringByAppendingString:
        [directory substringFromIndex:homeLength]];
    }
  }

  return directory;
}

static NSString *StrappyStringForModelRow(NSDictionary *row, NSString *key)
{
  NSString *value;

  value = [row objectForKey:key];
  if (![value isKindOfClass:[NSString class]]) {
    return @"";
  }
  return value;
}

static NSString *StrappyModelDisplayNameForRow(NSDictionary *row)
{
  NSString *name;

  name = StrappyStringForModelRow(row, @"name");
  if ([name length] > 0U) {
    return name;
  }
  return StrappyStringForModelRow(row, @"id");
}

static NSString *StrappyModelNumberString(NSDictionary *row, NSString *key)
{
  NSNumber *value;
  unsigned long long count;

  value = [row objectForKey:key];
  if (![value isKindOfClass:[NSNumber class]] || ([value longLongValue] <= 0LL)) {
    return @"";
  }
  count = [value unsignedLongLongValue];
  return [NSString stringWithFormat:@"%lluk", (count + 500ULL) / 1000ULL];
}

static NSString *StrappyModelPricingString(NSDictionary *row, NSString *key)
{
  static NSNumberFormatter *formatter = nil;
  NSString *value;
  double dollarsPerMillion;
  NSString *formatted;

  value = StrappyStringForModelRow(row, key);
  if ([value length] == 0U) {
    return @"";
  }

  if (formatter == nil) {
    NSLocale *locale;

    formatter = [[NSNumberFormatter alloc] init];
    [formatter setFormatterBehavior:NSNumberFormatterBehavior10_4];
    [formatter setNumberStyle:NSNumberFormatterCurrencyStyle];
    locale = [[NSLocale alloc] initWithLocaleIdentifier:@"en_US_POSIX"];
    [formatter setLocale:locale];
    [locale release];
    [formatter setCurrencyCode:@"USD"];
    [formatter setCurrencySymbol:@"$"];
    [formatter setMinimumFractionDigits:0U];
    [formatter setMaximumFractionDigits:6U];
  }

  dollarsPerMillion = [value doubleValue] * 1000000.0;
  formatted =
    [formatter stringFromNumber:[NSNumber numberWithDouble:dollarsPerMillion]];
  return (formatted != nil) ? formatted : @"";
}

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

@interface PreferencesWindowController ()
- (void)buildContentView;
- (NSView *)apiTokenPaneWithFrame:(NSRect)frame;
- (NSView *)modelPaneWithFrame:(NSRect)frame;
- (NSView *)systemPromptPaneWithFrame:(NSRect)frame;
- (NSView *)databaseScanningPaneWithFrame:(NSRect)frame;
- (NSTextField *)labelWithFrame:(NSRect)frame text:(NSString *)text;
- (void)refreshAPITokenStatusWithSaved:(BOOL)saved;
- (void)loadSystemPrompt;
- (NSString *)currentModelSearchText;
- (void)loadOpenRouterModels;
- (void)modelSearchChanged:(id)sender;
- (void)modelSearchTextDidChange:(NSNotification *)notification;
- (void)selectCurrentOpenRouterModelRow;
- (void)beginOpenRouterModelChangeSheetForRow:(NSDictionary *)row;
- (void)openRouterModelChangeSheetDidEnd:(NSAlert *)alert
                              returnCode:(NSInteger)returnCode
                             contextInfo:(void *)contextInfo;
- (void)setModelCatalogRefreshing:(BOOL)refreshing;
- (void)modelCatalogRefreshDidStart:(NSNotification *)notification;
- (void)modelCatalogRefreshDidFinish:(NSNotification *)notification;
- (void)modelCatalogDidChange:(NSNotification *)notification;
- (void)loadCatalogedDatabases;
- (void)setScanning:(BOOL)scanning;
- (void)scanDatabasesInBackground:(NSString *)rootPath;
- (void)scanDatabasesDidFinish:(NSDictionary *)result;
- (void)databaseTableViewDidPressSpace:(NSTableView *)tableView;
- (BOOL)databaseRowCanBeAllowed:(NSDictionary *)row;
- (NSNumber *)allowedValueForDatabaseRow:(NSDictionary *)row;
@end

@implementation PreferencesWindowController

- (id)init
{
  NSWindow *window;
  NSUInteger styleMask;

  styleMask = (XPWindowStyleMaskTitled |
               XPWindowStyleMaskClosable |
               XPWindowStyleMaskMiniaturizable |
               XPWindowStyleMaskResizable);
  window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0.0,
                                     0.0,
                                     kStrappyPreferencesWidth,
                                     kStrappyPreferencesHeight)
                styleMask:styleMask
                  backing:NSBackingStoreBuffered
                    defer:NO];
  [window setTitle:NSLocalizedString(@"Preferences", nil)];
  [window setReleasedWhenClosed:NO];
  if (![window setFrameUsingName:kStrappyPreferencesFrameAutosaveName]) {
    [window setContentSize:NSMakeSize(kStrappyPreferencesWidth,
                                      kStrappyPreferencesHeight)];
    [window center];
  }
  [window setFrameAutosaveName:kStrappyPreferencesFrameAutosaveName];

  if ((self = [super initWithWindow:window])) {
    modelRows_ = [[NSArray alloc] init];
    databaseRows_ = [[NSArray alloc] init];
    [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(modelCatalogRefreshDidStart:)
             name:StrappySessionModelCatalogRefreshDidStartNotification
           object:nil];
    [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(modelCatalogRefreshDidFinish:)
             name:StrappySessionModelCatalogRefreshDidFinishNotification
           object:nil];
    [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(modelCatalogDidChange:)
             name:StrappySessionModelCatalogDidChangeNotification
           object:nil];
    [self buildContentView];
    [self loadSystemPrompt];
    [self setModelCatalogRefreshing:[StrappySession isModelCatalogRefreshInFlight]];
    [self loadOpenRouterModels];
    [self setScanning:NO];
    [self loadCatalogedDatabases];
  }

  [window release];
  return self;
}

- (void)buildContentView
{
  NSView *contentView;
  NSTabViewItem *apiTokenItem;
  NSTabViewItem *modelItem;
  NSTabViewItem *systemPromptItem;
  NSTabViewItem *databaseItem;
  NSRect bounds;
  NSRect paneFrame;

  contentView = [[self window] contentView];
  bounds = [contentView bounds];
  tabView_ = [[NSTabView alloc] initWithFrame:NSInsetRect(bounds,
                                                          kStrappyPreferencesInset,
                                                          kStrappyPreferencesInset)];
  [tabView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

  paneFrame = NSMakeRect(0.0,
                         0.0,
                         bounds.size.width - 48.0,
                         bounds.size.height - 72.0);

  apiTokenItem =
    [[[NSTabViewItem alloc] initWithIdentifier:@"api_token"] autorelease];
  [apiTokenItem setLabel:NSLocalizedString(@"API Token", nil)];
  [apiTokenItem setView:[self apiTokenPaneWithFrame:paneFrame]];
  [tabView_ addTabViewItem:apiTokenItem];

  modelItem =
    [[[NSTabViewItem alloc] initWithIdentifier:@"models"] autorelease];
  [modelItem setLabel:NSLocalizedString(@"Model", nil)];
  [modelItem setView:[self modelPaneWithFrame:paneFrame]];
  [tabView_ addTabViewItem:modelItem];

  databaseItem =
    [[[NSTabViewItem alloc] initWithIdentifier:@"databases"] autorelease];
  [databaseItem setLabel:NSLocalizedString(@"Database Search", nil)];
  [databaseItem setView:[self databaseScanningPaneWithFrame:paneFrame]];
  [tabView_ addTabViewItem:databaseItem];

  systemPromptItem =
    [[[NSTabViewItem alloc] initWithIdentifier:@"system_prompt"] autorelease];
  [systemPromptItem setLabel:NSLocalizedString(@"System Prompt", nil)];
  [systemPromptItem setView:[self systemPromptPaneWithFrame:paneFrame]];
  [tabView_ addTabViewItem:systemPromptItem];

  [contentView addSubview:tabView_];
}

- (NSView *)apiTokenPaneWithFrame:(NSRect)frame
{
  NSView *view;
  NSTextField *endpointLabel;
  NSTextField *tokenLabel;
  NSTextField *hintLabel;
  NSButton *saveButton;
  NSString *apiEndpoint;
  NSString *apiToken;
  NSRect bounds;
  CGFloat labelWidth;
  CGFloat topY;
  CGFloat tokenY;
  CGFloat fieldX;
  CGFloat fieldWidth;

  view = [[[NSView alloc] initWithFrame:frame] autorelease];
  [view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

  bounds = [view bounds];
  labelWidth = 104.0;
  topY = NSMaxY(bounds) - kStrappyPreferencesInset - 28.0;
  tokenY = topY - 34.0;
  fieldX = kStrappyPreferencesInset + labelWidth;
  fieldWidth = NSWidth(bounds) - fieldX - kStrappyPreferencesInset;

  endpointLabel = [self labelWithFrame:NSMakeRect(kStrappyPreferencesInset,
                                                  topY + 3.0,
                                                  labelWidth - 8.0,
                                                  20.0)
                                  text:NSLocalizedString(@"API Endpoint:", nil)];
  [view addSubview:endpointLabel];

  apiEndpoint = [[StrappyKeychain sharedKeychain] apiEndpoint];
  if ([apiEndpoint length] == 0U) {
    apiEndpoint = [NSString stringWithUTF8String:STRAPPY_CONFIG_DEFAULT_API_ENDPOINT];
  }
  apiEndpointField_ =
    [[NSTextField alloc] initWithFrame:NSMakeRect(fieldX,
                                                  topY,
                                                  fieldWidth,
                                                  24.0)];
  [apiEndpointField_ setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
  [apiEndpointField_ setStringValue:(apiEndpoint != nil) ? apiEndpoint : @""];
  [[apiEndpointField_ cell] setPlaceholderString:
    NSLocalizedString(@"https://openrouter.ai/api/v1/chat/completions", nil)];
  [view addSubview:apiEndpointField_];

  tokenLabel = [self labelWithFrame:NSMakeRect(kStrappyPreferencesInset,
                                               tokenY + 3.0,
                                               labelWidth - 8.0,
                                               20.0)
                               text:NSLocalizedString(@"API Token:", nil)];
  [view addSubview:tokenLabel];

  apiToken = [[StrappyKeychain sharedKeychain] apiToken];
  apiTokenField_ =
    [[NSSecureTextField alloc] initWithFrame:NSMakeRect(fieldX,
                                                        tokenY,
                                                        fieldWidth,
                                                        24.0)];
  [apiTokenField_ setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
  [apiTokenField_ setStringValue:(apiToken != nil) ? apiToken : @""];
  [[apiTokenField_ cell] setPlaceholderString:
    NSLocalizedString(@"Paste API token", nil)];
  [view addSubview:apiTokenField_];

  hintLabel = [self labelWithFrame:NSMakeRect(fieldX,
                                              tokenY - 46.0,
                                              fieldWidth,
                                              38.0)
                              text:NSLocalizedString(
    @"APIENDPOINT or APITOKEN in .env or the process environment overrides keychain values while set.",
    nil)];
  [hintLabel setFont:[NSFont systemFontOfSize:11.0]];
  [hintLabel setTextColor:[NSColor disabledControlTextColor]];
  [hintLabel setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
  [[hintLabel cell] setWraps:YES];
  [view addSubview:hintLabel];

  saveButton = [[[NSButton alloc]
    initWithFrame:NSMakeRect(NSMaxX(bounds) - kStrappyPreferencesInset - 96.0,
                             kStrappyPreferencesInset,
                             96.0,
                             24.0)] autorelease];
  [saveButton setAutoresizingMask:NSViewMinXMargin | NSViewMaxYMargin];
  [saveButton setTitle:NSLocalizedString(@"Save", nil)];
  [saveButton setBezelStyle:XPBezelStyleRounded];
  [saveButton setButtonType:XPButtonTypeMomentaryLight];
  [saveButton setKeyEquivalent:@"\r"];
  [saveButton setTarget:self];
  [saveButton setAction:@selector(saveAPICredentials:)];
  [view addSubview:saveButton];

  apiTokenStatusLabel_ =
    [[NSTextField alloc] initWithFrame:NSMakeRect(kStrappyPreferencesInset,
                                                  kStrappyPreferencesInset + 2.0,
                                                  NSWidth(bounds) - 132.0,
                                                  20.0)];
  [apiTokenStatusLabel_ setAutoresizingMask:NSViewWidthSizable | NSViewMaxYMargin];
  [apiTokenStatusLabel_ setBezeled:NO];
  [apiTokenStatusLabel_ setDrawsBackground:NO];
  [apiTokenStatusLabel_ setEditable:NO];
  [apiTokenStatusLabel_ setSelectable:NO];
  [apiTokenStatusLabel_ setFont:[NSFont systemFontOfSize:11.0]];
  [apiTokenStatusLabel_ setTextColor:[NSColor disabledControlTextColor]];
  [view addSubview:apiTokenStatusLabel_];
  [self refreshAPITokenStatusWithSaved:NO];

  return view;
}

- (NSView *)modelPaneWithFrame:(NSRect)frame
{
  NSView *view;
  NSScrollView *scrollView;
  NSTableColumn *nameColumn;
  NSTableColumn *idColumn;
  NSTableColumn *contextColumn;
  NSTableColumn *promptColumn;
  NSTableColumn *completionColumn;
  NSTextFieldCell *textCell;
  NSTextFieldCell *rightCell;
  CGFloat topY;
  CGFloat searchY;

  view = [[[NSView alloc] initWithFrame:frame] autorelease];
  [view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  topY = NSMaxY([view bounds]) - kStrappyPreferencesInset - 24.0;

  fetchModelsButton_ = [[NSButton alloc]
      initWithFrame:NSMakeRect(kStrappyPreferencesInset, topY, 116.0, 24.0)];
  [fetchModelsButton_ setAutoresizingMask:NSViewMaxXMargin | NSViewMinYMargin];
  [fetchModelsButton_ setTitle:NSLocalizedString(@"Fetch Models", nil)];
  [fetchModelsButton_ setBezelStyle:XPBezelStyleRounded];
  [fetchModelsButton_ setButtonType:XPButtonTypeMomentaryLight];
  [fetchModelsButton_ setToolTip:
    NSLocalizedString(@"Refresh the OpenRouter model list.", nil)];
  [fetchModelsButton_ setTarget:self];
  [fetchModelsButton_ setAction:@selector(refreshModels:)];
  [view addSubview:fetchModelsButton_];

  modelProgressIndicator_ = [[NSProgressIndicator alloc]
      initWithFrame:NSMakeRect(NSMaxX([fetchModelsButton_ frame]) + 10.0,
                               topY + 2.0,
                               20.0,
                               20.0)];
  [modelProgressIndicator_ setAutoresizingMask:NSViewMaxXMargin | NSViewMinYMargin];
  [modelProgressIndicator_ setStyle:XPProgressIndicatorStyleSpinning];
  [modelProgressIndicator_ setIndeterminate:YES];
  [modelProgressIndicator_ setDisplayedWhenStopped:NO];
  [view addSubview:modelProgressIndicator_];

  modelStatusLabel_ =
    [[NSTextField alloc] initWithFrame:NSMakeRect(NSMaxX([modelProgressIndicator_ frame]) + 8.0,
                                                  topY + 3.0,
                                                  NSWidth([view bounds]) -
                                                    NSMaxX([modelProgressIndicator_ frame]) -
                                                    20.0,
                                                  20.0)];
  [modelStatusLabel_ setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
  [modelStatusLabel_ setBezeled:NO];
  [modelStatusLabel_ setDrawsBackground:NO];
  [modelStatusLabel_ setEditable:NO];
  [modelStatusLabel_ setSelectable:NO];
  [modelStatusLabel_ setFont:[NSFont systemFontOfSize:11.0]];
  [modelStatusLabel_ setTextColor:[NSColor disabledControlTextColor]];
  [view addSubview:modelStatusLabel_];

  searchY = topY - 34.0;
  modelSearchField_ =
    [[NSSearchField alloc] initWithFrame:NSMakeRect(kStrappyPreferencesInset,
                                                    searchY,
                                                    NSWidth([view bounds]) -
                                                      (kStrappyPreferencesInset * 2.0),
                                                    24.0)];
  [modelSearchField_ setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
  [modelSearchField_ setTarget:self];
  [modelSearchField_ setAction:@selector(modelSearchChanged:)];
  [view addSubview:modelSearchField_];
  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(modelSearchTextDidChange:)
           name:NSControlTextDidChangeNotification
         object:modelSearchField_];

  scrollView = [[[NSScrollView alloc]
      initWithFrame:NSMakeRect(kStrappyPreferencesInset,
                               kStrappyPreferencesInset,
                               NSWidth([view bounds]) - (kStrappyPreferencesInset * 2.0),
                               searchY - (kStrappyPreferencesInset * 2.0))]
      autorelease];
  [scrollView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [scrollView setBorderType:NSBezelBorder];
  [scrollView setHasVerticalScroller:YES];
  [scrollView setHasHorizontalScroller:NO];
  [scrollView setAutohidesScrollers:YES];

  modelTableView_ =
    [[NSTableView alloc] initWithFrame:[[scrollView contentView] bounds]];
  [modelTableView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [modelTableView_ setDataSource:self];
  [modelTableView_ setDelegate:self];
  [modelTableView_ setAllowsMultipleSelection:NO];
  [modelTableView_ setUsesAlternatingRowBackgroundColors:YES];
  [modelTableView_ setRowHeight:22.0];
  [modelTableView_ setColumnAutoresizingStyle:NSTableViewSequentialColumnAutoresizingStyle];

  nameColumn = [[[NSTableColumn alloc] initWithIdentifier:@"model_name"] autorelease];
  [[nameColumn headerCell] setStringValue:NSLocalizedString(@"Model", nil)];
  [nameColumn setWidth:160.0];
  [nameColumn setMinWidth:120.0];
  [nameColumn setEditable:NO];
  textCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [textCell setLineBreakMode:NSLineBreakByTruncatingTail];
  [nameColumn setDataCell:textCell];
  [modelTableView_ addTableColumn:nameColumn];

  idColumn = [[[NSTableColumn alloc] initWithIdentifier:@"model_id"] autorelease];
  [[idColumn headerCell] setStringValue:NSLocalizedString(@"ID", nil)];
  [idColumn setWidth:186.0];
  [idColumn setMinWidth:150.0];
  [idColumn setEditable:NO];
  textCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [textCell setLineBreakMode:NSLineBreakByTruncatingMiddle];
  [textCell setTextColor:[NSColor disabledControlTextColor]];
  [idColumn setDataCell:textCell];
  [modelTableView_ addTableColumn:idColumn];

  contextColumn =
    [[[NSTableColumn alloc] initWithIdentifier:@"model_context"] autorelease];
  [[contextColumn headerCell] setStringValue:NSLocalizedString(@"Context", nil)];
  [[contextColumn headerCell] setAlignment:XPTextAlignmentRight];
  [contextColumn setWidth:60.0];
  [contextColumn setMinWidth:54.0];
  [contextColumn setEditable:NO];
  rightCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [rightCell setAlignment:XPTextAlignmentRight];
  [contextColumn setDataCell:rightCell];
  [modelTableView_ addTableColumn:contextColumn];

  promptColumn =
    [[[NSTableColumn alloc] initWithIdentifier:@"model_prompt_price"] autorelease];
  [[promptColumn headerCell] setStringValue:NSLocalizedString(@"Cost In (1M)", nil)];
  [[promptColumn headerCell] setAlignment:XPTextAlignmentRight];
  [promptColumn setWidth:98.0];
  [promptColumn setMinWidth:92.0];
  [promptColumn setEditable:NO];
  rightCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [rightCell setAlignment:XPTextAlignmentRight];
  [promptColumn setDataCell:rightCell];
  [modelTableView_ addTableColumn:promptColumn];

  completionColumn =
    [[[NSTableColumn alloc] initWithIdentifier:@"model_completion_price"] autorelease];
  [[completionColumn headerCell] setStringValue:NSLocalizedString(@"Cost Out (1M)", nil)];
  [[completionColumn headerCell] setAlignment:XPTextAlignmentRight];
  [completionColumn setWidth:104.0];
  [completionColumn setMinWidth:98.0];
  [completionColumn setEditable:NO];
  rightCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [rightCell setAlignment:XPTextAlignmentRight];
  [completionColumn setDataCell:rightCell];
  [modelTableView_ addTableColumn:completionColumn];

  [scrollView setDocumentView:modelTableView_];
  [view addSubview:scrollView];
  return view;
}

- (NSView *)systemPromptPaneWithFrame:(NSRect)frame
{
  NSView *view;
  NSScrollView *scrollView;

  view = [[[NSView alloc] initWithFrame:frame] autorelease];
  [view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

  scrollView = [[[NSScrollView alloc]
      initWithFrame:NSInsetRect([view bounds],
                                kStrappyPreferencesInset,
                                kStrappyPreferencesInset)] autorelease];
  [scrollView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [scrollView setBorderType:NSBezelBorder];
  [scrollView setHasVerticalScroller:YES];
  [scrollView setHasHorizontalScroller:NO];
  [scrollView setAutohidesScrollers:YES];

  systemPromptTextView_ =
    [[NSTextView alloc] initWithFrame:[[scrollView contentView] bounds]];
  [systemPromptTextView_ setMinSize:NSMakeSize(0.0, 0.0)];
  [systemPromptTextView_ setMaxSize:NSMakeSize(100000.0, 100000.0)];
  [systemPromptTextView_ setVerticallyResizable:YES];
  [systemPromptTextView_ setHorizontallyResizable:NO];
  [systemPromptTextView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [[systemPromptTextView_ textContainer] setWidthTracksTextView:YES];
  [systemPromptTextView_ setEditable:NO];
  [systemPromptTextView_ setSelectable:YES];
  [systemPromptTextView_ setRichText:NO];
  [systemPromptTextView_ setFont:[NSFont userFixedPitchFontOfSize:12.0]];
  [systemPromptTextView_ setString:@""];

  [scrollView setDocumentView:systemPromptTextView_];
  [view addSubview:scrollView];
  return view;
}

- (NSView *)databaseScanningPaneWithFrame:(NSRect)frame
{
  NSView *view;
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

  view = [[[NSView alloc] initWithFrame:frame] autorelease];
  [view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  topY = NSMaxY([view bounds]) - kStrappyPreferencesInset - 24.0;

  scanButton_ = [[NSButton alloc]
      initWithFrame:NSMakeRect(kStrappyPreferencesInset, topY, 128.0, 24.0)];
  [scanButton_ setAutoresizingMask:NSViewMaxXMargin | NSViewMinYMargin];
  [scanButton_ setTitle:NSLocalizedString(@"Scan Databases", nil)];
  [scanButton_ setBezelStyle:XPBezelStyleRounded];
  [scanButton_ setButtonType:XPButtonTypeMomentaryLight];
  [scanButton_ setToolTip:
    NSLocalizedString(@"Scan your home folder for SQLite databases.", nil)];
  [scanButton_ setTarget:self];
  [scanButton_ setAction:@selector(scanDatabases:)];
  [view addSubview:scanButton_];

  scanProgressIndicator_ = [[NSProgressIndicator alloc]
      initWithFrame:NSMakeRect(NSMaxX([scanButton_ frame]) + 10.0,
                               topY + 2.0,
                               20.0,
                               20.0)];
  [scanProgressIndicator_ setAutoresizingMask:NSViewMaxXMargin | NSViewMinYMargin];
  [scanProgressIndicator_ setStyle:XPProgressIndicatorStyleSpinning];
  [scanProgressIndicator_ setIndeterminate:YES];
  [scanProgressIndicator_ setDisplayedWhenStopped:NO];
  [view addSubview:scanProgressIndicator_];

  scrollView = [[[NSScrollView alloc]
      initWithFrame:NSMakeRect(kStrappyPreferencesInset,
                               kStrappyPreferencesInset,
                               NSWidth([view bounds]) - (kStrappyPreferencesInset * 2.0),
                               topY - (kStrappyPreferencesInset * 2.0))]
      autorelease];
  [scrollView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [scrollView setBorderType:NSBezelBorder];
  [scrollView setHasVerticalScroller:YES];
  [scrollView setHasHorizontalScroller:NO];
  [scrollView setAutohidesScrollers:YES];

  databaseTableView_ =
    [[StrappyDatabaseTableView alloc] initWithFrame:[[scrollView contentView] bounds]];
  [databaseTableView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [databaseTableView_ setDataSource:self];
  [databaseTableView_ setDelegate:self];
  [databaseTableView_ setAllowsMultipleSelection:YES];
  [databaseTableView_ setUsesAlternatingRowBackgroundColors:YES];
  [databaseTableView_ setRowHeight:22.0];
  [databaseTableView_ setColumnAutoresizingStyle:NSTableViewSequentialColumnAutoresizingStyle];

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
  [databaseTableView_ addTableColumn:allowedColumn];

  nameColumn = [[[NSTableColumn alloc] initWithIdentifier:@"name"] autorelease];
  [[nameColumn headerCell] setStringValue:NSLocalizedString(@"Database", nil)];
  [nameColumn setWidth:210.0];
  [nameColumn setMinWidth:120.0];
  [nameColumn setEditable:NO];
  nameCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [nameCell setLineBreakMode:NSLineBreakByTruncatingTail];
  [nameColumn setDataCell:nameCell];
  [databaseTableView_ addTableColumn:nameColumn];

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
  [databaseTableView_ addTableColumn:locationColumn];

  sizeColumn = [[[NSTableColumn alloc] initWithIdentifier:@"size"] autorelease];
  [[sizeColumn headerCell] setStringValue:NSLocalizedString(@"Size", nil)];
  [sizeColumn setWidth:76.0];
  [sizeColumn setMinWidth:66.0];
  [sizeColumn setEditable:NO];
  sizeCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [sizeCell setAlignment:XPTextAlignmentRight];
  [sizeColumn setDataCell:sizeCell];
  [databaseTableView_ addTableColumn:sizeColumn];

  [scrollView setDocumentView:databaseTableView_];
  [view addSubview:scrollView];
  return view;
}

- (NSTextField *)labelWithFrame:(NSRect)frame text:(NSString *)text
{
  NSTextField *label;

  label = [[[NSTextField alloc] initWithFrame:frame] autorelease];
  [label setStringValue:(text != nil) ? text : @""];
  [label setBezeled:NO];
  [label setDrawsBackground:NO];
  [label setEditable:NO];
  [label setSelectable:NO];
  [label setFont:[NSFont systemFontOfSize:13.0]];
  return label;
}

- (void)refreshAPITokenStatusWithSaved:(BOOL)saved
{
  NSString *message;

  if (apiTokenStatusLabel_ == nil) {
    return;
  }

  if (saved) {
    message = NSLocalizedString(@"API credentials saved to keychain.", nil);
  } else if ([[StrappyKeychain sharedKeychain] hasAPICredentials]) {
    message = NSLocalizedString(@"API credentials are available.", nil);
  } else {
    message = NSLocalizedString(@"No API credentials are saved in the keychain.", nil);
  }
  [apiTokenStatusLabel_ setStringValue:message];
}

- (void)loadSystemPrompt
{
  NSString *path;
  NSString *prompt;

  path = [[NSBundle mainBundle] pathForResource:@"PromptSystem" ofType:@"txt"];
  if ([path length] == 0U) {
    [systemPromptTextView_ setString:
      NSLocalizedString(@"System prompt template is missing from the app bundle.", nil)];
    return;
  }

  prompt = [NSString stringWithContentsOfFile:path
                                     encoding:NSUTF8StringEncoding
                                        error:nil];
  if (prompt == nil) {
    prompt = NSLocalizedString(@"System prompt template could not be read.", nil);
  }
  [systemPromptTextView_ setString:prompt];
}

- (NSString *)currentModelSearchText
{
  NSString *searchText;

  if (modelSearchField_ == nil) {
    return nil;
  }

  searchText = [[modelSearchField_ stringValue]
    stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
  if ([searchText length] == 0U) {
    return nil;
  }
  return searchText;
}

- (void)loadOpenRouterModels
{
  NSArray *rows;
  NSString *searchText;

  searchText = [self currentModelSearchText];
  rows = [StrappySession openRouterModelCatalogMatchingSearchText:searchText
                                                            error:nil];
  if (rows != nil) {
    [modelRows_ release];
    modelRows_ = [rows copy];
    [modelTableView_ reloadData];
    [self selectCurrentOpenRouterModelRow];
    if (!refreshingModels_) {
      if ([modelRows_ count] == 0U) {
        if ([searchText length] > 0U) {
          [modelStatusLabel_ setStringValue:
            NSLocalizedString(@"No matching models.", nil)];
        } else {
          [modelStatusLabel_ setStringValue:
            NSLocalizedString(@"No models have been fetched yet.", nil)];
        }
      } else {
        if ([searchText length] > 0U) {
          [modelStatusLabel_ setStringValue:
            [NSString stringWithFormat:NSLocalizedString(@"%lu models shown.", nil),
              (unsigned long)[modelRows_ count]]];
        } else {
          [modelStatusLabel_ setStringValue:
            [NSString stringWithFormat:NSLocalizedString(@"%lu models available.", nil),
              (unsigned long)[modelRows_ count]]];
        }
      }
    }
    return;
  }

  [modelStatusLabel_ setStringValue:
    NSLocalizedString(@"Model list could not be loaded.", nil)];
}

- (void)modelSearchChanged:(id)sender
{
  (void)sender;
  [self loadOpenRouterModels];
}

- (void)modelSearchTextDidChange:(NSNotification *)notification
{
  if ([notification object] == modelSearchField_) {
    [self loadOpenRouterModels];
  }
}

- (void)selectCurrentOpenRouterModelRow
{
  NSInteger selectedRow;
  NSUInteger index;

  if (modelTableView_ == nil) {
    return;
  }

  selectedRow = -1;
  for (index = 0U; index < [modelRows_ count]; index++) {
    NSDictionary *row;
    NSNumber *selected;

    row = [modelRows_ objectAtIndex:index];
    selected = [row objectForKey:@"selected"];
    if ([selected isKindOfClass:[NSNumber class]] && [selected boolValue]) {
      selectedRow = (NSInteger)index;
      break;
    }
  }

  suppressingModelSelectionChange_ = YES;
  if (selectedRow >= 0) {
    [modelTableView_ selectRowIndexes:
      [NSIndexSet indexSetWithIndex:(NSUInteger)selectedRow]
                   byExtendingSelection:NO];
    [modelTableView_ scrollRowToVisible:selectedRow];
  } else {
    [modelTableView_ deselectAll:self];
  }
  suppressingModelSelectionChange_ = NO;
}

- (void)beginOpenRouterModelChangeSheetForRow:(NSDictionary *)row
{
  NSAlert *alert;
  NSString *modelName;
  NSString *modelId;

  modelName = StrappyModelDisplayNameForRow(row);
  modelId = StrappyStringForModelRow(row, @"id");
  if ([modelId length] == 0U) {
    [self selectCurrentOpenRouterModelRow];
    return;
  }

  if (pendingOpenRouterModelIdentifier_ != nil) {
    return;
  }

  alert = [[[NSAlert alloc] init] autorelease];
  [alert setMessageText:NSLocalizedString(@"Change model?", nil)];
  [alert setInformativeText:
    [NSString stringWithFormat:
      NSLocalizedString(@"Use %@ for future prompt requests?\n%@", nil),
      ([modelName length] > 0U) ? modelName : modelId,
      modelId]];
  [alert addButtonWithTitle:NSLocalizedString(@"Change Model", nil)];
  [alert addButtonWithTitle:NSLocalizedString(@"Cancel", nil)];

  pendingOpenRouterModelIdentifier_ = [modelId copy];
  [alert XP_beginSheetModalForWindow:[self window]
                       modalDelegate:self
                      didEndSelector:@selector(openRouterModelChangeSheetDidEnd:returnCode:contextInfo:)
                         contextInfo:NULL];
}

- (void)openRouterModelChangeSheetDidEnd:(NSAlert *)alert
                              returnCode:(NSInteger)returnCode
                             contextInfo:(void *)contextInfo
{
  NSString *modelId;

  (void)alert;
  (void)contextInfo;
  modelId = [pendingOpenRouterModelIdentifier_ copy];
  [pendingOpenRouterModelIdentifier_ release];
  pendingOpenRouterModelIdentifier_ = nil;

  if ((returnCode == NSAlertFirstButtonReturn) &&
      ([modelId length] > 0U)) {
    if (![StrappySession setSelectedOpenRouterModelIdentifier:modelId
                                                       error:nil]) {
      NSBeep();
      [self selectCurrentOpenRouterModelRow];
    }
  } else {
    [self selectCurrentOpenRouterModelRow];
  }

  [modelId release];
}

- (void)setModelCatalogRefreshing:(BOOL)refreshing
{
  refreshingModels_ = refreshing;
  [fetchModelsButton_ setEnabled:(refreshingModels_ ? NO : YES)];
  if (refreshingModels_) {
    [modelProgressIndicator_ startAnimation:self];
    [modelStatusLabel_ setStringValue:NSLocalizedString(@"Fetching models...", nil)];
  } else {
    [modelProgressIndicator_ stopAnimation:self];
  }
}

- (void)refreshModels:(id)sender
{
  NSError *error;
  NSString *message;
  NSAlert *alert;

  (void)sender;
  if (refreshingModels_) {
    return;
  }

  error = nil;
  if (![StrappySession beginOpenRouterModelCatalogRefreshWithError:&error]) {
    message = [error localizedDescription];
    if ([message length] == 0U) {
      message = NSLocalizedString(@"Model refresh could not start.", nil);
    }
    alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:NSLocalizedString(@"Could not fetch models", nil)];
    [alert setInformativeText:message];
    [alert runModal];
    return;
  }

  [self setModelCatalogRefreshing:YES];
}

- (void)modelCatalogRefreshDidStart:(NSNotification *)notification
{
  (void)notification;
  [self setModelCatalogRefreshing:YES];
}

- (void)modelCatalogRefreshDidFinish:(NSNotification *)notification
{
  NSDictionary *userInfo;
  NSString *errorMessage;
  NSNumber *count;

  userInfo = [notification userInfo];
  errorMessage = [userInfo objectForKey:@"error"];
  [self setModelCatalogRefreshing:NO];
  if ([errorMessage isKindOfClass:[NSString class]] &&
      ([errorMessage length] > 0U)) {
    [modelStatusLabel_ setStringValue:errorMessage];
    NSBeep();
    return;
  }

  [self loadOpenRouterModels];
  count = [userInfo objectForKey:@"model_count"];
  if (([self currentModelSearchText] == nil) &&
      [count isKindOfClass:[NSNumber class]]) {
    [modelStatusLabel_ setStringValue:
      [NSString stringWithFormat:NSLocalizedString(@"%lu models available.", nil),
        (unsigned long)[count unsignedIntegerValue]]];
  }
}

- (void)modelCatalogDidChange:(NSNotification *)notification
{
  (void)notification;
  [self loadOpenRouterModels];
}

- (void)loadCatalogedDatabases
{
  NSArray *rows;

  rows = [[FileScanner sharedScanner] catalogedSQLiteDatabasesWithError:nil];
  if (rows != nil) {
    [databaseRows_ release];
    databaseRows_ = [rows copy];
    [databaseTableView_ reloadData];
    return;
  }

  NSBeep();
}

- (void)setScanning:(BOOL)scanning
{
  scanning_ = scanning;
  [scanButton_ setEnabled:(scanning_ ? NO : YES)];
  if (scanning_) {
    [scanProgressIndicator_ startAnimation:self];
  } else {
    [scanProgressIndicator_ stopAnimation:self];
  }
}

- (void)saveAPICredentials:(id)sender
{
  NSString *apiEndpoint;
  NSString *apiToken;
  NSAlert *alert;

  (void)sender;
  apiEndpoint = [[apiEndpointField_ stringValue]
    stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
  apiToken = [[apiTokenField_ stringValue]
    stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
  if (([apiEndpoint length] == 0U) || ([apiToken length] == 0U)) {
    alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:NSLocalizedString(@"API credentials are required", nil)];
    [alert setInformativeText:NSLocalizedString(
      @"Enter an API endpoint and token before saving them to the keychain.", nil)];
    [alert runModal];
    return;
  }

  if (![[StrappyKeychain sharedKeychain] saveAPIEndpoint:apiEndpoint
                                                   token:apiToken]) {
    alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:NSLocalizedString(@"Could not save API credentials", nil)];
    [alert setInformativeText:NSLocalizedString(
      @"The keychain refused the write.", nil)];
    [alert runModal];
    return;
  }

  [apiEndpointField_ setStringValue:apiEndpoint];
  [apiTokenField_ setStringValue:apiToken];
  [self refreshAPITokenStatusWithSaved:YES];
}

- (void)scanDatabases:(id)sender
{
  NSString *rootPath;

  (void)sender;
  if (scanning_) {
    return;
  }

  rootPath = [NSHomeDirectory() copy];
  [self setScanning:YES];
  [self retain];
  [NSThread detachNewThreadSelector:@selector(scanDatabasesInBackground:)
                           toTarget:self
                         withObject:rootPath];
  [rootPath release];
}

- (void)scanDatabasesInBackground:(NSString *)rootPath
{
  NSAutoreleasePool *pool;
  NSArray *rows;
  NSMutableDictionary *result;

  pool = [[NSAutoreleasePool alloc] init];
  rows = [[FileScanner sharedScanner] scanDirectoryForSQLiteDatabasesAtPath:rootPath
                                            savingResultsToCatalogWithError:nil];
  result = [[NSMutableDictionary alloc] init];
  if (rows != nil) {
    [result setObject:rows forKey:@"rows"];
  }

  [self performSelectorOnMainThread:@selector(scanDatabasesDidFinish:)
                         withObject:result
                      waitUntilDone:NO];
  [result release];
  [pool release];
  [self release];
}

- (void)scanDatabasesDidFinish:(NSDictionary *)result
{
  NSArray *rows;

  rows = [result objectForKey:@"rows"];
  if ([rows isKindOfClass:[NSArray class]]) {
    [databaseRows_ release];
    databaseRows_ = [rows copy];
    [databaseTableView_ reloadData];
  } else {
    NSBeep();
  }

  [self setScanning:NO];
}

- (void)databaseTableViewDidPressSpace:(NSTableView *)tableView
{
  NSIndexSet *selectedRows;
  NSUInteger rowIndex;
  NSDictionary *database;
  NSNumber *catalogId;
  BOOL shouldAllow;
  NSUInteger eligibleCount;

  (void)tableView;
  selectedRows = [databaseTableView_ selectedRowIndexes];
  if ([selectedRows count] == 0U) {
    return;
  }

  shouldAllow = NO;
  eligibleCount = 0U;
  for (rowIndex = [selectedRows firstIndex];
       rowIndex != NSNotFound;
       rowIndex = [selectedRows indexGreaterThanIndex:rowIndex]) {
    if (rowIndex >= [databaseRows_ count]) {
      continue;
    }

    database = [databaseRows_ objectAtIndex:rowIndex];
    if (![self databaseRowCanBeAllowed:database]) {
      continue;
    }

    eligibleCount++;
    if (![[self allowedValueForDatabaseRow:database] boolValue]) {
      shouldAllow = YES;
    }
  }

  if (eligibleCount == 0U) {
    NSBeep();
    return;
  }

  for (rowIndex = [selectedRows firstIndex];
       rowIndex != NSNotFound;
       rowIndex = [selectedRows indexGreaterThanIndex:rowIndex]) {
    if (rowIndex >= [databaseRows_ count]) {
      continue;
    }

    database = [databaseRows_ objectAtIndex:rowIndex];
    if (![self databaseRowCanBeAllowed:database]) {
      continue;
    }
    if ([[self allowedValueForDatabaseRow:database] boolValue] == shouldAllow) {
      continue;
    }

    catalogId = [database objectForKey:@"catalog_id"];
    if (![[FileScanner sharedScanner] setCatalogedDatabaseAllowed:shouldAllow
                                             forCatalogIdentifier:catalogId
                                                            error:nil]) {
      NSBeep();
      [databaseTableView_ reloadData];
      return;
    }
  }

  [self loadCatalogedDatabases];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView
{
  if (tableView == modelTableView_) {
    return (NSInteger)[modelRows_ count];
  }
  return (NSInteger)[databaseRows_ count];
}

- (id)tableView:(NSTableView *)tableView
    objectValueForTableColumn:(NSTableColumn *)tableColumn
                          row:(NSInteger)row
{
  NSDictionary *database;
  NSDictionary *model;
  NSString *identifier;

  if (tableView == modelTableView_) {
    if ((row < 0) || (row >= (NSInteger)[modelRows_ count])) {
      return nil;
    }

    model = [modelRows_ objectAtIndex:(NSUInteger)row];
    identifier = [tableColumn identifier];
    if ([identifier isEqualToString:@"model_name"]) {
      return StrappyModelDisplayNameForRow(model);
    }
    if ([identifier isEqualToString:@"model_id"]) {
      return StrappyStringForModelRow(model, @"id");
    }
    if ([identifier isEqualToString:@"model_context"]) {
      return StrappyModelNumberString(model, @"context_length");
    }
    if ([identifier isEqualToString:@"model_prompt_price"]) {
      return StrappyModelPricingString(model, @"pricing_prompt");
    }
    if ([identifier isEqualToString:@"model_completion_price"]) {
      return StrappyModelPricingString(model, @"pricing_completion");
    }
    return nil;
  }

  if ((row < 0) || (row >= (NSInteger)[databaseRows_ count])) {
    return nil;
  }

  database = [databaseRows_ objectAtIndex:(NSUInteger)row];
  identifier = [tableColumn identifier];
  if ([identifier isEqualToString:@"allowed"]) {
    return [self allowedValueForDatabaseRow:database];
  }
  if ([identifier isEqualToString:@"name"]) {
    return StrappyDatabaseNameForRow(database);
  }
  if ([identifier isEqualToString:@"location"]) {
    return StrappyDatabaseLocationForRow(database);
  }
  if ([identifier isEqualToString:@"size"]) {
    return StrappyByteCountString([database objectForKey:@"size"]);
  }

  return nil;
}

- (NSString *)tableView:(NSTableView *)tableView
         toolTipForCell:(NSCell *)cell
                   rect:(NSRectPointer)rect
            tableColumn:(NSTableColumn *)tableColumn
                    row:(NSInteger)row
          mouseLocation:(NSPoint)mouseLocation
{
  NSDictionary *database;
  NSDictionary *model;
  NSString *identifier;
  NSString *validationError;

  (void)cell;
  (void)rect;
  (void)mouseLocation;
  if (tableView == modelTableView_) {
    if ((row < 0) || (row >= (NSInteger)[modelRows_ count])) {
      return nil;
    }

    model = [modelRows_ objectAtIndex:(NSUInteger)row];
    identifier = [tableColumn identifier];
    if ([identifier isEqualToString:@"model_name"]) {
      NSString *description;

      description = StrappyStringForModelRow(model, @"description");
      if ([description length] > 0U) {
        return description;
      }
      return StrappyStringForModelRow(model, @"id");
    }
    if ([identifier isEqualToString:@"model_id"]) {
      return StrappyStringForModelRow(model, @"id");
    }
    return nil;
  }

  if ((row < 0) || (row >= (NSInteger)[databaseRows_ count])) {
    return nil;
  }

  database = [databaseRows_ objectAtIndex:(NSUInteger)row];
  identifier = [tableColumn identifier];
  if ([identifier isEqualToString:@"allowed"] &&
      ![self databaseRowCanBeAllowed:database]) {
    validationError = [database objectForKey:@"validation_error"];
    if ([validationError isKindOfClass:[NSString class]] &&
        ([validationError length] > 0U)) {
      return validationError;
    }
    return nil;
  }
  if ([identifier isEqualToString:@"name"] ||
      [identifier isEqualToString:@"location"]) {
    return StrappyDatabasePathForRow(database);
  }

  return nil;
}

- (void)tableViewSelectionDidChange:(NSNotification *)notification
{
  NSTableView *tableView;
  NSInteger row;
  NSDictionary *model;
  NSNumber *selected;
  NSString *modelId;

  tableView = [notification object];
  if (tableView != modelTableView_) {
    return;
  }
  if (suppressingModelSelectionChange_) {
    return;
  }
  if (pendingOpenRouterModelIdentifier_ != nil) {
    return;
  }

  row = [modelTableView_ selectedRow];
  if ((row < 0) || (row >= (NSInteger)[modelRows_ count])) {
    [self selectCurrentOpenRouterModelRow];
    return;
  }

  model = [modelRows_ objectAtIndex:(NSUInteger)row];
  selected = [model objectForKey:@"selected"];
  if ([selected isKindOfClass:[NSNumber class]] && [selected boolValue]) {
    return;
  }

  modelId = StrappyStringForModelRow(model, @"id");
  if ([modelId length] == 0U) {
    [self selectCurrentOpenRouterModelRow];
    return;
  }

  [self beginOpenRouterModelChangeSheetForRow:model];
}

- (void)tableView:(NSTableView *)tableView
   setObjectValue:(id)object
   forTableColumn:(NSTableColumn *)tableColumn
              row:(NSInteger)row
{
  NSDictionary *database;
  NSString *identifier;
  NSNumber *catalogId;
  BOOL allowed;

  (void)tableView;
  if ((row < 0) || (row >= (NSInteger)[databaseRows_ count])) {
    return;
  }

  identifier = [tableColumn identifier];
  if (![identifier isEqualToString:@"allowed"]) {
    return;
  }

  database = [databaseRows_ objectAtIndex:(NSUInteger)row];
  allowed = ([object respondsToSelector:@selector(boolValue)] &&
             [object boolValue]) ? YES : NO;
  if (allowed && ![self databaseRowCanBeAllowed:database]) {
    NSBeep();
    [databaseTableView_ reloadData];
    return;
  }

  catalogId = [database objectForKey:@"catalog_id"];
  if (![[FileScanner sharedScanner] setCatalogedDatabaseAllowed:allowed
                                           forCatalogIdentifier:catalogId
                                                          error:nil]) {
    NSBeep();
    [databaseTableView_ reloadData];
    return;
  }

  [self loadCatalogedDatabases];
}

- (BOOL)tableView:(NSTableView *)tableView
 shouldEditTableColumn:(NSTableColumn *)tableColumn
              row:(NSInteger)row
{
  NSDictionary *database;

  (void)tableView;
  if (![[tableColumn identifier] isEqualToString:@"allowed"]) {
    return NO;
  }
  if ((row < 0) || (row >= (NSInteger)[databaseRows_ count])) {
    return NO;
  }

  database = [databaseRows_ objectAtIndex:(NSUInteger)row];
  return [self databaseRowCanBeAllowed:database];
}

- (void)tableView:(NSTableView *)tableView
  willDisplayCell:(id)cell
   forTableColumn:(NSTableColumn *)tableColumn
              row:(NSInteger)row
{
  NSDictionary *database;

  (void)tableView;
  if (![[tableColumn identifier] isEqualToString:@"allowed"] ||
      ![cell respondsToSelector:@selector(setEnabled:)]) {
    return;
  }
  if ((row < 0) || (row >= (NSInteger)[databaseRows_ count])) {
    [cell setEnabled:NO];
    return;
  }

  database = [databaseRows_ objectAtIndex:(NSUInteger)row];
  [cell setEnabled:[self databaseRowCanBeAllowed:database]];
}

- (BOOL)databaseRowCanBeAllowed:(NSDictionary *)row
{
  NSNumber *valid;

  valid = [row objectForKey:@"is_valid_sqlite"];
  return ([valid isKindOfClass:[NSNumber class]] && [valid boolValue]) ? YES : NO;
}

- (NSNumber *)allowedValueForDatabaseRow:(NSDictionary *)row
{
  NSString *decision;

  decision = [row objectForKey:@"user_decision"];
  return [NSNumber numberWithBool:[decision isEqualToString:@"allowed"]];
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [tabView_ release];
  [apiEndpointField_ release];
  [apiTokenField_ release];
  [apiTokenStatusLabel_ release];
  [modelSearchField_ release];
  [modelTableView_ release];
  [fetchModelsButton_ release];
  [modelProgressIndicator_ release];
  [modelStatusLabel_ release];
  [systemPromptTextView_ release];
  [databaseTableView_ release];
  [scanButton_ release];
  [scanProgressIndicator_ release];
  [modelRows_ release];
  [databaseRows_ release];
  [pendingOpenRouterModelIdentifier_ release];
  [super dealloc];
}

@end
