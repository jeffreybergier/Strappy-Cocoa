/* Native macOS Foundation fixture for the actual provider implementation.
 * Fake private classes/functions verify policy, not Apple's implementation.
 * Only temporary fixture sockets; no SpringBoard, credentials or device access. */
#import <Foundation/Foundation.h>
#include <assert.h>
#include <stdio.h>
static char fixtureDirectory[] = "/tmp/strappy-provider-XXXXXX";
static char fixtureSocket[104], fixtureLock[110];
#define XP_PROVIDER_DIRECTORY fixtureDirectory
#define XP_PROVIDER_SOCKET fixtureSocket
#define XP_PROVIDER_LOCK fixtureLock
#include "../../iOS/notifications/provider/XPStrappyNotificationProvider.m"

@interface BBBulletinRequest : NSObject {
  NSMutableDictionary *values_;
}
@end
@implementation BBBulletinRequest
- (id)init;
{
  self = [super init];
  if (self) values_ = [[NSMutableDictionary alloc] init];
  return self;
}
#define FIELD(setter, getter) \
- (void)setter:(id)value { [values_ setObject:value forKey:@#getter]; } \
- (id)getter { return [values_ objectForKey:@#getter]; }
FIELD(setSectionID, sectionID)
FIELD(setBulletinID, bulletinID)
FIELD(setPublisherBulletinID, publisherBulletinID)
FIELD(setRecordID, recordID)
FIELD(setTitle, title)
FIELD(setMessage, message)
FIELD(setDate, date)
FIELD(setDefaultAction, defaultAction)
FIELD(setSound, sound)
- (void)setClearable:(BOOL)value;
{
  assert(value == YES);
}
- (void)dealloc;
{
  [values_ release]; [super dealloc];
}
@end
@interface BBAction : NSObject
@end
@implementation BBAction
+ (id)actionWithLaunchBundleID:(id)identity callblock:(id)block;
{
  (void)block;
  return identity;
}
@end

static unsigned int added, withdrawn;
static BOOL throwOnAdd;
static void Add(id provider, id bulletin)
{
  (void)provider;
  assert([[bulletin sectionID] isEqual:@"com.altivecintelligence.Strappy"]);
  assert([[bulletin defaultAction] isEqual:[bulletin sectionID]]);
  assert([[bulletin sound] isEqual:@"fixture-sound"]);
  ++added;
  if (throwOnAdd) [NSException raise:@"Fixture" format:@"publication failed"];
}
static void Withdraw(id provider, NSString *record)
{
  (void)provider;
  assert([record hasPrefix:@"com.altivecintelligence.Strappy.run."]);
  ++withdrawn;
}
@interface BBSound : NSObject
@end
@implementation BBSound
+ (id)alertSoundWithSystemSoundID:(SystemSoundID)soundID;
{
  assert(soundID == 1009);
  return @"fixture-sound";
}
@end
@interface FixtureProvider : XPStrappyNotificationProvider
@end
@implementation FixtureProvider
- (void)prepare;
{
  /* The fixture supplies the runtime dependencies normally resolved on iOS 6.
   * alloc already zeroed the inherited socket/lifecycle fields. */
  history_ = [[NSMutableArray alloc] init];
  recent_ = [[NSMutableArray alloc] init];
  add_ = Add; withdraw_ = Withdraw;
  lockFD_ = -1;
}
@end

/* Fake icon routing exercises the real test badge bridge. */
static NSString *testBadge;
static unsigned int badgeUpdates;
static BOOL modernBadgeAPI;
@interface SBIconController : NSObject
@end
@implementation SBIconController
+ (BOOL)respondsToSelector:(SEL)selector;
{
  if (selector==NSSelectorFromString(@"applicationIconForBundleIdentifier:")) return modernBadgeAPI;
  if (selector==NSSelectorFromString(@"applicationIconForDisplayIdentifier:")) return !modernBadgeAPI;
  return [super respondsToSelector:selector];
}
+ (id)applicationIconForBundleIdentifier:(NSString *)identity;
{
  assert(modernBadgeAPI);
  assert([identity isEqualToString:@"com.altivecintelligence.Strappy"]);
  return self;
}
+ (id)sharedInstance;
{
  return self;
}
+ (id)model;
{
  return self;
}
+ (id)applicationIconForDisplayIdentifier:(NSString *)identity;
{
  assert([identity isEqualToString:@"com.altivecintelligence.Strappy"]);
  return self;
}
+ (void)setBadge:(NSString *)value;
{
  [testBadge release]; testBadge = [value copy]; ++badgeUpdates;
}
+ (id)badgeNumberOrString;
{
  return testBadge;
}
@end

static strappy_notification_packet Packet(unsigned int run)
{
  strappy_notification_packet packet;
  memset(&packet, 0, sizeof(packet));
  memcpy(packet.magic, "STN1", 4);
  snprintf(packet.run, sizeof(packet.run), "%u", run);
  strcpy(packet.title, "Tokyo News");
  strcpy(packet.body, "Hello 🌸");
  return packet;
}
int main(void)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  FixtureProvider *provider = [FixtureProvider alloc];
  [provider prepare];
  strappy_notification_packet packet = Packet(1);
  [provider acceptPacket:&packet];
  [provider acceptPacket:&packet];
  assert(added == 1);
  NSArray *list = [provider bulletinsFilteredBy:0 count:32 lastCleared:nil];
  assert([list count] == 1);
  assert([[[list objectAtIndex:0] title] isEqual:@"Tokyo News"]);
  assert([[[list objectAtIndex:0] message] isEqual:@"Hello 🌸"]);
  id cleared = [provider clearedInfoForBulletins:list lastClearedInfo:nil];
  assert([cleared count] == 1);
  assert([[provider bulletinsFilteredBy:0 count:32 lastCleared:nil] count] == 0);
  [provider acceptPacket:&packet];
  assert(added == 1);
  for (unsigned int i = 2; i <= 35; ++i) {
    packet = Packet(i); [provider acceptPacket:&packet];
  }
  assert(added == 35 && withdrawn == 2);
  list = [provider bulletinsFilteredBy:0 count:2 lastCleared:cleared];
  assert([list count] == 2);
  assert([[[list objectAtIndex:0] recordID] hasSuffix:@".35"]);
  id newest = [[list objectAtIndex:0] recordID];
  list = [provider bulletinsFilteredBy:0 count:32
    lastCleared:[NSArray arrayWithObject:newest]];
  assert([list count] == 31);
  assert([[provider bulletinsFilteredBy:0 count:0 lastCleared:nil] count] == 0);
  assert([[provider bulletinsFilteredBy:0 count:32 lastCleared:@"unknown"] count] == 0);
  packet = Packet(36); packet.magic[0] = 'X'; [provider acceptPacket:&packet];
  packet = Packet(36); memset(packet.body, 'x', sizeof(packet.body));
  [provider acceptPacket:&packet];
  packet = Packet(36); packet.body[0] = (char)0xff; [provider acceptPacket:&packet];
  packet = Packet(36); strcpy(packet.run, "01"); [provider acceptPacket:&packet];
  packet = Packet(36); strcpy(packet.run, "9223372036854775808");
  [provider acceptPacket:&packet];
  assert(added == 35);
  packet = Packet(36); packet.body[0] = 0; [provider acceptPacket:&packet];
  assert(added == 36);
  throwOnAdd = YES;
  packet = Packet(37);
  @try { [provider acceptPacket:&packet]; assert(0); }
  @catch (NSException *exception) { (void)exception; }
  throwOnAdd = NO;
  [provider acceptPacket:&packet];
  assert(added == 37);
  for (id bulletin in [provider bulletinsFilteredBy:0 count:32 lastCleared:nil])
    assert(![[bulletin recordID] hasSuffix:@".37"]);
  [provider release];
  provider = [FixtureProvider alloc];
  [provider prepare];
  assert([[provider bulletinsFilteredBy:0 count:32 lastCleared:nil] count] == 0);
  /* Exercise the actual socket receiver, ownership lock and crash recovery. */
  assert(mkdtemp(fixtureDirectory));
  snprintf(fixtureSocket, sizeof(fixtureSocket), "%s/receiver.sock", fixtureDirectory);
  snprintf(fixtureLock, sizeof(fixtureLock), "%s.lock", fixtureSocket);
  [provider startReceiving];
  struct stat before, after;
  assert(lstat(fixtureSocket, &before) == 0);
  assert((before.st_mode & 0777) == 0600);
  FixtureProvider *second = [FixtureProvider alloc];
  [second prepare];
  [second startReceiving];
  [second release];
  assert(lstat(fixtureSocket, &after) == 0 && before.st_ino == after.st_ino);
  int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
  assert(fd >= 0);
  struct sockaddr_un address;
  memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  strcpy(address.sun_path, fixtureSocket);
  unsigned int baseline = added;
  packet = Packet(1000);
  assert(sendto(fd, &packet, 8, 0, (void *)&address, sizeof(address)) == 8);
  unsigned char oversized[sizeof(packet) + 1];
  memcpy(oversized, &packet, sizeof(packet));
  assert(sendto(fd, oversized, sizeof(oversized), 0,
    (void *)&address, sizeof(address)) == sizeof(oversized));
  assert(sendto(fd, &packet, sizeof(packet), 0,
    (void *)&address, sizeof(address)) == sizeof(packet));
  [provider receivePackets];
  assert(added == baseline + 1);
  for (unsigned int i = 1001; i <= 1009; ++i) {
    packet = Packet(i);
    assert(sendto(fd, &packet, sizeof(packet), 0,
      (void *)&address, sizeof(address)) == sizeof(packet));
  }
  [provider receivePackets];
  assert(added == baseline + 9);
  [provider receivePackets];
  assert(added == baseline + 10);
  close(fd);
  [provider stopReceiving];
  assert(lstat(fixtureSocket, &after) != 0 && errno == ENOENT);
  /* A live legacy receiver does not hold our lock: do not steal its socket. */
  fd = socket(AF_UNIX, SOCK_DGRAM, 0);
  assert(bind(fd, (void *)&address, sizeof(address)) == 0);
  assert(lstat(fixtureSocket, &before) == 0);
  [provider startReceiving];
  assert(lstat(fixtureSocket, &after) == 0 && before.st_ino == after.st_ino);
  assert(fcntl(fd, F_SETFL, O_NONBLOCK) == 0);
  assert(recv(fd, &packet, sizeof(packet), 0) == -1 && errno == EAGAIN);
  [provider stopReceiving];
  assert(lstat(fixtureSocket, &after) == 0 && before.st_ino == after.st_ino);
  close(fd);
  unlink(fixtureSocket);
  /* Simulate a dead process's socket: its lock has been released. */
  fd = socket(AF_UNIX, SOCK_DGRAM, 0);
  assert(bind(fd, (void *)&address, sizeof(address)) == 0);
  close(fd);
  [provider startReceiving];
  assert(lstat(fixtureSocket, &after) == 0);
  [provider stopReceiving];
  /* Refuse a substituted filesystem entry and preserve it on teardown. */
  assert(symlink("missing", fixtureSocket) == 0);
  [provider startReceiving];
  [provider release];
  assert(lstat(fixtureSocket, &after) == 0 && S_ISLNK(after.st_mode));
  unlink(fixtureSocket);
  unlink(fixtureLock);
  rmdir(fixtureDirectory);
  assert(badgeUpdates == 0); /* Ordinary packets never touch the badge. */
  provider = [FixtureProvider alloc]; [provider prepare];
  for (unsigned int i = 3; i <= 7; ++i) {
    packet = Packet(i);
    snprintf(packet.run, sizeof(packet.run), "%019lld",
      9000000000000000000LL + i);
    [provider acceptPacket:&packet];
    [provider acceptPacket:&packet]; /* Duplicate must not update again. */
#if XP_PROVIDER_DIAGNOSTIC
    if (i >= 4) {
      assert([testBadge intValue] == (int)i);
      assert(badgeUpdates == i - 3);
    } else assert(badgeUpdates == 0);
#else
    assert(badgeUpdates == 0);
#endif
  }
  [provider release];
  provider=[FixtureProvider alloc]; [provider prepare];
  strappy_badge_packet badge;
  memset(&badge,0,sizeof(badge)); memcpy(badge.magic,"STB1",4);
  strcpy(badge.revision,"1"); strcpy(badge.count,"3");
  unsigned int publications=added, updates=badgeUpdates;
  assert([provider acceptBadgePacket:&badge]);
  assert([testBadge intValue]==3 && badgeUpdates==updates+1);
  assert([provider acceptBadgePacket:&badge]);
  assert(badgeUpdates==updates+1); /* Lost ACK retry does not mutate again. */
  strcpy(badge.count,"4"); assert(![provider acceptBadgePacket:&badge]);
  modernBadgeAPI=YES; /* iOS 8 renamed selector; same badge-only behavior. */
  strcpy(badge.revision,"2"); strcpy(badge.count,"0");
  assert([provider acceptBadgePacket:&badge]); assert(testBadge==nil);
  strcpy(badge.revision,"1"); strcpy(badge.count,"9");
  assert([provider acceptBadgePacket:&badge]); assert(testBadge==nil);
  strcpy(badge.revision,"3"); strcpy(badge.count,"-1");
  assert(![provider acceptBadgePacket:&badge]);
  strcpy(badge.count,"01"); assert(![provider acceptBadgePacket:&badge]);
  strcpy(badge.count,"9223372036854775808"); assert(![provider acceptBadgePacket:&badge]);
  memset(badge.count,'9',sizeof(badge.count)); assert(![provider acceptBadgePacket:&badge]);
  assert(added==publications); /* Badge-only commands never publish a bulletin. */
  [provider release]; [testBadge release]; testBadge = nil;
  [pool drain];
  puts("notification provider policy: passed");
  return 0;
}
