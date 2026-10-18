# Native Strappy notification provider candidate

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

The ZIP contains `StrappyNotificationProvider.bundle/Info.plist` and its signed
armv7/arm64 `MH_BUNDLE` executable. It is an offline artifact, not a DEB. The
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

**Result: candidate readiness failed; notification display was not tested.**
After installation and one restart, SpringBoard PID changed from 1873 to 2512.
The candidate socket was absent and there were no candidate registration or
section-query messages in syslog. The production receiver did log readiness.
No test datagram was sent. This does not establish whether the bundle failed to
load, its initializer returned nil, or the server rejected/ignored its section:
the candidate currently has no class-load or initializer-entry logging. A
conflict with the existing Strappy section is only a hypothesis. The successful
isolated probe used a separate identity and resolved publication symbols later,
at send time; the candidate resolves both functions in its initializer. Inspect
those differences and add early lifecycle diagnostics before another test.

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
