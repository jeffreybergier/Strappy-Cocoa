#import <Foundation/Foundation.h>

@interface XPKeychain : NSObject

+ (BOOL)findInternetPasswordForAccount:(NSString *)account
                                outURL:(NSString **)outURL
                           outPassword:(NSString **)outPassword;
+ (BOOL)deleteInternetPasswordForAccount:(NSString *)account;
+ (BOOL)setInternetPasswordForAccount:(NSString *)account
                                  URL:(NSString *)url
                             password:(NSString *)password;

@end
