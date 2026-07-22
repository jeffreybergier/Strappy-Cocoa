#import "StrappyPreferencesDatabaseStudyView.h"

#import "StrappyBottomToolbarView.h"
#import "XPAppKit.h"

static const CGFloat kStrappyStudyInset = 12.0;
static const CGFloat kStrappyStudyToolbarHeight = 42.0;
static const CGFloat kStrappyStudyButtonWidth = 92.0;

@implementation StrappyPreferencesDatabaseStudyView

- (id)initWithFrame:(NSRect)frame
{
  return [self initWithFrame:frame target:nil];
}

- (id)initWithFrame:(NSRect)frame target:(id)target
{
  if ((self = [super initWithFrame:frame])) {
    NSRect bounds;
    NSScrollView *scrollView;
    StrappyBottomToolbarView *toolbar;

    [self setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    bounds = [self bounds];
    toolbar = [[[StrappyBottomToolbarView alloc]
      initWithFrame:NSMakeRect(0.0,
                               0.0,
                               NSWidth(bounds),
                               kStrappyStudyToolbarHeight)] autorelease];
    [toolbar setAutoresizingMask:NSViewWidthSizable | NSViewMaxYMargin];
    [self addSubview:toolbar];

    resetButton_ = [[NSButton alloc]
      initWithFrame:NSMakeRect(kStrappyStudyInset,
                               9.0,
                               kStrappyStudyButtonWidth,
                               24.0)];
    [resetButton_ setTitle:NSLocalizedString(@"Reset", nil)];
    [resetButton_ setBezelStyle:XPBezelStyleRounded];
    [resetButton_ setButtonType:XPButtonTypeMomentaryLight];
    [resetButton_ setTarget:target];
    [resetButton_ setAction:@selector(resetDatabaseStudy:)];
    [toolbar addSubview:resetButton_];

    studyButton_ = [[NSButton alloc]
      initWithFrame:NSMakeRect(NSWidth(bounds) - kStrappyStudyInset -
                               kStrappyStudyButtonWidth,
                               9.0,
                               kStrappyStudyButtonWidth,
                               24.0)];
    [studyButton_ setAutoresizingMask:NSViewMinXMargin];
    [studyButton_ setTitle:NSLocalizedString(@"Study", nil)];
    [studyButton_ setBezelStyle:XPBezelStyleRounded];
    [studyButton_ setButtonType:XPButtonTypeMomentaryLight];
    [studyButton_ setKeyEquivalent:@"\r"];
    [studyButton_ setTarget:target];
    [studyButton_ setAction:@selector(beginDatabaseStudy:)];
    [toolbar addSubview:studyButton_];

    scrollView = [[[NSScrollView alloc]
      initWithFrame:NSMakeRect(kStrappyStudyInset,
                               kStrappyStudyToolbarHeight + kStrappyStudyInset,
                               NSWidth(bounds) - (2.0 * kStrappyStudyInset),
                               NSHeight(bounds) - kStrappyStudyToolbarHeight -
                                 (2.0 * kStrappyStudyInset))] autorelease];
    [scrollView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [scrollView setBorderType:NSBezelBorder];
    [scrollView setHasVerticalScroller:YES];
    [scrollView setHasHorizontalScroller:NO];
    [scrollView setAutohidesScrollers:YES];

    textView_ = [[NSTextView alloc]
      initWithFrame:[[scrollView contentView] bounds]];
    [textView_ setMinSize:NSMakeSize(0.0, 0.0)];
    [textView_ setMaxSize:NSMakeSize(100000.0, 100000.0)];
    [textView_ setVerticallyResizable:YES];
    [textView_ setHorizontallyResizable:NO];
    [textView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [[textView_ textContainer] setWidthTracksTextView:YES];
    [textView_ setEditable:NO];
    [textView_ setSelectable:YES];
    [textView_ setRichText:NO];
    [textView_ setFont:[NSFont userFixedPitchFontOfSize:12.0]];
    [textView_ setString:@""];
    [scrollView setDocumentView:textView_];
    [self addSubview:scrollView];
  }
  return self;
}

- (NSTextView *)textView
{
  return textView_;
}

- (void)dealloc
{
  [textView_ release];
  [resetButton_ release];
  [studyButton_ release];
  [super dealloc];
}

@end
