# Native Strappy notification provider candidate

## Current checklist (2026-10-19)

This is the authoritative status checklist for the notification investigation on
`experiment/ios-sandbox-ipc`. Checked native results apply to gomadango
(iOS 6.1.3, armv7) unless another target is named. Historical entries below record
what was known at the time; their pending statements do not override this list.

### Completed

- [x] Demonstrate notifications without converting Strappy to a sandboxed IPA.
- [x] Load a native BulletinBoard provider and replace the Substrate receiver.
- [x] Confirm Strappy name/icon, notification settings, banners, lock-screen
  presentation, Notification Center history, and built-in Glass sound.
- [x] Confirm tap-to-open, dismissal, clearing, repeated delivery and restart
  behavior through owner observation.
- [x] Deploy the production migration, verify package ownership/socket readiness,
  remove the candidate package, and retain rollback packages and matching symbols.
- [x] Confirm real-prompt completion through the production provider.
- [x] Implement durable unread-session state and acknowledged/retried badge
  updates; confirm badge transitions 2 → 1 → cleared by opening sessions.
- [x] Confirm observed-session suppression and alerts for another session while
  Strappy remains foregrounded.
- [x] Confirm completion while the UI is suspended or terminated and correct
  processing/unread row icons (owner confirmation, 2026-10-19).
- [x] Run offline policy/execution tests, clean builds for both iOS and all four
  Mac architectures, provider analysis and rootful/rootless package checks.
- [x] Test candidate loading and actual badge readback on iOS 8.4.1 armv7;
  this preceded the production visual test below.
- [x] Confirm production background notification, sound, unread badge and
  clearing on opening the session on koolphone5 (iOS 8.4.1 armv7, 2026-10-19).
- [x] Confirm observed-session suppression on koolphone5: no banner or sound
  while staying in the completing session.
- [x] Confirm foreground alerts for another session and unread-session badge
  transitions 2 → 1 → cleared on koolphone5.
- [x] Confirm lock-screen presentation, tap-to-open, notification settings and
  Notification Center history/dismissal/clearing on koolphone5.
- [x] Complete koolphone5 production acceptance: badge 1 survives SpringBoard
  restart and a fresh completion afterward delivers notification, sound and badge.

### Outstanding compatibility work

- [ ] Repeat production visual acceptance on other intended supported versions,
  including arm64 and rootless installations. Cross-builds and package checks
  alone do not establish native compatibility.
- [ ] Resolve the iOS 5 notification support boundary: the provider rejects
  pre-iOS-6 runtimes. Implement and validate support or document the limitation.

### Optional investigations, not blockers for the demonstrated iOS 6 behavior

- [ ] Retest the custom WAV with silent mode off, if custom sound is wanted.
  Built-in Glass is already confirmed; the earlier WAV result was inconclusive.
- [ ] Trace private threading/teardown and optional cleared-info callback
  contracts if deeper API evidence is needed. Observable lifecycle behavior and
  bounded policy tests are complete, but do not prove every private contract.
- [ ] Establish enforced sandbox access only if an IPA conversion is pursued.
  The original sandbox probe did not prove enforcement and is not needed for
  the working DEB notification route.

### Deployment and evidence

On 2026-10-19, production build `ad47a44` was installed on koolphone5;
[its deployment and visual test record](session-unread-notification-plan.md#koolphone5-production-acceptance-2026-10-19)
records the completed production acceptance checks.

The latest gomadango installation recorded in these documents is `b881428`,
following `6a239a9` (provider migration), `f37bf03` (unread/badges), and `02b1b34`
(synchronous Send). This is a documentary record, not a fresh device inventory.
The 2026-10-19 owner confirmation supplies acceptance evidence without a new
build fingerprint or instrumented suspension trace.

See the [policy and acceptance record](session-unread-notification-plan.md),
[migration and rollback procedure](ios6-provider-migration.md), and
[original sandbox results](ios-sandbox-experiment.md). Detailed chronological
records below preserve observations and corrections; they are historical.

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

**The daemon sends to `receiver.sock`, now owned by the production provider.**
The explicit diagnostic Makefiles retain the separate candidate endpoint;
production traffic is never routed to it. No synthetic notification is sent at load time. There is no sender,
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
The owner confirmed the observable native lifecycle behavior on 2026-10-18.
This does not independently establish every private threading/teardown contract.
Policy tests use fake private classes, so they cannot establish OS contracts.

The optional cleared-info signatures are documented in the historical
[Ryan Petrich provider template](https://gist.github.com/rpetrich/2340050).
The owner has confirmed dismissal and clearing behavior. The exact optional
callback form and opaque array round-trip have not been independently traced.

## Local validation

In the configured Altivec container, from the repository root:

```sh
make -C source/iOS -f notification_provider.mk \
  BUILD_ROOT="$PWD/build/experiments/notification-provider" clean release
make -C source/iOS -f notification_provider.mk \
  BUILD_ROOT="$PWD/build/experiments/notification-provider" validate-package analyze
```

The ZIP contains `StrappyNotificationProvider.bundle/Info.plist` and its signed
armv7/arm64 `MH_BUNDLE` executable. The custom WAV remains in source but is not
packaged; the selected Glass sound is supplied by iOS. The ZIP is an offline
artifact, not the separately built device-test DEB. The
build directory also retains matching dSYMs. Production packaging migration was subsequently completed; see the current
checklist above.

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

## Production migration preparation (2026-10-18)

The production Makefile now packages the native provider at
`/System/Library/BulletinBoardPlugins/StrappyNotificationProvider.bundle`
in place of `StrappyBulletin.dylib` and its Substrate filter. The provider uses
`receiver.sock`; strappyd and the STN1 protocol are unchanged. Glass (1009),
Strappy identity, bounded memory-only history and runtime iOS 6 gating remain.
Production has no diagnostic badge code or unread-count policy. Explicit
diagnostic builds retain `provider-candidate.sock` and the literal badge probe.

The package preflight refuses an installed/unpacked test-provider package before
unpacking the shared bundle path. Remove that package with dpkg first. Upgrading
the Strappy package lets dpkg remove the old owned dylib/filter; no script
manually deletes system files. The provider also refuses an existing live socket
even when its owner does not hold the new lock (the legacy receiver). Dead
sockets can be recovered after restart. Installation prints a restart reminder;
it does not restart SpringBoard automatically. See the
[cutover and rollback procedure](ios6-provider-migration.md).

This is prepared code/package work, not a completed device cutover. The last
recorded device state above remains unchanged until deployment.

Validation for this preparation:

- Full clean production release builds and package validation passed for iOS
  armv7/arm64 and macOS PPC/i386/x86_64/arm64, without compiler warnings.
- Clean diagnostic ZIP and device-test DEB builds, static analysis and package
  validation passed; production-provider static analysis also passed.
- The offline Linux suite passed. Package tests passed for both production DEBs:
  provider present, old receiver/filter and diagnostic sender absent, production
  socket selected, literal badge code absent, and candidate preflight ordered
  before the new install script's service stop. Preflight tests cover installed,
  unpacked, broken, absent and removed packages, plus query failures.
- Both production and diagnostic native macOS fixtures cross-compiled with
  warnings treated as errors. Native execution was skipped because the configured
  Mac hostname `ichigodaifuku.local` did not resolve. The updated fixture covers
  refusal of a live receiver without a lock, payload-free probing, stale-socket
  recovery, and absence of diagnostic badge updates in production.
- No phone installation, notification send or SpringBoard restart was performed.
  The owner reported seeing a notification during preparation; it is not evidence
  of this migration, which was still local.

Logs are retained under `build/provider-migration-*.log`. Run the package test
with `python3 source/daemon/tests/notification_package_test.py` followed by the
rootful and rootless production DEB paths to repeat its artifact checks.

## Original next milestone

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

Candidate 0.1.3 was subsequently replaced by 0.1.4 below. Production
file hashes/package version remained unchanged. Matching package, armv7 symbols
and logs are retained in `build/device-runs/gomadango-provider-candidate-v5/`.
Native macOS tests, clean candidate armv7/arm64 builds, analysis and package
validation passed. Full clean iOS and four-architecture Mac production builds
and package validation also passed without compiler warnings. Logs use
`build/provider-glass-*.log`. These build results do not establish audible
playback; only the 1007 comparison has owner-confirmed sound so far.


## Restore the confirmed default tone: 2026-10-18

Glass test 3 at 16:13:38 also produced a banner without sound, as confirmed
by the owner. At their request, candidate 0.1.4 restores explicit built-in
SystemSoundID 1007, the setting previously confirmed audible. It does not
omit the sound or assume iOS supplies one automatically. Alternate built-in
and custom-resource playback remain unresolved and outside this restoration.

Installed package SHA-256
`aba79b21916afc4b5199a4807c037fd472073505a0b266d68067baf41acf35d7`
using staging `strappy-provider-candidate-v6`. Restarted SpringBoard once
(PID 3559), and live socket readiness passed. After the owner confirmed the
Home Screen, sent test 1 at 16:15:26. Logs show sound 1007 attached and
publication returned. The owner then discovered the phone was on silent,
so this observation and earlier silent results cannot establish a sound defect.
Production file hashes and package version remained unchanged. Candidate 0.1.4
was subsequently replaced for the Glass retest below; production daemon
routing is still unchanged.

Matching package, armv7 symbols and device logs are retained in
`build/device-runs/gomadango-provider-candidate-v6/`. Build/test logs use
`build/provider-restore-*.log`.


## Silent-switch correction and Glass retest: 2026-10-18

The owner discovered silent mode was enabled and asked to retry Glass. This
supersedes earlier assertions that silent mode was off: the earlier no-sound
observations are inconclusive, including the custom chime. There is not enough
evidence to conclude either custom resources or alternate built-in sounds are
broken. Earlier entries above retain the chronological observations only.

Reloaded the exact previously built and validated 0.1.3 Glass package from
v5, SHA-256 `54ed9ff116c0ef7be750dba057f812609ea978b68f0603768a0727b9d306abdf`,
in fresh staging `strappy-provider-candidate-v7`. Reverted the uncommitted
1007 restoration in source to the already committed 1009 implementation.
The intervening 0.1.4 clean builds, tests, analysis and package checks passed,
but that version is not the final selection. No new implementation changes
are required to repeat the Glass test.

Restarted SpringBoard once (PID 3691), and live socket readiness passed. The
owner explicitly confirmed Home Screen and silent mode disabled. Sent test 1
at 16:16:52; logs show sound 1009 attached and publication returned. **The
owner confirmed the Glass test worked.** Glass is the final selected sound;
0.1.3 remains installed. Production hashes and package version are unchanged.
The custom WAV has not been retested with the corrected switch state and must
not be described as a confirmed implementation failure.

Matching package, symbols and device logs are retained in
`build/device-runs/gomadango-provider-candidate-v7/`. The existing validated
Glass binary was reused without rebuilding; the final code matches commit
`7c3a5ed`. This follow-up changes only the test record. Production daemon
migration, badge lifecycle and tap-to-open validation remain separate work.


## Literal badge-number probe: 2026-10-18

The owner requested only proof of badge display, with the diagnostic test
number as the badge (4, 5, 6, 7, ...), and confirmed sound/badges enabled in
Settings. Candidate 0.1.5 adds a main-thread bridge through
`SBIconController.sharedInstance`, `model`,
`applicationIconForDisplayIdentifier:`, and the icon's object-valued
`setBadge:`. These selectors/signatures were checked against gomadango's
retained SpringBoard metadata; its own badge forwarding method uses this
same icon-model route. Runtime selector checks guard unavailable APIs.

Only reserved diagnostic run IDs `9000000000000000004` through
`9000000000000000099` set badges. The sender now accepts tests 1..99; tests 1..3
and ordinary packets do not change the badge. After bulletin publication, the
provider sets the literal decimal test number on Strappy's icon and logs
whether `badgeNumberOrString` reads back the requested value. Readback does
not prove visible display. Badge exceptions are isolated from publication.
The existing duplicate guard prevents repeated packets changing it again.
This is a diagnostic value, not an unread count; no clearing/history policy
is introduced and this probe must not migrate into production daemon routing.

Installed package 0.1.5, SHA-256
`36abdc02dea4775454e86933e3b7de72cd79b167af76151e207d5ec58bdffbc8`,
using staging `strappy-provider-candidate-v8`, with one SpringBoard restart.
Live socket readiness passed; production hashes/package version were unchanged.
The owner subsequently confirmed the Home Screen and visible badge 4, as
recorded below.

Native macOS fixtures exercise the actual badge bridge with a fake icon,
checking Strappy identity, literal values 4..7, no ordinary-packet mutations,
and duplicate suppression. Sender tests also cover diagnostic IDs 4..7/99 and
reject 100. Matching package, armv7 symbols and device logs are retained in
`build/device-runs/gomadango-provider-candidate-v8/`. Build/test logs use
`build/provider-badge-*.log`.

Native fixtures passed. Clean candidate armv7/arm64 builds, static analysis,
and ZIP/DEB validation passed. Full clean production builds for both iOS
architectures and all four Mac architectures and package validation passed
without compiler warnings. The test package remains installed.

After the owner confirmed the Home Screen, sent test 4 at approximately
16:22:20. SpringBoard PID 3818 logged publication and `test badge 4 set;
readback matches=1` at 16:22:19. **The owner confirmed Strappy's icon visibly
showed 4.** This establishes badge display through the candidate's icon-model
bridge, alongside the earlier banner, Notification Center and Glass sound
confirmations. Badge 4 remains set; replacement/clearing and production unread
count policy were not tested or implemented by this probe.


## Production device cutover: 2026-10-18

At the owner's explicit request, installed the production rootful package built
from `6a239a9` (package version remains 1.2.0), SHA-256:

```
0d4cc50446a9655235b33cce887e012101aa2d1e1b805f161a187dfda5971dd0
```

Before changing the phone, compared every installed production file against the
retained `ac50d5f` rollback DEB and every candidate file against candidate 0.1.5;
both matched. Staged all three packages and their checksums in
`/var/mobile/tmp_altivec/strappy-provider-production-6a239a9/`, with an independent
root SSH recovery session open. Removed the candidate with dpkg, installed
production with dpkg, and verified every installed payload hash. dpkg now assigns
the provider bundle to Strappy. The old owned dylib/filter and diagnostic sender
are absent. No unrelated test packages were removed.

Restarted SpringBoard once: PID 3818 became 4046. strappyd restarted from PID
1568 to 4031. At 16:39:55 device time, the new SpringBoard process logged
`dataProviderDidLoad` and readiness at the production `receiver.sock`, followed
by a history-list query. A staged sender built with its socket override set to
`STRAPPY_NOTIFICATION_SOCKET` was used only for its payload-free `ready` check
as mobile; it passed. Its inherited message says "Candidate receiver accepts
connections", but this binary targets the production socket. No diagnostic
notification was sent. Readiness took time after restart; a stale pathname was
not treated as success. Crash-report listings were unchanged.

Matching production armv7 app/daemon/provider symbols, packages, payload
manifests, install logs, readiness output, service state, and syslog are retained
under `build/device-runs/gomadango-provider-production-6a239a9/`. The owner subsequently reported that the real-prompt notification worked. Installation/readiness
does not by itself prove end-to-end completion delivery. Rollback packages remain
staged; no chat data or notification preferences were deliberately modified.


The simplified unread/observation design and foreground/badge-only experiments
are recorded in [the session notification plan](session-unread-notification-plan.md).
