#import "StrappyPreferencesSystemPromptsTableViewController.h"

@interface StrappyPreferencesSystemPromptsTableViewController ()
@property (nonatomic, copy) NSString *promptText;
- (void)loadPrompt;
@end

@implementation StrappyPreferencesSystemPromptsTableViewController

- (instancetype)init
{
  if ((self = [super initWithStyle:UITableViewStyleGrouped])) {
    [[self navigationItem] setTitle:NSLocalizedString(@"Prompts", nil)];
  }
  return self;
}

- (void)viewDidLoad
{
  [super viewDidLoad];
  [self loadPrompt];
}

- (void)viewWillAppear:(BOOL)animated
{
  [super viewWillAppear:animated];
  [[self navigationController] setToolbarHidden:YES animated:animated];
}

- (void)loadPrompt
{
  NSString *path;
  NSString *prompt;

  path = [[NSBundle mainBundle] pathForResource:@"PromptSystem" ofType:@"txt"];
  if ([path length] == 0U) {
    [self setPromptText:
      NSLocalizedString(@"System prompt template is missing from the app bundle.", nil)];
    return;
  }

  prompt = [NSString stringWithContentsOfFile:path
                                     encoding:NSUTF8StringEncoding
                                        error:nil];
  [self setPromptText:(prompt != nil)
    ? prompt
    : NSLocalizedString(@"System prompt template could not be read.", nil)];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
  (void)tableView;
  return 1;
}

- (NSInteger)tableView:(UITableView *)tableView
 numberOfRowsInSection:(NSInteger)section
{
  (void)tableView;
  return (section == 0) ? 1 : 0;
}

- (NSString *)tableView:(UITableView *)tableView
titleForHeaderInSection:(NSInteger)section
{
  (void)tableView;
  return (section == 0) ? NSLocalizedString(@"System Prompt", nil) : nil;
}

- (CGFloat)tableView:(UITableView *)tableView
heightForRowAtIndexPath:(NSIndexPath *)indexPath
{
  CGFloat height;

  (void)indexPath;
  height = CGRectGetHeight([tableView bounds]) - 90.0f;
  return (height > 280.0f) ? height : 280.0f;
}

- (UITableViewCell *)tableView:(UITableView *)tableView
         cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
  UITableViewCell *cell;
  UITextView *textView;

  (void)indexPath;
  cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
                                reuseIdentifier:nil];
  [cell setSelectionStyle:UITableViewCellSelectionStyleNone];

  textView = [[UITextView alloc] initWithFrame:
    CGRectInset([[cell contentView] bounds], 6.0f, 6.0f)];
  [textView setAutoresizingMask:
    UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight];
  [textView setEditable:NO];
  [textView setFont:[UIFont fontWithName:@"Courier" size:12.0f]];
  if ([textView font] == nil) {
    [textView setFont:[UIFont systemFontOfSize:12.0f]];
  }
  [textView setText:[self promptText]];
  [[cell contentView] addSubview:textView];
  return cell;
}

@end
