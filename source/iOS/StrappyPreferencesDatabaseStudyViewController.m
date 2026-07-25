#import "StrappyPreferencesDatabaseStudyViewController.h"

#import "AIFontAwesome.h"
#import "StrappySession.h"
#import "XPUIKit.h"

static const CGFloat kStrappyDatabaseStudyCompactRowHeight = 44.0f;
static const CGFloat kStrappyDatabaseStudyCellInset = 8.0f;
static const CGFloat kStrappyDatabaseStudyTextTop = 42.0f;
static const CGFloat kStrappyDatabaseStudyCheckCanvasSize = 24.0f;
static const CGFloat kStrappyDatabaseStudyDetailsFontSize = 13.0f;
static const CGFloat kStrappyDatabaseStudyMeasurementHeight = 1000000.0f;

enum {
  kStrappyDatabaseStudyResetAlertTag = 9101,
  kStrappyDatabaseStudyRunActionSheetTag = 9102
};

static UIFont *StrappyDatabaseStudyDetailsFont(void)
{
  return [UIFont systemFontOfSize:kStrappyDatabaseStudyDetailsFontSize];
}

@interface StrappyDatabaseStudyCell : UITableViewCell {
 @private
  UILabel *databaseNameLabel_;
  UILabel *studyDateLabel_;
  UILabel *studyDetailsLabel_;
  UIImageView *studiedImageView_;
}
- (void)setDatabaseName:(NSString *)databaseName
              studyDate:(NSString *)studyDate
                 details:(NSString *)details
                 studied:(BOOL)studied
                expanded:(BOOL)expanded;
@end

@implementation StrappyDatabaseStudyCell

- (id)initWithStyle:(UITableViewCellStyle)style
    reuseIdentifier:(NSString *)reuseIdentifier
{
  if ((self = [super initWithStyle:style reuseIdentifier:reuseIdentifier])) {
    CGRect bounds;
    CGFloat detailHeight;

    bounds = [[self contentView] bounds];

    databaseNameLabel_ = [[UILabel alloc] initWithFrame:
      CGRectMake(kStrappyDatabaseStudyCellInset,
                 2.0f,
                 CGRectGetWidth(bounds) -
                   (3.0f * kStrappyDatabaseStudyCellInset) -
                   kStrappyDatabaseStudyCheckCanvasSize,
                 22.0f)];
    [databaseNameLabel_ setAutoresizingMask:
      UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleBottomMargin];
    [databaseNameLabel_ setBackgroundColor:[UIColor clearColor]];
    [databaseNameLabel_ setFont:[UIFont boldSystemFontOfSize:18.0f]];
    [databaseNameLabel_ setNumberOfLines:1];
    [[self contentView] addSubview:databaseNameLabel_];

    studyDateLabel_ = [[UILabel alloc] initWithFrame:
      CGRectMake(kStrappyDatabaseStudyCellInset,
                 23.0f,
                 CGRectGetWidth(bounds) -
                   (3.0f * kStrappyDatabaseStudyCellInset) -
                   kStrappyDatabaseStudyCheckCanvasSize,
                 17.0f)];
    [studyDateLabel_ setAutoresizingMask:
      UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleBottomMargin];
    [studyDateLabel_ setBackgroundColor:[UIColor clearColor]];
    [studyDateLabel_ setFont:[UIFont systemFontOfSize:12.0f]];
    [studyDateLabel_ setTextColor:[UIColor grayColor]];
    [studyDateLabel_ setNumberOfLines:1];
    [[self contentView] addSubview:studyDateLabel_];

    studiedImageView_ = [[UIImageView alloc] initWithImage:
      [AIFontAwesome imageForIcon:AIFACheck
                            style:AIFontAwesomeStyleSolid
                         iconSize:16.0f
                       canvasSize:kStrappyDatabaseStudyCheckCanvasSize
                            color:[UIColor colorWithRed:0.12f
                                                 green:0.48f
                                                  blue:0.94f
                                                 alpha:1.0f]
                            scale:0.0f]];
    [studiedImageView_ setFrame:
      CGRectMake(CGRectGetWidth(bounds) -
                   kStrappyDatabaseStudyCellInset -
                   kStrappyDatabaseStudyCheckCanvasSize,
                 9.0f,
                 kStrappyDatabaseStudyCheckCanvasSize,
                 kStrappyDatabaseStudyCheckCanvasSize)];
    [studiedImageView_ setAutoresizingMask:
      UIViewAutoresizingFlexibleLeftMargin |
      UIViewAutoresizingFlexibleBottomMargin];
    [studiedImageView_ setContentMode:UIViewContentModeCenter];
    [studiedImageView_ setHidden:YES];
    [[self contentView] addSubview:studiedImageView_];

    detailHeight = CGRectGetHeight(bounds) -
      kStrappyDatabaseStudyTextTop - kStrappyDatabaseStudyCellInset;
    if (detailHeight < 0.0f) {
      detailHeight = 0.0f;
    }
    studyDetailsLabel_ = [[UILabel alloc] initWithFrame:
      CGRectMake(kStrappyDatabaseStudyCellInset,
                 kStrappyDatabaseStudyTextTop,
                 CGRectGetWidth(bounds) -
                   (2.0f * kStrappyDatabaseStudyCellInset),
                 detailHeight)];
    [studyDetailsLabel_ setAutoresizingMask:
      UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight];
    [studyDetailsLabel_ setBackgroundColor:[UIColor clearColor]];
    [studyDetailsLabel_ setFont:StrappyDatabaseStudyDetailsFont()];
    [studyDetailsLabel_ XP_setLineBreakModeWordWrapping];
    [studyDetailsLabel_ setNumberOfLines:0];
    [studyDetailsLabel_ setHidden:YES];
    [[self contentView] addSubview:studyDetailsLabel_];
  }
  return self;
}

- (void)setDatabaseName:(NSString *)databaseName
              studyDate:(NSString *)studyDate
                 details:(NSString *)details
                 studied:(BOOL)studied
                expanded:(BOOL)expanded
{
  NSString *currentDetails;

  [databaseNameLabel_ setText:(databaseName != nil) ? databaseName : @""];
  [studyDateLabel_ setText:(studyDate != nil) ? studyDate : @""];
  currentDetails = [studyDetailsLabel_ text];
  if ((details != currentDetails) && ![currentDetails isEqualToString:details]) {
    [studyDetailsLabel_ setText:(details != nil) ? details : @""];
  }
  [studiedImageView_ setHidden:studied ? NO : YES];
  [studyDetailsLabel_ setHidden:expanded ? NO : YES];
}

@end

static NSString *StrappyStudyStringForRow(NSDictionary *row, NSString *key)
{
  NSString *value;

  value = [row objectForKey:key];
  return [value isKindOfClass:[NSString class]] ? value : @"";
}

static NSString *StrappyStudyDatabaseNameForRow(NSDictionary *row)
{
  NSString *name;
  NSString *path;

  path = StrappyStudyStringForRow(row, @"path");
  name = [path lastPathComponent];
  if ([name length] > 0U) {
    return name;
  }
  return StrappyStudyStringForRow(row, @"database_id");
}

static NSString *StrappyStudyAppNameForRow(NSDictionary *row)
{
  NSString *appName;

  appName = StrappyStudyStringForRow(row, @"app_name");
  return ([appName length] > 0U) ?
    appName : NSLocalizedString(@"Other", nil);
}

static NSString *StrappyStudyAppGroupKeyForRow(NSDictionary *row)
{
  NSString *appName;
  NSString *groupKey;

  groupKey = StrappyStudyStringForRow(row, @"app_group_key");
  if ([groupKey length] > 0U) {
    return groupKey;
  }
  appName = StrappyStudyAppNameForRow(row);
  return [@"app-name:" stringByAppendingString:[appName lowercaseString]];
}

static BOOL StrappyStudyRowIsStudied(NSDictionary *row)
{
  NSNumber *studied;

  studied = [row objectForKey:@"studied"];
  return ([studied isKindOfClass:[NSNumber class]] && [studied boolValue]) ?
    YES : NO;
}

static NSString *StrappyStudyDetailsForRow(NSDictionary *row)
{
  return [NSString stringWithFormat:@"%@:\n%@\n\n%@:\n%@",
    NSLocalizedString(@"Description", nil),
    StrappyStudyStringForRow(row, @"description"),
    NSLocalizedString(@"Context", nil),
    StrappyStudyStringForRow(row, @"context")];
}

static NSComparisonResult StrappyStudyCompareStrings(NSString *left,
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

static NSComparisonResult StrappyStudyCompareRows(id left,
                                                  id right,
                                                  void *context)
{
  NSDictionary *leftRow;
  NSDictionary *rightRow;
  NSComparisonResult result;

  (void)context;
  leftRow = [left isKindOfClass:[NSDictionary class]] ? left : nil;
  rightRow = [right isKindOfClass:[NSDictionary class]] ? right : nil;
  result = StrappyStudyCompareStrings(StrappyStudyAppNameForRow(leftRow),
                                      StrappyStudyAppNameForRow(rightRow));
  if (result != NSOrderedSame) {
    return result;
  }
  result = StrappyStudyCompareStrings(StrappyStudyAppGroupKeyForRow(leftRow),
                                      StrappyStudyAppGroupKeyForRow(rightRow));
  if (result != NSOrderedSame) {
    return result;
  }
  result = StrappyStudyCompareStrings(StrappyStudyDatabaseNameForRow(leftRow),
                                      StrappyStudyDatabaseNameForRow(rightRow));
  if (result != NSOrderedSame) {
    return result;
  }
  return StrappyStudyCompareStrings(
    StrappyStudyStringForRow(leftRow, @"path"),
    StrappyStudyStringForRow(rightRow, @"path"));
}

static NSArray *StrappyStudySectionsForRows(NSArray *rows)
{
  NSMutableArray *validRows;
  NSMutableArray *sections;
  NSMutableArray *sectionRows;
  NSArray *sortedRows;
  NSString *currentGroupKey;
  NSString *currentTitle;
  NSUInteger index;

  validRows = [NSMutableArray array];
  for (index = 0U; index < [rows count]; index++) {
    NSDictionary *row;

    row = [rows objectAtIndex:index];
    if (![row isKindOfClass:[NSDictionary class]]) {
      continue;
    }
    [validRows addObject:row];
  }
  sortedRows =
    [validRows sortedArrayUsingFunction:StrappyStudyCompareRows context:NULL];

  sections = [NSMutableArray array];
  sectionRows = nil;
  currentGroupKey = nil;
  currentTitle = nil;
  for (index = 0U; index < [sortedRows count]; index++) {
    NSDictionary *row;
    NSString *groupKey;

    row = [sortedRows objectAtIndex:index];
    groupKey = StrappyStudyAppGroupKeyForRow(row);
    if ((currentGroupKey == nil) ||
        ![currentGroupKey isEqualToString:groupKey]) {
      if ([sectionRows count] > 0U) {
        [sections addObject:[NSDictionary dictionaryWithObjectsAndKeys:
          currentTitle, @"title",
          [NSArray arrayWithArray:sectionRows], @"rows",
          nil]];
      }
      currentGroupKey = groupKey;
      currentTitle = StrappyStudyAppNameForRow(row);
      sectionRows = [NSMutableArray array];
    }
    [sectionRows addObject:row];
  }
  if ([sectionRows count] > 0U) {
    [sections addObject:[NSDictionary dictionaryWithObjectsAndKeys:
      currentTitle, @"title",
      [NSArray arrayWithArray:sectionRows], @"rows",
      nil]];
  }
  return sections;
}

@interface StrappyPreferencesDatabaseStudyViewController ()
- (void)reloadStudyRows;
- (void)reloadStudySections;
- (void)updateStudyProgress;
- (void)updateStudyActionButtonForAllStudied:(BOOL)allStudied;
- (NSDictionary *)studyRowAtIndexPath:(NSIndexPath *)indexPath;
- (NSIndexPath *)indexPathForDatabaseIdentifier:(NSString *)databaseIdentifier;
- (BOOL)studyRowIsExpanded:(NSDictionary *)row;
- (void)invalidateExpandedStudyMeasurement;
- (void)prepareExpandedStudyMeasurementForRow:(NSDictionary *)row
                                    tableView:(UITableView *)tableView;
- (CGFloat)expandedStudyRowHeightForRow:(NSDictionary *)row
                              tableView:(UITableView *)tableView;
- (NSString *)expandedStudyDetailsForRow:(NSDictionary *)row
                               tableView:(UITableView *)tableView;
- (NSString *)studyDateForRow:(NSDictionary *)row;
- (void)resetAction:(id)sender;
- (void)studyAction:(id)sender;
- (void)showError:(NSError *)error title:(NSString *)title;
@end

@implementation StrappyPreferencesDatabaseStudyViewController

- (id)init
{
  if ((self = [super initWithStyle:UITableViewStylePlain])) {
    [[self navigationItem] setTitle:NSLocalizedString(@"Study", nil)];
    studyRows_ = [NSArray array];
    studySections_ = [NSArray array];
    studyDateFormatter_ = [[NSDateFormatter alloc] init];
    [studyDateFormatter_ setFormatterBehavior:NSDateFormatterBehavior10_4];
    [studyDateFormatter_ setDateStyle:NSDateFormatterShortStyle];
    [studyDateFormatter_ setTimeStyle:NSDateFormatterShortStyle];
    expandedDatabaseIdentifier_ = nil;
    measuredDatabaseIdentifier_ = nil;
    measuredStudyDetails_ = nil;
    measuredStudyWidth_ = 0.0f;
    measuredStudyRowHeight_ = 0.0f;
  }
  return self;
}

- (void)viewDidLoad
{
  UIBarButtonItem *leftSpace;
  UIBarButtonItem *rightSpace;
  UIBarButtonItem *statusItem;
  UILabel *statusLabel;

  [super viewDidLoad];
  [[self tableView] setAllowsSelection:YES];

  studyActionButton_ = [[UIBarButtonItem alloc]
    initWithTitle:NSLocalizedString(@"Study", nil)
            style:UIBarButtonItemStyleDone
           target:self
           action:@selector(studyAction:)];
  [[self navigationItem] setRightBarButtonItem:studyActionButton_];
  leftSpace = [[UIBarButtonItem alloc]
    initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace
                         target:nil
                         action:NULL];
  rightSpace = [[UIBarButtonItem alloc]
    initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace
                         target:nil
                         action:NULL];
  statusLabel = [[UILabel alloc] initWithFrame:
    CGRectMake(0.0f, 0.0f, 120.0f, 30.0f)];
  [statusLabel setBackgroundColor:[UIColor clearColor]];
  [statusLabel setTextColor:[UIColor whiteColor]];
  [statusLabel setShadowColor:[UIColor colorWithWhite:0.0f alpha:0.5f]];
  [statusLabel setShadowOffset:CGSizeMake(0.0f, -1.0f)];
  [statusLabel setFont:[UIFont boldSystemFontOfSize:15.0f]];
  [statusLabel setNumberOfLines:1];
  [statusLabel XP_setTextAlignmentCenter];
  statusLabel_ = statusLabel;
  statusItem = [[UIBarButtonItem alloc] initWithCustomView:statusLabel];
  [self setToolbarItems:[NSArray arrayWithObjects:
    leftSpace, statusItem, rightSpace, nil]
              animated:NO];
}

- (void)viewWillAppear:(BOOL)animated
{
  [super viewWillAppear:animated];
  [[self navigationController] setToolbarHidden:NO animated:animated];
  [self reloadStudyRows];
}

- (void)reloadStudyRows
{
  NSError *error;
  NSArray *rows;

  error = nil;
  rows = [StrappySession databaseStudyRowsWithError:&error];
  if (![rows isKindOfClass:[NSArray class]]) {
    studyRows_ = [NSArray array];
    [self updateStudyActionButtonForAllStudied:NO];
    [statusLabel_ setText:NSLocalizedString(@"— of —", nil)];
    [self reloadStudySections];
    [self showError:error title:NSLocalizedString(@"Could Not Load Study", nil)];
    return;
  }
  studyRows_ = rows;
  [self updateStudyProgress];
  [self reloadStudySections];
}

- (void)reloadStudySections
{
  NSDictionary *expandedRow;
  NSIndexPath *expandedIndexPath;

  studySections_ = StrappyStudySectionsForRows(studyRows_);
  expandedIndexPath =
    [self indexPathForDatabaseIdentifier:expandedDatabaseIdentifier_];
  expandedRow = (expandedIndexPath != nil) ?
    [self studyRowAtIndexPath:expandedIndexPath] : nil;
  if ((expandedDatabaseIdentifier_ != nil) &&
      !StrappyStudyRowIsStudied(expandedRow)) {
    expandedDatabaseIdentifier_ = nil;
  }
  [self invalidateExpandedStudyMeasurement];
  [[self tableView] reloadData];
}

- (void)updateStudyProgress
{
  BOOL allStudied;
  NSUInteger index;
  NSUInteger studiedCount;

  studiedCount = 0U;
  for (index = 0U; index < [studyRows_ count]; index++) {
    NSDictionary *row;

    row = [studyRows_ objectAtIndex:index];
    if ([row isKindOfClass:[NSDictionary class]] &&
        StrappyStudyRowIsStudied(row)) {
      studiedCount++;
    }
  }
  allStudied = ([studyRows_ count] > 0U) &&
    (studiedCount == [studyRows_ count]);
  [self updateStudyActionButtonForAllStudied:allStudied];
  [statusLabel_ setText:[NSString stringWithFormat:
    NSLocalizedString(@"%lu of %lu", nil),
    (unsigned long)studiedCount,
    (unsigned long)[studyRows_ count]]];
}

- (void)updateStudyActionButtonForAllStudied:(BOOL)allStudied
{
  [studyActionButton_ setTitle:allStudied ?
    NSLocalizedString(@"Reset", nil) : NSLocalizedString(@"Study", nil)];
  [studyActionButton_ setStyle:allStudied ?
    UIBarButtonItemStyleBordered : UIBarButtonItemStyleDone];
  [studyActionButton_ setAction:allStudied ?
    @selector(resetAction:) : @selector(studyAction:)];
}

- (NSDictionary *)studyRowAtIndexPath:(NSIndexPath *)indexPath
{
  NSDictionary *section;
  NSArray *rows;

  if (([indexPath section] < 0) ||
      ((NSUInteger)[indexPath section] >= [studySections_ count])) {
    return nil;
  }
  section = [studySections_ objectAtIndex:(NSUInteger)[indexPath section]];
  rows = [section objectForKey:@"rows"];
  if (![rows isKindOfClass:[NSArray class]] ||
      ([indexPath row] < 0) ||
      ((NSUInteger)[indexPath row] >= [rows count])) {
    return nil;
  }
  return [rows objectAtIndex:(NSUInteger)[indexPath row]];
}

- (NSIndexPath *)indexPathForDatabaseIdentifier:(NSString *)databaseIdentifier
{
  NSUInteger rowIndex;
  NSUInteger sectionIndex;

  if ([databaseIdentifier length] == 0U) {
    return nil;
  }
  for (sectionIndex = 0U;
       sectionIndex < [studySections_ count];
       sectionIndex++) {
    NSDictionary *section;
    NSArray *rows;

    section = [studySections_ objectAtIndex:sectionIndex];
    rows = [section objectForKey:@"rows"];
    if (![rows isKindOfClass:[NSArray class]]) {
      continue;
    }
    for (rowIndex = 0U; rowIndex < [rows count]; rowIndex++) {
      NSDictionary *row;
      NSString *candidateIdentifier;

      row = [rows objectAtIndex:rowIndex];
      candidateIdentifier =
        StrappyStudyStringForRow(row, @"database_id");
      if ([candidateIdentifier isEqualToString:databaseIdentifier]) {
        return [NSIndexPath indexPathForRow:(NSInteger)rowIndex
                                 inSection:(NSInteger)sectionIndex];
      }
    }
  }
  return nil;
}

- (BOOL)studyRowIsExpanded:(NSDictionary *)row
{
  NSString *databaseIdentifier;

  if (!StrappyStudyRowIsStudied(row) ||
      ([expandedDatabaseIdentifier_ length] == 0U)) {
    return NO;
  }
  databaseIdentifier = StrappyStudyStringForRow(row, @"database_id");
  return [databaseIdentifier isEqualToString:expandedDatabaseIdentifier_] ?
    YES : NO;
}

- (void)invalidateExpandedStudyMeasurement
{
  measuredDatabaseIdentifier_ = nil;
  measuredStudyDetails_ = nil;
  measuredStudyWidth_ = 0.0f;
  measuredStudyRowHeight_ = 0.0f;
}

- (void)prepareExpandedStudyMeasurementForRow:(NSDictionary *)row
                                    tableView:(UITableView *)tableView
{
  NSString *databaseIdentifier;
  NSString *details;
  UIFont *font;
  CGSize constraint;
  CGSize measuredSize;
  CGFloat detailWidth;
  CGFloat rowHeight;

  if (![self studyRowIsExpanded:row]) {
    return;
  }

  databaseIdentifier = StrappyStudyStringForRow(row, @"database_id");
  detailWidth = CGRectGetWidth([tableView bounds]) -
    (2.0f * kStrappyDatabaseStudyCellInset);
  if (([databaseIdentifier length] == 0U) || (detailWidth <= 0.0f)) {
    return;
  }
  if ([databaseIdentifier isEqualToString:measuredDatabaseIdentifier_] &&
      (measuredStudyWidth_ == detailWidth) &&
      (measuredStudyDetails_ != nil) &&
      (measuredStudyRowHeight_ >= kStrappyDatabaseStudyCompactRowHeight)) {
    return;
  }

  details = StrappyStudyDetailsForRow(row);
  font = StrappyDatabaseStudyDetailsFont();
  constraint =
    CGSizeMake(detailWidth, kStrappyDatabaseStudyMeasurementHeight);
  measuredSize = [details XP_sizeWithFont:font constrainedToSize:constraint];
  rowHeight = kStrappyDatabaseStudyTextTop +
    measuredSize.height + kStrappyDatabaseStudyCellInset;
  if (rowHeight < kStrappyDatabaseStudyCompactRowHeight) {
    rowHeight = kStrappyDatabaseStudyCompactRowHeight;
  }

  measuredDatabaseIdentifier_ = [databaseIdentifier copy];
  measuredStudyDetails_ = [details copy];
  measuredStudyWidth_ = detailWidth;
  measuredStudyRowHeight_ = rowHeight;
}

- (CGFloat)expandedStudyRowHeightForRow:(NSDictionary *)row
                              tableView:(UITableView *)tableView
{
  [self prepareExpandedStudyMeasurementForRow:row tableView:tableView];
  return (measuredStudyRowHeight_ >= kStrappyDatabaseStudyCompactRowHeight) ?
    measuredStudyRowHeight_ : kStrappyDatabaseStudyCompactRowHeight;
}

- (NSString *)expandedStudyDetailsForRow:(NSDictionary *)row
                               tableView:(UITableView *)tableView
{
  [self prepareExpandedStudyMeasurementForRow:row tableView:tableView];
  return (measuredStudyDetails_ != nil) ? measuredStudyDetails_ : @"";
}

- (NSString *)studyDateForRow:(NSDictionary *)row
{
  NSDate *date;
  NSNumber *studiedAt;
  NSTimeInterval seconds;

  if (!StrappyStudyRowIsStudied(row)) {
    return @"";
  }
  studiedAt = [row objectForKey:@"studied_at_ms"];
  if (![studiedAt isKindOfClass:[NSNumber class]] ||
      ([studiedAt longLongValue] <= 0LL)) {
    return @"";
  }
  seconds = (NSTimeInterval)[studiedAt longLongValue] / 1000.0;
  date = [NSDate dateWithTimeIntervalSince1970:seconds];
  return [studyDateFormatter_ stringFromDate:date];
}

- (CGFloat)tableView:(UITableView *)tableView
heightForRowAtIndexPath:(NSIndexPath *)indexPath
{
  NSDictionary *row;

  row = [self studyRowAtIndexPath:indexPath];
  if (![self studyRowIsExpanded:row]) {
    return kStrappyDatabaseStudyCompactRowHeight;
  }
  return [self expandedStudyRowHeightForRow:row tableView:tableView];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
  (void)tableView;
  return (NSInteger)[studySections_ count];
}

- (NSInteger)tableView:(UITableView *)tableView
 numberOfRowsInSection:(NSInteger)section
{
  NSDictionary *sectionInfo;
  NSArray *rows;

  (void)tableView;
  if ((section < 0) || ((NSUInteger)section >= [studySections_ count])) {
    return 0;
  }
  sectionInfo = [studySections_ objectAtIndex:(NSUInteger)section];
  rows = [sectionInfo objectForKey:@"rows"];
  return [rows isKindOfClass:[NSArray class]] ? (NSInteger)[rows count] : 0;
}

- (NSString *)tableView:(UITableView *)tableView
titleForHeaderInSection:(NSInteger)section
{
  NSDictionary *sectionInfo;
  NSString *title;

  (void)tableView;
  if ((section < 0) || ((NSUInteger)section >= [studySections_ count])) {
    return nil;
  }
  sectionInfo = [studySections_ objectAtIndex:(NSUInteger)section];
  title = [sectionInfo objectForKey:@"title"];
  return [title isKindOfClass:[NSString class]] ? title : nil;
}

- (UITableViewCell *)tableView:(UITableView *)tableView
         cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
  static NSString * const cellIdentifier = @"DatabaseStudyCell";
  StrappyDatabaseStudyCell *cell;
  NSString *accessibilityHint;
  NSString *details;
  NSDictionary *row;
  BOOL expanded;
  BOOL studied;

  cell = (StrappyDatabaseStudyCell *)
    [tableView dequeueReusableCellWithIdentifier:cellIdentifier];
  if (cell == nil) {
    cell = [[StrappyDatabaseStudyCell alloc]
      initWithStyle:UITableViewCellStyleDefault
    reuseIdentifier:cellIdentifier];
  }
  row = [self studyRowAtIndexPath:indexPath];
  studied = StrappyStudyRowIsStudied(row);
  expanded = [self studyRowIsExpanded:row];
  details = expanded ?
    [self expandedStudyDetailsForRow:row tableView:tableView] : @"";
  [cell setDatabaseName:StrappyStudyDatabaseNameForRow(row)
              studyDate:[self studyDateForRow:row]
                 details:details
                 studied:studied
                expanded:expanded];
  [cell setAccessoryType:UITableViewCellAccessoryNone];
  [cell setSelectionStyle:UITableViewCellSelectionStyleNone];
  accessibilityHint = nil;
  if (studied) {
    accessibilityHint = expanded ?
      NSLocalizedString(@"Hides the recorded description and context.", nil) :
      NSLocalizedString(@"Shows the recorded description and context.", nil);
  }
  [cell setAccessibilityHint:accessibilityHint];
  return cell;
}

- (BOOL)tableView:(UITableView *)tableView
canEditRowAtIndexPath:(NSIndexPath *)indexPath
{
  (void)tableView;
  return StrappyStudyRowIsStudied([self studyRowAtIndexPath:indexPath]);
}

- (UITableViewCellEditingStyle)tableView:(UITableView *)tableView
 editingStyleForRowAtIndexPath:(NSIndexPath *)indexPath
{
  (void)tableView;
  return StrappyStudyRowIsStudied([self studyRowAtIndexPath:indexPath])
    ? UITableViewCellEditingStyleDelete
    : UITableViewCellEditingStyleNone;
}

- (NSString *)tableView:(UITableView *)tableView
titleForDeleteConfirmationButtonForRowAtIndexPath:(NSIndexPath *)indexPath
{
  (void)tableView;
  (void)indexPath;
  return NSLocalizedString(@"Delete", nil);
}

- (void)tableView:(UITableView *)tableView
commitEditingStyle:(UITableViewCellEditingStyle)editingStyle
forRowAtIndexPath:(NSIndexPath *)indexPath
{
  NSString *databaseIdentifier;
  NSDictionary *row;
  NSError *error;

  if (editingStyle != UITableViewCellEditingStyleDelete) {
    return;
  }

  row = [self studyRowAtIndexPath:indexPath];
  if (!StrappyStudyRowIsStudied(row)) {
    [tableView setEditing:NO animated:YES];
    return;
  }
  databaseIdentifier = StrappyStudyStringForRow(row, @"database_id");
  error = nil;
  if (![StrappySession deleteDatabaseStudyValuesForDatabaseIdentifier:
        databaseIdentifier
                                                               error:&error]) {
    [tableView setEditing:NO animated:YES];
    [self showError:error
              title:NSLocalizedString(@"Could Not Delete Study", nil)];
    return;
  }
  [self reloadStudyRows];
}

- (NSIndexPath *)tableView:(UITableView *)tableView
  willSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  NSDictionary *row;

  (void)tableView;
  row = [self studyRowAtIndexPath:indexPath];
  return StrappyStudyRowIsStudied(row) ? indexPath : nil;
}

- (void)tableView:(UITableView *)tableView
didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  NSString *databaseIdentifier;
  NSString *previousIdentifier;
  NSIndexPath *previousIndexPath;
  NSIndexPath *updatedIndexPath;
  NSMutableArray *indexPaths;
  NSDictionary *row;

  [tableView deselectRowAtIndexPath:indexPath animated:YES];
  row = [self studyRowAtIndexPath:indexPath];
  if (!StrappyStudyRowIsStudied(row)) {
    return;
  }

  previousIdentifier = expandedDatabaseIdentifier_;
  databaseIdentifier = StrappyStudyStringForRow(row, @"database_id");
  if ([databaseIdentifier isEqualToString:previousIdentifier]) {
    expandedDatabaseIdentifier_ = nil;
  } else {
    expandedDatabaseIdentifier_ = [databaseIdentifier copy];
  }
  [self invalidateExpandedStudyMeasurement];

  indexPaths = [NSMutableArray arrayWithCapacity:2U];
  previousIndexPath =
    [self indexPathForDatabaseIdentifier:previousIdentifier];
  updatedIndexPath =
    [self indexPathForDatabaseIdentifier:expandedDatabaseIdentifier_];
  if (previousIndexPath != nil) {
    [indexPaths addObject:previousIndexPath];
  }
  if ((updatedIndexPath != nil) &&
      ![updatedIndexPath isEqual:previousIndexPath]) {
    [indexPaths addObject:updatedIndexPath];
  }
  if ([indexPaths count] == 0U) {
    return;
  }

  [tableView beginUpdates];
  [tableView reloadRowsAtIndexPaths:indexPaths
                   withRowAnimation:UITableViewRowAnimationNone];
  [tableView endUpdates];
}

- (void)resetAction:(id)sender
{
  UIAlertView *alert;

  (void)sender;
  alert = [[UIAlertView alloc]
    initWithTitle:NSLocalizedString(@"Reset Database Study?", nil)
          message:NSLocalizedString(
            @"This clears every stored database description and context.", nil)
         delegate:self
cancelButtonTitle:NSLocalizedString(@"Cancel", nil)
otherButtonTitles:NSLocalizedString(@"Reset", nil), nil];
  [alert setTag:kStrappyDatabaseStudyResetAlertTag];
  [alert show];
}

- (void)studyAction:(id)sender
{
  UIActionSheet *actionSheet;

  actionSheet = [[UIActionSheet alloc]
    initWithTitle:NSLocalizedString(
      @"The default model will be used to study approved databases that are currently not studied.",
      nil)
            delegate:self
   cancelButtonTitle:NSLocalizedString(@"Cancel", nil)
destructiveButtonTitle:nil
   otherButtonTitles:NSLocalizedString(@"Study", nil), nil];
  [actionSheet setTag:kStrappyDatabaseStudyRunActionSheetTag];
  if ([sender isKindOfClass:[UIBarButtonItem class]]) {
    [actionSheet showFromBarButtonItem:(UIBarButtonItem *)sender animated:YES];
  } else {
    [actionSheet showInView:[self view]];
  }
}

- (void)alertView:(UIAlertView *)alertView
clickedButtonAtIndex:(NSInteger)buttonIndex
{
  NSError *error;

  if (buttonIndex == [alertView cancelButtonIndex]) {
    return;
  }
  error = nil;
  if ([alertView tag] == kStrappyDatabaseStudyResetAlertTag) {
    if (![StrappySession resetDatabaseStudyWithError:&error]) {
      [self showError:error title:NSLocalizedString(@"Could Not Reset Study", nil)];
      return;
    }
    [self reloadStudyRows];
  }
}

- (void)actionSheet:(UIActionSheet *)actionSheet
clickedButtonAtIndex:(NSInteger)buttonIndex
{
  NSError *error;

  if (([actionSheet tag] != kStrappyDatabaseStudyRunActionSheetTag) ||
      (buttonIndex == [actionSheet cancelButtonIndex])) {
    return;
  }
  error = nil;
  if ([StrappySession beginDatabaseStudyWithError:&error] == nil) {
    [self showError:error title:NSLocalizedString(@"Could Not Start Study", nil)];
    return;
  }
  [self dismissModalViewControllerAnimated:YES];
}

- (void)showError:(NSError *)error title:(NSString *)title
{
  NSString *message;
  UIAlertView *alert;

  message = [error localizedDescription];
  if ([message length] == 0U) {
    message = NSLocalizedString(@"The request failed.", nil);
  }
  alert = [[UIAlertView alloc] initWithTitle:title
                                     message:message
                                    delegate:nil
                           cancelButtonTitle:NSLocalizedString(@"OK", nil)
                           otherButtonTitles:nil];
  [alert show];
}

@end
