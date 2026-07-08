#import "SessionListViewController.h"

#import "AIFontAwesome.h"
#import "PreferencesTableViewController.h"
#import "StrappySession.h"
#import "XPFoundation.h"
#import "XPUIKit.h"

static NSString * const kStrappySessionCellIdentifier = @"StrappySessionCell";
static const CGFloat kStrappyPromptIconCanvasSize = 14.0f;
static const CGFloat kStrappyPromptIconSize = 10.0f;
static const AIFontAwesomeIcon kStrappySessionPromptActiveIcon =
  AIFAArrowsRotate;

static NSString *StrappySessionPromptPreview(NSDictionary *session)
{
  NSString *name;
  NSString *prompt;

  name = [session objectForKey:@"name"];
  if ([name isKindOfClass:[NSString class]] && ([name length] > 0U)) {
    return name;
  }

  prompt = [session objectForKey:@"prompt"];
  if (![prompt isKindOfClass:[NSString class]] || ([prompt length] == 0U)) {
    return NSLocalizedString(@"Untitled Session", nil);
  }

  return prompt;
}

static NSString *StrappySessionDisplayTimestamp(NSString *timestamp)
{
  static NSDateFormatter *inputFormatter = nil;
  static NSDateFormatter *displayFormatter = nil;
  NSDate *date;

  if (![timestamp isKindOfClass:[NSString class]] || ([timestamp length] == 0U)) {
    return @"";
  }

  if (inputFormatter == nil) {
    inputFormatter = [[NSDateFormatter alloc] init];
    [inputFormatter setFormatterBehavior:NSDateFormatterBehavior10_4];
    [inputFormatter setDateFormat:@"yyyy-MM-dd'T'HH:mm:ss.SSS'Z'"];
    [inputFormatter setTimeZone:[NSTimeZone timeZoneForSecondsFromGMT:0]];
  }
  if (displayFormatter == nil) {
    displayFormatter = [[NSDateFormatter alloc] init];
    [displayFormatter setFormatterBehavior:NSDateFormatterBehavior10_4];
    [displayFormatter setDateStyle:NSDateFormatterShortStyle];
    [displayFormatter setTimeStyle:NSDateFormatterShortStyle];
  }

  date = [inputFormatter dateFromString:timestamp];
  if (date == nil) {
    return timestamp;
  }
  return [displayFormatter stringFromDate:date];
}

static BOOL StrappySessionPromptIsInFlight(NSDictionary *session)
{
  NSNumber *identifier;

  identifier = [session objectForKey:@"id"];
  return [StrappySession isPromptInFlightForSessionIdentifier:identifier];
}

static NSString *StrappySessionSubtitle(NSDictionary *session)
{
  NSString *timestamp;

  timestamp =
    StrappySessionDisplayTimestamp([session objectForKey:@"last_message_at"]);
  if ([timestamp length] > 0U) {
    return timestamp;
  }
  return NSLocalizedString(@"No messages yet", nil);
}

@interface SessionListViewController () <UIAlertViewDelegate>
@property (nonatomic, copy) NSArray *sessions;
@property (nonatomic, copy) NSNumber *selectedSessionId;
@property (nonatomic, strong) UIBarButtonItem *addButton;
@property (nonatomic, strong) UIBarButtonItem *settingsButton;
@property (nonatomic, copy) NSNumber *pendingDeleteSessionIdentifier;
@property (nonatomic, assign) BOOL creatingSession;
- (void)strappySessionDidUpdate:(NSNotification *)notification;
- (void)sessionPromptActivityDidChange:(NSNotification *)notification;
- (NSIndexPath *)indexPathForSessionIdentifier:(NSNumber *)sessionIdentifier;
- (NSDictionary *)sessionAtIndexPath:(NSIndexPath *)indexPath;
- (BOOL)canDeleteSession:(NSDictionary *)session;
- (void)confirmDeleteSession:(NSDictionary *)session;
- (void)deleteSessionIdentifier:(NSNumber *)sessionIdentifier;
- (UIView *)promptInFlightAccessoryViewForCell:(UITableViewCell *)cell;
- (void)configureSessionCell:(UITableViewCell *)cell
                     session:(NSDictionary *)session;
- (void)notifySelectedSession;
- (void)setControlsEnabled:(BOOL)enabled;
- (void)showDeleteError:(NSError *)error;
- (void)showError:(NSError *)error title:(NSString *)title;
@end

@implementation SessionListViewController

- (instancetype)init
{
  if ((self = [super initWithStyle:UITableViewStylePlain])) {
    self.title = NSLocalizedString(@"Strappy", nil);
    self.sessions = [NSArray array];
  }
  return self;
}

- (void)viewDidLoad
{
  [super viewDidLoad];

  self.addButton =
    [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemAdd
                                                  target:self
                                                  action:@selector(addSession:)];
  [self setSettingsButton:
    [[UIBarButtonItem alloc] initWithImage:[self whiteBarIconForIcon:AIFAGear]
                                     style:UIBarButtonItemStyleBordered
                                    target:self
                                    action:@selector(settingsAction:)]];
  [[self settingsButton]
    setAccessibilityLabel:NSLocalizedString(@"Preferences", nil)];
  [[self navigationItem] setRightBarButtonItem:[self addButton]];
  [[self navigationItem] setLeftBarButtonItem:[self settingsButton]];

  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(strappySessionDidUpdate:)
           name:StrappySessionDidUpdateNotification
         object:nil];
  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(sessionPromptActivityDidChange:)
           name:StrappySessionPromptDidStartNotification
         object:nil];
  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(sessionPromptActivityDidChange:)
           name:StrappySessionPromptDidFinishNotification
         object:nil];

  [self reloadData];
}

- (void)reloadDataAction:(id)sender
{
  (void)sender;
  [self reloadData];
}

- (UIImage *)whiteBarIconForIcon:(AIFontAwesomeIcon)icon
{
  return [AIFontAwesome imageForIcon:icon
                               style:AIFontAwesomeStyleSolid
                            iconSize:18.0f
                          canvasSize:28.0f
                               color:[UIColor whiteColor]
                               scale:0.0f];
}

- (void)settingsAction:(id)sender
{
  PreferencesTableViewController *preferences;
  UINavigationController *navigationController;

  (void)sender;
  preferences = [[PreferencesTableViewController alloc] init];
  navigationController =
    [[UINavigationController alloc] initWithRootViewController:preferences];
  [self presentModalViewController:navigationController animated:YES];
}

- (void)setControlsEnabled:(BOOL)enabled
{
  [[self addButton] setEnabled:enabled];
  [[self settingsButton] setEnabled:enabled];
}

- (void)reloadData
{
  NSError *error;
  NSArray *sessions;

  error = nil;
  sessions = [StrappySession sessionSummariesWithError:&error];
  if (sessions == nil) {
    self.sessions = [NSArray array];
  } else {
    self.sessions = sessions;
  }

  [self.tableView reloadData];
  [self selectSessionIdentifier:self.selectedSessionId];
}

- (void)reloadSessionIdentifier:(NSNumber *)sessionIdentifier
                         select:(BOOL)select
{
  if (select) {
    self.selectedSessionId = sessionIdentifier;
  }
  [self reloadData];
  if (select) {
    [self notifySelectedSession];
  }
}

- (NSIndexPath *)indexPathForSessionIdentifier:(NSNumber *)sessionIdentifier
{
  NSUInteger index;

  if (![sessionIdentifier isKindOfClass:[NSNumber class]]) {
    return nil;
  }

  for (index = 0U; index < [self.sessions count]; index++) {
    NSDictionary *session;
    NSNumber *candidate;

    session = [self.sessions objectAtIndex:index];
    candidate = [session objectForKey:@"id"];
    if ([candidate isKindOfClass:[NSNumber class]] &&
        [candidate isEqualToNumber:sessionIdentifier]) {
      return [NSIndexPath indexPathForRow:(NSInteger)index inSection:0];
    }
  }
  return nil;
}

- (void)selectSessionIdentifier:(NSNumber *)sessionIdentifier
{
  NSIndexPath *indexPath;

  self.selectedSessionId = sessionIdentifier;
  indexPath = [self indexPathForSessionIdentifier:sessionIdentifier];
  if (indexPath != nil) {
    [self.tableView selectRowAtIndexPath:indexPath
                                animated:NO
                          scrollPosition:UITableViewScrollPositionMiddle];
  } else {
    NSIndexPath *selectedIndexPath;

    selectedIndexPath = [self.tableView indexPathForSelectedRow];
    if (selectedIndexPath != nil) {
      [self.tableView deselectRowAtIndexPath:selectedIndexPath animated:NO];
    }
  }
}

- (void)addSession:(id)sender
{
  NSError *error;
  StrappySession *session;
  NSNumber *identifier;

  (void)sender;
  if (self.creatingSession) {
    return;
  }

  self.creatingSession = YES;
  [self setControlsEnabled:NO];

  error = nil;
  session = [StrappySession createSessionWithError:&error];

  self.creatingSession = NO;
  [self setControlsEnabled:YES];

  identifier = [session sessionIdentifier];
  if (![identifier isKindOfClass:[NSNumber class]]) {
    [self showError:error
              title:NSLocalizedString(@"Could not create conversation", nil)];
    return;
  }

  [self reloadSessionIdentifier:identifier select:YES];
}

- (void)strappySessionDidUpdate:(NSNotification *)notification
{
  NSDictionary *session;
  NSNumber *identifier;

  session = [[notification userInfo] objectForKey:@"session"];
  if (![session isKindOfClass:[NSDictionary class]]) {
    [self reloadData];
    return;
  }

  identifier = [session objectForKey:@"id"];
  [self reloadSessionIdentifier:identifier select:NO];
}

- (void)sessionPromptActivityDidChange:(NSNotification *)notification
{
  StrappySession *session;
  NSNumber *identifier;
  NSDictionary *summary;

  session = [notification object];
  identifier = nil;
  if ([session isKindOfClass:[StrappySession class]]) {
    identifier = [session sessionIdentifier];
  }
  if (![identifier isKindOfClass:[NSNumber class]]) {
    summary = [[notification userInfo] objectForKey:@"session"];
    if ([summary isKindOfClass:[NSDictionary class]]) {
      identifier = [summary objectForKey:@"id"];
    }
  }
  [self reloadSessionIdentifier:identifier select:NO];
}

- (NSDictionary *)sessionAtIndexPath:(NSIndexPath *)indexPath
{
  NSUInteger row;

  if ((indexPath == nil) || (indexPath.section != 0)) {
    return nil;
  }
  row = (NSUInteger)indexPath.row;
  if (row >= [self.sessions count]) {
    return nil;
  }
  return [self.sessions objectAtIndex:row];
}

- (BOOL)canDeleteSession:(NSDictionary *)session
{
  NSNumber *sessionIdentifier;

  if (![session isKindOfClass:[NSDictionary class]]) {
    return NO;
  }

  sessionIdentifier = [session objectForKey:@"id"];
  if (![sessionIdentifier isKindOfClass:[NSNumber class]]) {
    return NO;
  }

  if (StrappySessionPromptIsInFlight(session) ||
      [StrappySession isPromptInFlightForSessionIdentifier:sessionIdentifier]) {
    return NO;
  }

  return YES;
}

- (void)confirmDeleteSession:(NSDictionary *)session
{
  NSNumber *sessionIdentifier;
  NSString *title;
  NSString *message;
  UIAlertView *alert;

  if (![self canDeleteSession:session]) {
    return;
  }

  sessionIdentifier = [session objectForKey:@"id"];
  title = StrappySessionPromptPreview(session);
  message = [NSString stringWithFormat:
    NSLocalizedString(@"This will permanently delete \"%@\" and all of its messages. This cannot be undone.", nil),
    title];

  [self setPendingDeleteSessionIdentifier:sessionIdentifier];
  alert = [[UIAlertView alloc] initWithTitle:NSLocalizedString(@"Delete Chat?", nil)
                                     message:message
                                    delegate:self
                           cancelButtonTitle:NSLocalizedString(@"Cancel", nil)
                           otherButtonTitles:NSLocalizedString(@"Delete", nil), nil];
  [alert show];
}

- (void)deleteSessionIdentifier:(NSNumber *)sessionIdentifier
{
  NSError *error;
  BOOL wasSelectedSession;

  if (![sessionIdentifier isKindOfClass:[NSNumber class]]) {
    return;
  }

  error = nil;
  if (![StrappySession deleteSessionWithIdentifier:sessionIdentifier
                                             error:&error]) {
    [self showDeleteError:error];
    return;
  }

  wasSelectedSession =
    ([[self selectedSessionId] isEqualToNumber:sessionIdentifier] ? YES : NO);
  if (wasSelectedSession) {
    [self setSelectedSessionId:nil];
  }

  [self reloadData];
  if (wasSelectedSession) {
    [self notifySelectedSession];
  }
}

- (UIView *)promptInFlightAccessoryViewForCell:(UITableViewCell *)cell
{
  UIColor *color;
  UIImage *image;
  UIImageView *imageView;

  color = [[cell detailTextLabel] textColor];
  if (color == nil) {
    color = [UIColor grayColor];
  }

  image = [AIFontAwesome imageForIcon:kStrappySessionPromptActiveIcon
                                style:AIFontAwesomeStyleSolid
                             iconSize:kStrappyPromptIconSize
                           canvasSize:kStrappyPromptIconCanvasSize
                                color:color
                                scale:0.0f];
  if (image == nil) {
    return nil;
  }

  imageView = [[UIImageView alloc] initWithImage:image];
  [imageView setFrame:CGRectMake(0.0f,
                                 0.0f,
                                 kStrappyPromptIconCanvasSize,
                                 kStrappyPromptIconCanvasSize)];
  return imageView;
}

- (void)notifySelectedSession
{
  NSIndexPath *indexPath;
  NSDictionary *summary;
  StrappySession *session;

  if ((self.delegate == nil) ||
      ![self.delegate respondsToSelector:@selector(sessionListViewController:didSelectSession:)]) {
    return;
  }

  indexPath = [self.tableView indexPathForSelectedRow];
  summary = [self sessionAtIndexPath:indexPath];
  session = [StrappySession sessionWithSummary:summary];
  [self.delegate sessionListViewController:self didSelectSession:session];
}

- (void)showDeleteError:(NSError *)error
{
  NSString *message;
  UIAlertView *alert;

  message = [error localizedDescription];
  if ([message length] == 0U) {
    message = NSLocalizedString(@"The chat could not be deleted.", nil);
  }

  alert = [[UIAlertView alloc] initWithTitle:NSLocalizedString(@"Could Not Delete Chat", nil)
                                     message:message
                                    delegate:nil
                           cancelButtonTitle:NSLocalizedString(@"OK", nil)
                           otherButtonTitles:nil];
  [alert show];
}

- (void)showError:(NSError *)error title:(NSString *)title
{
  NSString *message;
  UIAlertView *alert;

  message = [error localizedDescription];
  if ([message length] == 0U) {
    message = NSLocalizedString(@"An unknown error occurred.", nil);
  }

  alert = [[UIAlertView alloc] initWithTitle:title
                                     message:message
                                    delegate:nil
                           cancelButtonTitle:NSLocalizedString(@"OK", nil)
                           otherButtonTitles:nil];
  [alert show];
}

#pragma mark - UITableViewDataSource

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
  (void)tableView;
  return 1;
}

- (NSInteger)tableView:(UITableView *)tableView
 numberOfRowsInSection:(NSInteger)section
{
  (void)tableView;
  if (section != 0) {
    return 0;
  }
  return (NSInteger)[self.sessions count];
}

- (NSString *)tableView:(UITableView *)tableView
titleForHeaderInSection:(NSInteger)section
{
  (void)tableView;
  (void)section;
  return nil;
}

- (UITableViewCell *)tableView:(UITableView *)tableView
         cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
  UITableViewCell *cell;
  NSDictionary *session;

  cell = [tableView dequeueReusableCellWithIdentifier:kStrappySessionCellIdentifier];
  if (cell == nil) {
    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                  reuseIdentifier:kStrappySessionCellIdentifier];
    [[cell textLabel] setNumberOfLines:1];
    [[cell detailTextLabel] setNumberOfLines:1];
  }

  session = [self sessionAtIndexPath:indexPath];
  [self configureSessionCell:cell session:session];
  return cell;
}

- (BOOL)tableView:(UITableView *)tableView
canEditRowAtIndexPath:(NSIndexPath *)indexPath
{
  (void)tableView;
  return [self canDeleteSession:[self sessionAtIndexPath:indexPath]];
}

- (UITableViewCellEditingStyle)tableView:(UITableView *)tableView
 editingStyleForRowAtIndexPath:(NSIndexPath *)indexPath
{
  (void)tableView;
  return [self canDeleteSession:[self sessionAtIndexPath:indexPath]]
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
  NSDictionary *session;

  if (editingStyle != UITableViewCellEditingStyleDelete) {
    return;
  }

  session = [self sessionAtIndexPath:indexPath];
  if (![self canDeleteSession:session]) {
    [tableView setEditing:NO animated:YES];
    return;
  }

  [tableView setEditing:NO animated:YES];
  [self confirmDeleteSession:session];
}

- (void)configureSessionCell:(UITableViewCell *)cell
                     session:(NSDictionary *)session
{
  [[cell textLabel] setText:StrappySessionPromptPreview(session)];
  [[cell detailTextLabel] setText:StrappySessionSubtitle(session)];
  [[cell imageView] setImage:nil];
  if (StrappySessionPromptIsInFlight(session)) {
    [cell setAccessoryView:[self promptInFlightAccessoryViewForCell:cell]];
    [cell setAccessoryType:UITableViewCellAccessoryNone];
  } else {
    [cell setAccessoryView:nil];
    [cell setAccessoryType:UITableViewCellAccessoryNone];
  }
  [cell setEditingAccessoryType:UITableViewCellAccessoryNone];
  [cell setSelectionStyle:UITableViewCellSelectionStyleBlue];
}

#pragma mark - UITableViewDelegate

- (NSIndexPath *)tableView:(UITableView *)tableView
  willSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  (void)tableView;
  return ([self sessionAtIndexPath:indexPath] != nil) ? indexPath : nil;
}

- (void)tableView:(UITableView *)tableView
    didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  NSDictionary *session;
  NSNumber *identifier;

  (void)tableView;
  session = [self sessionAtIndexPath:indexPath];
  identifier = [session objectForKey:@"id"];
  self.selectedSessionId =
    [identifier isKindOfClass:[NSNumber class]] ? identifier : nil;
  [self notifySelectedSession];
}

#pragma mark - UIAlertViewDelegate

- (void)alertView:(UIAlertView *)alertView
clickedButtonAtIndex:(NSInteger)buttonIndex
{
  NSNumber *sessionIdentifier;

  sessionIdentifier = [self pendingDeleteSessionIdentifier];
  if (sessionIdentifier == nil) {
    return;
  }

  [self setPendingDeleteSessionIdentifier:nil];
  if (buttonIndex == [alertView cancelButtonIndex]) {
    return;
  }

  [self deleteSessionIdentifier:sessionIdentifier];
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
}

@end
