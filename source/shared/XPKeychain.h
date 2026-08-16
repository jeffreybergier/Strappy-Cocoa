#import <Foundation/Foundation.h>

@interface XPKeychain : NSObject

+ (BOOL)findInternetPasswordForAccount:(NSString *)account
                                outURL:(NSString **)outURL
                           outPassword:(NSString **)outPassword;
+ (BOOL)deleteInternetPasswordForAccount:(NSString *)account;
+ (BOOL)setInternetPasswordForAccount:(NSString *)account
                                  URL:(NSString *)url
                             password:(NSString *)password;

+ (BOOL)findGenericPasswordDataForService:(NSString *)service
                                  account:(NSString *)account
                                  outData:(NSData **)outData;
+ (BOOL)deleteGenericPasswordForService:(NSString *)service
                                account:(NSString *)account;
+ (BOOL)setGenericPasswordData:(NSData *)data
                       service:(NSString *)service
                       account:(NSString *)account;

@end
