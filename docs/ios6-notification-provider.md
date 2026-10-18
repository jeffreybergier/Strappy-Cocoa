# Native Strappy notification provider candidate

Current native status: on gomadango, the owner confirmed the native provider's
Notification Center entry with the correct Strappy name/icon, a visible banner,
and a Strappy notification-settings entry. The custom-sound test was published;
its audible result and app-launch behavior are awaiting owner confirmation. The
test package is currently installed; the production daemon still uses the old
receiver and the candidate still uses its separate socket.

First implementation milestone: an independently built bundle, not included in
production packages and not installed on a phone. The existing daemon and
Substrate receiver remain unchanged. This is an iOS 6 runtime candidate; the
arm64 slice satisfies build coverage but initialization refuses iOS 7+.

## Architecture

`source/iOS/notifications/provider/XPStrappyNotificationProvider.m` implements
`BBDataProvider` using the native loading route established by the
[isolated provider experiment](ios6-provider-probe.md). The bundle has its own
identifier, `com.altivecintelligence.Strappy.notification-provider`, while its
section and app-launch action use `com.altivecintelligence.Strappy`.

After `dataProviderDidLoad`, it opens a Unix datagram receiver on the main run
loop. It accepts the unchanged daemon `STN1` packet format and publishes a
`BBBulletinRequest` through `BBDataProviderAddBulletin`. It preserves separate
title/body fields and sets an app-launch action and clearable flag. It neither
chooses a presentation controller nor overwrites notification preferences.

The candidate listens at:

```
/var/mobile/Library/Application Support/StrappyAlerts/provider-candidate.sock
```

**The daemon still sends to `receiver.sock`.** This separate endpoint is
intentional for the offline milestone; production traffic is not routed to the
candidate. No synthetic notification is sent at load time. There is no sender,
launchd job, Substrate filter, or installation script in the candidate archive.

The directory must be owned by the receiver user with mode 0700; the socket is
0600. A lifetime `flock` on the candidate's adjacent `.lock` file excludes a
second instance and allows stale socket recovery after process exit. Symlinks,
foreign ownership and permissive directories/lock files are refused. Shutdown
invalidates the run-loop source/socket before releasing the callback target and
only unlinks the socket with the recorded device/inode. Same-user processes
share the trust boundary. Reply text is not logged or written to disk.

## History and dismissal policy

- Keep at most 32 bulletins in memory, newest first in list queries. Eviction
  withdraws the old record from BulletinBoard.
- Stable record and publisher IDs derive from the daemon run ID. Remember up to
  256 recent IDs to suppress duplicate packets, including recently cleared or
  evicted bulletins. This is bounded suppression, not permanent deduplication.
- Implement both historical `clearedInfoForBulletins:` callback forms. Cleared
  info is a bounded array of record IDs, safe for system serialization. Remove
  those records from retained history and honor `lastCleared` on later queries.
  Unknown cleared-info formats yield no history rather than restoring alerts.
- Honor the requested count. Records have no custom filter groups; the filter
  argument does not partition this single class of completion alerts.
- History and deduplication state do not survive SpringBoard restart. The
  candidate never reconstructs alerts from chat storage or replays daemon work.
- Limit socket draining to eight datagrams per callback. Reject wrong packet
  sizes/version, missing terminators, invalid UTF-8 and noncanonical run IDs.
  The daemon remains responsible for preview limits and event eligibility.

History access is synchronized; publication/withdrawal occur outside the lock.
IPC publication runs on the main thread, following the successful probe. The
server owns the provider, and socket callbacks borrow it until invalidation.
The actual private API threading, teardown and filter contracts still require
native validation before production migration. Policy tests use fake private
classes, so they cannot establish these OS contracts.

The optional cleared-info signatures are documented in the historical
[Ryan Petrich provider template](https://gist.github.com/rpetrich/2340050).
Their use and opaque array round-trip on our iOS 6 device have not yet been
observed. In particular, validate individual dismissal, section clearing and
subsequent list queries, not just successful publication.

## Local validation

In the configured Altivec container, from the repository root:

```sh
make -C source/iOS -f notification_provider.mk \
  BUILD_ROOT="$PWD/build/experiments/notification-provider" clean release
make -C source/iOS -f notification_provider.mk \
  BUILD_ROOT="$PWD/build/experiments/notification-provider" validate-package analyze
```

The ZIP contains `StrappyNotificationProvider.bundle/Info.plist`, the custom
`StrappyChime.wav`, and its signed armv7/arm64 `MH_BUNDLE` executable. It is an offline artifact, not a DEB. The
build directory also retains matching dSYMs. Production packaging migration
belongs to the next milestone.

On a native Mac:

```sh
python3 source/daemon/tests/notification_provider_test.py
```

The fixture compiles the actual provider implementation with fake BulletinBoard
classes and functions. It tests content/app identity, duplicates, dismissal,
list limits, eviction/withdrawal, invalid packets, empty bodies, publication
exceptions, restart history policy, actual datagram reception, bounded burst
draining, exclusive receiver ownership, socket permissions, stale socket
recovery and symlink refusal. It uses an owned temporary directory and never
opens the production socket or accesses phone data.

## Next milestone

Prepare production packaging and migration with one receiver owning the real
socket. Remove the old owned dylib/filter and coordinate a SpringBoard restart;
do not install this candidate alongside a production provider for the same
section. Reuse the app's icon resources if system identity resolution proves
insufficient. Verify the real Strappy icon/name, app launch, settings, history,
clearing, repeated completions, restart behavior and lock/DND policy during an
agreed device window. This milestone does not establish native display or
production readiness, nor authorize phone installation.

## Validation recorded 2026-10-18

- Native policy/IPC fixture passed on macOS 15.7.3 arm64, with warnings treated
  as errors (excluding deprecation and the project's method-semicolon style).
- Candidate clean armv7/arm64 build and armv7 static analysis passed without
  diagnostics. ZIP inspection confirmed exactly the plist and signed executable,
  the correct principal class, and `MH_BUNDLE` in both Mach-O slices.
- Full offline Linux daemon/shared-core suite passed.
- Clean production release builds passed for iOS armv7/arm64 and macOS
  PPC/i386/x86_64/arm64 without compiler warnings. Mac ZIP and both iOS DEB
  variants passed package validation.
- No native iOS test, installation, respring or provider migration was performed.

Ignored logs are `build/notification-provider-build.log`,
`build/notification-provider-native-test.log`,
`build/notification-provider-offline.log`, and
`build/notification-provider-production-{clean,build,package}.log`.

## First candidate device test: 2026-10-18

The owner authorized testing on gomadango and confirmed the Home Screen before
and after the test. Installed the separate package built by
`source/iOS/notification_provider_device.mk`, version 0.1.0, SHA-256:

```
2c60b18ad281d157b763ed0b5b842eddf6afaca41303bf992c4f2a6ca9a65d96
```

Its three payload files are the native provider executable and plist beneath
`/System/Library/BulletinBoardPlugins/StrappyNotificationProvider.bundle`, and
`/usr/libexec/strappy-provider-test`. The sender accepts only test numbers 1–3,
uses fixed diagnostic text and high diagnostic run IDs, and targets only the
candidate socket. The package has no lifecycle scripts or production dependency.
The test runner is `source/daemon/tests/notification_provider_device/device-test.sh`.
Its `remove` action followed by an explicit `respring` is the rollback; a second
root SSH shell was available throughout. No production package was replaced.

**Corrected result: candidate readiness succeeded; notification display was not
tested.** The initial readiness-failure report below was premature. Later
retrieval of syslog established registration and socket readiness in the tested
SpringBoard process; see the diagnosis below.
After installation and one restart, SpringBoard PID changed from 1873 to 2512.
The immediate checks found no candidate socket and the captured syslog snapshots
contained no candidate registration or section-query messages. The production receiver did log readiness.
No test datagram was sent. This does not establish whether the bundle failed to
load, its initializer returned nil, or the server rejected/ignored its section:
the candidate has no class-load or initializer-entry logging. A
conflict with the existing Strappy section is only a hypothesis. The successful
isolated probe used a separate identity and resolved publication symbols later,
at send time; the candidate resolves both functions in its initializer. Those were investigation hypotheses, not established causes. The later evidence
below supersedes the failed-initialization interpretation.

Removed the test package and restarted SpringBoard once more (PID 2593).
The bundle and sender were absent; the original receiver logged readiness.
Production strappyd retained PID 1568 throughout. The app, daemon and two
Substrate files retained their four baseline SHA-256 hashes, the production
package remained 1.2.0, and crash-directory listings were unchanged. Shared
notification preferences were not modified. After removal, the owner confirmed
Strappy opens normally and has no entry in Settings > Notifications. This is a
post-removal observation, not a check during candidate installation. DND/mute
state was not explicitly reported, and no display/policy conclusion can be drawn.

Local evidence, the tested DEB and matching armv7 provider symbols are retained
under `build/device-runs/gomadango-provider-candidate-v1/`. Remote staging is
`/var/mobile/tmp_altivec/strappy-provider-candidate-v1/`. The test package is no
longer installed. Candidate policy tests, clean armv7/arm64 test-package builds,
static analysis and package validation passed. Full clean production builds
passed for both iOS and all four Mac architectures without compiler warnings;
production package validation also passed. Logs use
`build/notification-provider-device-{policy,build}.log` and
`build/provider-device-production-{clean,build,package}.log`.


## Diagnosis and corrected readiness test: 2026-10-18

The original candidate did load and register. A later read of the phone's syslog
contains these events in the original test process, SpringBoard PID 2512:

```
Oct 18 13:18:32 ... StrappyProvider: dataProviderDidLoad
Oct 18 13:18:32 ... StrappyProvider: default section requested
Oct 18 13:18:32 ... StrappyProvider: candidate receiver ready
Oct 18 13:18:32 ... StrappyProvider: list filter=0 count=5 cleared=none
```

The production receiver had logged readiness at 13:18:29. The test checked too
soon after restarting SpringBoard and treated the absence in the captured log
snapshots as a failure. These later-retrieved events establish candidate
registration, default section queries, IPC readiness and a bulletin-list query.
They do not establish visible notification delivery, icon resolution, tap
routing or dismissal: no notification was sent before the premature rollback.
The post-removal absence of a Settings entry says nothing about visibility
while the candidate was installed.

Evidence is retained as
`build/device-runs/gomadango-provider-candidate-v1/provider-syslog-late.log`.
No provider change was needed to establish this result.

Additional foreground diagnostics on gomadango, run as mobile from staging,
confirmed that the tested binary loads, the exception-runtime imports and both
publication/withdrawal symbols resolve, the bundle's principal class conforms
to the runtime BBDataProvider protocol, and its initializer succeeds with
CoreFoundation 793.00. They did not invoke server registration, publish, reinstall
anything or restart SpringBoard. The final reusable diagnostic is
`source/daemon/tests/notification_provider_device/loader.m`, built with
`source/iOS/notification_provider_loader.mk`. Pass a staged `.bundle` directory.
The loader intentionally leaves image teardown to process exit after releasing
the initialized candidate. It must not invoke `dataProviderDidLoad`.

Offline inspection of the retained iOS 6 BulletinBoard loader also showed that
`_loadDataProviderPluginBundle:` uses the principal class and protocol conformance,
and `_addDataProvider:sortSectionsNow:` calls `dataProviderDidLoad`. Its duplicate
section path logs a warning but continues registration; that hypothesis did not
explain the originally missing callback. Generated disassembly remains ignored
under `build/provider-*-annotated.txt`. The retained cache has rebased pointers;
analysis accounted for the 0x02003000 pointer slide rather than treating those
pointers as unslid virtual addresses.

The device runner now has an explicit `ready` action: poll for up to 30 seconds
using the sender's payload-free Unix datagram `connect` check, then report an
actual live receiver. `send` repeats this check before sending. A leftover socket
pathname is insufficient: a stale endpoint from the removed candidate remains
on the phone, and the new check correctly returned exit 1 against it. The next
provider startup already has lock-protected stale-socket recovery. Syslog is
supporting evidence, not the readiness gate. After a future install/respring,
run `sh device-test.sh ready` before asking the owner to observe a notification.

Native macOS tests now cover absent, stale and live endpoints, verify that the
readiness check sends no datagram, and check that an explicit test send still
produces the expected packet. No further SpringBoard restart or notification
test occurred during diagnosis. Production hashes and SpringBoard/daemon PIDs
were unchanged by the foreground checks. Logs are
`build/provider-{loader,bundle,receiver,readiness}-native.log`,
`build/provider-readiness-tests.log`, `build/provider-loader-build.log`,
`build/provider-readiness-build.log`, and
`build/provider-diagnosis-production-{clean,build,package,state}.log`.

Final diagnosis checks: native macOS policy/readiness tests passed; clean
armv7/arm64 diagnostic and test-package builds, static analysis and package
validation passed. Full clean production builds for both iOS and all four Mac
architectures and production package validation passed without compiler warnings.


## Visible notification retry and custom chime: 2026-10-18

Retried with package 0.1.0 from commit `321ff20`, SHA-256
`ee05624b40581b40d4ccbcfca63b5c057494c353d5c122fb914cfe05da7a58e0`,
using fresh staging `strappy-provider-candidate-v2`. The updated live-socket
readiness gate passed in SpringBoard PID 2776. With the phone unlocked on the
Home Screen and DND explicitly off, sent test 1 at 13:43:58. The owner confirmed
its Notification Center entry had Strappy's correct name and icon. The owner
was not watching for the first banner, so explicitly requested test 2; after
publication at 13:44:55, the owner confirmed seeing its banner. The owner also
confirmed the Strappy entry in Settings > Notifications and enabled banners
and sounds there. No app-owned code changed those preferences.

At the owner's request, added an original 0.65-second two-note chime, mono
16-bit PCM WAV at 22,050 Hz. The provider lazily creates a SystemSoundID from its
bundle resource, then uses the runtime-checked BBSound factory
`alertSoundWithSystemSoundID:` and attaches that sound to the bulletin. Playback
is still controlled by system settings. It does not play sound independently
through AudioServices, change volume or force sound through the silent switch.
The retained iOS 6 framework metadata supplied the factory signature. The sound
object and SystemSoundID are owned until provider teardown; history is released
before disposing the sound. Missing resources/APIs leave the alert silent.

The separately packaged candidate is now version 0.1.1. Upgraded only the test
package with SHA-256
`68d6b3ef162c31b93a33e8f449da4ec647d54475c755c300cdcc911d992f6bb0`,
using fresh staging `strappy-provider-candidate-v3`, then restarted SpringBoard
once (PID 2972). The live-socket check passed. The owner confirmed the Home
Screen again; silent-switch/volume state was requested but not separately
reported. Sent test 3 at 13:47:42. Logs show `custom sound attached` and
`publication returned`. These logs do not prove audible playback or visibility;
owner confirmation for that send and for tapping to open the app is pending.

Production file fingerprints and package version remained unchanged, strappyd
retained PID 1568, and no new crash reports appeared at the pre-cleanup checks.
The test package remains installed pending observation and cleanup. Matching
packages, armv7 symbols and logs are retained below
`build/device-runs/gomadango-provider-candidate-v2/` and `...-v3/`; remote staging
uses the same directory basenames under `/var/mobile/tmp_altivec/`.

Policy/readiness tests passed, including sound attachment through the actual
provider path with fake private classes. Clean candidate builds, static analysis
and ZIP/DEB validation passed for armv7/arm64. Full clean production builds and
package validation passed for both iOS and all four Mac architectures without
compiler warnings. Logs use `build/provider-sound-*.log`.


## Built-in sound comparison: 2026-10-18

The owner reported a visible banner and two Notification Center entries after
retrying the custom chime at 16:01:32, but no sound or badge. They subsequently
confirmed the silent switch was off and Ringer and Alerts volume audible.
Logs showed successful sound attachment and publication, with no reported
playback error. These messages do not establish playback. No badge update is
implemented; bulletin history alone does not set an application badge.

At the owner's request, candidate 0.1.2 substitutes built-in SystemSoundID 1007
for the dynamically created custom resource ID. It retains the same runtime
BBSound factory and bulletin publication path. The system owns this ID, so the
provider does not dispose it. The WAV remains in source for the experiment but
is no longer packaged. No independent audio playback or settings changes were
introduced. The native fixture now exercises the actual factory invocation
using a fake BBSound class rather than overriding notificationSound.

Upgraded the isolated test package, SHA-256
`f5fe7546c80565847c49ca84d53cf207eba4024d87bced5bc54101968f5d5a1a`,
in `strappy-provider-candidate-v4`, and restarted SpringBoard once (PID 3247).
Live socket readiness passed. After the owner confirmed the Home Screen and
silent mode disabled, sent test 1 at 16:10:14. Logs showed built-in sound 1007
attached and publication returned. **The owner confirmed both banner and
sound.** This confirms that this built-in sound can play through the provider; the
precise cause of the earlier failure remains unresolved. It does not test sound omission or establish
that a bulletin without a sound receives any automatic default.

Candidate 0.1.2 was subsequently superseded by the Glass test below.
Production hashes/package version were
unchanged. Daemon routing still uses the production receiver; this successful
candidate test does not complete the production migration. Tap-to-open and
badge lifecycle remain unverified/unimplemented respectively.

Native macOS policy/readiness tests passed. Clean armv7/arm64 candidate builds,
static analysis and ZIP/DEB validation passed. Full clean production builds
and package validation passed for both iOS architectures and all four Mac
architectures, without compiler warnings. Logs use
`build/provider-default-*.log`. Matching test package, armv7 symbols and device
logs are retained in `build/device-runs/gomadango-provider-candidate-v4/`;
remote staging uses the same basename under `/var/mobile/tmp_altivec/`.


## Alternate built-in Glass tone: 2026-10-18

The owner identified the successful 1007 test as the default sound and asked
for a different built-in tone. Candidate 0.1.3 selects SystemSoundID 1009
(Glass) through the same BBSound factory. Its system asset
`/System/Library/Audio/UISounds/sms-received3.caf` exists on gomadango.
The package SHA-256 is
`54ed9ff116c0ef7be750dba057f812609ea978b68f0603768a0727b9d306abdf`.
Installed it in staging `strappy-provider-candidate-v5` and restarted
SpringBoard once (PID 3373). Live socket readiness passed.

After the owner confirmed Home Screen and sound enabled, sent test 1 at
16:11:37. The owner reported **banner, no sound**. Logs showed sound 1009
attached and publication returned, with no playback confirmation. Sent test 2
at 16:11:58 to check repeatability after startup; owner observation is pending.
Thus a different built-in tone has not yet been validated. The earlier custom
WAV failure cannot be explained solely by its being a custom resource.

Candidate 0.1.3 remains installed, with no production routing changes. Production
file hashes/package version remained unchanged. Matching package, armv7 symbols
and logs are retained in `build/device-runs/gomadango-provider-candidate-v5/`.
Native macOS tests, clean candidate armv7/arm64 builds, analysis and package
validation passed. Full clean iOS and four-architecture Mac production builds
and package validation also passed without compiler warnings. Logs use
`build/provider-glass-*.log`. These build results do not establish audible
playback; only the 1007 comparison has owner-confirmed sound so far.
