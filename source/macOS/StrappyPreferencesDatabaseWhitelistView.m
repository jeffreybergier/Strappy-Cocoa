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

static NSString *StrappyDatabaseWhitelistAppNameForRow(NSDictionary *row)
{
  NSString *appName;
  NSString *groupKey;

  appName = [row objectForKey:@"app_name"];
  if ([appName isKindOfClass:[NSString class]] && ([appName length] > 0U)) {
    return appName;
  }

  groupKey = [row objectForKey:@"app_group_key"];
  if ([groupKey isKindOfClass:[NSString class]] && ([groupKey length] > 0U)) {
    return groupKey;
  }

  return NSLocalizedString(@"Other", nil);
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

static long long StrappyDatabaseWhitelistPriorityForRow(NSDictionary *row)
{
  NSNumber *hidden;

  hidden = [row objectForKey:@"hidden"];
  return ([hidden isKindOfClass:[NSNumber class]] && [hidden boolValue]) ?
    0LL : 100LL;
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
  NSTableColumn *appColumn;
  NSTableColumn *nameColumn;
  NSTableColumn *locationColumn;
  NSTableColumn *sizeColumn;
  NSButtonCell *allowedCell;
  NSTextFieldCell *appCell;
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

  appColumn = [[[NSTableColumn alloc] initWithIdentifier:@"application"] autorelease];
  [[appColumn headerCell] setStringValue:NSLocalizedString(@"Application", nil)];
  [appColumn setWidth:140.0];
  [appColumn setMinWidth:100.0];
  [appColumn setEditable:NO];
  [appColumn setSortDescriptorPrototype:
    [[[NSSortDescriptor alloc] initWithKey:@"application"
                                 ascending:YES] autorelease]];
  appCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [appCell setLineBreakMode:NSLineBreakByTruncatingTail];
  [appColumn setDataCell:appCell];
  [tableView addTableColumn:appColumn];

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
    [[[NSSortDescriptor alloc] initWithKey:@"application"
                                 ascending:YES] autorelease],
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
    [[[NSSortDescriptor alloc] initWithKey:@"database_priority"
                                 ascending:NO] autorelease],
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
          [key isEqualToString:@"application"] ||
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
  if ([key isEqualToString:@"application"]) {
    return StrappyDatabaseWhitelistCompareStrings(
      StrappyDatabaseWhitelistAppNameForRow(left),
      StrappyDatabaseWhitelistAppNameForRow(right));
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
