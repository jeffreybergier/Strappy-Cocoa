# DEB local notification experiment

2026-10-17, branch `experiment/ios-sandbox-ipc`, koolphone5: iPhone5,2,
iOS 8.4.1 (12H321), armv7, mobile UID 501.

Latest handoff: the owner confirmed **visible Home-screen and lock-screen
notifications** from the iOS 6 daemon-to-SpringBoard probe. With Do Not Disturb
enabled, the screen stayed off and the notification was visible after the owner
woke it with Home, as expected. Notification Center history and notification
settings registration remain unimplemented by this probe. The test component
remains installed on gomadango. See [native results and continuation instructions](#native-results-and-handoff-2026-10-17)
before rerunning or changing the experiment. Production completion integration
is implemented below and was installed on gomadango. The owner confirmed a
production alert for session 30. The later single-message formatting experiment
was reverted at the owner's request; separate title/body fields are restored.

## Result

An unsandboxed app installed by DEB under `/Applications` displayed a local
notification after adding `SBAppUsesLocalNotifications = true` to its Info.plist.
The owner confirmed seeing the alert. This disproves the earlier working
assumption that notification display requires an IPA or sandboxing on this phone.

The successful probe used Strappy's production entitlements, with only the app
identifier and unused Keychain group changed for isolation. It used the same
`AILocalNotificationCenter` as production. No new notification library, private
notification API, remote push service, daemon change, or shared-store change
was needed. The production iOS plist now contains the tested declaration.

The research lead was the upstream
[libnotifications README](https://github.com/CokePokes/libnotifications#readme),
which documents this plist key for apps in `/Applications`. That library targets
newer iOS versions and was not installed or linked; the iOS 8 result comes from
the native test below, not extrapolation from the README.

## Controls and observations

The notification-only mode reuses `sandbox_probe.m`. It skips all IPC, SQLite,
fixture writes, and sandbox initialization. It requests authorization at launch,
starts a short background task on Home, and submits a local alert after three
seconds. This background task exists to run a deterministic test and is not a
proposed change to the production app's lifecycle policy.

| Variant | Packaging | Plist declaration | Device result |
| --- | --- | --- | --- |
| `deb-control` | `/Applications/StrappyNotificationDebControl.app` | Absent | Types 7, authorization 2, submission 0; owner observed no permission dialog. Banner absence was not independently confirmed. |
| `deb-declared` | `/Applications/StrappyNotificationDebDeclared.app` | True | Types 7, authorization 2, submission 0; owner confirmed visible alert. |
| `ipa-control` | IPA | Absent | Cross-built and validated, not installed in this follow-up; earlier IPA results are in the sandbox experiment. |

Types 7 means alert, sound, and badge. Authorization 2 means Authorized;
submission 0 means Submitted. These numeric API results alone are not visual
confirmation. Both DEBs have fresh identities; neither reuses the production
app's saved notification preferences. SpringBoard logged `ADDING REMOTE` for
`com.altivecintelligence.StrappyNotificationDebDeclared` on first launch. No
equivalent provider-registration line appeared for the control in the captured
interval. Separate identities mean this is supporting evidence rather than a
same-identity reversal test proving the key was the sole cause.

The successful app reported `Home: /var/mobile`. Package inspection verified
that each DEB contains only its distinctly named app; neither contains daemon
files or lifecycle scripts. Installation used `dpkg -i`, followed by `uicache`
as mobile. The existing Strappy app, daemon, and database were not replaced.

## Reproduction

From the checkout root:

```sh
python3 source/daemon/tests/notification_probe.py deb-control
python3 source/daemon/tests/notification_probe.py deb-declared
python3 source/daemon/tests/notification_probe.py ipa-control
python3 source/daemon/tests/notification_probe.py deb-control analyze
python3 source/daemon/tests/notification_probe.py deb-control validate-package
python3 source/daemon/tests/notification_probe.py deb-declared validate-package
python3 source/daemon/tests/notification_probe.py ipa-control validate-package
```

The declarative `notification_probe.mk` uses Altivec's common compile, sign,
bundle and package rules. Outputs are under
`build/experiments/deb-notifications/<variant>/output/iOS/release/<product>/`.
Each build includes armv7 and arm64; matching debug symbols are retained.

Stage the DEBs below `~/tmp_altivec` on the authorized phone and install with
its package manager. Refresh registration with `su mobile -c uicache`. Open
`strappy-notification-deb-control://` or
`strappy-notification-deb-declared://` through `uiopen` as mobile (or tap the
matching Home Screen icon). Unlock the phone, respond to any permission dialog,
press Home, and observe the alert after three seconds.

Reports go to
`~/tmp_altivec/com.altivecintelligence.StrappyNotificationDebControl/notification-result.txt`
and the corresponding `StrappyNotificationDebDeclared` directory. Ensure those
test-owned child directories are writable by mobile if the staging parent is
root-owned. Initially the control could not create its report directory for this
reason; its registration/submission results were recovered from syslog, which
also receives probe reports. This did not prevent notification submission.

Generated logs, reports, packages, and symbols remain under the ignored build
directory. The isolated DEB apps remain installed for inspection; uninstall
through `dpkg -r` with their exact package IDs if desired:
`com.altivecintelligence.strappynotificationdebcontrol` and
`com.altivecintelligence.strappynotificationdebdeclared`.

## Binary comparison: iOS 6 and iOS 8

On 2026-10-17, copies of SpringBoard and the armv7s dyld shared cache were
searched for the exact `SBAppUsesLocalNotifications` string in ASCII, UTF-16LE,
and UTF-16BE. The key is present only in iOS 8 SpringBoard among these four
binaries; it is absent from both inspected iOS 6 binaries.

| Device / OS | Binary | Size (bytes) | Exact key search |
| --- | --- | ---: | --- |
| gomadango / iOS 6.1.3 | SpringBoard | 2,761,072 | Absent in all three encodings |
| gomadango / iOS 6.1.3 | dyld shared cache | 244,601,807 | Absent in all three encodings |
| koolphone5 / iOS 8.4.1 (12H321) | SpringBoard | 5,501,792 | ASCII at file offset `0x340bb4` |
| koolphone5 / iOS 8.4.1 (12H321) | dyld shared cache | 431,742,976 | Absent in all three encodings |

The iOS 8 match is in `__TEXT,__cstring`, at VM address `0x344bb4`, beside
`SBSystemAppNeedsUserNotificationPermission`. Neither UTF-16 encoding matched
that executable. The copied `SystemVersion.plist` confirms koolphone5's OS
version and build.

Source paths on each phone were
`/System/Library/CoreServices/SpringBoard.app/SpringBoard` and
`/System/Library/Caches/com.apple.dyld/dyld_shared_cache_armv7s`. Copies remain
under the ignored `build/investigations/gomadango/` and
`build/investigations/koolphone5/` directories, with koolphone5's version plist
alongside its binaries. These proprietary artifacts are not checked in.

This confirms a difference between the inspected iOS 6.1.3 and iOS 8.4.1
binaries and supports treating this plist declaration as an iOS 8 finding,
not an established fix for iOS 6. It does not establish the key's first release
(iOS 7 was not inspected), rule out a dynamically constructed key, or prove
that iOS 6 lacks another notification mechanism. In particular, iOS 6
SpringBoard contains `usesLocalNotifications`; tracing that implementation
and Mail's BulletinBoard integration remains a separate investigation.

### Mail binaries and online research

The iOS 6 phone's Mail executable and related native components were copied
read-only into `build/investigations/gomadango/mail/`, with their plists:

- `/Applications/MobileMail.app/MobileMail` (1,294,992 bytes).
- `/System/Library/PrivateFrameworks/DataAccess.framework/Support/dataaccessd`
  (11,664 bytes), plus `com.apple.dataaccess.dataaccessd.plist`. No separately
  named Mail daemon was found in the inspected launchd/libexec/sbin locations;
  this is a related service, not proof it owns every Mail notification.
- `/System/Library/BulletinBoardPlugins/MailDataProvider.bundle/MailDataProvider`
  (44,752 bytes), whose principal class is `MFMailDataProvider`.

The plugin imports `BBDataProviderAddBulletin`, `BBDataProviderModifyBulletin`,
`BBBulletinRequest`, and `MSNotificationObserver`. The copied shared cache places
`MSNotificationObserver` in `MailServices.framework` and contains the service
name `com.apple.mobilemail.services.xpc`, bulletin observer-registration and
message-request commands, and callbacks for fetched/deleted messages, summaries,
flags, and account changes. These strings/imports support the proposed flow
Mail service -> observer -> MailDataProvider -> BulletinBoard. The full call
sequence has not been disassembled or traced. Mail's plist contains
`SBMachServices` and `UIBackgroundModes` values `audio` and `continuous`, but
does not contain `SBAppUsesLocalNotifications`.

Online leads reviewed:

- [libbulletin](https://github.com/limneos/libbulletin) claims iOS 5 through
  12.1.2 support. Its SpringBoard injection filter and older-controller code
  provide the basis for the isolated presentation probe below. It constructs
  bulletins and calls banner/lock-screen controllers; this does not establish
  persistent Notification Center delivery or a standalone daemon API.
- [RocketBootstrap](https://github.com/rpetrich/RocketBootstrap/blob/master/rocketbootstrap.h)
  supplies Mach, CFMessagePort, and CPDistributedMessagingCenter IPC helpers,
  not notification presentation. It is not needed for our fixed Darwin signal.
- [libnotifications](https://github.com/CokePokes/libnotifications) requires
  iOS 10+, so it is not the iOS 6 implementation to use.

## Validation and limits

### Five-second daemon / SpringBoard probe

`source/daemon/tests/bulletin_probe.py` builds a separate rootful test DEB with
the real `strappyd` source compiled with `STRAPPY_BULLETIN_PROBE`. Its installed
name is `strappyd-bulletin-probe`; `--bulletin-probe` takes an early path before
opening the database, credentials, execution engine, or production socket.
It waits five seconds after launch, posts one fixed Darwin notification, then
waits up to five seconds for a SpringBoard dispatch acknowledgement and exits.
Production builds do not contain this test mode.

The DEB also installs a signed `StrappyBulletinProbe.dylib` and SpringBoard-only
MobileSubstrate filter. The receiver runs on the main thread, creates a fixed
`BBBulletin`, and invokes the iOS 6 banner or lock-screen controller according
to the current lock state. This follows the older-controller approach visible
in [libbulletin](https://github.com/limneos/libbulletin/blob/master/libbulletin.xm),
without linking that library or requiring RocketBootstrap. All private-selector
bridges and primitive `NSInvocation` calls are in `XPBulletinProbe.m`.

This is a presentation/IPC experiment, not a production notification service.
Darwin signals carry no payload and are not authenticated or correlated: run
one sender at a time. The receiver accepts only the fixed test message, limits
dispatch to once per five seconds, and does not read user data. An acknowledgement
means the private display invocation returned, not that an alert was visible.
Notification Center persistence, sounds, tap routing and general iOS-version
compatibility need separate validation. The initial receiver deliberately
requires the iOS 6 `SBAwayController` path, even though both iOS slices build.

Build and inspect using the existing common Make pipeline:

```sh
python3 source/daemon/tests/bulletin_probe.py
python3 source/daemon/tests/bulletin_probe.py analyze
python3 source/daemon/tests/bulletin_probe.py validate-package
```

The package and matching symbols are below
`build/experiments/bulletin-probe/output/iOS/release/`. The package installs only
`/usr/libexec/strappyd-bulletin-probe` and the two uniquely named MobileSubstrate
files. It has no launchd plist or lifecycle scripts and does not replace the
installed Strappy app or daemon. MobileSubstrate must already be installed.

For native testing, stage the DEB below the explicit phone's `~/tmp_altivec`,
inspect it, and install using `dpkg -i`. Create
`/var/mobile/tmp_altivec/strappy-bulletin-probe` owned by mobile for receiver
logging. Restart SpringBoard to load the component; this briefly interrupts the
phone UI. Check `receiver.log` there for `receiver ready`, then run as mobile:

```sh
/usr/libexec/strappyd-bulletin-probe --bulletin-probe
```

Observe the phone after five seconds, checking unlocked and locked states
separately. The sender reports IPC submission and acknowledgement separately;
the receiver logs request and display-dispatch timestamps. Preserve both logs.
Uninstall with `dpkg -r com.altivecintelligence.strappy.bulletin-probe`, then
restart SpringBoard to unload the receiver. Keep staging reports and local
symbols until the investigation is complete.

#### Native results and handoff, 2026-10-17

**Initial result: IPC and the display invocation succeeded, but the owner saw
no banner. The subsequent reruns below confirmed visible delivery.**

The probe ran on gomadango, iOS 6.1.3 / iPhone5,2 / armv7, as mobile. The phone
has MobileSubstrate 0.9.6301. A negative control before loading the receiver
waited five seconds, posted its request, then correctly exited with status 1
after five more seconds without acknowledgement. The isolated rootful DEB
was then installed. An initial install raced the file transfer and failed with
a truncated-archive error; reinstalling after transfer completion succeeded.

SpringBoard was deliberately restarted with SIGTERM to load the dylib. This
UI interruption initially looked like a crash to the owner. The replacement
SpringBoard remained running as PID 906 and logged `receiver ready`; no matching
new SpringBoard crash report was found. The production strappyd remained PID 68
through this check. The installed production app, daemon and data were not
replaced. No prompt or provider request was submitted.

The first actual receiver-loaded test returned status 0 with these reports:

```text
bulletin-probe: started; waiting five seconds
bulletin-probe: request posted; waiting for SpringBoard dispatch
bulletin-probe: SpringBoard dispatch acknowledged; visible display requires observation
813923276.012 receiver ready
813923337.470 received request
813923337.541 dispatched banner
```

Receiver times are CFAbsoluteTime seconds. SpringBoard took about 71 ms from
receiving the IPC signal to returning from its banner invocation. The owner
explicitly reported that nothing appeared. Syslog had reported Notification
Center open shortly before this test; it is only a possible suppression cause.
A retry from the unlocked Home screen was proposed but not performed before
the owner requested this handoff. No receiver-loaded lock-screen test occurred.

The proposed next experiment was to close Notification Center, stay on the
unlocked Home screen, and rerun the installed sender, then test the locked phone
separately. Those reruns are now complete; see the confirmed results below.
Do not treat the acknowledgement as visual success. The initial fallback plan
was to inspect the iOS 6 banner controller's filtering and section registration
and consider the Mail-style data-provider / BBServer route if presentation
remained invisible. `SpringBoard-objc.txt` was generated locally with `otool -ov`;
it identifies the controller's `observer:addBulletin:forFeed:` method for
further disassembly. No change to those filters has been made.

State recorded at the initial handoff (see reruns below for updated connectivity):

- Branch: `experiment/ios-sandbox-ipc`.
- Last SSH address overrides: gomadango `172.20.10.7`; koolphone5 `172.20.10.3`.
  Use the corresponding configured SSH alias, `-o Hostname=...`, and `scp -O`.
  Confirm the address and `uname -a` again; hotspot addresses can change.
- The probe package remains installed and its SpringBoard receiver remains
  loaded. No further respring is needed to rerun the unchanged sender:
  `su mobile -c '/usr/libexec/strappyd-bulletin-probe --bulletin-probe'`.
- Phone staging/logs: `/var/mobile/tmp_altivec/strappy-bulletin-probe/` contains
  `probe.deb`, the standalone sender copy `strappyd`, `no-receiver.log`,
  `sender-1.log`, and `receiver.log`. Run one sender at a time.
- Local build logs and retrieved reports are in
  `build/experiments/bulletin-probe/`; matching symbols are in its `output/`.
  Copied Apple binaries, generated disassembly, packages, logs and symbols are
  ignored local artifacts, not Git content. Rebuild or recopy them as needed;
  the essential observations are recorded above.

The dual-architecture probe build and DEB validation passed without compiler
warnings, and the app-boundary check passed. The full production build and
whole-daemon analysis were interrupted during the conversation. Resuming the
build in the same initially clean `build/bulletin-validation` directory completed
successfully for iOS armv7/arm64 and macOS PPC/i386/x86_64/arm64, with no compiler
warnings or errors. Full output is retained in `build/bulletin-validation.log`
and `build/bulletin-validation-resume.log`.
Focused analysis of the two new probe sources passed without diagnostics,
separately from the unfinished whole-daemon analysis. It can be repeated with:

```sh
make -C source/iOS -f bulletin_probe.mk \
  BUILD_ROOT=build/experiments/bulletin-probe/output \
  strappyd-bulletin-probe_SOURCES=../daemon/tests/bulletin_probe/probe.c analyze
```

Native coverage is limited to the iOS 6 armv7 observations above and the
owner-confirmed reruns below.

#### Confirmed reruns and DND behavior, 2026-10-17

Connected through gomadango's configured SSH alias at its normal address,
without the earlier hotspot override. The installed probe was reused without
restarting SpringBoard or changing the app, daemon, or receiver. Each sender
ran as mobile, waited five seconds, received a dispatch acknowledgement, and
exited with status 0. The owner confirmed visible delivery in all three reruns:

| Test | Receiver request / dispatch (CFAbsoluteTime) | Owner observation |
| --- | --- | --- |
| Unlocked Home screen | `813938719.886` / `813938719.911` | Banner appeared. |
| Locked, Do Not Disturb enabled | `813938747.699` / `813938747.732` | Screen did not light up; notification was visible after pressing Home manually. |
| Locked, Do Not Disturb disabled | `813938790.165` / `813938790.193` | Notification appeared. |

The DND test behaved as expected: the notification was available on the lock
screen without waking the screen. An initial interpretation that visibility
under DND suggested a policy bypass was incorrect; the owner clarified that
Home was pressed to wake the display. This observation does not demonstrate a
DND bypass or establish every aspect of notification policy. Sound behavior was
not separately confirmed.

Sender reports remain under
`/var/mobile/tmp_altivec/strappy-bulletin-probe/` as
`sender-rerun-20261017.log`, `sender-locked-20261017.log`, and
`sender-dnd-off-20261017.log`; receiver timestamps are in `receiver.log` there.

The owner also observed that these notifications were absent from Notification
Center and Strappy was absent from the notification settings list. The probe's
implementation explains these limits:

- The sender posts a payload-free Darwin signal. The receiver inside SpringBoard
  creates the fixed message; no remote push service is involved.
- The receiver directly invokes `observer:addBulletin:forFeed:` on the banner
  or lock-screen controller. It does not publish through a BulletinBoard data
  provider/server or implement Notification Center persistence.
- Setting the bulletin's section identity to Strappy's bundle identifier does
  not implement notification-provider or settings registration.
- The acknowledgement confirms the invocation returned; the owner's separate
  observation confirms visible delivery. Tap routing and sound remain untested.

The next investigation is the BulletinBoard data-provider/server route and
section registration for Notification Center and notification settings. Visible
presentation from the isolated sender is now confirmed; production prompt
completion integration is now implemented below, pending native validation.

### Production follow-up on gomadango

The rootful 1.2.0 package was installed on gomadango (iPhone5,2, iOS 6.1.3).
Daemon IPC v8 passed, but the owner saw no completion alert. Runtime logs showed
the app entering the background at 12:10:36 and receiving the prompt completion
only after becoming active at 12:12:50. The foreground guard then skipped the
alert. The earlier probe held a background task; production had removed it.

The app now holds one finite UIKit background task while prompts are active,
including submission before the daemon persists the run. Start/finish events
track session identifiers so overlapping prompts share the task. Completion
submits the alert before releasing the task. Expiration releases the assertion
without stopping daemon work and prevents renewal until the app becomes active
again. This does not prevent screen locking or promise alerts after iOS expires
the allotted background time. Submission results are logged without content.

The updated rootful DEB was installed on gomadango; daemon IPC v8 passed again.
Native notification display on iOS 6 remains a manual validation step: start a
prompt, press Home before completion, and verify both the visible alert and the
task begin/submission/end sequence in syslog. Also exercise overlapping prompts
and expiration with a long-running request; no renewal should occur in the
background after expiration.

Clean builds passed for both iOS and all four macOS architectures without
compiler warnings. Both iOS packages validated and the app-boundary check passed.
iOS static analysis reported no AppDelegate diagnostics, but flagged a possible
null source in the unchanged debug-capture `memcpy` at
`source/daemon/strappy_client.c:1230`. That separate finding is not resolved by
this lifecycle change. Logs and symbols are retained under
`build/device-runs/gomadango-background/`.

### Original isolated probe coverage

The three probe variants passed clean armv7/arm64 builds and package validation.
Clang analysis of the shared probe passed. Clean production builds cover all
four macOS and both iOS architectures; no compiler warnings were reported.
Native coverage here is iOS 8.4.1 / armv7 only.

The experiment confirms a visible alert from an unsandboxed DEB app. It does
not yet confirm an actual production prompt completion, foreground cleanup,
notification behavior while suspended, a reboot, existing production-ID
notification preferences, or other iOS versions/rootless jailbreaks. The full
production DEB was not deployed: its v8 daemon would replace the installed v5
daemon, which is outside this isolated notification comparison.

## Daemon completion integration, 2026-10-17

The production iOS DEB now includes a separately signed `StrappyBulletin.dylib`
and SpringBoard-only filter under `Library/MobileSubstrate/DynamicLibraries`
(with the package prefix for rootless). It reuses the tested private display
controller path, isolated in `source/iOS/notifications/XPStrappyBulletin.m`.
MobileSubstrate must already be available. Installation prints a reminder to
restart SpringBoard once; it does not automatically interrupt the UI. The old
fixed-message probe remains separate and is not triggered by prompt completion.

After a user prompt's completed/failed state commits, strappyd sends one
nonblocking Unix datagram to the receiver, regardless of app state. Unlike the
probe's payload-free Darwin signal, the datagram carries the title and reply
preview and keeps concurrent completions distinct. The receiver owns a private
0700 mobile directory and 0600 socket at
`/var/mobile/Library/Application Support/StrappyAlerts/receiver.sock`. Other processes running
as mobile share this trust boundary. No notification text is persisted to a
sidecar file or written to logs. There is no replay queue: receiver absence,
queue pressure, or unsupported private APIs may lose an alert without changing
or retrying the prompt. SpringBoard processes a bounded number per callback.

Success uses the session name (at most 100 Unicode scalars) and the first 400
Unicode scalars of this prompt's final answer, preserving UTF-8 boundaries. The
final answer is captured from the current execution result so an empty answer
cannot show a previous reply. The receiver sets the session name as the bulletin
title and the preview as its message. SpringBoard controls visible layout and
truncation. A failed prompt uses the same title and `Error` as its message, or
a short code-owned explanation for an exact recognized error such as missing
credentials. Arbitrary provider diagnostics are never
copied into alerts. Cancelled/interrupted runs and maintenance/Database Study
jobs do not alert. Durable request retries do not redisplay finished prompts.

The iOS app's old local completion submission was removed to prevent duplicates;
its finite background-task lifecycle remains intact. Mac notification behavior
is unchanged. This first receiver still depends on the verified iOS 6 private
API path. Notification Center persistence, settings registration, sound, and
session-specific tap routing are not added. The DND results above describe the
original probe; the production receiver and real prompt completion need their
own native confirmation after installation and a SpringBoard restart.

The full offline Linux suite passed. Focused execution tests also passed after
adding the credential-error fixture: renamed session, multibyte preview
truncation, empty answers, generic and recognized errors, cancellation,
duplicate retries, and receiver absence. Final clean release builds passed for
iOS armv7/arm64 and macOS PPC/i386/x86_64/arm64 without compiler warnings.
The retained Mac delegate recorder cross-built for all four Mac architectures;
it was not run on a native Mac in this change. Final rootful/rootless DEB and
Mac ZIP validation passed, including architecture, metadata, ownership,
lifecycle, and executable entitlements; the log is
`build/notification-package-final.log`.

Full iOS static analysis completed with no diagnostics in the notification
code. It repeated the existing possible-null-source `memcpy` finding in
`source/daemon/strappy_client.c:1230`; this change does not resolve that separate
debug-capture issue. Local logs are `build/notification-tests.log`,
`build/notification-execution-final.log`, `build/notification-build-final.log`,
`build/notification-analysis.log`, and
`build/notification-mac-fixture-build.log`. Generated logs remain ignored.

A read-only check on gomadango confirmed `net.local.dgram.maxdgram = 2048` and
`net.local.dgram.recvspace = 4096`. The versioned packet is 2,030 bytes; the
receiver requests a bounded 128 KiB receive buffer for bursts. The socket lives
under the mobile-owned Application Support directory rather than the device's
world-writable Caches directory. These checks do not establish native delivery
of the new payload transport: no new package was installed, no SpringBoard
restart was performed, and no production prompt was submitted in this change.

### Notification prefix correction

The first deployed build put the session name in the bulletin title and the
reply preview in its message. The owner observed a notification beginning with
the reply's Font Awesome shortcode instead of the requested session-name prefix.
For session 30, the stored name was `Tokyo News Today`, while the final reply
began with `[fa:regular:newspaper]` and a Markdown heading. Commit `e9261f9` changed the receiver to
format the actual message as `Tokyo News Today: <reply preview>` without setting
a separate title, attempting to satisfy the requested prefix. The
preview content and its 400-character limit are unchanged.

The original production package was installed from commit `3c4685f`; the daemon
passed IPC readiness, and SpringBoard logged `receiver ready` after restart.
The owner then confirmed receiving the production alert described above. Its
installation logs and armv7 symbols are retained under
`build/device-runs/gomadango-notifications-3c4685f/`.

### Restore separate title and body

After installing `e9261f9`, the owner reported seeing only a title and no body
and requested the original presentation again. The receiver is restored to the
`3c4685f` behavior: session name in `setTitle:`, reply preview (or error message)
in `setMessage:`. The owner suspects that the lock screen presents both fields
while unlocked banners show only the body. This is a working explanation of the
observations, not a general compatibility claim about every iOS 6 notification
API. The earlier assertion that iOS 6 required a single message was premature.
No shortcode stripping or other preview transformation was added.
