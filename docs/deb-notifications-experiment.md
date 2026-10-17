# DEB local notification experiment

2026-10-17, branch `experiment/ios-sandbox-ipc`, koolphone5: iPhone5,2,
iOS 8.4.1 (12H321), armv7, mobile UID 501.

Latest handoff: the iOS 6 daemon-to-SpringBoard probe receives and acknowledges
IPC, but the owner observed **no visible banner**. The test component remains
installed on gomadango. See [native results and continuation instructions](#native-results-and-handoff-2026-10-17)
before rerunning or changing the experiment.

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

**Current result: IPC and the display invocation succeeded, but the owner saw
no banner. Visible notification delivery on iOS 6 is still unresolved.**

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

The next experiment is to close Notification Center, stay on the unlocked Home
screen, and rerun the installed sender. Then test the locked phone separately.
Do not treat the acknowledgement as visual success. If still invisible, inspect
the iOS 6 banner controller's filtering and section registration, and consider
the Mail-style data-provider / BBServer route instead of direct controller
presentation. `SpringBoard-objc.txt` was generated locally with `otool -ov`;
it identifies the controller's `observer:addBulletin:forFeed:` method for
further disassembly. No change to those filters has been made.

State to carry to the next computer:

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

Native coverage is limited to the iOS 6 armv7 observations above, with no
visible-alert success yet.

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
