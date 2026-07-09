#import "PromptSendViewController.h"

#import "AIFontAwesome.h"

#import <QuartzCore/QuartzCore.h>

static const CGFloat kStrappySendCollapsedHeight = 44.0f;
static const CGFloat kStrappySendExpandedHeight = 104.0f;
static const CGFloat kStrappySendPad = 4.0f;
static const CGFloat kStrappySendButtonSize = 36.0f;
static const CGFloat kStrappySendDismissWidth = 22.0f;
static const CGFloat kStrappySendOptionsWidth = 36.0f;
static const CGFloat kStrappySendDismissGlyphSize = 14.0f;
static const CGFloat kStrappySendFontSize = 16.0f;

static NSString *StrappyMessageModelStringForRow(NSDictionary *row,
                                                 NSString *key)
{
  id value;

  if (![row isKindOfClass:[NSDictionary class]]) {
    return @"";
  }

  value = [row objectForKey:key];
  return [value isKindOfClass:[NSString class]] ? value : @"";
}

static NSString *StrappyMessageModelDisplayNameForRow(NSDictionary *row)
{
  NSString *name;
  NSString *modelIdentifier;

  name = StrappyMessageModelStringForRow(row, @"name");
  if ([name length] > 0U) {
    return name;
  }

  modelIdentifier = StrappyMessageModelStringForRow(row, @"id");
  return ([modelIdentifier length] > 0U)
    ? modelIdentifier
    : NSLocalizedString(@"Model", nil);
}

@class StrappyPromptOptionsTableViewController;

enum {
  kStrappyPromptOptionsSectionModels = 0,
  kStrappyPromptOptionsSectionStreaming,
  kStrappyPromptOptionsSectionCount
};

@interface PromptSendViewController () <UITextViewDelegate>
@property (nonatomic, strong) UIButton *dismissButton;
@property (nonatomic, strong) UIButton *optionsButton;
@property (nonatomic, strong) UITextView *textView;
@property (nonatomic, strong) UILabel *placeholderLabel;
@property (nonatomic, strong) UIButton *sendButton;
@property (nonatomic, strong) UINavigationController *optionsNavigationController;
@property (nonatomic, strong) StrappyPromptOptionsTableViewController *optionsController;
@property (nonatomic, assign) BOOL controlsEnabled;
@property (nonatomic, assign) BOOL composing;
@property (nonatomic, assign) BOOL expanded;
@property (nonatomic, assign) BOOL sending;
@property (nonatomic, assign) BOOL cancellationRequested;
@property (nonatomic, assign) BOOL streamingEnabled;
- (void)buildSubviews;
- (UIImage *)iconImageForIcon:(AIFontAwesomeIcon)icon
                        style:(AIFontAwesomeStyle)style
                    pointSize:(CGFloat)pointSize
                        color:(UIColor *)color;
- (NSString *)trimmedPromptText;
- (void)updateControls;
- (void)updateExpansion;
- (void)updatePlaceholderVisibility;
- (NSArray *)currentAllowedModels;
- (NSString *)currentSelectedModelIdentifier;
- (BOOL)setSelectedModelIdentifierFromOptions:(NSString *)modelIdentifier;
- (BOOL)setStreamingEnabledFromOptions:(BOOL)enabled;
- (UIViewController *)containingViewController;
- (void)dismissOptionsControllerAnimated:(BOOL)animated;
- (void)dismissTapped:(id)sender;
- (void)optionsTapped:(id)sender;
- (void)sendTapped:(id)sender;
@end

@interface StrappyPromptOptionsTableViewController : UITableViewController
@property (nonatomic, assign) PromptSendViewController *promptSendViewController;
@property (nonatomic, copy) NSArray *models;
@property (nonatomic, copy) NSString *selectedModelIdentifier;
@property (nonatomic, strong) UISwitch *streamingSwitch;
@property (nonatomic, assign) BOOL streamingEnabled;
- (instancetype)initWithPromptSendViewController:
    (PromptSendViewController *)promptSendViewController;
- (void)reloadOptionsFromPrompt;
@end

@implementation StrappyPromptOptionsTableViewController

- (instancetype)initWithPromptSendViewController:
    (PromptSendViewController *)promptSendViewController
{
  if ((self = [super initWithStyle:UITableViewStyleGrouped])) {
    [self setPromptSendViewController:promptSendViewController];
    [[self navigationItem] setTitle:NSLocalizedString(@"Prompt Options", nil)];
    [self reloadOptionsSnapshot];
  }
  return self;
}

- (void)viewDidLoad
{
  UISwitch *streamingSwitch;

  [super viewDidLoad];

  streamingSwitch = [[UISwitch alloc] initWithFrame:CGRectZero];
  [streamingSwitch addTarget:self
                      action:@selector(streamingSwitchChanged:)
            forControlEvents:UIControlEventValueChanged];
  [self setStreamingSwitch:streamingSwitch];
  [self reloadOptionsFromPrompt];

  [[self navigationItem] setRightBarButtonItem:
    [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone
                                                  target:self
                                                  action:@selector(doneAction:)]];
}

- (void)reloadOptionsSnapshot
{
  PromptSendViewController *promptSendViewController;

  promptSendViewController = [self promptSendViewController];
  [self setModels:
    (promptSendViewController != nil)
      ? [promptSendViewController currentAllowedModels]
      : [NSArray array]];
  [self setSelectedModelIdentifier:
    (promptSendViewController != nil)
      ? [promptSendViewController currentSelectedModelIdentifier]
      : @""];
  [self setStreamingEnabled:
    (promptSendViewController != nil)
      ? [promptSendViewController streamingEnabled]
      : NO];
}

- (void)reloadOptionsFromPrompt
{
  [self reloadOptionsSnapshot];
  [[self streamingSwitch] setOn:[self streamingEnabled] animated:NO];
  [[self tableView] reloadData];
}

- (void)doneAction:(id)sender
{
  (void)sender;
  [[self promptSendViewController] dismissOptionsControllerAnimated:YES];
}

- (void)streamingSwitchChanged:(UISwitch *)sender
{
  PromptSendViewController *promptSendViewController;

  promptSendViewController = [self promptSendViewController];
  if (promptSendViewController != nil) {
    (void)[promptSendViewController setStreamingEnabledFromOptions:
      [sender isOn]];
    [self setStreamingEnabled:[promptSendViewController streamingEnabled]];
  }
  [sender setOn:[self streamingEnabled] animated:YES];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
  (void)tableView;
  return kStrappyPromptOptionsSectionCount;
}

- (NSInteger)tableView:(UITableView *)tableView
 numberOfRowsInSection:(NSInteger)section
{
  (void)tableView;
  if (section == kStrappyPromptOptionsSectionModels) {
    return (NSInteger)[[self models] count];
  }
  if (section == kStrappyPromptOptionsSectionStreaming) {
    return 1;
  }
  return 0;
}

- (NSString *)tableView:(UITableView *)tableView
titleForHeaderInSection:(NSInteger)section
{
  (void)tableView;
  if (section == kStrappyPromptOptionsSectionModels) {
    return ([[self models] count] > 0U) ? NSLocalizedString(@"Models", nil) : nil;
  }
  if (section == kStrappyPromptOptionsSectionStreaming) {
    return NSLocalizedString(@"Streaming", nil);
  }
  return nil;
}

- (UITableViewCell *)tableView:(UITableView *)tableView
         cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
  UITableViewCell *cell;

  if ([indexPath section] == kStrappyPromptOptionsSectionStreaming) {
    cell = [tableView dequeueReusableCellWithIdentifier:@"StreamingCell"];
    if (cell == nil) {
      cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
                                    reuseIdentifier:@"StreamingCell"];
      [cell setSelectionStyle:UITableViewCellSelectionStyleNone];
    }
    [[cell textLabel] setText:NSLocalizedString(@"Stream Responses", nil)];
    [[self streamingSwitch] setOn:[self streamingEnabled] animated:NO];
    [cell setAccessoryView:[self streamingSwitch]];
    return cell;
  }

  cell = [tableView dequeueReusableCellWithIdentifier:@"ModelCell"];
  if (cell == nil) {
    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                  reuseIdentifier:@"ModelCell"];
    [[cell textLabel] setNumberOfLines:1];
    [[cell detailTextLabel] setNumberOfLines:1];
  }

  {
    NSDictionary *model;
    NSString *identifier;

    model = [[self models] objectAtIndex:(NSUInteger)[indexPath row]];
    identifier = StrappyMessageModelStringForRow(model, @"id");
    [[cell textLabel] setText:StrappyMessageModelDisplayNameForRow(model)];
    [[cell detailTextLabel] setText:identifier];
    [[cell textLabel] setTextColor:[UIColor blackColor]];
    [[cell detailTextLabel] setTextColor:[UIColor grayColor]];
    [cell setSelectionStyle:UITableViewCellSelectionStyleBlue];
    [cell setAccessoryType:
      [identifier isEqualToString:[self selectedModelIdentifier]]
        ? UITableViewCellAccessoryCheckmark
        : UITableViewCellAccessoryNone];
  }
  return cell;
}

- (NSIndexPath *)tableView:(UITableView *)tableView
  willSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  (void)tableView;
  if ([indexPath section] != kStrappyPromptOptionsSectionModels) {
    return nil;
  }
  return ([[self models] count] > 0U) ? indexPath : nil;
}

- (void)tableView:(UITableView *)tableView
didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  NSDictionary *model;
  NSString *modelIdentifier;

  [tableView deselectRowAtIndexPath:indexPath animated:YES];
  if (([indexPath section] != kStrappyPromptOptionsSectionModels) ||
      ([[self models] count] == 0U)) {
    return;
  }

  model = [[self models] objectAtIndex:(NSUInteger)[indexPath row]];
  modelIdentifier = StrappyMessageModelStringForRow(model, @"id");
  if ([modelIdentifier length] == 0U) {
    return;
  }

  if ([[self promptSendViewController]
        setSelectedModelIdentifierFromOptions:modelIdentifier]) {
    [self setSelectedModelIdentifier:modelIdentifier];
    [[self tableView] reloadSections:
      [NSIndexSet indexSetWithIndex:kStrappyPromptOptionsSectionModels]
                  withRowAnimation:UITableViewRowAnimationNone];
  } else {
    [self reloadOptionsFromPrompt];
  }
}

@end

@implementation PromptSendViewController

- (instancetype)initWithFrame:(CGRect)frame
{
  if ((self = [super initWithFrame:frame])) {
    [self setBackgroundColor:[UIColor colorWithWhite:0.94f alpha:1.0f]];
    [self setAutoresizingMask:
      UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleTopMargin];
    [self setControlsEnabled:YES];
    [self buildSubviews];
  }
  return self;
}

- (UIImage *)iconImageForIcon:(AIFontAwesomeIcon)icon
                        style:(AIFontAwesomeStyle)style
                    pointSize:(CGFloat)pointSize
                        color:(UIColor *)color
{
  CGFloat scale;

  scale = [[UIScreen mainScreen] scale];
  if (scale <= 0.0f) {
    scale = 1.0f;
  }

  return [AIFontAwesome imageForIcon:icon
                               style:style
                            iconSize:pointSize
                          canvasSize:kStrappySendButtonSize
                               color:color
                               scale:scale];
}

- (void)buildSubviews
{
  UIButton *dismiss;
  UIButton *options;
  UITextView *textView;
  UILabel *placeholder;
  UIButton *send;
  CGFloat hairline;

  hairline = 1.0f / [[UIScreen mainScreen] scale];
  if (hairline <= 0.0f) {
    hairline = 1.0f;
  }

  dismiss = [UIButton buttonWithType:UIButtonTypeCustom];
  [dismiss setAlpha:0.0f];
  [dismiss setImage:[self iconImageForIcon:AIFAChevronDown
                                      style:AIFontAwesomeStyleSolid
                                  pointSize:kStrappySendDismissGlyphSize
                                      color:[UIColor darkGrayColor]]
             forState:UIControlStateNormal];
  [dismiss setAccessibilityLabel:NSLocalizedString(@"Dismiss Keyboard", nil)];
  [dismiss addTarget:self
              action:@selector(dismissTapped:)
    forControlEvents:UIControlEventTouchUpInside];
  [self addSubview:dismiss];
  [self setDismissButton:dismiss];

  options = [UIButton buttonWithType:UIButtonTypeCustom];
  [options setImage:[self iconImageForIcon:AIFAMicrochip
                                     style:AIFontAwesomeStyleSolid
                                 pointSize:18.0f
                                     color:[UIColor darkGrayColor]]
            forState:UIControlStateNormal];
  [options setAccessibilityLabel:NSLocalizedString(@"Models", nil)];
  [options addTarget:self
              action:@selector(optionsTapped:)
    forControlEvents:UIControlEventTouchUpInside];
  [self addSubview:options];
  [self setOptionsButton:options];

  textView = [[UITextView alloc] initWithFrame:CGRectZero];
  [textView setDelegate:self];
  [textView setFont:[UIFont systemFontOfSize:kStrappySendFontSize]];
  [textView setBackgroundColor:[UIColor whiteColor]];
  [textView setReturnKeyType:UIReturnKeyDefault];
  [textView setAutocorrectionType:UITextAutocorrectionTypeDefault];
  [textView setScrollsToTop:NO];
  [textView setAutoresizingMask:UIViewAutoresizingFlexibleWidth |
                                UIViewAutoresizingFlexibleHeight];
  [[textView layer] setCornerRadius:7.0f];
  [[textView layer] setBorderWidth:hairline];
  [[textView layer] setBorderColor:[[UIColor colorWithWhite:0.76f alpha:1.0f] CGColor]];
  [self addSubview:textView];
  [self setTextView:textView];

  placeholder = [[UILabel alloc] initWithFrame:CGRectZero];
  [placeholder setText:NSLocalizedString(@"Ask Strappy", nil)];
  [placeholder setFont:[UIFont systemFontOfSize:kStrappySendFontSize]];
  [placeholder setTextColor:[UIColor colorWithWhite:0.58f alpha:1.0f]];
  [placeholder setBackgroundColor:[UIColor clearColor]];
  [placeholder setUserInteractionEnabled:NO];
  [self addSubview:placeholder];
  [self setPlaceholderLabel:placeholder];

  send = [UIButton buttonWithType:UIButtonTypeCustom];
  [send setImage:[self iconImageForIcon:AIFAPaperPlane
                                  style:AIFontAwesomeStyleRegular
                              pointSize:18.0f
                                  color:[UIColor whiteColor]]
         forState:UIControlStateNormal];
  [send setBackgroundColor:[UIColor colorWithRed:0.0f
                                           green:0.48f
                                            blue:0.92f
                                           alpha:1.0f]];
  [[send layer] setCornerRadius:kStrappySendButtonSize * 0.5f];
  [send setAccessibilityLabel:NSLocalizedString(@"Send Prompt", nil)];
  [send addTarget:self
           action:@selector(sendTapped:)
 forControlEvents:UIControlEventTouchUpInside];
  [self addSubview:send];
  [self setSendButton:send];

  [self updateControls];
  [self updatePlaceholderVisibility];
}

- (void)layoutSubviews
{
  CGRect bounds;
  CGFloat optionsX;
  CGFloat textX;
  CGFloat sendX;
  CGFloat textRight;
  CGFloat textHeight;
  CGFloat placeholderHeight;
  CGFloat placeholderWidth;

  [super layoutSubviews];

  bounds = [self bounds];
  sendX = bounds.size.width - kStrappySendPad - kStrappySendButtonSize;
  optionsX = sendX - kStrappySendPad - kStrappySendOptionsWidth;
  textX = [self composing] ? kStrappySendDismissWidth : kStrappySendPad;
  textRight = optionsX - kStrappySendPad;
  if (textRight < textX) {
    textRight = textX;
  }
  textHeight = bounds.size.height - (kStrappySendPad * 2.0f);
  if (textHeight < kStrappySendButtonSize) {
    textHeight = kStrappySendButtonSize;
  }

  [[self dismissButton] setAlpha:[self composing] ? 1.0f : 0.0f];
  [[self dismissButton] setFrame:CGRectMake(0.0f,
                                            kStrappySendPad,
                                            kStrappySendDismissWidth,
                                            kStrappySendButtonSize)];
  [[self optionsButton] setFrame:CGRectMake(optionsX,
                                            kStrappySendPad,
                                            kStrappySendOptionsWidth,
                                            kStrappySendButtonSize)];
  [[self sendButton] setFrame:CGRectMake(sendX,
                                         kStrappySendPad,
                                         kStrappySendButtonSize,
                                         kStrappySendButtonSize)];
  [[self textView] setFrame:CGRectMake(textX,
                                       kStrappySendPad,
                                       textRight - textX,
                                       textHeight)];

  placeholderHeight = (CGFloat)ceilf((float)[[[self textView] font] lineHeight]);
  placeholderWidth = (textRight - textX) - 16.0f;
  if (placeholderWidth < 0.0f) {
    placeholderWidth = 0.0f;
  }
  [[self placeholderLabel] setFrame:CGRectMake(textX + 8.0f,
                                               kStrappySendPad + 8.0f,
                                               placeholderWidth,
                                               placeholderHeight)];
}

- (CGFloat)preferredHeight
{
  return ([self composing] && [self expanded])
    ? kStrappySendExpandedHeight
    : kStrappySendCollapsedHeight;
}

- (void)setComposing:(BOOL)composing
{
  _composing = composing ? YES : NO;
  [self setNeedsLayout];
  [self updateControls];
}

- (void)setEnabled:(BOOL)enabled
{
  [self setControlsEnabled:enabled ? YES : NO];
  [[self textView] setEditable:enabled ? YES : NO];
  [self updateControls];
}

- (void)setSending:(BOOL)sending
{
  _sending = sending ? YES : NO;
  if (!sending) {
    [self setCancellationRequested:NO];
  }
  [self updateControls];
}

- (void)setCancellationRequested:(BOOL)requested
{
  _cancellationRequested = requested ? YES : NO;
  [self updateControls];
}

- (void)setStreamingEnabled:(BOOL)enabled
{
  _streamingEnabled = enabled ? YES : NO;
  [[self optionsController] reloadOptionsFromPrompt];
}

- (void)reloadOptionsMenu
{
  [self updateControls];
  [[self optionsController] reloadOptionsFromPrompt];
}

- (NSString *)trimmedPromptText
{
  NSString *text;

  text = [[self textView] text];
  if (![text isKindOfClass:[NSString class]]) {
    text = @"";
  }
  return [text stringByTrimmingCharactersInSet:
    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
}

- (BOOL)canSendCurrentPrompt
{
  if (![self controlsEnabled] || [self sending] || ([self textView] == nil)) {
    return NO;
  }
  return ([[self trimmedPromptText] length] > 0U) ? YES : NO;
}

- (void)updateControls
{
  BOOL canSend;
  BOOL sendEnabled;
  UIImage *sendImage;
  UIColor *sendColor;

  canSend = [self canSendCurrentPrompt];
  sendEnabled = [self sending]
    ? ([self controlsEnabled] && ![self cancellationRequested])
    : canSend;

  [[self optionsButton] setEnabled:
    ([self controlsEnabled] && ![self sending]) ? YES : NO];
  [[self dismissButton] setEnabled:
    ([self controlsEnabled] && [self composing]) ? YES : NO];
  [[self textView] setEditable:
    ([self controlsEnabled] && ![self sending]) ? YES : NO];
  [[self sendButton] setEnabled:sendEnabled];

  if ([self sending]) {
    sendImage = [self iconImageForIcon:AIFAStop
                                 style:AIFontAwesomeStyleSolid
                             pointSize:16.0f
                                 color:[UIColor whiteColor]];
    [[self sendButton] setAccessibilityLabel:NSLocalizedString(@"Cancel Prompt", nil)];
  } else {
    sendImage = [self iconImageForIcon:AIFAPaperPlane
                                 style:AIFontAwesomeStyleRegular
                             pointSize:18.0f
                                 color:[UIColor whiteColor]];
    [[self sendButton] setAccessibilityLabel:NSLocalizedString(@"Send Prompt", nil)];
  }
  [[self sendButton] setImage:sendImage forState:UIControlStateNormal];

  if (!sendEnabled) {
    sendColor = [UIColor colorWithWhite:0.70f alpha:1.0f];
  } else if ([self sending]) {
    sendColor = [UIColor colorWithRed:0.72f green:0.18f blue:0.16f alpha:1.0f];
  } else {
    sendColor = [UIColor colorWithRed:0.0f green:0.48f blue:0.92f alpha:1.0f];
  }
  [[self sendButton] setBackgroundColor:sendColor];
}

- (void)updatePlaceholderVisibility
{
  [[self placeholderLabel] setHidden:[[[self textView] text] length] > 0U];
}

- (void)updateExpansion
{
  CGFloat lineHeight;
  CGFloat oneLineHeight;
  CGFloat availableWidth;
  CGFloat neededHeight;
  BOOL expanded;

  if ([self textView] == nil) {
    return;
  }

  lineHeight = (CGFloat)ceilf((float)[[[self textView] font] lineHeight]);
  if (lineHeight <= 0.0f) {
    lineHeight = 20.0f;
  }
  oneLineHeight = lineHeight + 16.0f;
  availableWidth = [[self textView] frame].size.width;
  if (availableWidth < 1.0f) {
    availableWidth = 1.0f;
  }

  neededHeight =
    [[self textView] sizeThatFits:CGSizeMake(availableWidth, 10000.0f)].height;
  expanded = (neededHeight > (oneLineHeight + (lineHeight * 0.5f))) ? YES : NO;
  if (expanded == [self expanded]) {
    return;
  }

  [self setExpanded:expanded];
  if ([[self delegate] respondsToSelector:
        @selector(promptSendViewControllerDidChangeHeight:)]) {
    [[self delegate] promptSendViewControllerDidChangeHeight:self];
  }
}

- (void)performSend:(id)sender
{
  NSString *prompt;

  (void)sender;
  if (![self canSendCurrentPrompt]) {
    return;
  }

  prompt = [self trimmedPromptText];
  if ([prompt length] == 0U) {
    return;
  }

  [[self textView] setText:@""];
  [self updatePlaceholderVisibility];
  [self updateExpansion];
  [self updateControls];

  if ([[self delegate] respondsToSelector:
        @selector(promptSendViewController:didSubmitPrompt:)]) {
    [[self delegate] promptSendViewController:self didSubmitPrompt:prompt];
  }
}

- (void)sendTapped:(id)sender
{
  (void)sender;
  if ([self sending]) {
    if (![self cancellationRequested] &&
        [[self delegate] respondsToSelector:
          @selector(promptSendViewControllerDidCancelPrompt:)]) {
      [self setCancellationRequested:YES];
      [[self delegate] promptSendViewControllerDidCancelPrompt:self];
    }
    return;
  }

  [self performSend:sender];
}

- (NSArray *)currentAllowedModels
{
  NSArray *models;
  NSMutableArray *filteredModels;
  NSUInteger index;

  models = nil;
  if ([[self delegate] respondsToSelector:
        @selector(allowedModelsForPromptSendViewController:)]) {
    models = [[self delegate] allowedModelsForPromptSendViewController:self];
  }
  if (![models isKindOfClass:[NSArray class]]) {
    return [NSArray array];
  }

  filteredModels = [NSMutableArray arrayWithCapacity:[models count]];
  for (index = 0U; index < [models count]; index++) {
    NSDictionary *model;

    model = [models objectAtIndex:index];
    if (![model isKindOfClass:[NSDictionary class]]) {
      continue;
    }
    if ([StrappyMessageModelStringForRow(model, @"id") length] == 0U) {
      continue;
    }
    [filteredModels addObject:model];
  }
  return filteredModels;
}

- (NSString *)currentSelectedModelIdentifier
{
  NSString *selectedModelIdentifier;

  selectedModelIdentifier = @"";
  if ([[self delegate] respondsToSelector:
        @selector(selectedModelIdentifierForPromptSendViewController:)]) {
    selectedModelIdentifier =
      [[self delegate] selectedModelIdentifierForPromptSendViewController:self];
  }
  return [selectedModelIdentifier isKindOfClass:[NSString class]]
    ? selectedModelIdentifier
    : @"";
}

- (BOOL)setSelectedModelIdentifierFromOptions:(NSString *)modelIdentifier
{
  if (([modelIdentifier length] == 0U) ||
      ![[self delegate] respondsToSelector:
        @selector(promptSendViewController:setSelectedModelIdentifier:)]) {
    return NO;
  }
  return [[self delegate] promptSendViewController:self
                       setSelectedModelIdentifier:modelIdentifier];
}

- (BOOL)setStreamingEnabledFromOptions:(BOOL)enabled
{
  BOOL changed;

  changed = NO;
  if ([[self delegate] respondsToSelector:
        @selector(promptSendViewController:setStreamingEnabled:)]) {
    changed = [[self delegate] promptSendViewController:self
                                    setStreamingEnabled:(enabled ? YES : NO)];
  }
  if (changed) {
    [self setStreamingEnabled:enabled];
  }
  return changed;
}

- (UIViewController *)containingViewController
{
  UIResponder *responder;

  responder = self;
  while ((responder = [responder nextResponder]) != nil) {
    if ([responder isKindOfClass:[UIViewController class]]) {
      return (UIViewController *)responder;
    }
  }
  return nil;
}

- (void)dismissOptionsControllerAnimated:(BOOL)animated
{
  UINavigationController *navigationController;

  navigationController = [self optionsNavigationController];
  [self setOptionsController:nil];
  [self setOptionsNavigationController:nil];
  if (navigationController != nil) {
    [navigationController dismissModalViewControllerAnimated:animated];
  }
}

- (void)dismissTapped:(id)sender
{
  (void)sender;
  [[self textView] resignFirstResponder];
}

- (void)optionsTapped:(id)sender
{
  UIViewController *presentingController;
  StrappyPromptOptionsTableViewController *optionsController;
  UINavigationController *navigationController;

  (void)sender;
  if ([self sending] || ([self optionsNavigationController] != nil)) {
    return;
  }

  presentingController = [self containingViewController];
  if (presentingController == nil) {
    return;
  }

  [[self textView] resignFirstResponder];
  optionsController =
    [[StrappyPromptOptionsTableViewController alloc]
      initWithPromptSendViewController:self];
  navigationController =
    [[UINavigationController alloc] initWithRootViewController:optionsController];
  [self setOptionsController:optionsController];
  [self setOptionsNavigationController:navigationController];
  [presentingController presentModalViewController:navigationController
                                          animated:YES];
}

- (void)textViewDidChange:(UITextView *)textView
{
  (void)textView;
  [self updatePlaceholderVisibility];
  [self updateControls];
  [self updateExpansion];
}

- (void)dealloc
{
  [[self textView] setDelegate:nil];
}

@end
