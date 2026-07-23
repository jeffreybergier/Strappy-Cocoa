#import "StrappyPreferencesDatabaseStudyViewController.h"

#import "AIFontAwesome.h"
#import "StrappySession.h"
#import "XPUIKit.h"

static const CGFloat kStrappyDatabaseStudyFilterIconSize = 18.0f;
static const CGFloat kStrappyDatabaseStudyFilterCanvasSize = 28.0f;

enum {
  kStrappyDatabaseStudyResetAlertTag = 9101,
  kStrappyDatabaseStudyRunActionSheetTag = 9102
};

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

static NSArray *StrappyStudySectionsForRows(NSArray *rows,
                                            BOOL unstudiedOnly)
{
  NSMutableArray *filteredRows;
  NSMutableArray *sections;
  NSMutableArray *sectionRows;
  NSArray *sortedRows;
  NSString *currentGroupKey;
  NSString *currentTitle;
  NSUInteger index;

  filteredRows = [NSMutableArray array];
  for (index = 0U; index < [rows count]; index++) {
    NSDictionary *row;

    row = [rows objectAtIndex:index];
    if (![row isKindOfClass:[NSDictionary class]] ||
        (unstudiedOnly && StrappyStudyRowIsStudied(row))) {
      continue;
    }
    [filteredRows addObject:row];
  }
  sortedRows =
    [filteredRows sortedArrayUsingFunction:StrappyStudyCompareRows context:NULL];

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
- (void)applyStudyFilter;
- (void)updateFilterButton;
- (void)updateStudyProgress;
- (NSDictionary *)studyRowAtIndexPath:(NSIndexPath *)indexPath;
- (NSString *)studyDateForRow:(NSDictionary *)row;
- (void)filterAction:(id)sender;
- (void)resetAction:(id)sender;
- (void)studyAction:(id)sender;
- (void)showError:(NSError *)error title:(NSString *)title;
@end

@implementation StrappyPreferencesDatabaseStudyViewController

- (id)init
{
  if ((self = [super initWithStyle:UITableViewStylePlain])) {
    [[self navigationItem] setTitle:NSLocalizedString(@"Study", nil)];
    allStudyRows_ = [NSArray array];
    studySections_ = [NSArray array];
    showsUnstudiedOnly_ = NO;
    studyDateFormatter_ = [[NSDateFormatter alloc] init];
    [studyDateFormatter_ setFormatterBehavior:NSDateFormatterBehavior10_4];
    [studyDateFormatter_ setDateStyle:NSDateFormatterShortStyle];
    [studyDateFormatter_ setTimeStyle:NSDateFormatterShortStyle];
  }
  return self;
}

- (void)viewDidLoad
{
  UIBarButtonItem *leftSpace;
  UIBarButtonItem *resetButton;
  UIBarButtonItem *rightSpace;
  UIBarButtonItem *statusItem;
  UIBarButtonItem *studyButton;
  UILabel *statusLabel;
  UIImage *filterImage;

  [super viewDidLoad];
  [[self tableView] setAllowsSelection:NO];

  filterImage = [AIFontAwesome imageForIcon:AIFAFilter
                                       style:AIFontAwesomeStyleSolid
                                    iconSize:kStrappyDatabaseStudyFilterIconSize
                                  canvasSize:kStrappyDatabaseStudyFilterCanvasSize
                                       color:[UIColor whiteColor]
                                       scale:0.0f];
  filterButton_ = [[UIBarButtonItem alloc] initWithImage:filterImage
                                                   style:UIBarButtonItemStyleBordered
                                                  target:self
                                                  action:@selector(filterAction:)];
  [filterButton_ setEnabled:NO];
  [[self navigationItem] setRightBarButtonItem:filterButton_];
  [self updateFilterButton];

  resetButton = [[UIBarButtonItem alloc]
    initWithTitle:NSLocalizedString(@"Reset", nil)
            style:UIBarButtonItemStyleBordered
           target:self
           action:@selector(resetAction:)];
  studyButton = [[UIBarButtonItem alloc]
    initWithTitle:NSLocalizedString(@"Study", nil)
            style:UIBarButtonItemStyleDone
           target:self
           action:@selector(studyAction:)];
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
    resetButton, leftSpace, statusItem, rightSpace, studyButton, nil]
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
    allStudyRows_ = [NSArray array];
    showsUnstudiedOnly_ = NO;
    [filterButton_ setEnabled:NO];
    [self updateFilterButton];
    [statusLabel_ setText:NSLocalizedString(@"— of —", nil)];
    [self applyStudyFilter];
    [self showError:error title:NSLocalizedString(@"Could Not Load Study", nil)];
    return;
  }
  allStudyRows_ = rows;
  [self updateStudyProgress];
  [self applyStudyFilter];
}

- (void)applyStudyFilter
{
  studySections_ =
    StrappyStudySectionsForRows(allStudyRows_, showsUnstudiedOnly_);
  [[self tableView] reloadData];
}

- (void)updateStudyProgress
{
  BOOL allStudied;
  NSUInteger index;
  NSUInteger studiedCount;

  studiedCount = 0U;
  for (index = 0U; index < [allStudyRows_ count]; index++) {
    NSDictionary *row;

    row = [allStudyRows_ objectAtIndex:index];
    if ([row isKindOfClass:[NSDictionary class]] &&
        StrappyStudyRowIsStudied(row)) {
      studiedCount++;
    }
  }
  allStudied = (studiedCount == [allStudyRows_ count]) ? YES : NO;
  if (allStudied) {
    showsUnstudiedOnly_ = NO;
  }
  [filterButton_ setEnabled:allStudied ? NO : YES];
  [self updateFilterButton];
  [statusLabel_ setText:[NSString stringWithFormat:
    NSLocalizedString(@"%lu of %lu", nil),
    (unsigned long)studiedCount,
    (unsigned long)[allStudyRows_ count]]];
}

- (void)updateFilterButton
{
  [filterButton_ setStyle:showsUnstudiedOnly_ ?
    UIBarButtonItemStyleDone : UIBarButtonItemStyleBordered];
  [filterButton_ setAccessibilityLabel:showsUnstudiedOnly_ ?
    NSLocalizedString(@"Show All Databases", nil) :
    NSLocalizedString(@"Show Unstudied Databases", nil)];
}

- (void)filterAction:(id)sender
{
  (void)sender;
  showsUnstudiedOnly_ = showsUnstudiedOnly_ ? NO : YES;
  [self updateFilterButton];
  [self applyStudyFilter];
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
  UITableViewCell *cell;
  NSDictionary *row;
  BOOL studied;

  cell = [tableView dequeueReusableCellWithIdentifier:cellIdentifier];
  if (cell == nil) {
    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                  reuseIdentifier:cellIdentifier];
    [[cell textLabel] setNumberOfLines:1];
    [[cell detailTextLabel] setNumberOfLines:1];
  }
  row = [self studyRowAtIndexPath:indexPath];
  studied = StrappyStudyRowIsStudied(row);
  [[cell textLabel] setText:StrappyStudyDatabaseNameForRow(row)];
  [[cell detailTextLabel] setText:[self studyDateForRow:row]];
  [[cell detailTextLabel] setTextColor:[UIColor grayColor]];
  [cell setAccessoryType:studied ?
    UITableViewCellAccessoryCheckmark : UITableViewCellAccessoryNone];
  [cell setSelectionStyle:UITableViewCellSelectionStyleNone];
  return cell;
}

- (NSIndexPath *)tableView:(UITableView *)tableView
  willSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  (void)tableView;
  (void)indexPath;
  return nil;
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
