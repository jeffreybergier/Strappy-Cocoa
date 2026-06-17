#import "PromptSendViewController.h"

static const CGFloat kPromptSendHeightCollapsed = 32.0;
static const CGFloat kPromptSendHeightExpanded = 108.0;
static const CGFloat kPromptSendPad = 4.0;

static NSColor *StrappyInputBezelBackgroundColor(void) { return [NSColor controlBackgroundColor]; }
static NSColor *StrappyInputBezelBorderColor(void) { return [NSColor gridColor]; }
static NSColor *StrappyInputBezelHighlightColor(void) { return XPColorControlHighlight; }

@interface StrappyPromptToolbarView : NSView
@end

@implementation StrappyPromptToolbarView

- (BOOL)isOpaque
{
  return YES;
}

- (void)drawRect:(NSRect)dirtyRect
{
  NSRect bounds;

  (void)dirtyRect;
  bounds = [self bounds];

  [XPColorWindowFrame set];
  NSRectFill(bounds);

  [XPColorControlHighlight set];
  NSRectFill(NSMakeRect(bounds.origin.x,
                        bounds.origin.y + bounds.size.height - 1.0,
                        bounds.size.width,
                        1.0));
}

@end

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
- (void)updateSendSegment;
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
  StrappyPromptToolbarView *bar;

  bar = [[StrappyPromptToolbarView alloc]
      initWithFrame:NSMakeRect(0.0, 0.0, 400.0, kPromptSendHeightCollapsed)];
  barView_ = bar;
  [barView_ setAutoresizingMask:NSViewWidthSizable | NSViewMaxYMargin];
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

  scrollView_ = [[NSScrollView alloc] initWithFrame:NSZeroRect];
  [scrollView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [scrollView_ setBorderType:NSNoBorder];
  [scrollView_ setHasVerticalScroller:YES];
  [scrollView_ setHasHorizontalScroller:NO];
  [scrollView_ setAutohidesScrollers:YES];
  [bezelView_ addSubview:scrollView_];

  textView_ = [[NSTextView alloc] initWithFrame:NSZeroRect];
  [textView_ setMinSize:NSMakeSize(0.0, 0.0)];
  [textView_ setMaxSize:NSMakeSize(100000.0, 100000.0)];
  [textView_ setVerticallyResizable:YES];
  [textView_ setHorizontallyResizable:NO];
  [textView_ setAutoresizingMask:NSViewWidthSizable];
  [[textView_ textContainer] setWidthTracksTextView:YES];
  [textView_ setFont:[NSFont systemFontOfSize:13.0]];
  [textView_ setDrawsBackground:YES];
  [textView_ setBackgroundColor:[NSColor controlBackgroundColor]];
  [textView_ setTextContainerInset:NSMakeSize(2.0, 2.0)];
  [textView_ setDelegate:self];
  [scrollView_ setDocumentView:textView_];

  sendSegmented_ = [[NSSegmentedControl alloc] initWithFrame:NSZeroRect];
  [sendSegmented_ setSegmentCount:1];
  [[sendSegmented_ cell] setTrackingMode:NSSegmentSwitchTrackingMomentary];
  if ([[sendSegmented_ cell] respondsToSelector:@selector(setSegmentStyle:)]) {
    [[sendSegmented_ cell] setSegmentStyle:NSSegmentStyleTexturedRounded];
  }
  [sendSegmented_ setLabel:NSLocalizedString(@"Send", nil) forSegment:0];
  [sendSegmented_ setTarget:self];
  [sendSegmented_ setAction:@selector(performSend:)];
  [sendSegmented_ setAutoresizingMask:NSViewMinXMargin | NSViewMaxYMargin];
  [sendSegmented_ sizeToFit];
  [barView_ addSubview:sendSegmented_];

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
  segmentWidth = [sendSegmented_ frame].size.width;
  segmentHeight = [sendSegmented_ frame].size.height;
  inputWidth = bounds.size.width - segmentWidth - (kPromptSendPad * 3.0);
  if (inputWidth < 0.0) {
    inputWidth = 0.0;
  }

  [sendSegmented_ setFrame:NSMakeRect(NSMaxX(bounds) - kPromptSendPad - segmentWidth,
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

- (void)updateSendSegment
{
  [sendSegmented_ setEnabled:[self canSendCurrentPrompt] forSegment:0];
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
  if (sendSegmented_ != nil) {
    [sendSegmented_ setEnabled:enabled_];
    [self updateSendSegment];
  }
}

- (BOOL)canSendCurrentPrompt
{
  NSString *text;
  NSString *trimmed;

  if (!enabled_ || (textView_ == nil)) {
    return NO;
  }

  text = [textView_ string];
  trimmed = [text stringByTrimmingCharactersInSet:
    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
  return ([trimmed length] > 0U) ? YES : NO;
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
  [self updateSendSegment];
  [self updateExpansion];
}

- (void)textDidChange:(NSNotification *)notification
{
  (void)notification;
  [self updateSendSegment];
  [self updateExpansion];
}

- (void)dealloc
{
  [scrollView_ release];
  [textView_ release];
  [sendSegmented_ release];
  [super dealloc];
}

@end
