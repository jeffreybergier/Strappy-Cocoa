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
static const CGFloat kStrappySendFieldRadius = 8.0f;

@interface StrappyPromptFieldInnerShadowView : UIView
@end

@implementation StrappyPromptFieldInnerShadowView

- (instancetype)initWithFrame:(CGRect)frame
{
  if ((self = [super initWithFrame:frame])) {
    [self setOpaque:NO];
    [self setBackgroundColor:[UIColor clearColor]];
    [self setUserInteractionEnabled:NO];
    [self setContentMode:UIViewContentModeRedraw];
  }
  return self;
}

- (void)drawRect:(CGRect)rect
{
  CGContextRef context;
  CGRect bounds;
  UIBezierPath *roundedPath;
  CGMutablePathRef ringPath;

  (void)rect;
  context = UIGraphicsGetCurrentContext();
  if (context == NULL) {
    return;
  }

  bounds = [self bounds];
  roundedPath = [UIBezierPath bezierPathWithRoundedRect:bounds
                                           cornerRadius:kStrappySendFieldRadius];

  CGContextSaveGState(context);
  [roundedPath addClip];
  CGContextSetShadowWithColor(context,
                              CGSizeMake(0.0f, 1.0f),
                              3.0f,
                              [[UIColor colorWithWhite:0.0f alpha:0.22f] CGColor]);
  ringPath = CGPathCreateMutable();
  CGPathAddRect(ringPath,
                NULL,
                CGRectInset(bounds,
                            -(kStrappySendFieldRadius * 2.0f + 8.0f),
                            -(kStrappySendFieldRadius * 2.0f + 8.0f)));
  CGPathAddPath(ringPath, NULL, [roundedPath CGPath]);
  CGContextAddPath(context, ringPath);
  CGContextSetFillColorWithColor(context, [[UIColor blackColor] CGColor]);
  CGContextEOFillPath(context);
  CGPathRelease(ringPath);
  CGContextRestoreGState(context);
}

@end

@interface StrappyPromptPressableIconButton : UIButton
@end

@implementation StrappyPromptPressableIconButton

- (instancetype)initWithFrame:(CGRect)frame
{
  if ((self = [super initWithFrame:frame])) {
    [self setOpaque:NO];
    [self setBackgroundColor:[UIColor clearColor]];
    [[self imageView] setContentMode:UIViewContentModeCenter];
  }
  return self;
}

- (void)setHighlighted:(BOOL)highlighted
{
  [super setHighlighted:highlighted];
  [self setNeedsDisplay];
}

- (void)drawRect:(CGRect)rect
{
  UIBezierPath *backgroundPath;

  (void)rect;
  if (![self isHighlighted] || ![self isEnabled]) {
    return;
  }

  backgroundPath =
    [UIBezierPath bezierPathWithRoundedRect:CGRectInset([self bounds],
                                                        2.0f,
                                                        2.0f)
                               cornerRadius:5.0f];
  [[UIColor colorWithWhite:0.0f alpha:0.12f] setFill];
  [backgroundPath fill];
}

@end

@interface StrappyPromptBorderedIconButton : StrappyPromptPressableIconButton
@end

@implementation StrappyPromptBorderedIconButton

- (CGFloat)hairline
{
  CGFloat scale;

  scale = [[UIScreen mainScreen] scale];
  if (scale <= 0.0f) {
    scale = 1.0f;
  }
  return 1.0f / scale;
}

- (void)setEnabled:(BOOL)enabled
{
  [super setEnabled:enabled];
  [self setNeedsDisplay];
}

- (void)drawRect:(CGRect)rect
{
  CGFloat hairline;
  UIBezierPath *buttonPath;
  UIColor *fillColor;
  UIColor *strokeColor;

  (void)rect;
  hairline = [self hairline];
  buttonPath =
    [UIBezierPath bezierPathWithRoundedRect:CGRectInset([self bounds],
                                                        hairline * 0.5f,
                                                        hairline * 0.5f)
                               cornerRadius:6.0f];
  fillColor = [UIColor whiteColor];
  strokeColor = [self isEnabled]
    ? [UIColor colorWithWhite:0.78f alpha:1.0f]
    : [UIColor colorWithWhite:0.84f alpha:1.0f];

  [fillColor setFill];
  [buttonPath fill];

  if ([self isHighlighted] && [self isEnabled]) {
    [[UIColor colorWithWhite:0.0f alpha:0.12f] setFill];
    [buttonPath fill];
  }

  [buttonPath setLineWidth:hairline];
  [strokeColor setStroke];
  [buttonPath stroke];
}

@end

@interface StrappyPromptSendButton : UIButton
@property (nonatomic, assign, getter=isDestructive) BOOL destructive;
@end

@implementation StrappyPromptSendButton

- (instancetype)initWithFrame:(CGRect)frame
{
  if ((self = [super initWithFrame:frame])) {
    [self setOpaque:NO];
    [self setBackgroundColor:[UIColor clearColor]];
    [[self imageView] setContentMode:UIViewContentModeCenter];
  }
  return self;
}

- (void)setEnabled:(BOOL)enabled
{
  [super setEnabled:enabled];
  [self setNeedsDisplay];
}

- (void)setHighlighted:(BOOL)highlighted
{
  [super setHighlighted:highlighted];
  [self setNeedsDisplay];
}

- (void)setDestructive:(BOOL)destructive
{
  _destructive = destructive ? YES : NO;
  [self setNeedsDisplay];
}

- (void)drawRect:(CGRect)rect
{
  CGContextRef context;
  CGRect bounds;
  UIBezierPath *discPath;
  UIColor *fillColor;
  CGMutablePathRef ringPath;

  (void)rect;
  context = UIGraphicsGetCurrentContext();
  if (context == NULL) {
    return;
  }

  bounds = [self bounds];
  if (![self isEnabled]) {
    fillColor = [UIColor colorWithWhite:0.74f alpha:1.0f];
  } else if ([self isDestructive]) {
    fillColor = [self isHighlighted]
      ? [UIColor colorWithRed:0.58f green:0.12f blue:0.11f alpha:1.0f]
      : [UIColor colorWithRed:0.72f green:0.18f blue:0.16f alpha:1.0f];
  } else {
    fillColor = [self isHighlighted]
      ? [UIColor colorWithRed:0.0f green:0.40f blue:0.80f alpha:1.0f]
      : [UIColor colorWithRed:0.0f green:0.52f blue:1.0f alpha:1.0f];
  }

  discPath = [UIBezierPath bezierPathWithOvalInRect:bounds];
  [fillColor setFill];
  [discPath fill];

  CGContextSaveGState(context);
  [discPath addClip];
  CGContextSetShadowWithColor(context,
                              CGSizeMake(0.0f, 1.0f),
                              2.5f,
                              [[UIColor colorWithWhite:0.0f alpha:0.35f] CGColor]);
  ringPath = CGPathCreateMutable();
  CGPathAddRect(ringPath,
                NULL,
                CGRectInset(bounds,
                            -bounds.size.width,
                            -bounds.size.height));
  CGPathAddEllipseInRect(ringPath, NULL, bounds);
  CGContextAddPath(context, ringPath);
  CGContextSetFillColorWithColor(context, [[UIColor blackColor] CGColor]);
  CGContextEOFillPath(context);
  CGPathRelease(ringPath);
  CGContextRestoreGState(context);
}

@end

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
  kStrappyPromptOptionsSectionWebSearch,
  kStrappyPromptOptionsSectionStreaming,
  kStrappyPromptOptionsSectionCount
};

@interface PromptSendViewController () <UITextViewDelegate>
@property (nonatomic, strong) UIView *topSeparator;
@property (nonatomic, strong) UIView *topHighlight;
@property (nonatomic, strong) UIButton *dismissButton;
@property (nonatomic, strong) UIButton *optionsButton;
@property (nonatomic, strong) UITextView *textView;
@property (nonatomic, strong) StrappyPromptFieldInnerShadowView *textViewShadow;
@property (nonatomic, strong) UILabel *placeholderLabel;
@property (nonatomic, strong) StrappyPromptSendButton *sendButton;
@property (nonatomic, strong) UINavigationController *optionsNavigationController;
@property (nonatomic, strong) StrappyPromptOptionsTableViewController *optionsController;
@property (nonatomic, assign) BOOL controlsEnabled;
@property (nonatomic, assign) BOOL composing;
@property (nonatomic, assign) BOOL expanded;
@property (nonatomic, assign) BOOL sending;
@property (nonatomic, assign) BOOL cancellationRequested;
@property (nonatomic, assign) BOOL webSearchEnabled;
@property (nonatomic, assign) BOOL streamingEnabled;
- (void)buildSubviews;
- (UIImage *)iconImageForIcon:(AIFontAwesomeIcon)icon
                        style:(AIFontAwesomeStyle)style
                    pointSize:(CGFloat)pointSize
                        color:(UIColor *)color;
- (CGFloat)hairline;
- (BOOL)showsExpanded;
- (CGFloat)collapsedTextWidth;
- (NSString *)trimmedPromptText;
- (void)updateControls;
- (void)updateExpansion;
- (void)updatePlaceholderVisibility;
- (NSArray *)currentAllowedModels;
- (NSString *)currentSelectedModelIdentifier;
- (BOOL)setSelectedModelIdentifierFromOptions:(NSString *)modelIdentifier;
- (BOOL)setWebSearchEnabledFromOptions:(BOOL)enabled;
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
@property (nonatomic, strong) UISwitch *webSearchSwitch;
@property (nonatomic, strong) UISwitch *streamingSwitch;
@property (nonatomic, assign) BOOL webSearchEnabled;
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
  UISwitch *webSearchSwitch;
  UISwitch *streamingSwitch;

  [super viewDidLoad];

  webSearchSwitch = [[UISwitch alloc] initWithFrame:CGRectZero];
  [webSearchSwitch addTarget:self
                      action:@selector(webSearchSwitchChanged:)
            forControlEvents:UIControlEventValueChanged];
  [self setWebSearchSwitch:webSearchSwitch];

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
  [self setWebSearchEnabled:
    (promptSendViewController != nil)
      ? [promptSendViewController webSearchEnabled]
      : YES];
}

- (void)reloadOptionsFromPrompt
{
  [self reloadOptionsSnapshot];
  [[self webSearchSwitch] setOn:[self webSearchEnabled] animated:NO];
  [[self streamingSwitch] setOn:[self streamingEnabled] animated:NO];
  [[self tableView] reloadData];
}

- (void)doneAction:(id)sender
{
  (void)sender;
  [[self promptSendViewController] dismissOptionsControllerAnimated:YES];
}

- (void)webSearchSwitchChanged:(UISwitch *)sender
{
  PromptSendViewController *promptSendViewController;

  promptSendViewController = [self promptSendViewController];
  if (promptSendViewController != nil) {
    (void)[promptSendViewController setWebSearchEnabledFromOptions:
      [sender isOn]];
    [self setWebSearchEnabled:[promptSendViewController webSearchEnabled]];
  }
  [sender setOn:[self webSearchEnabled] animated:YES];
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
  if (section == kStrappyPromptOptionsSectionWebSearch) {
    return 1;
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
  if (section == kStrappyPromptOptionsSectionWebSearch) {
    return NSLocalizedString(@"Web Search", nil);
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

  if ([indexPath section] == kStrappyPromptOptionsSectionWebSearch) {
    cell = [tableView dequeueReusableCellWithIdentifier:@"WebSearchCell"];
    if (cell == nil) {
      cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
                                    reuseIdentifier:@"WebSearchCell"];
      [cell setSelectionStyle:UITableViewCellSelectionStyleNone];
    }
    [[cell textLabel] setText:NSLocalizedString(@"Search Web", nil)];
    [[self webSearchSwitch] setOn:[self webSearchEnabled] animated:NO];
    [cell setAccessoryView:[self webSearchSwitch]];
    return cell;
  }

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
    [self setWebSearchEnabled:YES];
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

- (CGFloat)hairline
{
  CGFloat scale;

  scale = [[UIScreen mainScreen] scale];
  if (scale <= 0.0f) {
    scale = 1.0f;
  }
  return 1.0f / scale;
}

- (BOOL)showsExpanded
{
  return ([self composing] && [self expanded]) ? YES : NO;
}

- (CGFloat)collapsedTextWidth
{
  CGRect bounds;
  CGFloat textX;
  CGFloat sendX;
  CGFloat optionsX;
  CGFloat textRight;
  CGFloat width;

  bounds = [self bounds];
  textX = [self composing] ? kStrappySendDismissWidth : kStrappySendPad;
  sendX = bounds.size.width - kStrappySendPad - kStrappySendButtonSize;
  optionsX = sendX - kStrappySendPad - kStrappySendOptionsWidth;
  textRight = optionsX - kStrappySendPad;
  width = textRight - textX;
  return (width > 1.0f) ? width : 1.0f;
}

- (void)buildSubviews
{
  UIView *topSeparator;
  UIView *topHighlight;
  UIButton *dismiss;
  UIButton *options;
  UITextView *textView;
  StrappyPromptFieldInnerShadowView *textViewShadow;
  UILabel *placeholder;
  StrappyPromptSendButton *send;
  CGFloat hairline;

  hairline = [self hairline];

  topSeparator = [[UIView alloc] initWithFrame:CGRectZero];
  [topSeparator setBackgroundColor:[UIColor colorWithWhite:0.72f alpha:1.0f]];
  [topSeparator setUserInteractionEnabled:NO];
  [self addSubview:topSeparator];
  [self setTopSeparator:topSeparator];

  topHighlight = [[UIView alloc] initWithFrame:CGRectZero];
  [topHighlight setBackgroundColor:[UIColor whiteColor]];
  [topHighlight setUserInteractionEnabled:NO];
  [self addSubview:topHighlight];
  [self setTopHighlight:topHighlight];

  dismiss = [[StrappyPromptPressableIconButton alloc] initWithFrame:CGRectZero];
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

  options = [[StrappyPromptBorderedIconButton alloc] initWithFrame:CGRectZero];
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
  [textView setClipsToBounds:YES];
  [[textView layer] setCornerRadius:kStrappySendFieldRadius];
  [[textView layer] setBorderWidth:hairline];
  [[textView layer] setBorderColor:[[UIColor colorWithWhite:0.80f alpha:1.0f] CGColor]];
  [self addSubview:textView];
  [self setTextView:textView];

  textViewShadow =
    [[StrappyPromptFieldInnerShadowView alloc] initWithFrame:CGRectZero];
  [self addSubview:textViewShadow];
  [self setTextViewShadow:textViewShadow];

  placeholder = [[UILabel alloc] initWithFrame:CGRectZero];
  [placeholder setText:NSLocalizedString(@"Ask Strappy", nil)];
  [placeholder setFont:[UIFont systemFontOfSize:kStrappySendFontSize]];
  [placeholder setTextColor:[UIColor colorWithWhite:0.58f alpha:1.0f]];
  [placeholder setBackgroundColor:[UIColor clearColor]];
  [placeholder setUserInteractionEnabled:NO];
  [self addSubview:placeholder];
  [self setPlaceholderLabel:placeholder];

  send = [[StrappyPromptSendButton alloc] initWithFrame:CGRectZero];
  [send setImage:[self iconImageForIcon:AIFAPaperPlane
                                  style:AIFontAwesomeStyleRegular
                              pointSize:18.0f
                                  color:[UIColor whiteColor]]
         forState:UIControlStateNormal];
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
  CGFloat optionsY;
  CGFloat textX;
  CGFloat sendX;
  CGFloat textRight;
  CGFloat textHeight;
  CGFloat placeholderHeight;
  CGFloat placeholderWidth;
  CGFloat hairline;

  [super layoutSubviews];

  bounds = [self bounds];
  hairline = [self hairline];
  sendX = bounds.size.width - kStrappySendPad - kStrappySendButtonSize;
  if ([self showsExpanded]) {
    optionsX = sendX;
    optionsY = kStrappySendPad + kStrappySendButtonSize + kStrappySendPad;
  } else {
    optionsX = sendX - kStrappySendPad - kStrappySendOptionsWidth;
    optionsY = kStrappySendPad;
  }
  textX = [self composing] ? kStrappySendDismissWidth : kStrappySendPad;
  textRight = [self showsExpanded]
    ? (sendX - kStrappySendPad)
    : (optionsX - kStrappySendPad);
  if (textRight < textX) {
    textRight = textX;
  }
  textHeight = bounds.size.height - (kStrappySendPad * 2.0f);
  if (textHeight < kStrappySendButtonSize) {
    textHeight = kStrappySendButtonSize;
  }

  [[self topSeparator] setFrame:CGRectMake(0.0f,
                                           0.0f,
                                           bounds.size.width,
                                           hairline)];
  [[self topHighlight] setFrame:CGRectMake(0.0f,
                                           hairline,
                                           bounds.size.width,
                                           hairline)];
  [[self dismissButton] setAlpha:[self composing] ? 1.0f : 0.0f];
  [[self dismissButton] setFrame:CGRectMake(0.0f,
                                            kStrappySendPad,
                                            kStrappySendDismissWidth,
                                            kStrappySendButtonSize)];
  [[self optionsButton] setFrame:CGRectMake(optionsX,
                                            optionsY,
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
  [[self textViewShadow] setFrame:[[self textView] frame]];

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
  return [self showsExpanded]
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

- (void)setWebSearchEnabled:(BOOL)enabled
{
  _webSearchEnabled = enabled ? YES : NO;
  [[self optionsController] reloadOptionsFromPrompt];
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
  [[self sendButton] setDestructive:[self sending] ? YES : NO];
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
  availableWidth = [self collapsedTextWidth];
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

- (BOOL)setWebSearchEnabledFromOptions:(BOOL)enabled
{
  BOOL changed;

  changed = NO;
  if ([[self delegate] respondsToSelector:
        @selector(promptSendViewController:setWebSearchEnabled:)]) {
    changed = [[self delegate] promptSendViewController:self
                                   setWebSearchEnabled:(enabled ? YES : NO)];
  }
  if (changed) {
    [self setWebSearchEnabled:enabled];
  }
  return changed;
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
