#import "StrappyPreferencesDatabaseStudyView.h"

#import "XPAppKit.h"

static const CGFloat kStrappyStudyButtonWidth = 76.0;
static const CGFloat kStrappyStudyControlHeight = 24.0;
static const CGFloat kStrappyStudyCompactRowHeight = 24.0;
static const CGFloat kStrappyStudyExpandedCellInset = 5.0;
static const CGFloat kStrappyStudyMeasurementHeight = 1000000.0;

static NSString *StrappyStudyViewStringForRow(NSDictionary *row, NSString *key)
{
  NSString *value;

  value = [row objectForKey:key];
  return [value isKindOfClass:[NSString class]] ? value : @"";
}

static NSString *StrappyStudyViewDatabaseNameForRow(NSDictionary *row)
{
  NSString *name;
  NSString *path;

  path = StrappyStudyViewStringForRow(row, @"path");
  name = [path lastPathComponent];
  return ([name length] > 0U) ?
    name : StrappyStudyViewStringForRow(row, @"database_id");
}

static NSString *StrappyStudyViewApplicationNameForRow(NSDictionary *row)
{
  NSString *appName;

  appName = StrappyStudyViewStringForRow(row, @"app_name");
  return ([appName length] > 0U) ?
    appName : NSLocalizedString(@"Other", nil);
}

static BOOL StrappyStudyViewRowIsStudied(NSDictionary *row)
{
  NSNumber *studied;

  studied = [row objectForKey:@"studied"];
  return ([studied isKindOfClass:[NSNumber class]] && [studied boolValue]) ?
    YES : NO;
}

static long long StrappyStudyViewStudiedAtForRow(NSDictionary *row)
{
  NSNumber *studiedAt;

  studiedAt = [row objectForKey:@"studied_at_ms"];
  return [studiedAt isKindOfClass:[NSNumber class]] ?
    [studiedAt longLongValue] : 0LL;
}

static NSComparisonResult StrappyStudyViewCompareStrings(NSString *left,
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

static NSComparisonResult StrappyStudyViewCompareLongLong(long long left,
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

static CGFloat StrappyStudyViewHeightForText(NSString *text, CGFloat width)
{
  NSDictionary *attributes;
  NSRect bounds;

  if (![text isKindOfClass:[NSString class]] || ([text length] == 0U) ||
      (width <= 0.0)) {
    return 0.0;
  }

  attributes = [NSDictionary dictionaryWithObject:[NSFont systemFontOfSize:12.0]
                                           forKey:NSFontAttributeName];
  bounds = [text boundingRectWithSize:NSMakeSize(width,
                                                  kStrappyStudyMeasurementHeight)
                              options:(NSStringDrawingUsesLineFragmentOrigin |
                                       NSStringDrawingUsesFontLeading)
                           attributes:attributes];
  return NSHeight(bounds);
}

@implementation StrappyPreferencesDatabaseStudyView

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
  if ((self = [super initWithFrame:frame
                            target:target
                        dataSource:dataSource
                          delegate:delegate])) {
    [[self tableView] setTarget:target];
    [[self tableView] setAction:@selector(databaseStudyRowClicked:)];
  }
  return self;
}

- (CGFloat)topAccessoryTrailingControlWidth
{
  return kStrappyStudyButtonWidth;
}

- (void)configureTopAccessoryView:(NSView *)view target:(id)target
{
  NSRect bounds;

  bounds = [view bounds];
  studyButton_ = [[NSButton alloc] initWithFrame:NSMakeRect(
    NSWidth(bounds) - kStrappyStudyButtonWidth,
    NSMaxY(bounds) - kStrappyStudyControlHeight,
    kStrappyStudyButtonWidth,
    kStrappyStudyControlHeight)];
  [studyButton_ setAutoresizingMask:NSViewMinXMargin | NSViewMinYMargin];
  [studyButton_ setBezelStyle:XPBezelStyleRounded];
  [studyButton_ setButtonType:XPButtonTypeMomentaryLight];
  [studyButton_ setTitle:NSLocalizedString(@"Study", nil)];
  [studyButton_ setToolTip:NSLocalizedString(
    @"Study databases to save time and tokens when Strappy tries to query them in future prompts. Study sessions use the default model under the \"Session Defaults\" menu.",
    nil)];
  [studyButton_ setTarget:target];
  [studyButton_ setAction:@selector(beginDatabaseStudy:)];
  [view addSubview:studyButton_];
}

- (void)configureTableView:(NSTableView *)tableView
{
  [tableView setAllowsMultipleSelection:NO];
  [tableView setRowHeight:kStrappyStudyCompactRowHeight];
}

- (void)addTableColumnsToTableView:(NSTableView *)tableView
{
  NSTableColumn *studiedColumn;
  NSTableColumn *applicationColumn;
  NSTableColumn *nameColumn;
  NSTableColumn *lastStudiedColumn;
  NSTableColumn *descriptionColumn;
  NSTableColumn *contextColumn;
  NSButtonCell *studiedCell;
  NSTextFieldCell *textCell;

  studiedColumn =
    [[[NSTableColumn alloc] initWithIdentifier:@"study_studied"] autorelease];
  [[studiedColumn headerCell] setStringValue:NSLocalizedString(@"Studied", nil)];
  [studiedColumn setWidth:62.0];
  [studiedColumn setMinWidth:58.0];
  [studiedColumn setMaxWidth:72.0];
  [studiedColumn setEditable:NO];
  [studiedColumn setSortDescriptorPrototype:
    [[[NSSortDescriptor alloc] initWithKey:@"study_studied"
                                 ascending:NO] autorelease]];
  studiedCell = [[[NSButtonCell alloc] init] autorelease];
  [studiedCell setButtonType:XPButtonTypeSwitch];
  [studiedCell setTitle:@""];
  [studiedCell setAlignment:XPTextAlignmentCenter];
  [studiedColumn setDataCell:studiedCell];
  [tableView addTableColumn:studiedColumn];

  applicationColumn =
    [[[NSTableColumn alloc] initWithIdentifier:@"study_application"] autorelease];
  [[applicationColumn headerCell]
    setStringValue:NSLocalizedString(@"Application", nil)];
  [applicationColumn setWidth:115.0];
  [applicationColumn setMinWidth:90.0];
  [applicationColumn setEditable:NO];
  [applicationColumn setSortDescriptorPrototype:
    [[[NSSortDescriptor alloc] initWithKey:@"study_application"
                                 ascending:YES] autorelease]];
  textCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [textCell setLineBreakMode:NSLineBreakByTruncatingTail];
  [applicationColumn setDataCell:textCell];
  [tableView addTableColumn:applicationColumn];

  nameColumn =
    [[[NSTableColumn alloc] initWithIdentifier:@"study_name"] autorelease];
  [[nameColumn headerCell] setStringValue:NSLocalizedString(@"Database", nil)];
  [nameColumn setWidth:130.0];
  [nameColumn setMinWidth:100.0];
  [nameColumn setEditable:NO];
  [nameColumn setSortDescriptorPrototype:
    [[[NSSortDescriptor alloc] initWithKey:@"study_name"
                                 ascending:YES] autorelease]];
  textCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [textCell setLineBreakMode:NSLineBreakByTruncatingTail];
  [nameColumn setDataCell:textCell];
  [tableView addTableColumn:nameColumn];

  lastStudiedColumn =
    [[[NSTableColumn alloc] initWithIdentifier:@"study_last_studied"] autorelease];
  [[lastStudiedColumn headerCell]
    setStringValue:NSLocalizedString(@"Last Studied", nil)];
  [lastStudiedColumn setWidth:120.0];
  [lastStudiedColumn setMinWidth:105.0];
  [lastStudiedColumn setEditable:NO];
  [lastStudiedColumn setSortDescriptorPrototype:
    [[[NSSortDescriptor alloc] initWithKey:@"study_last_studied"
                                 ascending:NO] autorelease]];
  textCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [textCell setLineBreakMode:NSLineBreakByTruncatingTail];
  [lastStudiedColumn setDataCell:textCell];
  [tableView addTableColumn:lastStudiedColumn];

  descriptionColumn =
    [[[NSTableColumn alloc] initWithIdentifier:@"study_description"] autorelease];
  [[descriptionColumn headerCell]
    setStringValue:NSLocalizedString(@"Description", nil)];
  [descriptionColumn setWidth:160.0];
  [descriptionColumn setMinWidth:120.0];
  [descriptionColumn setEditable:NO];
  [descriptionColumn setSortDescriptorPrototype:
    [[[NSSortDescriptor alloc] initWithKey:@"study_description"
                                 ascending:YES] autorelease]];
  textCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [textCell setLineBreakMode:NSLineBreakByTruncatingTail];
  [descriptionColumn setDataCell:textCell];
  [tableView addTableColumn:descriptionColumn];

  contextColumn =
    [[[NSTableColumn alloc] initWithIdentifier:@"study_context"] autorelease];
  [[contextColumn headerCell] setStringValue:NSLocalizedString(@"Context", nil)];
  [contextColumn setWidth:190.0];
  [contextColumn setMinWidth:140.0];
  [contextColumn setEditable:NO];
  [contextColumn setSortDescriptorPrototype:
    [[[NSSortDescriptor alloc] initWithKey:@"study_context"
                                 ascending:YES] autorelease]];
  textCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [textCell setLineBreakMode:NSLineBreakByTruncatingTail];
  [contextColumn setDataCell:textCell];
  [tableView addTableColumn:contextColumn];

  [tableView setSortDescriptors:[NSArray arrayWithObject:
    [[[NSSortDescriptor alloc] initWithKey:@"study_application"
                                 ascending:YES] autorelease]]];
}

- (NSSortDescriptor *)defaultPrimarySortDescriptor
{
  return [[[NSSortDescriptor alloc] initWithKey:@"study_application"
                                      ascending:YES] autorelease];
}

- (NSArray *)fallbackSortDescriptors
{
  return [NSArray arrayWithObjects:
    [[[NSSortDescriptor alloc] initWithKey:@"study_name"
                                 ascending:YES] autorelease],
    [[[NSSortDescriptor alloc] initWithKey:@"study_database_id"
                                 ascending:YES] autorelease],
    nil];
}

- (NSString *)stableSortKey
{
  return @"study_database_id";
}

- (BOOL)sortKeyIsKnown:(NSString *)key
{
  return ([key isEqualToString:@"study_studied"] ||
          [key isEqualToString:@"study_application"] ||
          [key isEqualToString:@"study_name"] ||
          [key isEqualToString:@"study_last_studied"] ||
          [key isEqualToString:@"study_description"] ||
          [key isEqualToString:@"study_context"] ||
          [key isEqualToString:@"study_database_id"]) ? YES : NO;
}

- (NSComparisonResult)compareRow:(NSDictionary *)left
                             row:(NSDictionary *)right
                      forSortKey:(NSString *)key
{
  if ([key isEqualToString:@"study_studied"]) {
    return StrappyStudyViewCompareLongLong(
      StrappyStudyViewRowIsStudied(left) ? 1LL : 0LL,
      StrappyStudyViewRowIsStudied(right) ? 1LL : 0LL);
  }
  if ([key isEqualToString:@"study_application"]) {
    return StrappyStudyViewCompareStrings(
      StrappyStudyViewApplicationNameForRow(left),
      StrappyStudyViewApplicationNameForRow(right));
  }
  if ([key isEqualToString:@"study_name"]) {
    return StrappyStudyViewCompareStrings(
      StrappyStudyViewDatabaseNameForRow(left),
      StrappyStudyViewDatabaseNameForRow(right));
  }
  if ([key isEqualToString:@"study_last_studied"]) {
    return StrappyStudyViewCompareLongLong(
      StrappyStudyViewStudiedAtForRow(left),
      StrappyStudyViewStudiedAtForRow(right));
  }
  if ([key isEqualToString:@"study_description"]) {
    return StrappyStudyViewCompareStrings(
      StrappyStudyViewStringForRow(left, @"description"),
      StrappyStudyViewStringForRow(right, @"description"));
  }
  if ([key isEqualToString:@"study_context"]) {
    return StrappyStudyViewCompareStrings(
      StrappyStudyViewStringForRow(left, @"context"),
      StrappyStudyViewStringForRow(right, @"context"));
  }
  if ([key isEqualToString:@"study_database_id"]) {
    return StrappyStudyViewCompareStrings(
      StrappyStudyViewStringForRow(left, @"database_id"),
      StrappyStudyViewStringForRow(right, @"database_id"));
  }
  return NSOrderedSame;
}

- (NSButton *)studyButton
{
  return studyButton_;
}

- (CGFloat)expandedRowHeightForDescription:(NSString *)description
                                    context:(NSString *)context
{
  NSTableColumn *descriptionColumn;
  NSTableColumn *contextColumn;
  CGFloat descriptionHeight;
  CGFloat contextHeight;
  CGFloat height;

  descriptionColumn = [[self tableView]
    tableColumnWithIdentifier:@"study_description"];
  contextColumn = [[self tableView] tableColumnWithIdentifier:@"study_context"];
  descriptionHeight = StrappyStudyViewHeightForText(
    description,
    [descriptionColumn width] - (2.0 * kStrappyStudyExpandedCellInset));
  contextHeight = StrappyStudyViewHeightForText(
    context,
    [contextColumn width] - (2.0 * kStrappyStudyExpandedCellInset));
  height = (descriptionHeight > contextHeight) ?
    descriptionHeight : contextHeight;
  height = height + (2.0 * kStrappyStudyExpandedCellInset);
  return (height > kStrappyStudyCompactRowHeight) ?
    height : kStrappyStudyCompactRowHeight;
}

- (void)dealloc
{
  [studyButton_ release];
  [super dealloc];
}

@end
