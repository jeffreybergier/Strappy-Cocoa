#import <UIKit/UIKit.h>

@interface AppDelegate : UIResponder <UIApplicationDelegate> {
  UIBackgroundTaskIdentifier promptBackgroundTask_;
  NSMutableSet *activePromptIdentifiers_;
  BOOL promptBackgroundTaskExpired_;
}

@property (nonatomic, strong) UIWindow *window;

@end
