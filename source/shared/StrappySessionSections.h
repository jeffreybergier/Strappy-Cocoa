#import <Foundation/Foundation.h>

extern NSString * const StrappySessionDateBoundariesDidChangeNotification;

/* Counts and identities never materialize rows. NSNotFound indicates a read
 * error for counts, or an absent identity for index lookup. */
@protocol StrappySessionListSource <NSObject>
- (NSUInteger)count;
- (NSDictionary *)objectAtIndex:(NSUInteger)index;
- (NSUInteger)countSinceTimestamp:(long long)timestamp;
- (NSUInteger)indexForSessionIdentifier:(NSNumber *)identifier;
@end

/* Section dictionaries hold title, offset and a lazy sessions slice. */
@interface StrappySessionSections : NSObject
+ (NSArray *)sectionsForSessions:(id<StrappySessionListSource>)sessions;
+ (NSArray *)sectionsForSessions:(id<StrappySessionListSource>)sessions
                           date:(NSDate *)date
                       calendar:(NSCalendar *)calendar;
/* Called on the main thread by macOS. iOS uses UIKit time notifications. */
+ (void)startMidnightTimer;
@end

@interface StrappySessionTableRows : NSArray {
  id<StrappySessionListSource> source_;
  NSArray *sections_;
}
- (id)initWithSessions:(id<StrappySessionListSource>)sessions sections:(NSArray *)sections;
- (BOOL)isSectionAtIndex:(NSUInteger)index;
- (NSUInteger)indexForSessionIdentifier:(NSNumber *)identifier;
@end
