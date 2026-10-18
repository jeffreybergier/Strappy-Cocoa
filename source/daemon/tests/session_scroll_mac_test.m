#import <AppKit/AppKit.h>
#import "../../macOS/SessionListViewController.h"
#import "StrappySession.h"
#include <assert.h>
extern StrappySessionRows *StrappyCheckSessionRows(NSString *, NSString **);
@interface SessionListViewController (ScrollProbe)
- (void)refreshTableCollection;
- (void)sidebarLoaded:(NSDictionary *)result;
- (void)sessionPromptActivityDidChange:(NSNotification *)notification;
@end
/* Exercise the modern selector bridge even when running on Tiger. */
@interface ScrollReloadProbe : NSTableView {
 @public NSUInteger reloadCount; NSUInteger updated;
}
@end
@implementation ScrollReloadProbe
- (NSInteger)numberOfRows; { return 5; }
- (NSRect)visibleRect; { return NSMakeRect(0,0,100,60); }
- (NSRect)rectOfRow:(NSInteger)row;
{ return NSMakeRect(0,row*20,100,20); }
- (void)reloadDataForRowIndexes:(NSIndexSet *)rows
                columnIndexes:(NSIndexSet *)columns;
{
  assert([rows count] == 1 && [columns count] == 1);
  ++reloadCount; updated = [rows firstIndex];
}
@end
@interface ScrollList : SessionListViewController
@end
@implementation ScrollList
- (void)reloadData {} /* No production store or daemon. */
@end
int main(void)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  ScrollReloadProbe *probe = [[[ScrollReloadProbe alloc]
    initWithFrame:NSMakeRect(0,0,100,60)] autorelease];
  [probe addTableColumn:[[[NSTableColumn alloc] initWithIdentifier:@"fixture"] autorelease]];
  [probe XP_reloadVisibleRow:2];
  assert(probe->reloadCount == 1 && probe->updated == 2);
  [probe XP_reloadVisibleRow:4];
  assert(probe->reloadCount == 1);
  NSString *report = nil;
  StrappySessionRows *sessions = StrappyCheckSessionRows([[NSBundle mainBundle]
    pathForResource:@"sidebar-fixture" ofType:@"sqlite"], &report);
  ScrollList *owner = [[ScrollList alloc] init];
  NSView *view = [owner view];
  NSArray *sections = [StrappySessionSections sectionsForSessions:sessions];
  StrappySessionTableRows *rows = [[[StrappySessionTableRows alloc]
    initWithSessions:sessions sections:sections] autorelease];
  [owner setValue:sessions forKey:@"sessionRows_"];
  [owner setValue:rows forKey:@"rows_"];
  [[owner valueForKey:@"tableView_"] reloadData];
  NSTableView *table = [owner valueForKey:@"tableView_"];
  NSImage *canvas = [[[NSImage alloc] initWithSize:NSMakeSize(400, 220)] autorelease];
  NSCell *cell = [[[NSClassFromString(@"StrappySessionCell") alloc] init] autorelease];
  NSMutableDictionary *row = [NSMutableDictionary dictionaryWithDictionary:[sessions objectAtIndex:0]];
  [canvas lockFocus];
  [[NSColor whiteColor] set]; NSRectFill(NSMakeRect(0,0,400,220));
  NSUInteger i;
  for (i=0; i<3; ++i) {
    [row setObject:[NSNumber numberWithBool:i==1] forKey:@"prompt_in_flight"];
    [row setObject:[NSNumber numberWithBool:i>0] forKey:@"has_new_answers"];
    [cell setObjectValue:row];
    [cell drawInteriorWithFrame:NSMakeRect(0, i*70, 400, 68) inView:view];
  }
  [canvas unlockFocus];
  [[canvas TIFFRepresentation] writeToFile:@"session-status.tiff" atomically:YES];
  NSUInteger jumps[] = {1, 20000, 1025, 65, 513, 1};
  for (i=0; i<6; ++i) {
    [table scrollRowToVisible:(NSInteger)jumps[i]];
    NSDictionary *value = [owner tableView:table objectValueForTableColumn:nil row:(NSInteger)jumps[i]];
    assert([value objectForKey:@"id"]);
    NSPoint before = [[table superview] bounds].origin;
    [owner sessionPromptActivityDidChange:[NSNotification
      notificationWithName:StrappySessionListActivityDidChangeNotification object:nil
      userInfo:[NSDictionary dictionaryWithObject:[NSNumber numberWithBool:YES]
        forKey:[value objectForKey:@"id"]]]];
    assert(NSEqualPoints(before, [[table superview] bounds].origin));
    value = [owner tableView:table objectValueForTableColumn:nil row:(NSInteger)jumps[i]];
    assert([[value objectForKey:@"prompt_in_flight"] boolValue]);
    [canvas lockFocus];
    [cell setObjectValue:value];
    [cell drawInteriorWithFrame:NSMakeRect(0,0,400,68) inView:view];
    [canvas unlockFocus];
  }
  StrappySessionRows *replacement = [[[StrappySessionRows alloc]
    initWithDatabasePath:[[NSBundle mainBundle] pathForResource:
      @"sidebar-fixture" ofType:@"sqlite"] error:NULL] autorelease];
  [owner sidebarLoaded:[NSDictionary dictionaryWithObjectsAndKeys:
    replacement, @"sessions", [StrappySessionSections sectionsForSessions:replacement],
    @"sections", nil]];
  assert([[[replacement objectAtIndex:0] objectForKey:@"prompt_in_flight"] boolValue]);
  NSLog(@"%@; AppKit actual cell drawing, scroll jumps and scoped updates PASS", report);
  [owner release]; [pool drain]; return 0;
}
