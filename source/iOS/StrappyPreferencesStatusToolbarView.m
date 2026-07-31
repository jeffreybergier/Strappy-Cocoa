#import "StrappyPreferencesStatusToolbarView.h"

#import "StrappyAppearance.h"
#import "XPUIKit.h"

static const CGFloat kStrappyPreferencesToolbarSideItemWidth = 44.0f;
static const CGFloat kStrappyPreferencesToolbarLabelHeight = 30.0f;
static const CGFloat kStrappyPreferencesToolbarFallbackHeight = 44.0f;
static const CGFloat kStrappyPreferencesToolbarFallbackWidth = 320.0f;
static const CGFloat kStrappyPreferencesToolbarActionIconSize = 18.0f;

@interface StrappyPreferencesStatusToolbarView ()
@property (nonatomic, strong) UILabel *statusLabel;
@property (nonatomic, strong) UIButton *actionButton;
@property (nonatomic, strong) UIActivityIndicatorView *activityIndicatorView;
@property (nonatomic, assign) AIFontAwesomeIcon actionIcon;
@property (nonatomic, assign) CGFloat toolbarWidth;
@property (nonatomic, assign) CGFloat toolbarContentOffsetX;
- (void)buildStatusLabel;
- (void)buildActionButtonWithTarget:(id)target action:(SEL)action;
- (void)refreshWorkingState;
@end

@implementation StrappyPreferencesStatusToolbarView

- (instancetype)init
{
  return [self initWithFrame:
    CGRectMake(0.0f,
               0.0f,
               kStrappyPreferencesToolbarFallbackWidth,
               kStrappyPreferencesToolbarFallbackHeight)];
}

- (instancetype)initWithFrame:(CGRect)frame
{
  if (CGRectGetWidth(frame) <= 0.0f) {
    frame.size.width = kStrappyPreferencesToolbarFallbackWidth;
  }
  if (CGRectGetHeight(frame) <= 0.0f) {
    frame.size.height = kStrappyPreferencesToolbarFallbackHeight;
  }

  if ((self = [super initWithFrame:frame])) {
    [self setBackgroundColor:[UIColor clearColor]];
    [self setOpaque:NO];
    [self setAutoresizingMask:UIViewAutoresizingFlexibleWidth];
    [self setToolbarWidth:CGRectGetWidth(frame)];
    [self setToolbarContentOffsetX:0.0f];
    [self buildStatusLabel];
    [self refreshAppearanceForToolbar:nil];
  }
  return self;
}

- (instancetype)initWithActionIcon:(AIFontAwesomeIcon)actionIcon
                            target:(id)target
                            action:(SEL)action
{
  if ((self = [self init])) {
    [self setActionIcon:actionIcon];
    [self buildActionButtonWithTarget:target action:action];
    [self refreshAppearanceForToolbar:nil];
  }
  return self;
}

- (void)buildStatusLabel
{
  UILabel *label;

  label = [[UILabel alloc] initWithFrame:CGRectZero];
  [label setBackgroundColor:[UIColor clearColor]];
  [label setFont:[UIFont boldSystemFontOfSize:15.0f]];
  [label setNumberOfLines:1];
  [label XP_setTextAlignmentCenter];
  [label setAutoresizingMask:UIViewAutoresizingFlexibleWidth];
  [self addSubview:label];
  [self setStatusLabel:label];
}

- (void)buildActionButtonWithTarget:(id)target action:(SEL)action
{
  UIButton *button;
  UIActivityIndicatorView *activityView;

  button = [UIButton buttonWithType:UIButtonTypeCustom];
  [button setShowsTouchWhenHighlighted:YES];
  if ((target != nil) && (action != NULL)) {
    [button addTarget:target
               action:action
     forControlEvents:UIControlEventTouchUpInside];
  }
  [self addSubview:button];
  [self setActionButton:button];

  activityView = [[UIActivityIndicatorView alloc]
    initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleWhite];
  [activityView setHidesWhenStopped:YES];
  [self addSubview:activityView];
  [self setActivityIndicatorView:activityView];
  [self refreshWorkingState];
}

- (NSString *)text
{
  return [[self statusLabel] text];
}

- (void)setText:(NSString *)text
{
  [[self statusLabel] setText:text];
}

- (NSString *)actionAccessibilityLabel
{
  return [[self actionButton] accessibilityLabel];
}

- (void)setActionAccessibilityLabel:(NSString *)actionAccessibilityLabel
{
  [[self actionButton] setAccessibilityLabel:actionAccessibilityLabel];
}

- (void)setWorking:(BOOL)working
{
  if (_working == working) {
    return;
  }
  _working = working ? YES : NO;
  [self refreshWorkingState];
}

- (void)refreshWorkingState
{
  if ([self actionButton] == nil) {
    return;
  }
  if ([self isWorking]) {
    [[self activityIndicatorView] startAnimating];
    [[self actionButton] setHidden:YES];
    [[self activityIndicatorView] setHidden:NO];
  } else {
    [[self activityIndicatorView] stopAnimating];
    [[self activityIndicatorView] setHidden:YES];
    [[self actionButton] setHidden:NO];
  }
}

- (void)refreshAppearanceForToolbar:(UIToolbar *)toolbar
{
  UIColor *actionColor;
  BOOL usesIOS7Appearance;

  usesIOS7Appearance = [[UIDevice currentDevice]
    XP_isOperatingSystemAtLeastMajorVersion:7];
  if (usesIOS7Appearance) {
    actionColor = [toolbar tintColor];
    if (actionColor == nil) {
      actionColor = [StrappyAppearance primaryTintColor];
    }
    [[self statusLabel] setTextColor:[UIColor darkTextColor]];
    [[self statusLabel] setShadowColor:nil];
    [[self statusLabel] setShadowOffset:CGSizeZero];
    [[self activityIndicatorView]
      setActivityIndicatorViewStyle:UIActivityIndicatorViewStyleGray];
  } else {
    actionColor = [UIColor whiteColor];
    [[self statusLabel] setTextColor:[UIColor whiteColor]];
    [[self statusLabel]
      setShadowColor:[UIColor colorWithWhite:0.0f alpha:0.5f]];
    [[self statusLabel] setShadowOffset:CGSizeMake(0.0f, -1.0f)];
    [[self activityIndicatorView]
      setActivityIndicatorViewStyle:UIActivityIndicatorViewStyleWhite];
  }

  if ([self actionButton] != nil) {
    UIImage *actionImage;

    actionImage = [AIFontAwesome imageForIcon:[self actionIcon]
                                        style:AIFontAwesomeStyleSolid
                                     iconSize:kStrappyPreferencesToolbarActionIconSize
                                   canvasSize:kStrappyPreferencesToolbarSideItemWidth
                                        color:actionColor
                                        scale:0.0f];
    [[self actionButton] setImage:actionImage forState:UIControlStateNormal];
  }
  [self refreshWorkingState];
}

- (void)layoutForToolbar:(UIToolbar *)toolbar
          containingItem:(UIBarButtonItem *)toolbarItem
           fallbackWidth:(CGFloat)fallbackWidth
{
  CGRect frame;
  CGFloat contentOffsetX;
  CGFloat contentWidth;
  CGFloat height;
  CGFloat width;

  width = (toolbar != nil) ? CGRectGetWidth([toolbar bounds]) : 0.0f;
  if (width <= 0.0f) {
    width = fallbackWidth;
  }
  if (width <= 0.0f) {
    width = CGRectGetWidth([self bounds]);
  }
  if (width <= 0.0f) {
    width = kStrappyPreferencesToolbarFallbackWidth;
  }

  height = (toolbar != nil) ? CGRectGetHeight([toolbar bounds]) : 0.0f;
  if (height <= 0.0f) {
    height = kStrappyPreferencesToolbarFallbackHeight;
  }

  frame = [self frame];
  contentOffsetX = CGRectGetMinX(frame);
  if ((contentOffsetX < 0.0f) || (contentOffsetX >= width)) {
    contentOffsetX = 0.0f;
  }
  contentWidth = width - contentOffsetX;
  if (contentWidth <= 0.0f) {
    contentWidth = width;
  }

  [self setToolbarWidth:width];
  [self setToolbarContentOffsetX:contentOffsetX];
  frame.size.width = contentWidth;
  frame.size.height = height;
  [self setFrame:frame];
  [toolbarItem setWidth:contentWidth];
  [self refreshAppearanceForToolbar:toolbar];
  [self setNeedsLayout];
  [self layoutIfNeeded];
  [toolbar setNeedsLayout];
}

- (void)layoutSubviews
{
  CGFloat buttonX;
  CGFloat contentOffsetX;
  CGFloat height;
  CGFloat labelHeight;
  CGFloat labelWidth;
  CGFloat labelX;
  CGFloat labelY;
  CGFloat sideWidth;
  CGFloat toolbarWidth;
  CGFloat width;

  [super layoutSubviews];

  width = CGRectGetWidth([self bounds]);
  height = CGRectGetHeight([self bounds]);
  toolbarWidth = [self toolbarWidth];
  if (toolbarWidth <= 0.0f) {
    toolbarWidth = width;
  }
  contentOffsetX = [self toolbarContentOffsetX];
  sideWidth = kStrappyPreferencesToolbarSideItemWidth;
  if ((sideWidth * 2.0f) > toolbarWidth) {
    sideWidth = toolbarWidth * 0.5f;
  }

  labelX = sideWidth - contentOffsetX;
  if (labelX < 0.0f) {
    labelX = 0.0f;
  }
  buttonX = width - sideWidth;
  if (buttonX < 0.0f) {
    buttonX = 0.0f;
  }
  labelWidth = buttonX - labelX;
  if (labelWidth < 0.0f) {
    labelWidth = 0.0f;
  }
  labelHeight = kStrappyPreferencesToolbarLabelHeight;
  labelY = (CGFloat)floor((double)((height - labelHeight) * 0.5f));

  [[self statusLabel] setFrame:
    CGRectMake(labelX, labelY, labelWidth, labelHeight)];
  [[self actionButton] setFrame:
    CGRectMake(buttonX, 0.0f, sideWidth, height)];
  [[self activityIndicatorView] setFrame:
    CGRectMake(buttonX, 0.0f, sideWidth, height)];
}

@end
