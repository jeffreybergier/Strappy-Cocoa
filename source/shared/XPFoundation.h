#import <Foundation/Foundation.h>

/* Integer Compatibility — Tiger 10.4 SDK predates NSInteger/NSUInteger. */
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED < 1050
  #define XPInteger  int
  #define XPUInteger unsigned int
#else
  #define XPInteger  NSInteger
  #define XPUInteger NSUInteger
#endif

@interface NSNumber (XPFoundation)

/* NSNumber's NSInteger/NSUInteger convenience selectors are newer than the
 * oldest runtime Strappy supports. These category methods keep call sites
 * semantic while routing through long/unsignedLong selectors available there. */
+ (NSNumber *)XP_numberWithInteger:(XPInteger)value;
+ (NSNumber *)XP_numberWithUnsignedInteger:(XPUInteger)value;
- (XPInteger)XP_integerValue;
- (XPUInteger)XP_unsignedIntegerValue;

@end

@interface XPKeychain : NSObject

+ (BOOL)findInternetPasswordForAccount:(NSString *)account
                                outURL:(NSString **)outURL
                           outPassword:(NSString **)outPassword;
+ (BOOL)deleteInternetPasswordForAccount:(NSString *)account;
+ (BOOL)setInternetPasswordForAccount:(NSString *)account
                                  URL:(NSString *)url
                             password:(NSString *)password;

@end
