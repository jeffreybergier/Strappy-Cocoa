#import "SessionListViewController.h"

#import "StrappySession.h"
#import "XPFoundation.h"
#import "XPUIKit.h"

static const CGFloat kStrappySessionRowHeight = 72.0f;

static NSString * const kStrappySessionCellIdentifier = @"StrappySessionCell";

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
  NSString *lastText;

  if (StrappySessionPromptIsInFlight(session)) {
    return NSLocalizedString(@"Prompt in progress", nil);
  }

  timestamp =
    StrappySessionDisplayTimestamp([session objectForKey:@"last_message_at"]);
  lastText = [session objectForKey:@"last_message_text"];
  if (![lastText isKindOfClass:[NSString class]]) {
    lastText = @"";
  }

  if (([timestamp length] > 0U) && ([lastText length] > 0U)) {
    return [NSString stringWithFormat:@"%@  %@", timestamp, lastText];
  }
  if ([timestamp length] > 0U) {
    return timestamp;
  }
  if ([lastText length] > 0U) {
    return lastText;
  }
  return NSLocalizedString(@"No messages yet", nil);
}

@interface SessionListViewController ()
@property (nonatomic, copy) NSArray *sessions;
@property (nonatomic, copy) NSNumber *selectedSessionId;
@property (nonatomic, copy) NSString *loadErrorText;
@property (nonatomic, strong) UIBarButtonItem *addButton;
@property (nonatomic, strong) UIBarButtonItem *refreshButton;
@property (nonatomic, assign) BOOL creatingSession;
- (void)strappySessionDidUpdate:(NSNotification *)notification;
- (void)sessionPromptActivityDidChange:(NSNotification *)notification;
- (NSIndexPath *)indexPathForSessionIdentifier:(NSNumber *)sessionIdentifier;
- (NSDictionary *)sessionAtIndexPath:(NSIndexPath *)indexPath;
- (void)configureEmptyCell:(UITableViewCell *)cell;
- (void)configureSessionCell:(UITableViewCell *)cell
                     session:(NSDictionary *)session;
- (void)notifySelectedSession;
- (void)setControlsEnabled:(BOOL)enabled;
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

  self.tableView.rowHeight = kStrappySessionRowHeight;
  self.tableView.backgroundColor = [UIColor messagesBackgroundColor];
  self.tableView.separatorStyle = UITableViewCellSeparatorStyleSingleLine;

  self.addButton =
    [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemAdd
                                                  target:self
                                                  action:@selector(addSession:)];
  self.refreshButton =
    [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemRefresh
                                                  target:self
                                                  action:@selector(reloadDataAction:)];
  self.navigationItem.rightBarButtonItem = self.addButton;
  self.navigationItem.leftBarButtonItem = self.refreshButton;

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

- (void)setControlsEnabled:(BOOL)enabled
{
  self.addButton.enabled = enabled;
  self.refreshButton.enabled = enabled;
}

- (void)reloadData
{
  NSError *error;
  NSArray *sessions;

  error = nil;
  sessions = [StrappySession sessionSummariesWithError:&error];
  if (sessions == nil) {
    self.sessions = [NSArray array];
    self.loadErrorText = [error localizedDescription];
  } else {
    self.sessions = sessions;
    self.loadErrorText = nil;
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
  return ([self.sessions count] > 0U) ? (NSInteger)[self.sessions count] : 1;
}

- (NSString *)tableView:(UITableView *)tableView
titleForHeaderInSection:(NSInteger)section
{
  (void)tableView;
  return (section == 0) ? NSLocalizedString(@"Conversations", nil) : nil;
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
    cell.textLabel.numberOfLines = 2;
    cell.detailTextLabel.numberOfLines = 2;
  }

  session = [self sessionAtIndexPath:indexPath];
  if (session == nil) {
    [self configureEmptyCell:cell];
  } else {
    [self configureSessionCell:cell session:session];
  }
  return cell;
}

- (void)configureEmptyCell:(UITableViewCell *)cell
{
  NSString *detail;

  detail = self.loadErrorText;
  if ([detail length] == 0U) {
    detail = NSLocalizedString(@"Create a conversation to begin.", nil);
  }

  cell.textLabel.text = ([self.loadErrorText length] > 0U)
    ? NSLocalizedString(@"Could not load conversations", nil)
    : NSLocalizedString(@"No conversations yet", nil);
  cell.detailTextLabel.text = detail;
  cell.imageView.image = nil;
  cell.accessoryView = nil;
  cell.accessoryType = UITableViewCellAccessoryNone;
  cell.selectionStyle = UITableViewCellSelectionStyleNone;
}

- (void)configureSessionCell:(UITableViewCell *)cell
                     session:(NSDictionary *)session
{
  UIActivityIndicatorView *spinner;

  cell.textLabel.text = StrappySessionPromptPreview(session);
  cell.detailTextLabel.text = StrappySessionSubtitle(session);
  cell.imageView.image = nil;
  cell.selectionStyle = UITableViewCellSelectionStyleBlue;

  if (StrappySessionPromptIsInFlight(session)) {
    spinner = [[UIActivityIndicatorView alloc]
      initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleGray];
    [spinner startAnimating];
    cell.accessoryType = UITableViewCellAccessoryNone;
    cell.accessoryView = spinner;
  } else {
    cell.accessoryView = nil;
    cell.accessoryType = UITableViewCellAccessoryNone;
  }
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

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
}

@end
