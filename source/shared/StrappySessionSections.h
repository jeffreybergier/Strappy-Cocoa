#import <Foundation/Foundation.h>

extern NSString * const StrappySessionDateBoundariesDidChangeNotification;

/* Presentation-only groups of DB-backed summaries, preserving input order.
 * Each dictionary contains a title and a sessions array. Empty groups vanish. */
@interface StrappySessionSections : NSObject
+ (NSArray *)sectionsForSessions:(NSArray *)sessions;
+ (NSArray *)sectionsForSessions:(NSArray *)sessions
                           date:(NSDate *)date
                       calendar:(NSCalendar *)calendar;
/* Called on the main thread by macOS. iOS uses UIKit time notifications. */
+ (void)startMidnightTimer;
@end
