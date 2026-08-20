#import "PromptSendViewController.h"
#import "AIFontAwesome.h"
#import "StrappyBottomToolbarView.h"

static const CGFloat kPromptSendHeightCollapsed = 32.0;
static const CGFloat kPromptSendHeightExpanded = 108.0;
static const CGFloat kPromptSendPad = 4.0;
static const CGFloat kPromptActionButtonHeight = 24.0;
static const CGFloat kPromptActionGlyphSize = 14.0;
static const CGFloat kPromptActionSmallGlyphSize = 13.0;
static const CGFloat kPromptActionGlyphCanvasSize = 20.0;

enum {
  kPromptActionSegmentClose = 0,
  kPromptActionSegmentInspector = 1,
  kPromptActionSegmentSend = 2
};

static NSColor *StrappyInputBezelBackgroundColor(void) { return [NSColor controlBackgroundColor]; }
static NSColor *StrappyInputBezelBorderColor(void) { return [NSColor gridColor]; }
static NSColor *StrappyInputBezelHighlightColor(void) { return XPColorControlHighlight; }

@interface StrappyPromptInputBezelView : NSView
@end

@implementation StrappyPromptInputBezelView

- (void)drawRect:(NSRect)dirtyRect
{
  NSRect bounds;

  (void)dirtyRect;
  bounds = [self bounds];

  [StrappyInputBezelBackgroundColor() set];
  NSRectFill(bounds);

  if (AICCCurrentTier() >= AICCTierMiddle) {
    return;
  }

  [StrappyInputBezelBorderColor() set];
  NSFrameRect(bounds);

  [StrappyInputBezelHighlightColor() set];
  NSRectFill(NSMakeRect(bounds.origin.x + 1.0,
                        bounds.origin.y + bounds.size.height - 2.0,
                        bounds.size.width - 2.0,
                        1.0));
}

@end

@interface PromptSendViewController ()
- (void)layoutPromptViews;
- (void)updateExpansion;
- (void)updateActionControls;
- (void)updateSendButtonAppearance;
- (void)rebuildActionSegmentIcons;
- (void)updateSidebarButtonAppearance;
- (void)barDidMoveToWindow:(id)sender;
- (void)barViewFrameDidChange:(NSNotification *)notification;
- (void)sidebarSplitViewDidResize:(NSNotification *)notification;
- (void)sidebarSegmentClicked:(id)sender;
- (void)actionSegmentClicked:(id)sender;
- (void)sendButtonClicked:(id)sender;
@end

@implementation PromptSendViewController

- (id)init
{
  if ((self = [super init])) {
    enabled_ = YES;
  }
  return self;
}

- (void)setDelegate:(id<PromptSendViewControllerDelegate>)delegate
{
  delegate_ = delegate;
}

- (id<PromptSendViewControllerDelegate>)delegate
{
  return delegate_;
}

- (void)loadView
{
  StrappyBottomToolbarView *bar;

  bar = [[StrappyBottomToolbarView alloc]
      initWithFrame:NSMakeRect(0.0, 0.0, 400.0, kPromptSendHeightCollapsed)];
  barView_ = bar;
  [barView_ setAutoresizingMask:NSViewWidthSizable | NSViewMaxYMargin];
  [bar setWindowChangeTarget:self action:@selector(barDidMoveToWindow:)];
  [self setView:barView_];
  [barView_ release];
}

- (void)viewDidLoad
{
  StrappyPromptInputBezelView *bezel;

  [super viewDidLoad];

  bezel = [[StrappyPromptInputBezelView alloc] initWithFrame:NSZeroRect];
  bezelView_ = bezel;
  [bezelView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [barView_ addSubview:bezelView_];
  [bezel release];
  [bezelView_ XP_setWantsLayer:YES];
  [bezelView_ XP_setLayerCornerRadius:8.0];
  [bezelView_ XP_setLayerMasksToBounds:YES];
  [bezelView_ XP_setLayerBorderWidth:1.0];
  [bezelView_ XP_setLayerBorderColor:StrappyInputBezelBorderColor()];

  scrollView_ = [[NSScrollView alloc] initWithFrame:NSZeroRect];
  [scrollView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [scrollView_ setBorderType:NSNoBorder];
  [scrollView_ setHasVerticalScroller:YES];
  [scrollView_ setHasHorizontalScroller:NO];
  if ([scrollView_ respondsToSelector:@selector(setAutohidesScrollers:)]) {
    [scrollView_ setAutohidesScrollers:YES];
  }
  [bezelView_ addSubview:scrollView_];

  textView_ = [[NSTextView alloc] initWithFrame:NSZeroRect];
  [textView_ setMinSize:NSMakeSize(0.0, 0.0)];
  [textView_ setMaxSize:NSMakeSize(100000.0, 100000.0)];
  [textView_ setVerticallyResizable:YES];
  [textView_ setHorizontallyResizable:NO];
  [textView_ setAutoresizingMask:NSViewWidthSizable];
  [[textView_ textContainer] setWidthTracksTextView:YES];
  [textView_ setRichText:NO];
  [textView_ setImportsGraphics:NO];
  [textView_ setFont:XPFontTextStyleBody];
  [textView_ setDrawsBackground:YES];
  [textView_ setBackgroundColor:[NSColor controlBackgroundColor]];
  [textView_ setTextContainerInset:NSMakeSize(2.0, 2.0)];
  [textView_ setDelegate:self];
  [scrollView_ setDocumentView:textView_];

  sidebarSegmented_ = [[NSSegmentedControl alloc] initWithFrame:NSZeroRect];
  [sidebarSegmented_ setSegmentCount:1];
  [[sidebarSegmented_ cell]
    setTrackingMode:NSSegmentSwitchTrackingMomentary];
  [sidebarSegmented_ XP_setToolbarSegmentStyle];
  [sidebarSegmented_ setTarget:self];
  [sidebarSegmented_ setAction:@selector(sidebarSegmentClicked:)];
  [sidebarSegmented_
    setAutoresizingMask:NSViewMaxXMargin | NSViewMaxYMargin];
  [barView_ addSubview:sidebarSegmented_];

  /* Match ENIL's compact composer action control: icon-only Close and
   * Inspector segments followed by an icon-and-label Send segment. Momentary
   * tracking makes all three segments act like ordinary push buttons. The
   * toolbar segment style is runtime-probed by XPAppKit, leaving Tiger on its
   * native Aqua segmented-control appearance. */
  actionSegmented_ = [[NSSegmentedControl alloc] initWithFrame:NSZeroRect];
  [actionSegmented_ setSegmentCount:3];
  [[actionSegmented_ cell]
    setTrackingMode:NSSegmentSwitchTrackingMomentary];
  [actionSegmented_ XP_setToolbarSegmentStyle];
  [actionSegmented_ setTarget:self];
  [actionSegmented_ setAction:@selector(actionSegmentClicked:)];
  [actionSegmented_
    setAutoresizingMask:NSViewMinXMargin | NSViewMaxYMargin];
  [barView_ addSubview:actionSegmented_];
  [self updateSendButtonAppearance];

  [barView_ setPostsFrameChangedNotifications:YES];
  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(barViewFrameDidChange:)
             name:NSViewFrameDidChangeNotification
           object:barView_];
  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(sidebarSplitViewDidResize:)
             name:NSSplitViewDidResizeSubviewsNotification
           object:nil];

  [self setEnabled:enabled_];
  [self updateActionControls];
  [self updateSidebarButtonAppearance];
}

- (void)viewDidLayout
{
  [super viewDidLayout];
  [self layoutPromptViews];
}

- (void)layoutPromptViews
{
  NSRect bounds;
  NSSize actionSize;
  CGFloat actionWidth;
  CGFloat actionHeight;
  CGFloat sidebarWidth;
  CGFloat inputWidth;
  CGFloat inputX;
  CGFloat actionX;

  bounds = [barView_ bounds];
  actionSize = [actionSegmented_ frame].size;
  actionWidth = actionSize.width;
  actionHeight = actionSize.height;
  if (actionHeight <= 0.0) {
    actionHeight = kPromptActionButtonHeight;
  }
  sidebarWidth = actionHeight;
  inputWidth = bounds.size.width - sidebarWidth - actionWidth -
    (kPromptSendPad * 5.0);
  if (inputWidth < 0.0) {
    inputWidth = 0.0;
  }

  [sidebarSegmented_ setFrame:NSMakeRect(kPromptSendPad,
                                         kPromptSendPad,
                                         sidebarWidth,
                                         actionHeight)];
  inputX = kPromptSendPad + sidebarWidth + kPromptSendPad;
  actionX = NSMaxX(bounds) - kPromptSendPad - actionWidth;
  [actionSegmented_ setFrame:NSMakeRect(actionX,
                                        kPromptSendPad,
                                        actionWidth,
                                        actionHeight)];
  [bezelView_ setFrame:NSMakeRect(inputX,
                                  kPromptSendPad,
                                  inputWidth,
                                  bounds.size.height - (kPromptSendPad * 2.0))];
  [scrollView_ setFrame:[bezelView_ bounds]];
}

- (CGFloat)preferredHeight
{
  return expanded_ ? kPromptSendHeightExpanded : kPromptSendHeightCollapsed;
}

- (void)barViewFrameDidChange:(NSNotification *)notification
{
  (void)notification;
  [self updateExpansion];
}

- (void)updateSendButtonAppearance
{
  if (actionSegmented_ == nil) {
    return;
  }

  if (sending_) {
    [actionSegmented_ setLabel:NSLocalizedString(@"Cancel", nil)
                    forSegment:kPromptActionSegmentSend];
  } else {
    [actionSegmented_ setLabel:NSLocalizedString(@"Send", nil)
                    forSegment:kPromptActionSegmentSend];
  }
  [self rebuildActionSegmentIcons];
}

- (void)rebuildActionSegmentIcons
{
  CGFloat scale;
  CGFloat sendIconSize;
  AIFontAwesomeIcon sendIcon;
  AIFontAwesomeStyle sendStyle;

  if (actionSegmented_ == nil) {
    return;
  }

  scale = [[barView_ window] XP_backingScaleFactor];
  if (scale < 1.0) {
    scale = 1.0;
  }

  [actionSegmented_
    setImage:[AIFontAwesome imageForIcon:AIFACircleXmark
                                   style:AIFontAwesomeStyleRegular
                                iconSize:kPromptActionGlyphSize
                              canvasSize:kPromptActionGlyphCanvasSize
                                   scale:scale]
  forSegment:kPromptActionSegmentClose];
  [actionSegmented_
    setImage:[AIFontAwesome imageForIcon:AIFAGear
                                   style:AIFontAwesomeStyleSolid
                                iconSize:kPromptActionSmallGlyphSize
                              canvasSize:kPromptActionGlyphCanvasSize
                                   scale:scale]
  forSegment:kPromptActionSegmentInspector];

  sendIcon = sending_ ? AIFAStop : AIFAPaperPlane;
  sendIconSize = sending_ ?
    kPromptActionGlyphSize : kPromptActionSmallGlyphSize;
  sendStyle = sending_ ?
    AIFontAwesomeStyleSolid : AIFontAwesomeStyleRegular;
  [actionSegmented_
    setImage:[AIFontAwesome imageForIcon:sendIcon
                                   style:sendStyle
                                iconSize:sendIconSize
                              canvasSize:kPromptActionGlyphCanvasSize
                                   scale:scale]
  forSegment:kPromptActionSegmentSend];

  [actionSegmented_ XP_setToolTip:NSLocalizedString(@"Close Chat", nil)
                       forSegment:kPromptActionSegmentClose];
  [actionSegmented_ XP_setToolTip:NSLocalizedString(@"Session Options", nil)
                       forSegment:kPromptActionSegmentInspector];
  [actionSegmented_ XP_setToolTip:NSLocalizedString(
      sending_ ? @"Cancel Prompt" : @"Send Prompt", nil)
                       forSegment:kPromptActionSegmentSend];

  [actionSegmented_ sizeToFit];
  [self layoutPromptViews];
}

- (void)barDidMoveToWindow:(id)sender
{
  (void)sender;
  sidebarStateKnown_ = NO;
  [self updateSidebarButtonAppearance];
  [self rebuildActionSegmentIcons];
}

- (void)updateSidebarButtonAppearance
{
  AICookieCutterWindowController *windowController;
  AIFontAwesomeIcon icon;
  CGFloat scale;
  BOOL collapsed;

  if (sidebarSegmented_ == nil) {
    return;
  }

  windowController = (AICookieCutterWindowController *)
    [[barView_ window] windowController];
  collapsed = (windowController != nil) ?
    [windowController isSidebarCollapsed] : NO;
  if (sidebarStateKnown_ && (sidebarCollapsed_ == collapsed)) {
    return;
  }
  sidebarStateKnown_ = YES;
  sidebarCollapsed_ = collapsed;

  scale = [[barView_ window] XP_backingScaleFactor];
  if (scale < 1.0) {
    scale = 1.0;
  }

  /* These directional glyphs are the crisp raster equivalents of rotating
   * chevron-down 90 degrees while open and 270 degrees while collapsed. */
  icon = collapsed ? AIFAChevronRight : AIFAChevronLeft;
  [sidebarSegmented_ setImage:[AIFontAwesome imageForIcon:icon
                                                   style:AIFontAwesomeStyleSolid
                                                iconSize:kPromptActionSmallGlyphSize
                                              canvasSize:kPromptActionGlyphCanvasSize
                                                   scale:scale]
                        forSegment:0];
  [sidebarSegmented_ setLabel:@"" forSegment:0];
  [sidebarSegmented_ setToolTip:NSLocalizedString(
    collapsed ? @"Show Sidebar" : @"Hide Sidebar", nil)];
}

- (void)sidebarSplitViewDidResize:(NSNotification *)notification
{
  (void)notification;
  [self updateSidebarButtonAppearance];
}

- (void)sidebarSegmentClicked:(id)sender
{
  [[self nextResponder] tryToPerform:@selector(toggleSidebar:) with:sender];
  sidebarStateKnown_ = NO;
  [self updateSidebarButtonAppearance];
}

- (void)updateActionControls
{
  if (actionSegmented_ == nil) {
    return;
  }

  [actionSegmented_ setEnabled:enabled_
                    forSegment:kPromptActionSegmentClose];
  /* The inspector is a window-visibility control, so it remains usable even
   * when no session is selected; the inspector itself presents its empty
   * state and independently disables settings that cannot be edited. */
  [actionSegmented_ setEnabled:YES
                    forSegment:kPromptActionSegmentInspector];
  [actionSegmented_ setEnabled:(sending_ ?
    ((enabled_ || studyLocked_) && !cancellationRequested_) :
    [self canSendCurrentPrompt])
                    forSegment:kPromptActionSegmentSend];
}

- (void)updateExpansion
{
  NSLayoutManager *layoutManager;
  NSTextContainer *textContainer;
  NSRange glyphRange;
  NSUInteger lineCount;
  NSUInteger index;
  BOOL nowExpanded;

  if (textView_ == nil) {
    return;
  }

  layoutManager = [textView_ layoutManager];
  textContainer = [textView_ textContainer];
  glyphRange = [layoutManager glyphRangeForTextContainer:textContainer];
  lineCount = 0U;
  index = glyphRange.location;

  while (index < NSMaxRange(glyphRange)) {
    NSRange lineRange;

    (void)[layoutManager lineFragmentUsedRectForGlyphAtIndex:index
                                               effectiveRange:&lineRange];
    if (lineRange.length == 0U) {
      break;
    }
    lineCount++;
    index = NSMaxRange(lineRange);
  }

  nowExpanded = (lineCount > 1U) ? YES : NO;
  if (nowExpanded == expanded_) {
    return;
  }

  expanded_ = nowExpanded;
  if (delegate_ != nil) {
    [delegate_ promptSendViewControllerDidChangeHeight:self];
  }
}

- (void)setEnabled:(BOOL)enabled
{
  enabled_ = enabled ? YES : NO;
  [textView_ setEditable:enabled_];
  [textView_ setSelectable:enabled_];
  [textView_ setDrawsBackground:YES];
  [textView_ setBackgroundColor:enabled_
    ? [NSColor controlBackgroundColor]
    : [NSColor disabledControlTextColor]];
  [self updateActionControls];
}

- (void)setStudyLocked:(BOOL)studyLocked
{
  studyLocked_ = studyLocked ? YES : NO;
  [self updateActionControls];
}

- (void)setSending:(BOOL)sending
{
  sending_ = sending ? YES : NO;
  if (!sending_) {
    cancellationRequested_ = NO;
  }
  [self updateSendButtonAppearance];
  [self updateActionControls];
}

- (void)setCancellationRequested:(BOOL)requested
{
  cancellationRequested_ = requested ? YES : NO;
  [self updateActionControls];
}

- (BOOL)canSendCurrentPrompt
{
  NSString *text;
  NSString *trimmed;

  if (!enabled_ || sending_ || (textView_ == nil)) {
    return NO;
  }

  text = [textView_ string];
  trimmed = [text stringByTrimmingCharactersInSet:
    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
  return ([trimmed length] > 0U) ? YES : NO;
}

- (void)sendButtonClicked:(id)sender
{
  if (sending_) {
    if (!cancellationRequested_) {
      [self setCancellationRequested:YES];
      if (delegate_ != nil) {
        [delegate_ promptSendViewControllerDidCancelPrompt:self];
      }
    }
    return;
  }

  [self performSend:sender];
}

- (void)actionSegmentClicked:(id)sender
{
  NSInteger segment;

  if (![sender isKindOfClass:[NSSegmentedControl class]]) {
    return;
  }

  segment = [(NSSegmentedControl *)sender selectedSegment];
  if (segment == kPromptActionSegmentClose) {
    [[self nextResponder] tryToPerform:@selector(closeCurrentChat:) with:self];
  } else if (segment == kPromptActionSegmentInspector) {
    [[self nextResponder] tryToPerform:@selector(toggleInspector:) with:self];
  } else if (segment == kPromptActionSegmentSend) {
    [self sendButtonClicked:sender];
  }
}

- (void)performSend:(id)sender
{
  NSString *text;
  NSString *trimmed;

  (void)sender;
  if (![self canSendCurrentPrompt]) {
    return;
  }

  text = [textView_ string];
  trimmed = [text stringByTrimmingCharactersInSet:
    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
  if ([trimmed length] == 0U) {
    return;
  }

  if (delegate_ != nil) {
    [delegate_ promptSendViewController:self didSubmitPrompt:trimmed];
  }

  [textView_ setString:@""];
  [self updateActionControls];
  [self updateExpansion];
}

- (void)textDidChange:(NSNotification *)notification
{
  (void)notification;
  [self updateActionControls];
  [self updateExpansion];
}

- (BOOL)textView:(NSTextView *)textView doCommandBySelector:(SEL)commandSelector
{
  NSEvent *event;

  (void)textView;
  if (commandSelector != @selector(insertNewline:)) {
    return NO;
  }

  event = [NSApp currentEvent];
  if (([event modifierFlags] & XPEventModifierFlagCommand) == 0) {
    return NO;
  }

  if (sending_ || cancellationRequested_) {
    return YES;
  }

  [self performSend:textView_];
  return YES;
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [scrollView_ release];
  [textView_ release];
  [sidebarSegmented_ release];
  [actionSegmented_ release];
  [super dealloc];
}

@end
