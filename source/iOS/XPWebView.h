#import <UIKit/UIKit.h>

@class XPWebView;

@protocol XPWebViewDelegate <NSObject>
@optional
- (BOOL)xpWebView:(XPWebView *)webView
  shouldStartLoadWithRequest:(NSURLRequest *)request;
- (void)xpWebViewDidFinishLoad:(XPWebView *)webView;
- (void)xpWebView:(XPWebView *)webView
  didFailLoadWithError:(NSError *)error;
@end

/* Capability-selected WebView bridge. WKWebView is used only when the
 * runtime exposes every API Strappy needs for scoped local-file loading;
 * otherwise the bridge falls back to UIWebView. */
@interface XPWebView : UIView

@property (nonatomic, assign) id<XPWebViewDelegate> delegate;
@property (nonatomic, readonly) UIScrollView *scrollView;
@property (nonatomic, readonly, getter=isUsingWKWebView) BOOL usingWKWebView;

- (id)initWithFrame:(CGRect)frame readAccessURL:(NSURL *)readAccessURL;
- (void)loadFileURL:(NSURL *)fileURL;
- (void)evaluateJavaScript:(NSString *)javaScript;
- (void)stopLoading;
- (void)setBackgroundTransparent;
- (void)setVisibleFrame:(CGRect)frame;

@end
