#import "StrappyActivityAccessoryView.h"

#import "AIFontAwesome.h"

static const CGFloat kStrappyActivityAccessoryCanvasSize = 14.0f;
static const CGFloat kStrappyActivityAccessoryIconSize = 10.0f;

UIView *StrappyActivityAccessoryView(UIColor *color)
{
  return StrappyActivityAccessoryViewWithMetrics(
    color,
    kStrappyActivityAccessoryCanvasSize,
    kStrappyActivityAccessoryIconSize);
}

UIView *StrappyActivityAccessoryViewWithMetrics(UIColor *color,
                                                CGFloat canvasSize,
                                                CGFloat iconSize)
{
  UIImage *image;
  UIImageView *imageView;

  if (color == nil) {
    color = [UIColor grayColor];
  }
  if (canvasSize <= 0.0f) {
    canvasSize = kStrappyActivityAccessoryCanvasSize;
  }
  if (iconSize <= 0.0f) {
    iconSize = kStrappyActivityAccessoryIconSize;
  }

  image = [AIFontAwesome imageForIcon:AIFAArrowsRotate
                                style:AIFontAwesomeStyleSolid
                             iconSize:iconSize
                           canvasSize:canvasSize
                                color:color
                                scale:0.0f];
  if (image == nil) {
    return nil;
  }

  imageView = [[UIImageView alloc] initWithImage:image];
  [imageView setFrame:CGRectMake(0.0f, 0.0f, canvasSize, canvasSize)];
  [imageView setContentMode:UIViewContentModeCenter];
  return imageView;
}
