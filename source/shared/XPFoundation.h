#import <Foundation/Foundation.h>

/* Integer Compatibility — Tiger 10.4 SDK predates NSInteger/NSUInteger. */
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED < 1050
  #define XPInteger  int
  #define XPUInteger unsigned int
#else
  #define XPInteger  NSInteger
  #define XPUInteger NSUInteger
#endif

@interface NSThread (XPFoundation)

/* +isMainThread and +mainThread were added after Tiger. Prefer the modern
 * Foundation query where available and fall back to Darwin's Tiger-safe
 * pthread_main_np(). */
+ (BOOL)XP_isMainThread;

@end

@interface NSFileManager (XPFoundation)

- (BOOL)XP_createDirectoryAtPath:(NSString *)path
     withIntermediateDirectories:(BOOL)createIntermediates
                      attributes:(NSDictionary *)attributes
                           error:(NSError **)error;

@end

@interface NSNumber (XPFoundation)

/* NSNumber's NSInteger/NSUInteger convenience selectors are newer than the
 * oldest runtime Strappy supports. These category methods keep call sites
 * semantic while routing through long/unsignedLong selectors available there. */
+ (NSNumber *)XP_numberWithInteger:(XPInteger)value;
+ (NSNumber *)XP_numberWithUnsignedInteger:(XPUInteger)value;
- (XPInteger)XP_integerValue;
- (XPUInteger)XP_unsignedIntegerValue;

@end

@interface NSString (XPFoundation)

/* NSString gained -longLongValue after Tiger. Prefer it where present and
 * retain full-width parsing through strtoll on the 10.4 runtime. */
- (long long)XP_longLongValue;

@end
