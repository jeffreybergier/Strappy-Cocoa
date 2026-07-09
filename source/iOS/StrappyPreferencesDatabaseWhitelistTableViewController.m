#import "StrappyPreferencesDatabaseWhitelistTableViewController.h"

#import "AIFontAwesome.h"
#import "FileScanner.h"

static const CGFloat kStrappyDatabaseHiddenIconCanvasSize = 24.0f;
static const CGFloat kStrappyDatabaseHiddenIconSize = 20.0f;

static UIImage *StrappyDatabaseHiddenIconImage(void)
{
  static UIImage *image = nil;

  if (image == nil) {
    image = [AIFontAwesome imageForIcon:AIFAEyeSlash
                                  style:AIFontAwesomeStyleSolid
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

static NSString *StrappyDatabaseAppNameForRow(NSDictionary *row)
{
  NSString *appName;
  NSString *groupKey;

  appName = [row objectForKey:@"app_name"];
  if (StrappyDatabaseStringHasValue(appName)) {
    return appName;
  }

  groupKey = [row objectForKey:@"app_group_key"];
  if (StrappyDatabaseStringHasValue(groupKey)) {
    return groupKey;
  }

  return NSLocalizedString(@"Other", nil);
}

static NSString *StrappyDatabaseAppGroupKeyForRow(NSDictionary *row)
{
  NSString *groupKey;
  NSString *location;

  groupKey = [row objectForKey:@"app_group_key"];
  if (StrappyDatabaseStringHasValue(groupKey)) {
    return groupKey;
  }

  location = StrappyDatabaseLocationForRow(row);
  return [@"path:" stringByAppendingString:[location lowercaseString]];
}

static NSString *StrappyDatabaseBundleIdentifierForRow(NSDictionary *row)
{
  NSString *bundleIdentifier;

  bundleIdentifier = [row objectForKey:@"app_bundle_id"];
  return StrappyDatabaseStringHasValue(bundleIdentifier) ? bundleIdentifier : @"";
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

static NSString *StrappyDatabaseSectionTitle(NSString *appName,
                                             NSString *bundleIdentifier,
                                             BOOL disambiguate)
{
  NSString *title;

  title = StrappyDatabaseStringHasValue(appName) ? appName :
    NSLocalizedString(@"Other", nil);
  if (disambiguate && ([bundleIdentifier length] > 0U)) {
    title = [NSString stringWithFormat:@"%@ (%@)", title, bundleIdentifier];
  }

  return title;
}

static NSArray *StrappyDatabaseSectionsForRows(NSArray *rows)
{
  NSMutableDictionary *nameCounts;
  NSMutableArray *sections;
  NSString *currentGroupKey;
  NSString *currentAppName;
  NSString *currentBundleIdentifier;
  NSMutableArray *currentRows;
  NSUInteger index;

  if (![rows isKindOfClass:[NSArray class]] || ([rows count] == 0U)) {
    return [NSArray array];
  }

  nameCounts = [NSMutableDictionary dictionary];
  for (index = 0U; index < [rows count]; index++) {
    NSDictionary *row;
    NSString *appName;
    NSNumber *count;

    row = [rows objectAtIndex:index];
    if (![row isKindOfClass:[NSDictionary class]]) {
      continue;
    }
    appName = StrappyDatabaseAppNameForRow(row);
    count = [nameCounts objectForKey:appName];
    [nameCounts setObject:[NSNumber numberWithUnsignedInteger:
      ([count isKindOfClass:[NSNumber class]] ?
        [count unsignedIntegerValue] + 1U : 1U)]
                    forKey:appName];
  }

  sections = [NSMutableArray array];
  currentGroupKey = nil;
  currentAppName = nil;
  currentBundleIdentifier = nil;
  currentRows = [NSMutableArray array];
  for (index = 0U; index <= [rows count]; index++) {
    NSDictionary *row;
    NSString *groupKey;

    row = (index < [rows count]) ? [rows objectAtIndex:index] : nil;
    groupKey = [row isKindOfClass:[NSDictionary class]] ?
      StrappyDatabaseAppGroupKeyForRow(row) : nil;
    if ((currentGroupKey != nil) &&
        ((row == nil) || ![groupKey isEqualToString:currentGroupKey])) {
      NSNumber *appNameCount;
      BOOL disambiguate;
      NSString *title;
      NSDictionary *section;

      appNameCount = [nameCounts objectForKey:currentAppName];
      disambiguate = ([appNameCount isKindOfClass:[NSNumber class]] &&
                      ([appNameCount unsignedIntegerValue] >
                       [currentRows count])) ? YES : NO;
      title = StrappyDatabaseSectionTitle(currentAppName,
                                          currentBundleIdentifier,
                                          disambiguate);
      section = [NSDictionary dictionaryWithObjectsAndKeys:
        title, @"title",
        [NSArray arrayWithArray:currentRows], @"rows",
        currentGroupKey, @"app_group_key",
        nil];
      [sections addObject:section];
      [currentRows removeAllObjects];
      currentGroupKey = nil;
      currentAppName = nil;
      currentBundleIdentifier = nil;
    }

    if ([row isKindOfClass:[NSDictionary class]]) {
      if (currentGroupKey == nil) {
        currentGroupKey = groupKey;
        currentAppName = StrappyDatabaseAppNameForRow(row);
        currentBundleIdentifier = StrappyDatabaseBundleIdentifierForRow(row);
      }
      [currentRows addObject:row];
    }
  }

  return sections;
}

static NSComparisonResult StrappyCompareStrings(NSString *left, NSString *right)
{
  if (![left isKindOfClass:[NSString class]]) {
    left = @"";
  }
  if (![right isKindOfClass:[NSString class]]) {
    right = @"";
  }
  return [left caseInsensitiveCompare:right];
}

static NSComparisonResult StrappyCompareBooleans(BOOL left, BOOL right)
{
  if (left == right) {
    return NSOrderedSame;
  }
  return left ? NSOrderedAscending : NSOrderedDescending;
}

static NSComparisonResult StrappyCompareLongLong(long long left,
                                                  long long right)
{
  if (left < right) {
    return NSOrderedAscending;
  }
  if (left > right) {
    return NSOrderedDescending;
  }
  return NSOrderedSame;
}

static long long StrappyDatabaseSizeForRow(NSDictionary *row)
{
  NSNumber *size;

  size = [row objectForKey:@"size"];
  return [size isKindOfClass:[NSNumber class]] ? [size longLongValue] : 0LL;
}

static long long StrappyDatabaseRowPriority(NSDictionary *row)
{
  return StrappyDatabaseRowHiddenValue(row) ? 0LL : 100LL;
}

static BOOL StrappyDatabaseRowIsAllowed(NSDictionary *row)
{
  return StrappyDatabaseRowAllowedValue(row);
}

static NSComparisonResult StrappyCompareDatabaseRows(id left,
                                                     id right,
                                                     void *context)
{
  NSDictionary *leftRow;
  NSDictionary *rightRow;
  NSComparisonResult result;

  (void)context;
  leftRow = [left isKindOfClass:[NSDictionary class]] ? left : nil;
  rightRow = [right isKindOfClass:[NSDictionary class]] ? right : nil;
  result = StrappyCompareStrings(StrappyDatabaseAppNameForRow(leftRow),
                                 StrappyDatabaseAppNameForRow(rightRow));
  if (result != NSOrderedSame) {
    return result;
  }
  result = StrappyCompareStrings(StrappyDatabaseAppGroupKeyForRow(leftRow),
                                 StrappyDatabaseAppGroupKeyForRow(rightRow));
  if (result != NSOrderedSame) {
    return result;
  }
  result = StrappyCompareBooleans(StrappyDatabaseRowIsAllowed(leftRow),
                                  StrappyDatabaseRowIsAllowed(rightRow));
  if (result != NSOrderedSame) {
    return result;
  }
  result = StrappyCompareLongLong(StrappyDatabaseRowPriority(leftRow),
                                  StrappyDatabaseRowPriority(rightRow));
  if (result != NSOrderedSame) {
    return -result;
  }
  result = StrappyCompareLongLong(StrappyDatabaseSizeForRow(leftRow),
                                  StrappyDatabaseSizeForRow(rightRow));
  if (result != NSOrderedSame) {
    return -result;
  }
  return StrappyCompareStrings(StrappyDatabaseNameForRow(leftRow),
                               StrappyDatabaseNameForRow(rightRow));
}

@interface StrappyPreferencesDatabaseWhitelistTableViewController ()
@property (nonatomic, assign) BOOL scanning;
@property (nonatomic, assign) BOOL hiddenMode;
@property (nonatomic, copy) NSArray *databaseSections;
@property (nonatomic, strong) UIBarButtonItem *hiddenModeButton;
- (void)hiddenModeButtonPressed:(id)sender;
- (void)updateHiddenModeButton;
- (void)databaseCatalogScanDidStart:(NSNotification *)notification;
- (void)databaseCatalogScanDidFinish:(NSNotification *)notification;
@end

@implementation StrappyPreferencesDatabaseWhitelistTableViewController

- (instancetype)init
{
  return [super initWithTitle:NSLocalizedString(@"Databases", nil)];
}

- (void)viewDidLoad
{
  [super viewDidLoad];

  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(databaseCatalogScanDidStart:)
           name:FileScannerDatabaseCatalogScanDidStartNotification
         object:nil];
  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(databaseCatalogScanDidFinish:)
           name:FileScannerDatabaseCatalogScanDidFinishNotification
         object:nil];

  [self setHiddenModeButton:
    [[UIBarButtonItem alloc] initWithTitle:NSLocalizedString(@"Hidden", nil)
                                     style:UIBarButtonItemStyleBordered
                                    target:self
                                    action:@selector(hiddenModeButtonPressed:)]];
  [[self navigationItem] setRightBarButtonItem:[self hiddenModeButton]];
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

- (void)hiddenModeButtonPressed:(id)sender
{
  (void)sender;
  [self setHiddenMode:![self hiddenMode]];
}

- (void)updateHiddenModeButton
{
  [[self hiddenModeButton] setTitle:NSLocalizedString(@"Hidden", nil)];
  [[self hiddenModeButton] setStyle:[self hiddenMode] ?
    UIBarButtonItemStyleDone : UIBarButtonItemStyleBordered];
  [[self hiddenModeButton] setAccessibilityLabel:[self hiddenMode] ?
    NSLocalizedString(@"Editing hidden databases", nil) :
    NSLocalizedString(@"Edit hidden databases", nil)];
}

- (NSArray *)loadAllRowsWithError:(NSError **)error
{
  return [[FileScanner sharedScanner] catalogedSQLiteDatabasesWithError:error];
}

- (NSArray *)sortedRows:(NSArray *)rows
{
  return [rows sortedArrayUsingFunction:StrappyCompareDatabaseRows context:NULL];
}

- (BOOL)row:(NSDictionary *)row matchesSearchText:(NSString *)searchText
{
  NSString *appName;
  NSString *appBundleId;

  if ([searchText length] == 0U) {
    return YES;
  }
  appName = StrappyDatabaseAppNameForRow(row);
  appBundleId = StrappyDatabaseBundleIdentifierForRow(row);
  return ([StrappyDatabaseNameForRow(row)
            rangeOfString:searchText
                  options:NSCaseInsensitiveSearch].location != NSNotFound) ||
         ([appName rangeOfString:searchText
                          options:NSCaseInsensitiveSearch].location !=
          NSNotFound) ||
         ([appBundleId rangeOfString:searchText
                              options:NSCaseInsensitiveSearch].location !=
          NSNotFound) ||
         ([StrappyDatabaseLocationForRow(row)
            rangeOfString:searchText
                  options:NSCaseInsensitiveSearch].location != NSNotFound) ||
         ([StrappyDatabasePathForRow(row)
            rangeOfString:searchText
                  options:NSCaseInsensitiveSearch].location != NSNotFound);
}

- (void)applyRows
{
  NSMutableArray *matchingRows;
  NSArray *sortedRows;
  NSString *searchText;
  NSUInteger index;

  searchText = [self currentSearchText];
  matchingRows = [NSMutableArray arrayWithCapacity:[[self allRows] count]];
  for (index = 0U; index < [[self allRows] count]; index++) {
    NSDictionary *row;

    row = [[self allRows] objectAtIndex:index];
    if (![row isKindOfClass:[NSDictionary class]]) {
      continue;
    }
    if (![self hiddenMode] &&
        StrappyDatabaseRowHiddenValue(row) &&
        !StrappyDatabaseRowAllowedValue(row)) {
      continue;
    }
    if ([self row:row matchesSearchText:searchText]) {
      [matchingRows addObject:row];
    }
  }

  sortedRows = [self sortedRows:matchingRows];
  [self setRows:sortedRows];
  [self setDatabaseSections:StrappyDatabaseSectionsForRows(sortedRows)];
  [[self tableView] reloadData];
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
  return StrappyDatabaseRowIsAllowed(row);
}

- (void)setScanning:(BOOL)scanning
{
  if (_scanning == scanning) {
    return;
  }

  _scanning = scanning ? YES : NO;
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
  return NSLocalizedString(@"Scan Databases", nil);
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

- (void)actionButtonPressed:(id)sender
{
  NSError *error;
  NSString *rootPath;

  (void)sender;
  if ([self scanning]) {
    return;
  }

  rootPath = NSHomeDirectory();
  [self setStatusMessage:nil];
  error = nil;
  if (![FileScanner beginDatabaseCatalogScanAtPath:rootPath error:&error]) {
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

- (void)databaseCatalogScanDidFinish:(NSNotification *)notification
{
  NSDictionary *result;
  NSArray *rows;
  NSString *errorMessage;

  result = [notification userInfo];
  rows = [result objectForKey:@"rows"];
  errorMessage = [result objectForKey:@"error"];
  if ([rows isKindOfClass:[NSArray class]]) {
    [self setStatusMessage:nil];
    [self setAllRows:[self sortedRows:rows]];
    [self applyRows];
  } else {
    [self setStatusMessage:[errorMessage isKindOfClass:[NSString class]]
      ? errorMessage
      : NSLocalizedString(@"Database scan failed.", nil)];
  }

  [self setScanning:NO];
  [[self tableView] reloadData];
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
                title:NSLocalizedString(@"Could not update database", nil)];
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
              title:NSLocalizedString(@"Could not update database", nil)];
    return;
  }
  [self reloadRows];
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [[self hiddenModeButton] setTarget:nil];
}

@end
