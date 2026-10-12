#import <Foundation/Foundation.h>
#import <AIPagedRows.h>

extern NSString * const StrappyModelListReadFailedNotification;

/* StrappySession supplies the SQLite bridge; views and the bounded array do
 * not depend on C storage headers. Use from one owner thread at a time. A query worker may build and warm a
 * private snapshot, then transfer exclusive ownership to the UI. */
@protocol StrappyModelListSource <NSObject>
- (NSUInteger)count;
- (BOOL)hasConfiguredAccounts;
- (NSUInteger)totalCount;
- (NSUInteger)allowedCount;
- (BOOL)filterWithSearch:(NSString *)search sortDescriptors:(NSArray *)descriptors error:(NSError **)error;
- (NSArray *)pageAtOffset:(NSUInteger)offset error:(NSError **)error;
- (NSArray *)providerSectionsWithError:(NSError **)error;
- (NSUInteger)indexForModelIdentifier:(NSString *)identifier error:(NSError **)error;
@end

@interface StrappyModelRows : AIPagedRows {
 @private
  id<StrappyModelListSource> source_;
  NSError *readError_;
}
- (id)initWithModelSource:(id<StrappyModelListSource>)source;
- (BOOL)hasConfiguredAccounts;
- (NSUInteger)totalCount;
- (NSUInteger)allowedCount;
- (BOOL)filterWithSearch:(NSString *)search sortDescriptors:(NSArray *)descriptors error:(NSError **)error;
- (NSArray *)providerSectionsWithError:(NSError **)error;
- (NSUInteger)indexForModelIdentifier:(NSString *)identifier;
- (NSError *)readError;
/* Same concatenation/lowercasing as the original model preference searches. */
+ (NSString *)searchTextForValues:(NSArray *)values;
@end
