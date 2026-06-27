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
- (NSView *)systemPromptPaneWithFrame:(NSRect)frame;
- (NSView *)databaseScanningPaneWithFrame:(NSRect)frame;
- (void)loadSystemPrompt;
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
    databaseRows_ = [[NSArray alloc] init];
    [self buildContentView];
    [self loadSystemPrompt];
    [self setScanning:NO];
    [self loadCatalogedDatabases];
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
  NSString *identifier;
  NSString *validationError;

  (void)tableView;
  (void)cell;
  (void)rect;
  (void)mouseLocation;
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
  [tabView_ release];
  [systemPromptTextView_ release];
  [databaseTableView_ release];
  [scanButton_ release];
  [scanProgressIndicator_ release];
  [databaseRows_ release];
  [super dealloc];
}

@end
