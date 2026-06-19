#import "PreferencesWindowController.h"

#import "FileScanner.h"

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

@interface PreferencesWindowController ()
- (void)buildContentView;
- (NSView *)systemPromptPaneWithFrame:(NSRect)frame;
- (NSView *)databaseScanningPaneWithFrame:(NSRect)frame;
- (void)loadSystemPrompt;
- (void)setScanning:(BOOL)scanning;
- (void)scanDatabasesInBackground:(NSString *)rootPath;
- (void)scanDatabasesDidFinish:(NSDictionary *)result;
- (NSString *)statusForDatabaseRow:(NSDictionary *)row;
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
    databaseRows_ = [[NSArray alloc] init];
    [self buildContentView];
    [self loadSystemPrompt];
    [self setScanning:NO];
  }

  [window release];
  return self;
}

- (void)buildContentView
{
  NSView *contentView;
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

  systemPromptItem =
    [[[NSTabViewItem alloc] initWithIdentifier:@"system_prompt"] autorelease];
  [systemPromptItem setLabel:NSLocalizedString(@"System Prompt", nil)];
  [systemPromptItem setView:[self systemPromptPaneWithFrame:paneFrame]];
  [tabView_ addTabViewItem:systemPromptItem];

  databaseItem =
    [[[NSTabViewItem alloc] initWithIdentifier:@"databases"] autorelease];
  [databaseItem setLabel:NSLocalizedString(@"Databases", nil)];
  [databaseItem setView:[self databaseScanningPaneWithFrame:paneFrame]];
  [tabView_ addTabViewItem:databaseItem];

  [contentView addSubview:tabView_];
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
  NSTableColumn *pathColumn;
  NSTableColumn *statusColumn;
  NSTableColumn *sizeColumn;
  NSScrollView *scrollView;
  CGFloat topY;

  view = [[[NSView alloc] initWithFrame:frame] autorelease];
  [view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  topY = NSMaxY([view bounds]) - kStrappyPreferencesInset - 24.0;

  scanButton_ = [[NSButton alloc]
      initWithFrame:NSMakeRect(kStrappyPreferencesInset, topY, 110.0, 24.0)];
  [scanButton_ setAutoresizingMask:NSViewMaxXMargin | NSViewMinYMargin];
  [scanButton_ setTitle:NSLocalizedString(@"Scan Home", nil)];
  [scanButton_ setBezelStyle:XPBezelStyleRounded];
  [scanButton_ setButtonType:XPButtonTypeMomentaryLight];
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

  scanStatusField_ = [[NSTextField alloc]
      initWithFrame:NSMakeRect(NSMaxX([scanProgressIndicator_ frame]) + 8.0,
                               topY + 2.0,
                               NSWidth([view bounds]) - NSMaxX([scanProgressIndicator_ frame]) - 20.0,
                               20.0)];
  [scanStatusField_ setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
  [scanStatusField_ setEditable:NO];
  [scanStatusField_ setSelectable:NO];
  [scanStatusField_ setBordered:NO];
  [scanStatusField_ setDrawsBackground:NO];
  [scanStatusField_ setFont:[NSFont systemFontOfSize:11.0]];
  [scanStatusField_ setTextColor:[NSColor disabledControlTextColor]];
  [scanStatusField_ setStringValue:NSLocalizedString(@"Ready", nil)];
  [view addSubview:scanStatusField_];

  scrollView = [[[NSScrollView alloc]
      initWithFrame:NSMakeRect(kStrappyPreferencesInset,
                               kStrappyPreferencesInset,
                               NSWidth([view bounds]) - (kStrappyPreferencesInset * 2.0),
                               topY - (kStrappyPreferencesInset * 2.0))]
      autorelease];
  [scrollView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [scrollView setBorderType:NSBezelBorder];
  [scrollView setHasVerticalScroller:YES];
  [scrollView setHasHorizontalScroller:YES];
  [scrollView setAutohidesScrollers:YES];

  databaseTableView_ = [[NSTableView alloc] initWithFrame:[[scrollView contentView] bounds]];
  [databaseTableView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [databaseTableView_ setDataSource:self];
  [databaseTableView_ setDelegate:self];
  [databaseTableView_ setUsesAlternatingRowBackgroundColors:YES];
  [databaseTableView_ setRowHeight:22.0];

  pathColumn = [[[NSTableColumn alloc] initWithIdentifier:@"path"] autorelease];
  [[pathColumn headerCell] setStringValue:NSLocalizedString(@"Path", nil)];
  [pathColumn setWidth:430.0];
  [databaseTableView_ addTableColumn:pathColumn];

  statusColumn = [[[NSTableColumn alloc] initWithIdentifier:@"status"] autorelease];
  [[statusColumn headerCell] setStringValue:NSLocalizedString(@"Status", nil)];
  [statusColumn setWidth:140.0];
  [databaseTableView_ addTableColumn:statusColumn];

  sizeColumn = [[[NSTableColumn alloc] initWithIdentifier:@"size"] autorelease];
  [[sizeColumn headerCell] setStringValue:NSLocalizedString(@"Size", nil)];
  [sizeColumn setWidth:100.0];
  [databaseTableView_ addTableColumn:sizeColumn];

  [scrollView setDocumentView:databaseTableView_];
  [view addSubview:scrollView];
  return view;
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

- (void)setScanning:(BOOL)scanning
{
  scanning_ = scanning;
  [scanButton_ setEnabled:(scanning_ ? NO : YES)];
  if (scanning_) {
    [scanProgressIndicator_ startAnimation:self];
    [scanStatusField_ setStringValue:NSLocalizedString(@"Scanning...", nil)];
  } else {
    [scanProgressIndicator_ stopAnimation:self];
  }
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
  NSError *error;
  NSArray *rows;
  NSMutableDictionary *result;
  NSString *errorMessage;

  pool = [[NSAutoreleasePool alloc] init];
  error = nil;
  rows = [[FileScanner sharedScanner] scanDirectoryForSQLiteDatabasesAtPath:rootPath
                                                                      error:&error];
  result = [[NSMutableDictionary alloc] init];
  if (rows != nil) {
    [result setObject:rows forKey:@"rows"];
  } else {
    errorMessage = [error localizedDescription];
    if ([errorMessage length] == 0U) {
      errorMessage = NSLocalizedString(@"Scan failed.", nil);
    }
    [result setObject:errorMessage forKey:@"error"];
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
  NSString *errorMessage;

  rows = [result objectForKey:@"rows"];
  if ([rows isKindOfClass:[NSArray class]]) {
    [databaseRows_ release];
    databaseRows_ = [rows copy];
    [databaseTableView_ reloadData];
    [scanStatusField_ setStringValue:
      [NSString stringWithFormat:NSLocalizedString(@"Found %lu SQLite candidate(s).", nil),
        (unsigned long)[databaseRows_ count]]];
  } else {
    errorMessage = [result objectForKey:@"error"];
    if (![errorMessage isKindOfClass:[NSString class]]) {
      errorMessage = NSLocalizedString(@"Scan failed.", nil);
    }
    [scanStatusField_ setStringValue:errorMessage];
  }

  [self setScanning:NO];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView
{
  (void)tableView;
  return (NSInteger)[databaseRows_ count];
}

- (id)tableView:(NSTableView *)tableView
    objectValueForTableColumn:(NSTableColumn *)tableColumn
                          row:(NSInteger)row
{
  NSDictionary *database;
  NSString *identifier;

  (void)tableView;
  if ((row < 0) || (row >= (NSInteger)[databaseRows_ count])) {
    return nil;
  }

  database = [databaseRows_ objectAtIndex:(NSUInteger)row];
  identifier = [tableColumn identifier];
  if ([identifier isEqualToString:@"path"]) {
    return [database objectForKey:@"path"];
  }
  if ([identifier isEqualToString:@"status"]) {
    return [self statusForDatabaseRow:database];
  }
  if ([identifier isEqualToString:@"size"]) {
    return StrappyByteCountString([database objectForKey:@"size"]);
  }

  return nil;
}

- (NSString *)statusForDatabaseRow:(NSDictionary *)row
{
  NSNumber *valid;
  NSString *validationError;

  valid = [row objectForKey:@"is_valid_sqlite"];
  if ([valid isKindOfClass:[NSNumber class]] && [valid boolValue]) {
    return NSLocalizedString(@"SQLite", nil);
  }

  validationError = [row objectForKey:@"validation_error"];
  if ([validationError isKindOfClass:[NSString class]] &&
      ([validationError length] > 0U)) {
    return NSLocalizedString(@"Invalid", nil);
  }
  return NSLocalizedString(@"Candidate", nil);
}

- (void)dealloc
{
  [tabView_ release];
  [systemPromptTextView_ release];
  [databaseTableView_ release];
  [scanButton_ release];
  [scanProgressIndicator_ release];
  [scanStatusField_ release];
  [databaseRows_ release];
  [super dealloc];
}

@end
