#import "PromptSendViewController.h"

#import "AIFontAwesome.h"
#import "StrappyAppearance.h"
#import "StrappySession.h"
#import "StrappySessionOptionsTableViewController.h"
#import "XPUIKit.h"

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
      ? [StrappyAppearance highlightedPrimaryTintColor]
      : [StrappyAppearance primaryTintColor];
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

@interface PromptSendViewController ()
  <UITextViewDelegate, StrappySessionOptionsTableViewControllerDelegate>
@property (nonatomic, strong) UIView *topSeparator;
@property (nonatomic, strong) UIView *topHighlight;
@property (nonatomic, strong) UIButton *dismissButton;
@property (nonatomic, strong) UIButton *optionsButton;
@property (nonatomic, strong) UITextView *textView;
@property (nonatomic, strong) StrappyPromptFieldInnerShadowView *textViewShadow;
@property (nonatomic, strong) StrappyPromptSendButton *sendButton;
@property (nonatomic, strong) UINavigationController *optionsNavigationController;
@property (nonatomic, strong) StrappySessionOptionsTableViewController *optionsController;
@property (nonatomic, assign) BOOL controlsEnabled;
@property (nonatomic, assign) BOOL studyLocked;
@property (nonatomic, assign) BOOL composing;
@property (nonatomic, assign) BOOL expanded;
@property (nonatomic, assign) BOOL sending;
@property (nonatomic, assign) BOOL cancellationRequested;
@property (nonatomic, copy) StrappySessionOptions *sessionOptions;
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
- (NSArray *)currentAllowedModels;
- (NSArray *)currentAssistantSets;
- (BOOL)updateSessionOptions:(StrappySessionOptions *)options
               changedFields:(StrappySessionOptionMask)changedFields;
- (UIViewController *)containingViewController;
- (void)dismissOptionsControllerAnimated:(BOOL)animated;
- (void)dismissTapped:(id)sender;
- (void)optionsTapped:(id)sender;
- (void)sendTapped:(id)sender;
@end
@implementation PromptSendViewController

- (instancetype)initWithFrame:(CGRect)frame
{
  if ((self = [super initWithFrame:frame])) {
    [self setBackgroundColor:[UIColor colorWithWhite:0.94f alpha:1.0f]];
    [self setAutoresizingMask:
      UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleTopMargin];
    [self setControlsEnabled:YES];
    [self setSessionOptions:nil];
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
  [options setImage:[self iconImageForIcon:AIFAGear
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

  send = [[StrappyPromptSendButton alloc] initWithFrame:CGRectZero];
  [send setImage:[self iconImageForIcon:AIFAMarsStroke
                                  style:AIFontAwesomeStyleSolid
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
  [[self textView] setEditable:(enabled && ![self sending]) ? YES : NO];
  [self updateControls];
}

- (void)setStudyLocked:(BOOL)studyLocked
{
  _studyLocked = studyLocked ? YES : NO;
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

- (void)setSessionOptions:(StrappySessionOptions *)options
{
  StrappySessionOptions *value;

  value = options;
  if (value == nil) {
    value = [[StrappySessionOptions alloc]
      initWithModelIdentifier:@""
       assistantSetIdentifier:@""
                  webProvider:StrappyWebProviderNone
             webSearchEnabled:NO
                  bashEnabled:NO
               limitToOneTool:NO
                   roundLimit:StrappySessionDefaultRoundLimit
             workingDirectory:@""];
  }
  _sessionOptions = [value copy];
  [[self optionsController] reloadOptionsFromDelegate];
}

- (void)reloadOptionsMenu
{
  [self updateControls];
  [[self optionsController] reloadOptionsFromDelegate];
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
    ? (([self controlsEnabled] || [self studyLocked]) &&
       ![self cancellationRequested])
    : canSend;

  [[self optionsButton] setEnabled:
    ([self controlsEnabled] && ![self studyLocked] && ![self sending]) ? YES : NO];
  [[self dismissButton] setEnabled:
    ([self controlsEnabled] && [self composing]) ? YES : NO];
  [[self textView] setEditable:
    ([self controlsEnabled] && ![self sending]) ? YES : NO];
  [[self sendButton] setEnabled:sendEnabled];

  if ([self sending]) {
    sendImage = [self iconImageForIcon:AIFAHandMiddleFinger
                                 style:AIFontAwesomeStyleSolid
                             pointSize:16.0f
                                 color:[UIColor whiteColor]];
    [[self sendButton] setAccessibilityLabel:NSLocalizedString(@"Cancel Prompt", nil)];
  } else {
    sendImage = [self iconImageForIcon:AIFAMarsStroke
                                 style:AIFontAwesomeStyleSolid
                             pointSize:18.0f
                                 color:[UIColor whiteColor]];
    [[self sendButton] setAccessibilityLabel:NSLocalizedString(@"Send Prompt", nil)];
  }
  [[self sendButton] setImage:sendImage forState:UIControlStateNormal];
  [[self sendButton] setDestructive:[self sending] ? YES : NO];
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

- (NSArray *)currentAssistantSets
{
  NSArray *assistantSets;

  assistantSets = nil;
  if ([[self delegate] respondsToSelector:
        @selector(assistantSetsForPromptSendViewController:)]) {
    assistantSets =
      [[self delegate] assistantSetsForPromptSendViewController:self];
  }
  return [assistantSets isKindOfClass:[NSArray class]] ?
    assistantSets : [NSArray array];
}

- (BOOL)updateSessionOptions:(StrappySessionOptions *)options
               changedFields:(StrappySessionOptionMask)changedFields
{
  StrappySessionOptions *previousOptions;
  BOOL updated;

  if (options == nil || changedFields == 0U ||
      ![[self delegate] respondsToSelector:
        @selector(promptSendViewController:updateSessionOptions:changedFields:)]) {
    return NO;
  }
  previousOptions = [self sessionOptions];
  updated = [[self delegate] promptSendViewController:self
                                  updateSessionOptions:options
                                         changedFields:changedFields];
  if (updated && ([self sessionOptions] == previousOptions)) {
    [self setSessionOptions:options];
  }
  return updated;
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
    [navigationController XP_dismissViewControllerAnimated:animated];
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
  StrappySessionOptionsTableViewController *optionsController;
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
    [[StrappySessionOptionsTableViewController alloc]
      initWithOptionsDelegate:self
           presentedModally:YES];
  navigationController =
    [[UINavigationController alloc] initWithRootViewController:optionsController];
  [self setOptionsController:optionsController];
  [self setOptionsNavigationController:navigationController];
  [presentingController XP_presentViewController:navigationController
                                         animated:YES];
}

- (void)textViewDidChange:(UITextView *)textView
{
  (void)textView;
  [self updateControls];
  [self updateExpansion];
}

- (void)dealloc
{
  [[self textView] setDelegate:nil];
}

@end
