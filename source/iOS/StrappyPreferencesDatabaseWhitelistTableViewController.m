#import "StrappyPreferencesDatabaseWhitelistTableViewController.h"

#import "FileScanner.h"

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

static NSString *StrappyDatabaseSectionTitle(NSString *appName,
                                             NSString *bundleIdentifier,
                                             NSUInteger count,
                                             NSUInteger allowedCount,
                                             BOOL disambiguate)
{
  NSString *title;

  title = StrappyDatabaseStringHasValue(appName) ? appName :
    NSLocalizedString(@"Other", nil);
  if (disambiguate && ([bundleIdentifier length] > 0U)) {
    title = [NSString stringWithFormat:@"%@ (%@)", title, bundleIdentifier];
  }

  if (allowedCount > 0U) {
    return [NSString stringWithFormat:@"%@ - %lu/%lu",
      title,
      (unsigned long)allowedCount,
      (unsigned long)count];
  }

  return [NSString stringWithFormat:@"%@ - %lu",
    title,
    (unsigned long)count];
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
      NSUInteger rowIndex;
      NSUInteger allowedCount;
      NSNumber *appNameCount;
      BOOL disambiguate;
      NSString *title;
      NSDictionary *section;

      allowedCount = 0U;
      for (rowIndex = 0U; rowIndex < [currentRows count]; rowIndex++) {
        if (StrappyDatabaseRowAllowedValue([currentRows objectAtIndex:rowIndex])) {
          allowedCount++;
        }
      }
      appNameCount = [nameCounts objectForKey:currentAppName];
      disambiguate = ([appNameCount isKindOfClass:[NSNumber class]] &&
                      ([appNameCount unsignedIntegerValue] >
                       [currentRows count])) ? YES : NO;
      title = StrappyDatabaseSectionTitle(currentAppName,
                                          currentBundleIdentifier,
                                          [currentRows count],
                                          allowedCount,
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

static BOOL StrappyDatabasePathContains(NSString *path, NSString *needle)
{
  if (([path length] == 0U) || ([needle length] == 0U)) {
    return NO;
  }
  return ([path rangeOfString:needle
                      options:NSCaseInsensitiveSearch].location != NSNotFound) ?
    YES : NO;
}

static long long StrappyDatabaseRowPriority(NSDictionary *row)
{
  NSString *path;
  NSString *name;
  NSNumber *sizeNumber;
  long long size;

  path = StrappyDatabasePathForRow(row);
  name = [path lastPathComponent];
  sizeNumber = [row objectForKey:@"size"];
  size = [sizeNumber isKindOfClass:[NSNumber class]] ?
    [sizeNumber longLongValue] : 0LL;

  if (StrappyDatabasePathContains(path, @".localstorage")) {
    return 30LL;
  }
  if ([name caseInsensitiveCompare:@"ApplicationCache.db"] == NSOrderedSame) {
    return 35LL;
  }
  if ([name caseInsensitiveCompare:@"MapTiles.sqlitedb"] == NSOrderedSame) {
    return 35LL;
  }
  if ([name caseInsensitiveCompare:@"SafeBrowsing.db"] == NSOrderedSame) {
    return 35LL;
  }
  if ([name isEqualToString:@"Cache.db"] ||
      ([name caseInsensitiveCompare:@"nsurlcache"] == NSOrderedSame)) {
    return (size <= 4096LL) ? 5LL : 20LL;
  }
  if (([name isEqualToString:@"cache.db"]) &&
      StrappyDatabasePathContains(path, @"/Caches/")) {
    return 25LL;
  }
  if (StrappyDatabasePathContains(path, @"/Library/Caches/")) {
    return 40LL;
  }

  return 100LL;
}

static BOOL StrappyDatabaseRowIsAllowed(NSDictionary *row)
{
  NSString *decision;

  decision = [row objectForKey:@"user_decision"];
  return [decision isEqualToString:@"allowed"];
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
  result = StrappyCompareStrings(StrappyDatabaseLocationForRow(leftRow),
                                 StrappyDatabaseLocationForRow(rightRow));
  if (result != NSOrderedSame) {
    return result;
  }
  return StrappyCompareStrings(StrappyDatabaseNameForRow(leftRow),
                               StrappyDatabaseNameForRow(rightRow));
}

@interface StrappyPreferencesDatabaseWhitelistTableViewController ()
@property (nonatomic, assign) BOOL scanning;
@property (nonatomic, copy) NSArray *databaseSections;
- (void)scanDatabasesInBackground:(NSString *)rootPath;
- (void)scanDatabasesDidFinish:(NSDictionary *)result;
@end

@implementation StrappyPreferencesDatabaseWhitelistTableViewController

- (instancetype)init
{
  return [super initWithTitle:NSLocalizedString(@"Databases", nil)];
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

- (NSString *)statusText
{
  NSUInteger count;
  NSString *searchText;

  if ([self scanning]) {
    return NSLocalizedString(@"Scanning databases...", nil);
  }
  if ([[self statusMessage] length] > 0U) {
    return [self statusMessage];
  }

  count = [[self rows] count];
  searchText = [self currentSearchText];
  if ([searchText length] > 0U) {
    if (count == 0U) {
      return NSLocalizedString(@"No matching databases.", nil);
    }
    if (count == 1U) {
      return NSLocalizedString(@"1 database shown.", nil);
    }
    return [NSString stringWithFormat:
      NSLocalizedString(@"%lu databases shown.", nil), (unsigned long)count];
  }

  if (count == 0U) {
    return NSLocalizedString(@"No databases available.", nil);
  }
  if (count == 1U) {
    return NSLocalizedString(@"1 database available.", nil);
  }
  return [NSString stringWithFormat:
    NSLocalizedString(@"%lu databases available.", nil), (unsigned long)count];
}

- (NSString *)emptyText
{
  return NSLocalizedString(@"No databases available.", nil);
}

- (NSString *)actionButtonAccessibilityLabel
{
  return NSLocalizedString(@"Scan Databases", nil);
}

- (void)configureCell:(UITableViewCell *)cell withRow:(NSDictionary *)row
{
  NSMutableArray *details;
  NSString *location;
  NSString *size;

  details = [NSMutableArray array];
  location = StrappyDatabaseLocationForRow(row);
  size = StrappyByteCountString([row objectForKey:@"size"]);
  if ([location length] > 0U) {
    [details addObject:location];
  }
  if ([size length] > 0U) {
    [details addObject:size];
  }

  [[cell textLabel] setText:StrappyDatabaseNameForRow(row)];
  [[cell detailTextLabel] setText:[details componentsJoinedByString:@" | "]];
  [cell setAccessoryType:[self allowedValueForDatabaseRow:row]
    ? UITableViewCellAccessoryCheckmark
    : UITableViewCellAccessoryNone];

  if (![self databaseRowCanBeAllowed:row]) {
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
  return ([[self rows] count] > 0U) ?
    (NSInteger)[[self databaseSections] count] : 1;
}

- (NSInteger)tableView:(UITableView *)tableView
 numberOfRowsInSection:(NSInteger)section
{
  NSDictionary *sectionInfo;
  NSArray *sectionRows;

  (void)tableView;
  if ([[self rows] count] == 0U) {
    return (section == 0) ? 1 : 0;
  }
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
  if ([[self rows] count] == 0U ||
      (section < 0) ||
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

  if ([[self rows] count] == 0U) {
    [[cell textLabel] setText:[self emptyText]];
    [[cell detailTextLabel] setText:nil];
    [cell setAccessoryType:UITableViewCellAccessoryNone];
    [cell setSelectionStyle:UITableViewCellSelectionStyleNone];
    [[cell textLabel] setTextColor:[UIColor grayColor]];
    [[cell detailTextLabel] setTextColor:[UIColor grayColor]];
    return cell;
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
  NSString *rootPath;

  (void)sender;
  if ([self scanning]) {
    return;
  }

  rootPath = NSHomeDirectory();
  [self setScanning:YES];
  [self setWorking:YES];
  [[[self navigationItem] rightBarButtonItem] setEnabled:NO];
  [self setStatusMessage:nil];
  [[self tableView] reloadData];
  [self refreshStatusToolbar];
  [NSThread detachNewThreadSelector:@selector(scanDatabasesInBackground:)
                           toTarget:self
                         withObject:rootPath];
}

- (void)scanDatabasesInBackground:(NSString *)rootPath
{
  @autoreleasepool {
    NSError *error;
    NSArray *rows;
    NSMutableDictionary *result;
    NSString *message;

    error = nil;
    rows = [[FileScanner sharedScanner]
      scanDirectoryForSQLiteDatabasesAtPath:rootPath
             savingResultsToCatalogWithError:&error];
    result = [NSMutableDictionary dictionary];
    if (rows != nil) {
      [result setObject:rows forKey:@"rows"];
    } else {
      message = [error localizedDescription];
      if ([message length] == 0U) {
        message = NSLocalizedString(@"Database scan failed.", nil);
      }
      [result setObject:message forKey:@"error"];
    }

    [self performSelectorOnMainThread:@selector(scanDatabasesDidFinish:)
                           withObject:result
                        waitUntilDone:NO];
  }
}

- (void)scanDatabasesDidFinish:(NSDictionary *)result
{
  NSArray *rows;
  NSString *errorMessage;

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
  [self setWorking:NO];
  [[[self navigationItem] rightBarButtonItem] setEnabled:YES];
  [[self tableView] reloadData];
  [self refreshStatusToolbar];
}

- (void)useRow:(NSDictionary *)row atIndexPath:(NSIndexPath *)indexPath
{
  NSNumber *catalogId;
  BOOL shouldAllow;
  NSError *error;
  NSString *validationError;

  (void)indexPath;
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

  catalogId = [row objectForKey:@"catalog_id"];
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

@end
