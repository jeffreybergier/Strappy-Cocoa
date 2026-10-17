/* Isolated native experiment, intentionally imports the production C IPC
 * boundary. No production delegate, credentials, mutations, or provider calls.
 * UI/notification methods run on the main thread; reads run on a worker.
 * The application delegate lives for the process lifetime. */
#import <UIKit/UIKit.h>
#import <AILocalNotificationCenter.h>
#include "strappy_ipc.h"
#include <sqlite3.h>
#include <errno.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

@interface SandboxProbe : UIResponder <UIApplicationDelegate> {
  UIWindow *window_;
  UITextView *text_;
  NSMutableString *report_;
  UIBackgroundTaskIdentifier backgroundTask_;
}
@end

@implementation SandboxProbe
- (void)appendReport:(NSString *)line;
{
  [report_ appendFormat:@"%@\n", line];
  [text_ setText:report_];
  NSString *relative = [[NSBundle mainBundle] objectForInfoDictionaryKey:
    @"StrappyProbeReportPath"];
  NSString *path = [NSHomeDirectory() stringByAppendingPathComponent:
    relative ? relative : @"Documents/sandbox-result.txt"];
  NSError *error = nil;
  if (relative) {
    [[NSFileManager defaultManager] createDirectoryAtPath:
      [path stringByDeletingLastPathComponent] withIntermediateDirectories:YES
      attributes:nil error:&error];
    NSLog(@"Notification probe: %@", line);
  }
  if (![report_ writeToFile:path atomically:YES
      encoding:NSUTF8StringEncoding error:&error]) {
    NSLog(@"Sandbox report write failed: %@", error);
  }
}

- (void)runProbe;
{
  @autoreleasepool {
    NSMutableString *result = [NSMutableString string];
    /* Private diagnostic only, dynamically resolved on the test device.
     * A NULL operation asks whether a sandbox is attached (1=yes, 0=no). */
    int (*checkSandbox)(pid_t, const char *, int, ...) =
      (int (*)(pid_t, const char *, int, ...))dlsym(RTLD_DEFAULT,
        "sandbox_check");
    [result appendFormat:@"Runtime sandbox_check: %d (1=sandboxed, 0=none)\n"
      "Probe wire version: %u\n",
      checkSandbox ? checkSandbox(getpid(), NULL, 0) : -1,
      (unsigned int)STRAPPY_IPC_VERSION];
    const char *control =
      "/private/var/mobile/tmp_altivec/strappy-sandbox-control.txt";
    errno = 0;
    int controlFD = open(control, O_RDONLY);
    int controlError = errno;
    if (controlFD >= 0) close(controlFD);
    [result appendFormat:@"Outside control: open=%d errno=%d\n",
      controlFD, controlError];
    errno = 0;
    controlFD = open(control, O_WRONLY);
    controlError = errno;
    ssize_t written = -1;
    int writeError = 0;
    if (controlFD >= 0) {
      /* Fixture contains "sandbox control fixture\n". Rewriting its first
       * byte with the same value tests actual write enforcement harmlessly. */
      errno = 0;
      written = write(controlFD, "s", 1);
      writeError = errno;
      close(controlFD);
    }
    [result appendFormat:@"Outside control write-open: fd=%d errno=%d\n",
      controlFD, controlError];
    [result appendFormat:@"Outside control write: bytes=%ld errno=%d\n",
      (long)written, writeError];
    if (checkSandbox) {
      [result appendFormat:@"Policy process-fork: %d (0=allowed)\n"
        "Policy control file-write-data: %d (0=allowed)\n"
        "Policy store file-read-data: %d (0=allowed)\n",
        checkSandbox(getpid(), "process-fork", 0),
        checkSandbox(getpid(), "file-write-data", 1, control),
        checkSandbox(getpid(), "file-read-data", 1,
          "/private/var/mobile/Library/Application Support/Strappy/strappy.sqlite")];
    }

    errno = 0;
    int fd = strappy_ipc_connect("/var/log/strappy/service.sock", geteuid(),
      2000);
    [result appendFormat:@"Authenticated IPC connect: %@ (errno hint=%d)\n",
      fd >= 0 ? @"PASS" : @"FAIL", errno];
    /* errno is only a hint: the production helper also rejects peer mismatch
     * and deadlines without defining a new errno. Verify the daemon separately. */
    for (uint32_t op = STRAPPY_IPC_HELLO;
        fd >= 0 && op <= STRAPPY_IPC_SUBSCRIBE; ++op) {
      strappy_ipc_message message;
      memset(&message, 0, sizeof(message));
      message.kind = STRAPPY_IPC_REQUEST;
      message.operation = op;
      message.request_id = op;
      if (op == STRAPPY_IPC_HELLO) {
        message.length = 8;
        strappy_ipc_put32(message.payload, STRAPPY_IPC_VERSION);
        strappy_ipc_put32(message.payload + 4, STRAPPY_IPC_VERSION);
      }
      int sent = strappy_ipc_send(fd, &message, 2000);
      int received = sent == 0 ? strappy_ipc_receive(fd, &message, 2000) : -1;
      BOOL valid = received == 0 && message.kind == STRAPPY_IPC_REPLY &&
        message.operation == op && message.request_id == op &&
        message.status == STRAPPY_IPC_OK && message.length == 36 &&
        (strappy_ipc_get32(message.payload + 24) & 3U) == 3U &&
        strappy_ipc_get32(message.payload + 32) == STRAPPY_IPC_STORE_ABI;
      [result appendFormat:@"IPC operation %u: %@ send=%d receive=%d\n",
        (unsigned int)op, valid ? @"PASS" : @"FAIL", sent, received];
      if (!valid) break;
    }
    if (fd >= 0) close(fd);

    sqlite3 *db = NULL;
    int opened = sqlite3_open_v2(
      "/var/mobile/Library/Application Support/Strappy/strappy.sqlite",
      &db, SQLITE_OPEN_READONLY, NULL);
    int queried = opened;
    if (opened == SQLITE_OK) {
      sqlite3_busy_timeout(db, 2000);
      /* Read the actual WAL-backed store without returning user data. */
      queried = sqlite3_exec(db, "SELECT count(*) FROM sqlite_master",
        NULL, NULL, NULL);
    }
    [result appendFormat:@"Database read-only: open=%d query=%d\n",
      opened, queried];
    if (db) sqlite3_close(db);
    [self performSelectorOnMainThread:@selector(appendReport:)
      withObject:result waitUntilDone:NO];
  }
}

- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)options;
{
  (void)application;
  (void)options;
  backgroundTask_ = UIBackgroundTaskInvalid;
  report_ = [[NSMutableString alloc] init];
  window_ = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
  UIViewController *controller = [[UIViewController alloc] init];
  text_ = [[UITextView alloc] initWithFrame:[window_ bounds]];
  [text_ setEditable:NO];
  [text_ setFont:[UIFont systemFontOfSize:15]];
  [text_ setAutoresizingMask:UIViewAutoresizingFlexibleWidth |
    UIViewAutoresizingFlexibleHeight];
  [controller setView:text_];
  [window_ setRootViewController:controller];
  [window_ makeKeyAndVisible];
  [self appendReport:[NSString stringWithFormat:
    @"Variant: %@\niOS: %@\nUID: %u\nHome: %@\n"
     "Allow notifications, then press Home. A test alert is submitted after "
     "3 seconds in the background. Reported submission does not prove display.",
    [[NSBundle mainBundle] objectForInfoDictionaryKey:@"StrappySandboxVariant"],
    [[UIDevice currentDevice] systemVersion], (unsigned int)geteuid(),
    NSHomeDirectory()]];
  if ([[[NSBundle mainBundle] objectForInfoDictionaryKey:
      @"StrappySelfSandbox"] boolValue]) {
    int (*initializeSandbox)(const char *, uint64_t, char **) =
      (int (*)(const char *, uint64_t, char **))dlsym(RTLD_DEFAULT,
        "sandbox_init");
    void (*freeSandboxError)(char *) = (void (*)(char *))dlsym(RTLD_DEFAULT,
      "sandbox_free_error");
    char *error = NULL;
    int result = initializeSandbox ? initializeSandbox("container", 1,
      &error) : -1;
    NSString *line = [NSString stringWithFormat:
      @"Explicit container sandbox_init=%d error=%s", result,
      error ? error : "none"];
    NSLog(@"%@", line);
    [self appendReport:line];
    if (error && freeSandboxError) freeSandboxError(error);
  }
  [[AILocalNotificationCenter defaultCenter] requestAuthorization];
  if (![[[NSBundle mainBundle] objectForInfoDictionaryKey:
      @"StrappySkipStorageProbe"] boolValue]) {
    [NSThread detachNewThreadSelector:@selector(runProbe) toTarget:self
      withObject:nil];
  }
  return YES;
}

- (void)application:(UIApplication *)application
    didRegisterUserNotificationSettings:(UIUserNotificationSettings *)settings;
{
  (void)application;
  [self appendReport:[NSString stringWithFormat:
    @"Registered notification types=%lu (alert=4, sound=2, badge=1)",
    (unsigned long)[settings types]]];
}

- (void)finishBackgroundTask;
{
  if (backgroundTask_ != UIBackgroundTaskInvalid) {
    [[UIApplication sharedApplication] endBackgroundTask:backgroundTask_];
    backgroundTask_ = UIBackgroundTaskInvalid;
  }
}

- (void)sendTestNotification;
{
  AILocalNotificationCenter *center = [AILocalNotificationCenter defaultCenter];
  AINotificationResult result = [center postNotificationWithTitle:
    @"Strappy sandbox experiment" body:@"Sandbox background alert test."
    userInfo:nil];
  [self appendReport:[NSString stringWithFormat:
    @"Notification authorization=%d submission=%d (0=submitted, not display)",
    (int)[center authorizationStatus], (int)result]];
  [self finishBackgroundTask];
}

- (void)applicationDidEnterBackground:(UIApplication *)application;
{
  backgroundTask_ = [application beginBackgroundTaskWithExpirationHandler:^{
    [NSObject cancelPreviousPerformRequestsWithTarget:self
      selector:@selector(sendTestNotification) object:nil];
    [self finishBackgroundTask];
  }];
  [self performSelector:@selector(sendTestNotification) withObject:nil
    afterDelay:3.0];
}

- (void)applicationWillEnterForeground:(UIApplication *)application;
{
  (void)application;
  [NSObject cancelPreviousPerformRequestsWithTarget:self
    selector:@selector(sendTestNotification) object:nil];
  [self finishBackgroundTask];
}
@end

int main(int argc, char **argv)
{
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil, NSStringFromClass([SandboxProbe class]));
  }
}
