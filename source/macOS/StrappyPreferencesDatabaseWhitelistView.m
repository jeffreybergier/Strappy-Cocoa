#import "StrappyPreferencesDatabaseWhitelistView.h"

#import "XPAppKit.h"

static NSString *StrappyDatabaseWhitelistPathForRow(NSDictionary *row)
{
  NSString *path;

  path = [row objectForKey:@"path"];
  if (![path isKindOfClass:[NSString class]]) {
    return @"";
  }
  return path;
}

static NSString *StrappyDatabaseWhitelistNameForRow(NSDictionary *row)
{
  NSString *path;
  NSString *name;

  path = StrappyDatabaseWhitelistPathForRow(row);
  name = [path lastPathComponent];
  if ([name length] == 0U) {
    return path;
  }
  return name;
}

static NSString *StrappyDatabaseWhitelistLocationForRow(NSDictionary *row)
{
  NSString *path;
  NSString *directory;
  NSString *homeDirectory;
  NSUInteger homeLength;

  path = StrappyDatabaseWhitelistPathForRow(row);
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

static BOOL StrappyDatabaseWhitelistRowIsAllowed(NSDictionary *row)
{
  NSString *decision;

  decision = [row objectForKey:@"user_decision"];
  return [decision isEqualToString:@"allowed"];
}

static NSComparisonResult StrappyDatabaseWhitelistCompareStrings(NSString *left,
                                                                 NSString *right)
{
  if (![left isKindOfClass:[NSString class]]) {
    left = @"";
  }
  if (![right isKindOfClass:[NSString class]]) {
    right = @"";
  }
  return [left caseInsensitiveCompare:right];
}

static NSComparisonResult StrappyDatabaseWhitelistCompareLongLong(long long left,
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

static BOOL StrappyDatabaseWhitelistPathContains(NSString *path,
                                                 NSString *needle)
{
  if (([path length] == 0U) || ([needle length] == 0U)) {
    return NO;
  }
  return ([path rangeOfString:needle
                      options:NSCaseInsensitiveSearch].location != NSNotFound) ?
    YES : NO;
}

static long long StrappyDatabaseWhitelistPriorityForRow(NSDictionary *row)
{
  NSString *path;
  NSString *name;
  NSNumber *sizeNumber;
  long long size;

  path = StrappyDatabaseWhitelistPathForRow(row);
  name = [path lastPathComponent];
  sizeNumber = [row objectForKey:@"size"];
  size = [sizeNumber isKindOfClass:[NSNumber class]] ?
    [sizeNumber longLongValue] : 0LL;

  if (StrappyDatabaseWhitelistPathContains(path, @".localstorage")) {
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
      StrappyDatabaseWhitelistPathContains(path, @"/Caches/")) {
    return 25LL;
  }
  if (StrappyDatabaseWhitelistPathContains(path, @"/Library/Caches/")) {
    return 40LL;
  }

  return 100LL;
}

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
  return [super initWithFrame:frame
                       target:target
                refreshAction:@selector(scanDatabases:)
                 searchAction:NULL
               refreshToolTip:NSLocalizedString(
                 @"Scan your home folder for SQLite databases.", nil)
                   dataSource:dataSource
                     delegate:delegate];
}

- (void)addTableColumnsToTableView:(NSTableView *)tableView
{
  NSTableColumn *allowedColumn;
  NSTableColumn *nameColumn;
  NSTableColumn *locationColumn;
  NSTableColumn *sizeColumn;
  NSButtonCell *allowedCell;
  NSTextFieldCell *nameCell;
  NSTextFieldCell *locationCell;
  NSTextFieldCell *sizeCell;

  allowedColumn =
    [[[NSTableColumn alloc] initWithIdentifier:@"allowed"] autorelease];
  [[allowedColumn headerCell] setStringValue:NSLocalizedString(@"Use", nil)];
  [allowedColumn setWidth:48.0];
  [allowedColumn setMinWidth:44.0];
  [allowedColumn setMaxWidth:54.0];
  [allowedColumn setEditable:YES];
  [allowedColumn setSortDescriptorPrototype:
    [[[NSSortDescriptor alloc] initWithKey:@"allowed"
                                 ascending:NO] autorelease]];
  allowedCell = [[[NSButtonCell alloc] init] autorelease];
  [allowedCell setButtonType:XPButtonTypeSwitch];
  [allowedCell setTitle:@""];
  [allowedCell setAlignment:XPTextAlignmentCenter];
  [allowedColumn setDataCell:allowedCell];
  [tableView addTableColumn:allowedColumn];

  nameColumn = [[[NSTableColumn alloc] initWithIdentifier:@"name"] autorelease];
  [[nameColumn headerCell] setStringValue:NSLocalizedString(@"Database", nil)];
  [nameColumn setWidth:210.0];
  [nameColumn setMinWidth:120.0];
  [nameColumn setEditable:NO];
  [nameColumn setSortDescriptorPrototype:
    [[[NSSortDescriptor alloc] initWithKey:@"name"
                                 ascending:YES] autorelease]];
  nameCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [nameCell setLineBreakMode:NSLineBreakByTruncatingTail];
  [nameColumn setDataCell:nameCell];
  [tableView addTableColumn:nameColumn];

  locationColumn =
    [[[NSTableColumn alloc] initWithIdentifier:@"location"] autorelease];
  [[locationColumn headerCell] setStringValue:NSLocalizedString(@"Location", nil)];
  [locationColumn setWidth:270.0];
  [locationColumn setMinWidth:160.0];
  [locationColumn setEditable:NO];
  [locationColumn setSortDescriptorPrototype:
    [[[NSSortDescriptor alloc] initWithKey:@"location"
                                 ascending:YES] autorelease]];
  locationCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [locationCell setLineBreakMode:NSLineBreakByTruncatingMiddle];
  [locationCell setTextColor:[NSColor disabledControlTextColor]];
  [locationColumn setDataCell:locationCell];
  [tableView addTableColumn:locationColumn];

  sizeColumn = [[[NSTableColumn alloc] initWithIdentifier:@"size"] autorelease];
  [[sizeColumn headerCell] setStringValue:NSLocalizedString(@"Size", nil)];
  [sizeColumn setWidth:76.0];
  [sizeColumn setMinWidth:66.0];
  [sizeColumn setEditable:NO];
  [sizeColumn setSortDescriptorPrototype:
    [[[NSSortDescriptor alloc] initWithKey:@"size"
                                 ascending:NO] autorelease]];
  sizeCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [sizeCell setAlignment:XPTextAlignmentRight];
  [sizeColumn setDataCell:sizeCell];
  [tableView addTableColumn:sizeColumn];

  [tableView setSortDescriptors:[NSArray arrayWithObjects:
    [[[NSSortDescriptor alloc] initWithKey:@"database_priority"
                                 ascending:NO] autorelease],
    nil]];
}

- (NSSortDescriptor *)requiredSortDescriptor
{
  return [[[NSSortDescriptor alloc] initWithKey:@"allowed"
                                      ascending:NO] autorelease];
}

- (NSSortDescriptor *)defaultPrimarySortDescriptor
{
  return [[[NSSortDescriptor alloc] initWithKey:@"database_priority"
                                      ascending:NO] autorelease];
}

- (NSArray *)fallbackSortDescriptors
{
  return [NSArray arrayWithObjects:
    [[[NSSortDescriptor alloc] initWithKey:@"location"
                                 ascending:YES] autorelease],
    [[[NSSortDescriptor alloc] initWithKey:@"size"
                                 ascending:NO] autorelease],
    nil];
}

- (NSString *)stableSortKey
{
  return @"path";
}

- (BOOL)sortKeyIsKnown:(NSString *)key
{
  return ([key isEqualToString:@"allowed"] ||
          [key isEqualToString:@"database_priority"] ||
          [key isEqualToString:@"name"] ||
          [key isEqualToString:@"location"] ||
          [key isEqualToString:@"size"] ||
          [key isEqualToString:@"path"]) ? YES : NO;
}

- (NSComparisonResult)compareRow:(NSDictionary *)left
                             row:(NSDictionary *)right
                      forSortKey:(NSString *)key
{
  if ([key isEqualToString:@"allowed"]) {
    return StrappyDatabaseWhitelistCompareLongLong(
      StrappyDatabaseWhitelistRowIsAllowed(left) ? 1LL : 0LL,
      StrappyDatabaseWhitelistRowIsAllowed(right) ? 1LL : 0LL);
  }
  if ([key isEqualToString:@"database_priority"]) {
    return StrappyDatabaseWhitelistCompareLongLong(
      StrappyDatabaseWhitelistPriorityForRow(left),
      StrappyDatabaseWhitelistPriorityForRow(right));
  }
  if ([key isEqualToString:@"name"]) {
    return StrappyDatabaseWhitelistCompareStrings(
      StrappyDatabaseWhitelistNameForRow(left),
      StrappyDatabaseWhitelistNameForRow(right));
  }
  if ([key isEqualToString:@"location"]) {
    return StrappyDatabaseWhitelistCompareStrings(
      StrappyDatabaseWhitelistLocationForRow(left),
      StrappyDatabaseWhitelistLocationForRow(right));
  }
  if ([key isEqualToString:@"size"]) {
    NSNumber *leftValue;
    NSNumber *rightValue;

    leftValue = [left objectForKey:@"size"];
    rightValue = [right objectForKey:@"size"];
    return StrappyDatabaseWhitelistCompareLongLong(
      [leftValue isKindOfClass:[NSNumber class]] ? [leftValue longLongValue] : 0LL,
      [rightValue isKindOfClass:[NSNumber class]] ? [rightValue longLongValue] : 0LL);
  }
  if ([key isEqualToString:@"path"]) {
    return StrappyDatabaseWhitelistCompareStrings(
      StrappyDatabaseWhitelistPathForRow(left),
      StrappyDatabaseWhitelistPathForRow(right));
  }
  return NSOrderedSame;
}

- (NSButton *)scanButton
{
  return [self refreshButton];
}

@end
