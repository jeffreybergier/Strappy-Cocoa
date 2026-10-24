#import <Foundation/Foundation.h>

/* Mac app-owned service lifecycle, with a stable per-user executable path.
 * Calls are serialized and may block for service teardown. Returned errors
 * are autoreleased. No user credentials or database files are removed. */
@interface StrappyServiceController : NSObject
/* Background startup; migrates the old login agent to an app-loaded job. */
+ (BOOL)prepareForApplicationWithError:(NSError **)error;
/* Call on application termination. Waits for startup and service shutdown;
 * rejects later startup work for this process and retains installed files. */
+ (BOOL)stopForApplicationWithError:(NSString **)error;
/* Explicit maintenance: stops the service and removes its installed files. */
+ (BOOL)uninstallWithError:(NSString **)error;
@end
