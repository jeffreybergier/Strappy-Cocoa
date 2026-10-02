/* Offline UIWebView regression fixture; run as mobile on a test phone. */
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#include <stdio.h>
@interface Probe : NSObject <UIWebViewDelegate> {
@public BOOL loaded; int loads; int ready;
}
@end
@implementation Probe
- (BOOL)webView:(UIWebView *)view shouldStartLoadWithRequest:(NSURLRequest *)request navigationType:(UIWebViewNavigationType)type {
  (void)view; (void)type;
  if ([[[request URL] scheme] isEqualToString:@"strappy-action"]) { ready++; return NO; }
  return YES;
}
- (void)webViewDidFinishLoad:(UIWebView *)view { (void)view; loaded=YES; loads++; }
@end
int main(int argc, char **argv) {
  NSAutoreleasePool *pool=[[NSAutoreleasePool alloc] init];
  if(argc!=3) return 2;
  Probe *probe=[[Probe alloc] init];
  UIWebView *view=[[UIWebView alloc] initWithFrame:CGRectMake(0,0,320,480)];
  [view setDelegate:probe];
  [view loadRequest:[NSURLRequest requestWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]]]];
  NSDate *end=[NSDate dateWithTimeIntervalSinceNow:15];
  while(!probe->loaded && [end timeIntervalSinceNow]>0) [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
  [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.5]];
  NSString *script=[NSString stringWithContentsOfFile:[NSString stringWithUTF8String:argv[2]] encoding:NSUTF8StringEncoding error:NULL];
  NSString *result=[view stringByEvaluatingJavaScriptFromString:script];
  printf("loads=%d ready=%d result=%s\n",probe->loads,probe->ready,[result UTF8String]);
  int ok=probe->loads==1 && probe->ready==1 && [result isEqualToString:@"PASS"];
  [view setDelegate:nil]; [view release]; [probe release]; [pool drain]; return ok?0:1;
}
