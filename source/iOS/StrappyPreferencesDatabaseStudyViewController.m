#import "StrappyPreferencesDatabaseStudyViewController.h"

#import "StrappySession.h"

enum {
  kStrappyDatabaseStudyResetAlertTag = 9101,
  kStrappyDatabaseStudyRunAlertTag = 9102
};

@interface StrappyPreferencesDatabaseStudyViewController ()
- (void)reloadStudyJSON;
- (void)resetAction:(id)sender;
- (void)studyAction:(id)sender;
- (void)showError:(NSError *)error title:(NSString *)title;
@end

@implementation StrappyPreferencesDatabaseStudyViewController

- (id)init
{
  if ((self = [super init])) {
    [[self navigationItem] setTitle:NSLocalizedString(@"Study", nil)];
  }
  return self;
}

- (void)loadView
{
  UIView *view;
  UIFont *font;

  view = [[UIView alloc] initWithFrame:[[UIScreen mainScreen] applicationFrame]];
  [view setBackgroundColor:[UIColor whiteColor]];
  jsonTextView_ = [[UITextView alloc] initWithFrame:[view bounds]];
  [jsonTextView_ setAutoresizingMask:
    UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight];
  [jsonTextView_ setEditable:NO];
  [jsonTextView_ setAutocorrectionType:UITextAutocorrectionTypeNo];
  font = [UIFont fontWithName:@"Courier" size:12.0f];
  if (font == nil) {
    font = [UIFont systemFontOfSize:12.0f];
  }
  [jsonTextView_ setFont:font];
  [view addSubview:jsonTextView_];
  [self setView:view];
}

- (void)viewDidLoad
{
  UIBarButtonItem *resetButton;
  UIBarButtonItem *studyButton;
  UIBarButtonItem *space;

  [super viewDidLoad];
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
  space = [[UIBarButtonItem alloc]
    initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace
                         target:nil
                         action:NULL];
  [self setToolbarItems:[NSArray arrayWithObjects:
    resetButton, space, studyButton, nil] animated:NO];
}

- (void)viewWillAppear:(BOOL)animated
{
  [super viewWillAppear:animated];
  [[self navigationController] setToolbarHidden:NO animated:animated];
  [self reloadStudyJSON];
}

- (void)reloadStudyJSON
{
  NSError *error;
  NSString *json;

  error = nil;
  json = [StrappySession databaseStudyJSONWithError:&error];
  if (json == nil) {
    [jsonTextView_ setText:@""];
    [self showError:error title:NSLocalizedString(@"Could Not Load Study", nil)];
    return;
  }
  [jsonTextView_ setText:json];
  [jsonTextView_ setContentOffset:CGPointZero animated:NO];
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
  UIAlertView *alert;

  (void)sender;
  alert = [[UIAlertView alloc]
    initWithTitle:NSLocalizedString(@"Study Databases?", nil)
          message:NSLocalizedString(
            @"The default model will be used to study approved databases that are currently not studied.",
            nil)
         delegate:self
cancelButtonTitle:NSLocalizedString(@"Cancel", nil)
otherButtonTitles:NSLocalizedString(@"Study", nil), nil];
  [alert setTag:kStrappyDatabaseStudyRunAlertTag];
  [alert show];
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
    [self reloadStudyJSON];
    return;
  }
  if ([alertView tag] == kStrappyDatabaseStudyRunAlertTag) {
    if ([StrappySession beginDatabaseStudyWithError:&error] == nil) {
      [self showError:error title:NSLocalizedString(@"Could Not Start Study", nil)];
      return;
    }
    [self dismissModalViewControllerAnimated:YES];
  }
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
