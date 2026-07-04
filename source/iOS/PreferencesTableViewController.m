#import "PreferencesTableViewController.h"

#import "StrappyKeychain.h"
#import "StrappyPreferencesDatabaseWhitelistTableViewController.h"
#import "StrappyPreferencesModelWhitelistTableViewController.h"
#import "StrappyPreferencesSystemPromptsTableViewController.h"
#import "XPUIKit.h"

#include <stdlib.h>

static NSString *StrappyPreferencesTrimmedString(NSString *string)
{
  if (![string isKindOfClass:[NSString class]]) {
    return @"";
  }
  return [string stringByTrimmingCharactersInSet:
    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
}

static BOOL StrappyEnvironmentOverrideActive(void)
{
  const char *endpoint;
  const char *token;

  endpoint = getenv("APIENDPOINT");
  token = getenv("APITOKEN");
  return ((endpoint != NULL) && (endpoint[0] != '\0')) ||
         ((token != NULL) && (token[0] != '\0'));
}

enum {
  kStrappyPreferencesSectionAuthentication = 0,
  kStrappyPreferencesSectionPanes,
  kStrappyPreferencesSectionCount
};

enum {
  kStrappyAuthRowEndpoint = 0,
  kStrappyAuthRowToken,
  kStrappyAuthRowSave,
  kStrappyAuthRowCount
};

enum {
  kStrappyPaneRowModels = 0,
  kStrappyPaneRowDatabases,
  kStrappyPaneRowPrompts,
  kStrappyPaneRowCount
};

@interface PreferencesTableViewController () <UITextFieldDelegate>
@property (nonatomic, strong) UITextField *apiEndpointField;
@property (nonatomic, strong) UITextField *apiTokenField;
@property (nonatomic, copy) NSString *statusMessage;
@property (nonatomic, strong) UILabel *statusLabel;
@property (nonatomic, assign) BOOL authenticationDirty;
- (UITextField *)makeFieldSecure:(BOOL)secure placeholder:(NSString *)placeholder;
- (void)loadAuthenticationFields;
- (void)buildStatusToolbar;
- (void)refreshStatusToolbar;
- (NSString *)authenticationStatusText;
- (BOOL)saveAuthenticationIfNeeded;
- (BOOL)saveAuthentication;
- (void)fieldChanged:(id)sender;
- (void)doneAction:(id)sender;
@end

@implementation PreferencesTableViewController

- (instancetype)init
{
  if ((self = [super initWithStyle:UITableViewStyleGrouped])) {
    [[self navigationItem] setTitle:NSLocalizedString(@"Preferences", nil)];
  }
  return self;
}

- (void)viewDidLoad
{
  [super viewDidLoad];

  [self setApiEndpointField:
    [self makeFieldSecure:NO
              placeholder:NSLocalizedString(
                @"https://openrouter.ai/api/v1/chat/completions", nil)]];
  [self setApiTokenField:
    [self makeFieldSecure:YES
              placeholder:NSLocalizedString(@"Paste API token", nil)]];
  [self loadAuthenticationFields];

  [[self navigationItem] setRightBarButtonItem:
    [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone
                                                  target:self
                                                  action:@selector(doneAction:)]];
  [self buildStatusToolbar];
}

- (void)viewWillAppear:(BOOL)animated
{
  [super viewWillAppear:animated];
  [self refreshStatusToolbar];
  [[self navigationController] setToolbarHidden:NO animated:animated];
}

- (void)viewWillDisappear:(BOOL)animated
{
  [super viewWillDisappear:animated];
  [[self navigationController] setToolbarHidden:YES animated:animated];
}

- (void)buildStatusToolbar
{
  UIBarButtonItem *flexLeft;
  UIBarButtonItem *flexRight;
  UIBarButtonItem *statusItem;
  UILabel *label;
  CGFloat width;

  width = CGRectGetWidth([[self view] bounds]) - 80.0f;
  if (width < 160.0f) {
    width = 160.0f;
  }

  label = [[UILabel alloc] initWithFrame:CGRectMake(0.0f, 0.0f, width, 30.0f)];
  [label setBackgroundColor:[UIColor clearColor]];
  [label setTextColor:[UIColor whiteColor]];
  [label setShadowColor:[UIColor darkGrayColor]];
  [label setShadowOffset:CGSizeMake(0.0f, -1.0f)];
  [label setFont:[UIFont systemFontOfSize:12.0f]];
  [label setNumberOfLines:1];
  [label XP_setTextAlignmentCenter];
  [label setAutoresizingMask:UIViewAutoresizingFlexibleWidth];
  [self setStatusLabel:label];

  statusItem = [[UIBarButtonItem alloc] initWithCustomView:label];
  flexLeft = [[UIBarButtonItem alloc]
    initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace
                         target:nil
                         action:NULL];
  flexRight = [[UIBarButtonItem alloc]
    initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace
                         target:nil
                         action:NULL];
  [self setToolbarItems:
    [NSArray arrayWithObjects:flexLeft, statusItem, flexRight, nil]];
  [self refreshStatusToolbar];
}

- (void)refreshStatusToolbar
{
  [[self statusLabel] setText:[self authenticationStatusText]];
}

- (NSString *)authenticationStatusText
{
  if ([[self statusMessage] length] > 0U) {
    return [self statusMessage];
  }
  if (StrappyEnvironmentOverrideActive()) {
    return NSLocalizedString(
      @"APIENDPOINT or APITOKEN overrides keychain values while set.", nil);
  }
  return @"";
}

- (UITextField *)makeFieldSecure:(BOOL)secure placeholder:(NSString *)placeholder
{
  UITextField *field;

  field = [[UITextField alloc] initWithFrame:CGRectZero];
  [field setPlaceholder:placeholder];
  [field setSecureTextEntry:secure];
  [field setDelegate:self];
  [field setAutocorrectionType:UITextAutocorrectionTypeNo];
  [field setAutocapitalizationType:UITextAutocapitalizationTypeNone];
  [field setClearButtonMode:UITextFieldViewModeWhileEditing];
  [field setReturnKeyType:secure ? UIReturnKeyDone : UIReturnKeyNext];
  [field setKeyboardType:secure ? UIKeyboardTypeDefault : UIKeyboardTypeURL];
  [field addTarget:self
            action:@selector(fieldChanged:)
  forControlEvents:UIControlEventEditingChanged];
  return field;
}

- (void)loadAuthenticationFields
{
  NSString *endpoint;
  NSString *token;

  endpoint = [[StrappyKeychain sharedKeychain] apiEndpoint];
  if ([endpoint length] == 0U) {
    endpoint = [StrappyKeychain defaultAPIEndpoint];
  }
  token = [[StrappyKeychain sharedKeychain] apiToken];

  [[self apiEndpointField] setText:(endpoint != nil) ? endpoint : @""];
  [[self apiTokenField] setText:(token != nil) ? token : @""];
  [self setAuthenticationDirty:NO];

  if ([[StrappyKeychain sharedKeychain] hasAPICredentials]) {
    [self setStatusMessage:
      NSLocalizedString(@"API credentials are available.", nil)];
  } else {
    [self setStatusMessage:
      NSLocalizedString(@"No API credentials are saved in the keychain.", nil)];
  }
}

- (void)fieldChanged:(id)sender
{
  (void)sender;
  [self setAuthenticationDirty:YES];
}

- (void)doneAction:(id)sender
{
  (void)sender;
  [[self view] endEditing:YES];
  if (![self saveAuthenticationIfNeeded]) {
    return;
  }
  [self dismissModalViewControllerAnimated:YES];
}

- (BOOL)saveAuthenticationIfNeeded
{
  if (![self authenticationDirty]) {
    return YES;
  }
  return [self saveAuthentication];
}

- (BOOL)saveAuthentication
{
  NSString *endpoint;
  NSString *token;

  endpoint = StrappyPreferencesTrimmedString([[self apiEndpointField] text]);
  token = StrappyPreferencesTrimmedString([[self apiTokenField] text]);
  if (([endpoint length] == 0U) || ([token length] == 0U)) {
    [self setStatusMessage:
      NSLocalizedString(@"API endpoint and token are required.", nil)];
    [[self tableView] reloadSections:
      [NSIndexSet indexSetWithIndex:kStrappyPreferencesSectionAuthentication]
                  withRowAnimation:UITableViewRowAnimationNone];
    [self refreshStatusToolbar];
    return NO;
  }

  if (![[StrappyKeychain sharedKeychain] saveAPIEndpoint:endpoint token:token]) {
    [self setStatusMessage:
      NSLocalizedString(@"The keychain refused the write.", nil)];
    [[self tableView] reloadSections:
      [NSIndexSet indexSetWithIndex:kStrappyPreferencesSectionAuthentication]
                  withRowAnimation:UITableViewRowAnimationNone];
    [self refreshStatusToolbar];
    return NO;
  }

  [[self apiEndpointField] setText:endpoint];
  [[self apiTokenField] setText:token];
  [self setAuthenticationDirty:NO];
  [self setStatusMessage:
    NSLocalizedString(@"API credentials saved to keychain.", nil)];
  [[self tableView] reloadSections:
    [NSIndexSet indexSetWithIndex:kStrappyPreferencesSectionAuthentication]
                withRowAnimation:UITableViewRowAnimationNone];
  [self refreshStatusToolbar];
  return YES;
}

#pragma mark - UITextFieldDelegate

- (BOOL)textFieldShouldReturn:(UITextField *)textField
{
  if (textField == [self apiEndpointField]) {
    [[self apiTokenField] becomeFirstResponder];
    return NO;
  }

  [textField resignFirstResponder];
  [self saveAuthenticationIfNeeded];
  return NO;
}

#pragma mark - UITableViewDataSource

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
  (void)tableView;
  return kStrappyPreferencesSectionCount;
}

- (NSInteger)tableView:(UITableView *)tableView
 numberOfRowsInSection:(NSInteger)section
{
  (void)tableView;
  if (section == kStrappyPreferencesSectionAuthentication) {
    return kStrappyAuthRowCount;
  }
  if (section == kStrappyPreferencesSectionPanes) {
    return kStrappyPaneRowCount;
  }
  return 0;
}

- (NSString *)tableView:(UITableView *)tableView
titleForHeaderInSection:(NSInteger)section
{
  (void)tableView;
  if (section == kStrappyPreferencesSectionAuthentication) {
    return NSLocalizedString(@"Authentication", nil);
  }
  if (section == kStrappyPreferencesSectionPanes) {
    return NSLocalizedString(@"Preferences", nil);
  }
  return nil;
}

- (NSString *)tableView:(UITableView *)tableView
titleForFooterInSection:(NSInteger)section
{
  (void)tableView;
  (void)section;
  return nil;
}

- (UITableViewCell *)tableView:(UITableView *)tableView
         cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
  UITableViewCell *cell;
  UITextField *field;

  if ([indexPath section] == kStrappyPreferencesSectionAuthentication) {
    if ([indexPath row] == kStrappyAuthRowSave) {
      cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
                                    reuseIdentifier:nil];
      [[cell textLabel] setText:NSLocalizedString(@"Save API Credentials", nil)];
      [[cell textLabel] setTextColor:[UIColor blueColor]];
      [cell setSelectionStyle:UITableViewCellSelectionStyleBlue];
      return cell;
    }

    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
                                  reuseIdentifier:nil];
    [cell setSelectionStyle:UITableViewCellSelectionStyleNone];
    field = ([indexPath row] == kStrappyAuthRowEndpoint)
      ? [self apiEndpointField]
      : [self apiTokenField];
    [field setFrame:CGRectInset([[cell contentView] bounds], 15.0f, 7.0f)];
    [field setAutoresizingMask:
      UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight];
    [[cell contentView] addSubview:field];
    return cell;
  }

  cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1
                                reuseIdentifier:nil];
  [cell setAccessoryType:UITableViewCellAccessoryDisclosureIndicator];
  [cell setSelectionStyle:UITableViewCellSelectionStyleBlue];
  if ([indexPath row] == kStrappyPaneRowModels) {
    [[cell textLabel] setText:NSLocalizedString(@"Models", nil)];
    [[cell detailTextLabel] setText:NSLocalizedString(@"Fetch, search, use", nil)];
  } else if ([indexPath row] == kStrappyPaneRowDatabases) {
    [[cell textLabel] setText:NSLocalizedString(@"Databases", nil)];
    [[cell detailTextLabel] setText:NSLocalizedString(@"Scan, search, use", nil)];
  } else {
    [[cell textLabel] setText:NSLocalizedString(@"Prompts", nil)];
    [[cell detailTextLabel] setText:NSLocalizedString(@"System prompt", nil)];
  }
  return cell;
}

#pragma mark - UITableViewDelegate

- (void)tableView:(UITableView *)tableView
didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  UITableViewController *controller;

  [tableView deselectRowAtIndexPath:indexPath animated:YES];

  if ([indexPath section] == kStrappyPreferencesSectionAuthentication) {
    if ([indexPath row] == kStrappyAuthRowSave) {
      [[self view] endEditing:YES];
      [self saveAuthentication];
    }
    return;
  }

  controller = nil;
  if ([indexPath row] == kStrappyPaneRowModels) {
    controller =
      [[StrappyPreferencesModelWhitelistTableViewController alloc] init];
  } else if ([indexPath row] == kStrappyPaneRowDatabases) {
    controller =
      [[StrappyPreferencesDatabaseWhitelistTableViewController alloc] init];
  } else if ([indexPath row] == kStrappyPaneRowPrompts) {
    controller =
      [[StrappyPreferencesSystemPromptsTableViewController alloc] init];
  }

  if (controller != nil) {
    [[self navigationController] pushViewController:controller animated:YES];
  }
}

- (void)dealloc
{
  [[self apiEndpointField] setDelegate:nil];
  [[self apiTokenField] setDelegate:nil];
}

@end
