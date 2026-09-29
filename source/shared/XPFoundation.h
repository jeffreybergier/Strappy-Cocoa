#import <Foundation/Foundation.h>

/* Integer Compatibility — Tiger 10.4 SDK predates NSInteger/NSUInteger. */
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED < 1050
  #define XPInteger  int
  #define XPUInteger unsigned int
#else
  #define XPInteger  NSInteger
  #define XPUInteger NSUInteger
#endif

typedef enum XPPlatformFamily {
  XPPlatformFamilyGeneric = 0,
  XPPlatformFamilyIOS = 1,
  XPPlatformFamilyMacOS = 2
} XPPlatformFamily;

/* Preserve the original unit values on every SDK. In particular, the newer
 * week-of-year bit is unavailable on iOS 4 and Tiger. Only the SDK names are
 * deprecated; suppress their declaration warnings inside this boundary. */
#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
enum {
  XPCalendarUnitEra = NSEraCalendarUnit,
  XPCalendarUnitDay = NSDayCalendarUnit,
  XPCalendarUnitWeek = NSWeekCalendarUnit,
  XPCalendarUnitWeekday = NSWeekdayCalendarUnit,
  XPCalendarUnitMonth = NSMonthCalendarUnit,
  XPCalendarUnitYear = NSYearCalendarUnit
};
#if defined(__clang__)
  #pragma clang diagnostic pop
#endif

@interface NSCalendar (XPFoundation)
- (NSDate *)XP_startOfUnit:(NSCalendarUnit)unit forDate:(NSDate *)date;
@end

@interface NSProcessInfo (XPFoundation)

/* Keep build-target platform checks inside the compatibility layer. */
- (XPPlatformFamily)XP_platformFamily;

@end

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

@interface NSPropertyListSerialization (XPFoundation)

/* The NSError-based property-list APIs arrived after Tiger. These helpers
 * dispatch to them where present and retain the legacy string-error selectors
 * as a runtime fallback for macOS 10.4 and 10.5. */
+ (NSData *)XP_dataWithPropertyList:(id)propertyList
                              format:(NSPropertyListFormat)format;
+ (id)XP_propertyListWithData:(NSData *)data
                       options:(XPUInteger)options
                        format:(NSPropertyListFormat *)format;

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
