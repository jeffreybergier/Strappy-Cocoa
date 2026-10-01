#import <Foundation/Foundation.h>

/* Per-user heartbeat installation, independent of the UI and app bundle. */
@interface StrappyServiceController : NSObject
+ (BOOL)installWithError:(NSString **)error;
+ (BOOL)uninstallWithError:(NSString **)error;
@end
