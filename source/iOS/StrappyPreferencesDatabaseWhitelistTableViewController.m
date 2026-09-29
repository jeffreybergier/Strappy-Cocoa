#import "StrappyPreferencesDatabaseWhitelistTableViewController.h"

#import "AIFontAwesome.h"
#import "FileScanner.h"
#import "StrappyAppearance.h"
#import "StrappyPreferencesStatusToolbarView.h"

static const CGFloat kStrappyDatabaseHiddenIconCanvasSize = 24.0f;
static const CGFloat kStrappyDatabaseHiddenIconSize = 20.0f;

static UIImage *StrappyDatabaseHiddenIconImage(void)
{
  static UIImage *image = nil;

  if (image == nil) {
    image = [AIFontAwesome imageForIcon:AIFAEyeSlash
                                  style:AIFontAwesomeStyleRegular
                               iconSize:kStrappyDatabaseHiddenIconSize
                             canvasSize:kStrappyDatabaseHiddenIconCanvasSize
                                  color:[UIColor blackColor]
                                  scale:0.0f];
  }
  return image;
}

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
  return [path isKindOfClass:[NSString class]] ? path : @"";
}

static NSString *StrappyDatabaseNameForRow(NSDictionary *row)
{
  NSString *path;
  NSString *name;

  path = StrappyDatabasePathForRow(row);
  name = [path lastPathComponent];
  return ([name length] > 0U) ? name : path;
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

static BOOL StrappyDatabaseStringHasValue(NSString *string);

static NSString *StrappyDatabaseStringForRow(NSDictionary *row, NSString *key)
{
  NSString *value;

  value = [row objectForKey:key];
  return StrappyDatabaseStringHasValue(value) ? value : @"";
}

static NSString *StrappyDatabaseOriginTitleForRow(NSDictionary *row)
{
  NSString *originKind;

  originKind = StrappyDatabaseStringForRow(row, @"origin_kind");
  if ([originKind isEqualToString:@"app_bundle"]) {
    return NSLocalizedString(@"App Bundle", nil);
  }
  if ([originKind isEqualToString:@"documents"]) {
    return NSLocalizedString(@"Documents", nil);
  }
  if ([originKind isEqualToString:@"application_support"]) {
    return NSLocalizedString(@"Application Support", nil);
  }
  if ([originKind isEqualToString:@"app_library"]) {
    return NSLocalizedString(@"App Library", nil);
  }
  if ([originKind isEqualToString:@"system_library"]) {
    return NSLocalizedString(@"System Library", nil);
  }
  if ([originKind isEqualToString:@"media"]) {
    return NSLocalizedString(@"Media", nil);
  }
  if ([originKind isEqualToString:@"cache"]) {
    return NSLocalizedString(@"Cache", nil);
  }

  return NSLocalizedString(@"Other", nil);
}

static NSString *StrappyDatabaseLocationTailForRow(NSDictionary *row)
{
  NSString *locationTail;

  locationTail = StrappyDatabaseStringForRow(row, @"location_tail");
  return ([locationTail length] > 0U) ?
    locationTail : StrappyDatabaseLocationForRow(row);
}

static BOOL StrappyDatabaseStringHasValue(NSString *string)
{
  return ([string isKindOfClass:[NSString class]] && ([string length] > 0U)) ?
    YES : NO;
}

static BOOL StrappyDatabaseRowAllowedValue(NSDictionary *row)
{
  NSString *decision;

  decision = [row objectForKey:@"user_decision"];
  return [decision isEqualToString:@"allowed"];
}

static BOOL StrappyDatabaseRowHiddenValue(NSDictionary *row)
{
  NSNumber *hidden;

  hidden = [row objectForKey:@"hidden"];
  return ([hidden isKindOfClass:[NSNumber class]] && [hidden boolValue]) ?
    YES : NO;
}

@interface StrappyPreferencesDatabaseWhitelistTableViewController ()
  <UIActionSheetDelegate>
@property (nonatomic, assign) BOOL scanning;
@property (nonatomic, assign) BOOL hiddenMode;
@property (nonatomic, copy) NSArray *databaseSections;
@property (nonatomic, strong) UIBarButtonItem *scanButton;
- (void)loadCatalogUsingSnapshot:(BOOL)reuse;
- (void)scanButtonPressed:(id)sender;
- (void)updateHiddenModeButton;
- (void)beginDatabaseScanWithMode:(FileScannerDatabaseScanMode)scanMode;
- (void)databaseCatalogScanDidStart:(NSNotification *)notification;
- (void)databaseCatalogDidChange:(NSNotification *)notification;
- (void)databaseCatalogScanDidFinish:(NSNotification *)notification;
@end

@implementation StrappyPreferencesDatabaseWhitelistTableViewController

- (instancetype)init
{
  return [super initWithTitle:NSLocalizedString(@"Databases", nil)];
}

- (void)viewDidLoad
{
  UIBarButtonItem *scanButton;

  [super viewDidLoad];

  scanButton = [[UIBarButtonItem alloc]
    initWithTitle:NSLocalizedString(@"Scan", nil)
            style:UIBarButtonItemStyleBordered
           target:self
           action:@selector(scanButtonPressed:)];
  [self setScanButton:scanButton];
  [[self navigationItem] setRightBarButtonItem:scanButton];
  [StrappyAppearance applyLegacyTintToBarButtonItem:scanButton];

  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(databaseCatalogScanDidStart:)
           name:FileScannerDatabaseCatalogScanDidStartNotification
         object:nil];
  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(databaseCatalogDidChange:)
           name:FileScannerDatabaseCatalogDidChangeNotification
         object:nil];
  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(databaseCatalogScanDidFinish:)
           name:FileScannerDatabaseCatalogScanDidFinishNotification
         object:nil];

  [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(catalogReadFailed:)
    name:FileScannerCatalogReadFailedNotification object:nil];
  [self updateHiddenModeButton];
  [self setScanning:[FileScanner isDatabaseCatalogScanInFlight]];
}

- (void)setHiddenMode:(BOOL)hiddenMode
{
  if (_hiddenMode == hiddenMode) {
    return;
  }

  _hiddenMode = hiddenMode;
  [self updateHiddenModeButton];
  [self applyRows];
}

- (void)actionButtonPressed:(id)sender
{
  (void)sender;
  [self setHiddenMode:![self hiddenMode]];
}

- (void)updateHiddenModeButton
{
  [[self statusToolbarView]
    setActionButtonIcon:[self hiddenMode] ? AIFAEyeSlash : AIFAEye
                  style:AIFontAwesomeStyleRegular];
  [[self statusToolbarView]
    setActionAccessibilityLabel:[self actionButtonAccessibilityLabel]];
}

- (void)reloadRows
{
  [self loadCatalogUsingSnapshot:NO];
}

- (void)applyRows
{
  [self loadCatalogUsingSnapshot:YES];
}

- (void)searchBar:(UISearchBar *)searchBar textDidChange:(NSString *)searchText
{
  (void)searchBar; (void)searchText;
  [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(applyRows) object:nil];
  [self performSelector:@selector(applyRows) withObject:nil afterDelay:0.15];
}

- (void)searchBarSearchButtonClicked:(UISearchBar *)searchBar
{
  [self applyRows];
  [searchBar resignFirstResponder];
}

- (void)loadCatalogUsingSnapshot:(BOOL)reuse
{
  [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(applyRows) object:nil];
  NSError *error = nil;
  NSArray *descriptors = [NSArray arrayWithObjects:
    [[NSSortDescriptor alloc] initWithKey:@"application" ascending:YES],
    [[NSSortDescriptor alloc] initWithKey:@"group_key" ascending:YES],
    [[NSSortDescriptor alloc] initWithKey:@"database_priority" ascending:NO],
    [[NSSortDescriptor alloc] initWithKey:@"size" ascending:NO],
    [[NSSortDescriptor alloc] initWithKey:@"name" ascending:YES],nil];
  FileScannerCatalogRows *rows = nil;
  if (reuse && [[self rows] isKindOfClass:[FileScannerCatalogRows class]]) {
    rows = (FileScannerCatalogRows *)[self rows];
    if (![rows filterWithSearch:[self currentSearchText] showHidden:[self hiddenMode]
        sortDescriptors:descriptors error:&error]) rows = nil;
  } else {
    rows = [[FileScanner sharedScanner] catalogRowsMatchingSearch:[self currentSearchText]
      showHidden:[self hiddenMode] sortDescriptors:descriptors error:&error];
  }
  NSArray *sections = rows != nil ? [rows applicationSectionsWithError:&error] : nil;
  if (sections == nil) {
    [self setRows:[NSArray array]];
    [self setDatabaseSections:[NSArray array]];
    [[self tableView] reloadData];
    [self setStatusMessage:[error localizedDescription]];
  } else {
    [self setStatusMessage:nil];
    [self setRows:rows];
    [self setDatabaseSections:sections];
    [[self tableView] reloadData];
  }
  [self refreshStatusToolbar];
}

- (void)catalogReadFailed:(NSNotification *)notification
{
  if ([notification object] != [self rows]) return;
  [self setStatusMessage:[[[notification userInfo] objectForKey:@"error"] localizedDescription]];
  [self refreshStatusToolbar];
}

- (BOOL)databaseRowCanBeAllowed:(NSDictionary *)row
{
  NSNumber *valid;

  valid = [row objectForKey:@"is_valid_sqlite"];
  return ([valid isKindOfClass:[NSNumber class]] && [valid boolValue]) ? YES : NO;
}

- (BOOL)allowedValueForDatabaseRow:(NSDictionary *)row
{
  return StrappyDatabaseRowAllowedValue(row);
}

- (void)setScanning:(BOOL)scanning
{
  scanning = scanning ? YES : NO;
  if (_scanning == scanning) {
    [[self scanButton] setEnabled:scanning ? NO : YES];
    return;
  }

  _scanning = scanning;
  [[self scanButton] setEnabled:_scanning ? NO : YES];
  [self setWorking:_scanning];
  [[self tableView] reloadData];
  [self refreshStatusToolbar];
}

- (BOOL)rowIsSelected:(NSDictionary *)row
{
  return [self hiddenMode] ?
    StrappyDatabaseRowHiddenValue(row) :
    [self allowedValueForDatabaseRow:row];
}

- (NSString *)workingStatusText
{
  if ([self scanning]) {
    return NSLocalizedString(@"Scanning...", nil);
  }
  return nil;
}

- (NSString *)actionButtonAccessibilityLabel
{
  return [self hiddenMode] ?
    NSLocalizedString(@"Hide hidden databases", nil) :
    NSLocalizedString(@"Show hidden databases", nil);
}

- (void)configureCell:(UITableViewCell *)cell withRow:(NSDictionary *)row
{
  NSMutableArray *details;
  NSString *origin;
  NSString *locationTail;
  NSString *size;
  BOOL selected;

  details = [NSMutableArray array];
  origin = StrappyDatabaseOriginTitleForRow(row);
  locationTail = StrappyDatabaseLocationTailForRow(row);
  size = StrappyByteCountString([row objectForKey:@"size"]);
  if ([size length] > 0U) {
    [details addObject:size];
  }
  if ([origin length] > 0U) {
    [details addObject:origin];
  }
  if ([locationTail length] > 0U) {
    [details addObject:locationTail];
  }

  [[cell textLabel] setText:StrappyDatabaseNameForRow(row)];
  [[cell detailTextLabel] setText:[details componentsJoinedByString:@", "]];
  selected = [self rowIsSelected:row];
  [cell setAccessoryView:nil];
  if ([self hiddenMode]) {
    if (selected) {
      UIImageView *imageView;

      imageView = [[UIImageView alloc] initWithImage:StrappyDatabaseHiddenIconImage()];
      [imageView setFrame:CGRectMake(0.0f,
                                     0.0f,
                                     kStrappyDatabaseHiddenIconCanvasSize,
                                     kStrappyDatabaseHiddenIconCanvasSize)];
      [imageView setContentMode:UIViewContentModeCenter];
      [imageView setAccessibilityLabel:NSLocalizedString(@"Hidden", nil)];
      [cell setAccessoryView:imageView];
    }
    [cell setAccessoryType:UITableViewCellAccessoryNone];
  } else {
    [cell setAccessoryType:selected
      ? UITableViewCellAccessoryCheckmark
      : UITableViewCellAccessoryNone];
  }

  if (![self hiddenMode] &&
      (![self databaseRowCanBeAllowed:row] ||
       StrappyDatabaseRowHiddenValue(row))) {
    [[cell textLabel] setTextColor:[UIColor grayColor]];
    [[cell detailTextLabel] setTextColor:[UIColor grayColor]];
  }
}

- (NSDictionary *)databaseRowAtIndexPath:(NSIndexPath *)indexPath
{
  NSDictionary *section;
  NSArray *sectionRows;

  if (([indexPath section] < 0) ||
      ((NSUInteger)[indexPath section] >= [[self databaseSections] count])) {
    return nil;
  }

  section = [[self databaseSections] objectAtIndex:(NSUInteger)[indexPath section]];
  sectionRows = [section objectForKey:@"rows"];
  if (![sectionRows isKindOfClass:[NSArray class]] ||
      ([indexPath row] < 0) ||
      ((NSUInteger)[indexPath row] >= [sectionRows count])) {
    return nil;
  }

  return [sectionRows objectAtIndex:(NSUInteger)[indexPath row]];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
  (void)tableView;
  return (NSInteger)[[self databaseSections] count];
}

- (NSInteger)tableView:(UITableView *)tableView
 numberOfRowsInSection:(NSInteger)section
{
  NSDictionary *sectionInfo;
  NSArray *sectionRows;

  (void)tableView;
  if ((section < 0) ||
      ((NSUInteger)section >= [[self databaseSections] count])) {
    return 0;
  }

  sectionInfo = [[self databaseSections] objectAtIndex:(NSUInteger)section];
  sectionRows = [sectionInfo objectForKey:@"rows"];
  return [sectionRows isKindOfClass:[NSArray class]] ?
    (NSInteger)[sectionRows count] : 0;
}

- (NSString *)tableView:(UITableView *)tableView
titleForHeaderInSection:(NSInteger)section
{
  NSDictionary *sectionInfo;
  NSString *title;

  (void)tableView;
  if ((section < 0) ||
      ((NSUInteger)section >= [[self databaseSections] count])) {
    return nil;
  }

  sectionInfo = [[self databaseSections] objectAtIndex:(NSUInteger)section];
  title = [sectionInfo objectForKey:@"title"];
  return StrappyDatabaseStringHasValue(title) ? title : nil;
}

- (UITableViewCell *)tableView:(UITableView *)tableView
         cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
  UITableViewCell *cell;
  NSDictionary *row;

  cell = [tableView dequeueReusableCellWithIdentifier:@"CatalogCell"];
  if (cell == nil) {
    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                  reuseIdentifier:@"CatalogCell"];
    [[cell textLabel] setNumberOfLines:1];
    [[cell detailTextLabel] setNumberOfLines:1];
  }

  row = [self databaseRowAtIndexPath:indexPath];
  [[cell textLabel] setTextColor:[UIColor blackColor]];
  [[cell detailTextLabel] setTextColor:[UIColor grayColor]];
  [cell setSelectionStyle:UITableViewCellSelectionStyleBlue];
  [self configureCell:cell withRow:row];
  return cell;
}

- (NSIndexPath *)tableView:(UITableView *)tableView
  willSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  (void)tableView;
  return ([[self rows] count] > 0U) ? indexPath : nil;
}

- (void)tableView:(UITableView *)tableView
didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  NSDictionary *row;

  [tableView deselectRowAtIndexPath:indexPath animated:YES];
  if ([[self rows] count] == 0U) {
    return;
  }

  row = [self databaseRowAtIndexPath:indexPath];
  if (row != nil) {
    [self useRow:row atIndexPath:indexPath];
  }
}

- (void)scanButtonPressed:(id)sender
{
  UIActionSheet *actionSheet;

  (void)sender;
  if ([self scanning]) {
    return;
  }

  actionSheet = [[UIActionSheet alloc]
    initWithTitle:NSLocalizedString(
      @"Scans your home folder for SQLite databases. After scanning, whitelist desired databases so Strappy can read their data. Quick Scan saves time by only scanning files with common file extensions used for SQLite databases.",
      nil)
         delegate:self
cancelButtonTitle:NSLocalizedString(@"Cancel", nil)
destructiveButtonTitle:nil
otherButtonTitles:NSLocalizedString(@"Quick Scan", nil),
                  NSLocalizedString(@"Full Scan", nil),
                  nil];
  [actionSheet showFromToolbar:[[self navigationController] toolbar]];
}

- (void)actionSheet:(UIActionSheet *)actionSheet
clickedButtonAtIndex:(NSInteger)buttonIndex
{
  NSInteger firstOtherButtonIndex;

  if (buttonIndex == [actionSheet cancelButtonIndex]) {
    return;
  }

  firstOtherButtonIndex = [actionSheet firstOtherButtonIndex];
  if (buttonIndex == firstOtherButtonIndex) {
    [self beginDatabaseScanWithMode:FileScannerDatabaseScanModeQuick];
  } else if (buttonIndex == (firstOtherButtonIndex + 1)) {
    [self beginDatabaseScanWithMode:FileScannerDatabaseScanModeFull];
  }
}

- (void)beginDatabaseScanWithMode:(FileScannerDatabaseScanMode)scanMode
{
  NSError *error;
  NSString *rootPath;

  if ([self scanning]) {
    return;
  }

  rootPath = NSHomeDirectory();
  [self setStatusMessage:nil];
  error = nil;
  if (![FileScanner beginDatabaseCatalogScanAtPath:rootPath
                                          scanMode:scanMode
                                             error:&error]) {
    [self showError:error
              title:NSLocalizedString(@"Could not scan databases", nil)];
    return;
  }
  [self setScanning:YES];
}

- (void)databaseCatalogScanDidStart:(NSNotification *)notification
{
  (void)notification;
  [self setStatusMessage:nil];
  [self setScanning:YES];
}

- (void)databaseCatalogDidChange:(NSNotification *)notification
{
  (void)notification;
  [self reloadRows];
}

- (void)databaseCatalogScanDidFinish:(NSNotification *)notification
{
  NSString *errorMessage = [[notification userInfo] objectForKey:@"error"];
  if ([errorMessage isKindOfClass:[NSString class]]) [self setStatusMessage:errorMessage];
  [self setScanning:NO];
  [self refreshStatusToolbar];
}

- (void)useRow:(NSDictionary *)row atIndexPath:(NSIndexPath *)indexPath
{
  NSNumber *catalogId;
  BOOL shouldAllow;
  BOOL shouldHide;
  NSError *error;
  NSString *validationError;

  (void)indexPath;
  catalogId = [row objectForKey:@"catalog_id"];

  if ([self hiddenMode]) {
    shouldHide = StrappyDatabaseRowHiddenValue(row) ? NO : YES;
    error = nil;
    if (![[FileScanner sharedScanner] setCatalogedDatabaseHidden:shouldHide
                                            forCatalogIdentifier:catalogId
                                                           error:&error]) {
      [self showError:error
                title:NSLocalizedString(@"Failed to Save Changes", nil)];
      return;
    }
    [self reloadRows];
    return;
  }

  if (![self databaseRowCanBeAllowed:row]) {
    validationError = [row objectForKey:@"validation_error"];
    if (![validationError isKindOfClass:[NSString class]] ||
        ([validationError length] == 0U)) {
      validationError =
        NSLocalizedString(@"This file is not a valid SQLite database.", nil);
    }
    [self showMessage:validationError
                title:NSLocalizedString(@"Database cannot be used", nil)];
    return;
  }

  shouldAllow = [self allowedValueForDatabaseRow:row] ? NO : YES;
  error = nil;
  if (![[FileScanner sharedScanner] setCatalogedDatabaseAllowed:shouldAllow
                                           forCatalogIdentifier:catalogId
                                                          error:&error]) {
    [self showError:error
              title:NSLocalizedString(@"Failed to Save Changes", nil)];
    return;
  }
  [self reloadRows];
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [[self scanButton] setTarget:nil];
}

@end
