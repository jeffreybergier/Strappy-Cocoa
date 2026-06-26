#import "PromptSendViewController.h"
#import "AIFontAwesome.h"
#import "StrappyBottomToolbarView.h"

static const CGFloat kPromptSendHeightCollapsed = 32.0;
static const CGFloat kPromptSendHeightExpanded = 108.0;
static const CGFloat kPromptSendPad = 4.0;

enum {
  kPromptActionStop = 0,
  kPromptActionStream = 1,
  kPromptActionSend = 2
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
- (void)updateExpansion;
- (void)updateActionSegments;
- (void)rebuildActionSegmentIcons;
- (void)barDidMoveToWindow:(id)sender;
- (void)barViewFrameDidChange:(NSNotification *)notification;
- (void)actionSegmentClicked:(id)sender;
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
  [textView_ setFont:XPFontTextStyleBody];
  [textView_ setDrawsBackground:YES];
  [textView_ setBackgroundColor:[NSColor controlBackgroundColor]];
  [textView_ setTextContainerInset:NSMakeSize(2.0, 2.0)];
  [textView_ setDelegate:self];
  [scrollView_ setDocumentView:textView_];

  actionsSegmented_ = [[NSSegmentedControl alloc] initWithFrame:NSZeroRect];
  [actionsSegmented_ setSegmentCount:3];
  [[actionsSegmented_ cell] setTrackingMode:NSSegmentSwitchTrackingSelectAny];
  if ([[actionsSegmented_ cell] respondsToSelector:@selector(setSegmentStyle:)]) {
    [[actionsSegmented_ cell] setSegmentStyle:NSSegmentStyleTexturedRounded];
  }
  [actionsSegmented_ setLabel:NSLocalizedString(@"Stream", nil)
                   forSegment:kPromptActionStream];
  [actionsSegmented_ setLabel:NSLocalizedString(@"Send", nil)
                   forSegment:kPromptActionSend];
  [actionsSegmented_ setTarget:self];
  [actionsSegmented_ setAction:@selector(actionSegmentClicked:)];
  [actionsSegmented_ setAutoresizingMask:NSViewMinXMargin | NSViewMaxYMargin];
  [self rebuildActionSegmentIcons];
  [barView_ addSubview:actionsSegmented_];

  [barView_ setPostsFrameChangedNotifications:YES];
  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(barViewFrameDidChange:)
             name:NSViewFrameDidChangeNotification
           object:barView_];

  [self setEnabled:enabled_];
}

- (void)viewDidLayout
{
  NSRect bounds;
  CGFloat segmentWidth;
  CGFloat segmentHeight;
  CGFloat inputWidth;

  [super viewDidLayout];
  bounds = [barView_ bounds];
  segmentWidth = [actionsSegmented_ frame].size.width;
  segmentHeight = [actionsSegmented_ frame].size.height;
  inputWidth = bounds.size.width - segmentWidth - (kPromptSendPad * 3.0);
  if (inputWidth < 0.0) {
    inputWidth = 0.0;
  }

  [actionsSegmented_ setFrame:NSMakeRect(NSMaxX(bounds) - kPromptSendPad - segmentWidth,
                                         kPromptSendPad,
                                         segmentWidth,
                                         segmentHeight)];
  [bezelView_ setFrame:NSMakeRect(kPromptSendPad,
                                  kPromptSendPad,
                                  inputWidth,
                                  bounds.size.height - (kPromptSendPad * 2.0))];
  [scrollView_ setFrame:[bezelView_ bounds]];
}

- (CGFloat)preferredHeight
{
  return expanded_ ? kPromptSendHeightExpanded : kPromptSendHeightCollapsed;
}

- (void)barDidMoveToWindow:(id)sender
{
  (void)sender;
  [self rebuildActionSegmentIcons];
}

- (void)barViewFrameDidChange:(NSNotification *)notification
{
  (void)notification;
  [self updateExpansion];
}

- (void)rebuildActionSegmentIcons
{
  CGFloat scale;
  NSImage *stopImage;
  NSImage *streamImage;
  NSImage *sendImage;

  if (actionsSegmented_ == nil) {
    return;
  }

  scale = 1.0;
  if ([[barView_ window] respondsToSelector:@selector(XP_backingScaleFactor)]) {
    scale = [[barView_ window] XP_backingScaleFactor];
  }
  if (scale < 1.0) {
    scale = 1.0;
  }

  stopImage = [AIFontAwesome imageForIcon:AIFACircleStop
                                    style:AIFontAwesomeStyleRegular
                                 iconSize:14.0
                               canvasSize:20.0
                                    scale:scale];
  streamImage = [AIFontAwesome imageForIcon:AIFATowerBroadcast
                                      style:AIFontAwesomeStyleSolid
                                   iconSize:14.0
                                 canvasSize:20.0
                                      scale:scale];
  sendImage = [AIFontAwesome imageForIcon:AIFAPaperPlane
                                    style:AIFontAwesomeStyleRegular
                                 iconSize:14.0
                               canvasSize:20.0
                                    scale:scale];

  if (stopImage != nil) {
    [actionsSegmented_ setImage:stopImage forSegment:kPromptActionStop];
    [actionsSegmented_ setLabel:@"" forSegment:kPromptActionStop];
  } else {
    [actionsSegmented_ setLabel:NSLocalizedString(@"Stop", nil)
                     forSegment:kPromptActionStop];
  }

  if (streamImage != nil) {
    [actionsSegmented_ setImage:streamImage forSegment:kPromptActionStream];
    [actionsSegmented_ setLabel:@"" forSegment:kPromptActionStream];
  } else {
    [actionsSegmented_ setLabel:NSLocalizedString(@"Stream", nil)
                     forSegment:kPromptActionStream];
  }

  if (sendImage != nil) {
    [actionsSegmented_ setImage:sendImage forSegment:kPromptActionSend];
  }
  [actionsSegmented_ XP_setToolTip:NSLocalizedString(@"Stop Prompt Request", nil)
                         forSegment:kPromptActionStop];
  [actionsSegmented_ XP_setToolTip:NSLocalizedString(@"Stream Responses", nil)
                         forSegment:kPromptActionStream];
  [actionsSegmented_ XP_setToolTip:NSLocalizedString(@"Send Prompt", nil)
                         forSegment:kPromptActionSend];
  [actionsSegmented_ sizeToFit];
}

- (void)updateActionSegments
{
  [actionsSegmented_ setEnabled:(enabled_ && sending_ && !cancellationRequested_)
                     forSegment:kPromptActionStop];
  [actionsSegmented_ setEnabled:(enabled_ && !sending_)
                     forSegment:kPromptActionStream];
  [actionsSegmented_ setEnabled:[self canSendCurrentPrompt]
                     forSegment:kPromptActionSend];
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
  if (actionsSegmented_ != nil) {
    [actionsSegmented_ setEnabled:enabled_];
    [self updateActionSegments];
  }
}

- (void)setSending:(BOOL)sending
{
  sending_ = sending ? YES : NO;
  if (!sending_) {
    cancellationRequested_ = NO;
  }
  [self updateActionSegments];
}

- (void)setCancellationRequested:(BOOL)requested
{
  cancellationRequested_ = requested ? YES : NO;
  [self updateActionSegments];
}

- (void)setStreamingEnabled:(BOOL)enabled
{
  if (actionsSegmented_ == nil) {
    return;
  }
  [actionsSegmented_ setSelected:(enabled ? YES : NO)
                      forSegment:kPromptActionStream];
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

- (void)actionSegmentClicked:(id)sender
{
  NSInteger selectedSegment;

  selectedSegment = [(NSSegmentedControl *)sender selectedSegment];
  if (selectedSegment == kPromptActionStop) {
    [actionsSegmented_ setSelected:NO forSegment:kPromptActionStop];
    if (sending_ && !cancellationRequested_) {
      [self setCancellationRequested:YES];
      if (delegate_ != nil) {
        [delegate_ promptSendViewControllerDidCancelPrompt:self];
      }
    }
  } else if (selectedSegment == kPromptActionStream) {
    BOOL enabled;

    enabled = [actionsSegmented_ isSelectedForSegment:kPromptActionStream] ?
      YES : NO;
    if ((delegate_ == nil) ||
        ![delegate_ promptSendViewController:self
                         setStreamingEnabled:enabled]) {
      [actionsSegmented_ setSelected:(!enabled ? YES : NO)
                          forSegment:kPromptActionStream];
    }
  } else if (selectedSegment == kPromptActionSend) {
    [actionsSegmented_ setSelected:NO forSegment:kPromptActionSend];
    [self performSend:sender];
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
  [self updateActionSegments];
  [self updateExpansion];
}

- (void)textDidChange:(NSNotification *)notification
{
  (void)notification;
  [self updateActionSegments];
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

  [self performSend:textView_];
  return YES;
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [scrollView_ release];
  [textView_ release];
  [actionsSegmented_ release];
  [super dealloc];
}

@end
