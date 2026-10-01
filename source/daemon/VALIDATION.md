# Step 0 validation — 2026-10-01

The implementation is complete; the original **iOS 5 acceptance gate remains
open**. `koolphone5` is an iPhone 5 (`iPhone5,2`, armv7) running **iOS 8.4.1,
build 12H321**, with a rootful jailbreak. Its package inventory reports
`com.lukezgd.everuntether-bin 1.3.6`. Rootless iOS 15.0/Dopamine was subsequently tested on `pinkphone-rsa`; see
the rootless section below. No harness migration has started.

## Build and offline checks

- `make -C source clean release`: passed, no compiler warnings or errors;
  armv7/arm64 iOS `.deb` packages and PPC/i386/x86_64/arm64 macOS app.
- `make -C source/linux clean test`: passed, including `daemon_harness.py`.
  The daemon test checks immediate SIGTERM wakeup, startup/shutdown identity,
  replacement of the one rotated backup, 0600 log permissions, and failure
  for relative or unwritable/missing paths. It does not accelerate the real
  60-second heartbeat interval.
- Clang static analysis of `source/daemon/strappyd.c` on Linux and the armv7
  iOS 5 target: no findings.
- `llvm-objdump --macho --universal-headers --dylibs-used` confirms both daemon
  slices and only `/usr/lib/libSystem.B.dylib` as a runtime dependency.
- Parsed both staged launchd plists, checked absolute rootful/rootless paths,
  `mobile` user/group, executable mode 0755, and `sh -n` on all package scripts.

Full local build/test output was captured in `/tmp/strappy-step0-build.log`
and `/tmp/strappy-step0-linux.log`; these generated logs are not committed.

## Device procedure and observations

Package: `com.altivecintelligence.strappy` **1.1.2**, normal rootful `.deb`.
All timestamps below are UTC from the phone. Installation used:

```sh
scp -O source/iOS/build-release/Strappy-rootful.deb koolphone5:/var/tmp/strappy-step0.deb
ssh koolphone5 'dpkg -i /var/tmp/strappy-step0.deb'
ssh koolphone5 'launchctl list com.altivecintelligence.strappyd; cat /var/log/strappy/heartbeat.log'
```

An initial SSH outage interrupted deployment; overlapping retries produced a
transient dpkg lock error. The active installation completed, and a subsequent
`dpkg -s` confirmed `Status: install ok installed`. No second service appeared.
A temporary signed, native `sysctl(KERN_PROC_ALL)` probe counted processes
named `strappyd` and `Strappy` because this device does not provide `ps`.

### Installation and awake heartbeat

Without opening Strappy, launchd started exactly one daemon, PID **637**, as
**UID 501 (`mobile`)**. Auto-lock was initially on; it was temporarily disabled
with `switch off com.a3tweaks.switch.autolock`, and the screen was unlocked using
`activator send libactivator.lockscreen.dismiss`.

```text
2026-10-01T08:12:06Z event=startup pid=637 euid=501 heartbeat=0
2026-10-01T08:13:11Z event=heartbeat pid=637 euid=501 heartbeat=1
2026-10-01T08:14:12Z event=heartbeat pid=637 euid=501 heartbeat=2
2026-10-01T08:15:12Z event=heartbeat pid=637 euid=501 heartbeat=3
```

Intervals were 65, 61, and 60 seconds; the phone was initially locked until
08:12:45. The process probe showed no Strappy UI process before this check.

### UI independence

At 08:15:14–08:15:19, these commands opened the UI (PID 733), returned to the
home screen, and terminated it:

```sh
activator send com.altivecintelligence.Strappy
activator current-app
activator send libactivator.system.homebutton
killall Strappy
```

The process probe showed exactly one daemon, PID **637**, throughout, and no
remaining UI process after termination. Later heartbeats retained its counter.

### Locked-screen interval and forced restart

`activator send libactivator.lockscreen.show` locked the phone at **08:15:26**.
No SSH activity occurred during the next three minutes. At **08:18:42**,
`activator current-mode` still reported `lockscreen`; the screen was then
unlocked with `activator send libactivator.lockscreen.dismiss`.

```text
2026-10-01T08:15:12Z event=heartbeat pid=637 euid=501 heartbeat=3
2026-10-01T08:18:20Z event=heartbeat pid=637 euid=501 heartbeat=4
```

The **188-second gap** shows delayed scheduling while locked, consistent with
sleep; no startup/restart occurred, and the counter continued. There is no
claim of continuous CPU execution or a guaranteed 60-second cadence during
sleep. This validation did not instrument the device's power state.

At **08:18:43**, `killall -9 strappyd` caused launchd to start exactly one
replacement, PID **765**, with `LastExitStatus = 9` for the old instance. A
process probe two seconds later confirmed one daemon and no UI process:

```text
2026-10-01T08:18:43Z event=startup pid=765 euid=501 heartbeat=0
```

Auto-lock was restored to its original `on` setting before reboot testing.

### Reboot

`/sbin/reboot` was issued at **08:18:56**. The next kernel boot time was
**08:19:17 UTC** (`kern.boottime` epoch 1790842757). The daemon started at
**08:19:36**, approximately 19 seconds after boot:

```text
2026-10-01T08:19:36Z event=startup pid=80 euid=501 heartbeat=0
2026-10-01T08:20:37Z event=heartbeat pid=80 euid=501 heartbeat=1
2026-10-01T08:22:10Z event=heartbeat pid=80 euid=501 heartbeat=2
```

SSH became reachable again by **08:22:15**. No agent-issued jailbreak
reactivation or UI launch was needed. The first-unlock timing was not
observable from SSH; at reconnect Activator reported `springboard`, so this
is **not proof of startup before first unlock**. The device's installed
EverUntether package is the recorded jailbreak context, not a guarantee for
other jailbreaks. The process probe, recopied after reboot cleared `/var/tmp`,
confirmed only PID 80 and no Strappy UI process at 08:22:38.

### Reinstall, removal, restoration, and SIGTERM

After recopying the package (reboot clears this phone's `/var/tmp`),
`dpkg -i /var/tmp/Strappy-rootful.deb` ran from **08:22:38–08:22:54**. PID 80
stopped, and exactly one new daemon, PID **592**, started at **08:22:48**.
`dpkg -s` reported `install ok installed`, version 1.1.2.

`dpkg -r com.altivecintelligence.strappy` ran from **08:23:10–08:23:17**.
The process probe returned no daemon; `launchctl list
com.altivecintelligence.strappyd` reported no service, and both the launchd
plist and executable were absent. At **08:24:24** (67 seconds later), there
was still no process or job, and the log had no additional records. Diagnostic
logs and existing user app data were retained.

The normal package was restored with `dpkg -i /var/tmp/Strappy-rootful.deb`.
Explicit `killall -TERM strappyd` then verified cooperative shutdown on the
actual device:

```text
2026-10-01T08:24:26Z event=startup pid=648 euid=501 heartbeat=0
2026-10-01T08:24:32Z event=shutdown pid=648 euid=501 heartbeat=0
2026-10-01T08:24:36Z event=startup pid=657 euid=501 heartbeat=0
```

The package unloads and reboot did **not** produce shutdown records on this
OS; explicit SIGTERM did. The exact signal behavior of those launchd teardown
paths was not instrumented. Future service work must not assume every unload
or reboot provides time for cooperative cleanup.

At **08:24:44**, final checks showed `install ok installed`, one daemon
(PID **657**, UID **501**), no Strappy UI process, launchd `LastExitStatus = 0`,
a mobile-owned 0700 log directory, a mobile-owned 0600 log, and auto-lock `on`.
The temporary process probe and copied package were removed after testing.

## Remaining acceptance gates

- Run the full checklist on an actual **iOS 5 rootful device**. The unchecked
  iOS 5 acceptance items in `PLAN.md` are intentional.
- Complete the rootless UI/screen-state and reboot/first-unlock coverage
  identified below; installation and service lifecycle checks now have actual
  iOS 15.0/Dopamine device coverage.
- Establish first-unlock/jailbreak activation dependencies for each target
  configuration; this run does not resolve first-unlock timing.
- Do not infer uninterrupted locked-device work from process survival: the
  measured 188-second gap is a concrete limit of this experiment.

## Rootless follow-up: pinkphone-rsa, iOS 15.0

On 2026-10-01, tested the normal **1.1.2 rootless `.deb`** on an iPhone SE
(first generation), `iPhone8,4`, **iOS 15.0 build 19A346**. `/var/jb` resolves
to a Dopamine/Procursus installation under `/private/preboot`; installed
packages include `dopamine-basebin-link`, `libkrw0-dopamine`, and
`libroot-dopamine`. SSH logs in as mobile; package and system-launchd operations
used passwordless `sudo -n`.

The device has no `/bin/sh` or `/bin/launchctl` files. Package scripts now use
the layout's interpreter and launchctl paths explicitly: `/var/jb/bin/sh` and
`/var/jb/bin/launchctl` for rootless, retaining `/bin/...` for rootful. Their
PATH includes the corresponding jailbreak tools before stock system paths.
No daemon C changes were needed.

Validation commands included:

```sh
scp -O source/iOS/build-release/Strappy-rootless.deb pinkphone-rsa:/var/tmp/strappy-rootless-test.deb
ssh pinkphone-rsa 'sudo -n dpkg -i /var/tmp/strappy-rootless-test.deb'
ssh pinkphone-rsa 'sudo -n /var/jb/bin/launchctl print system/com.altivecintelligence.strappyd'
ssh pinkphone-rsa 'cat /var/jb/var/log/strappy/heartbeat.log'
ssh pinkphone-rsa 'ps -A -o pid,user,comm | grep -E "Strappy|strappyd"'
```

The package upgraded the existing 1.1.1 installation. Without opening the UI,
launchd started exactly one daemon as **mobile, UID 501**, PID **3058**:

```text
2026-10-01T08:28:05Z event=startup pid=3058 euid=501 heartbeat=0
2026-10-01T08:29:05Z event=heartbeat pid=3058 euid=501 heartbeat=1
2026-10-01T08:30:05Z event=heartbeat pid=3058 euid=501 heartbeat=2
2026-10-01T08:31:05Z event=heartbeat pid=3058 euid=501 heartbeat=3
```

All three intervals were exactly 60 seconds. Screen/power state was not
independently verified, so these intervals do not establish sleep behavior.

- `killall -TERM strappyd` at **08:31:25** logged shutdown with counter 3 and
  launchd started PID **3121**. `killall -9 strappyd` then caused one replacement,
  PID **3128**, to start at **08:31:35**, after launchd throttling.
- Reinstall and removal used `sudo -n dpkg -i` and `sudo -n dpkg -r
  com.altivecintelligence.strappy`. Removal completed at **08:31:52** with no
  daemon, no system job, and no executable/plist. At **08:33:01**, 69 seconds
  later, it remained unloaded. The diagnostic log was retained.
- Restoration started PID **3229** at **08:33:05**. Another reinstall replaced
  it with exactly one process, PID **3277**, starting at **08:33:44**. The
  transient half-configured state observed during that reinstall resolved
  normally; at **08:34:07** dpkg reported `install ok installed`, version 1.1.2.
- Per the user's instruction, **no reboot was performed**. Instead,
  `sudo -n /var/jb/usr/bin/sbreload` ran at **08:34:07**. SpringBoard changed
  from PID **487** to **3310**, while the daemon retained PID **3277**.
  Its next record was `2026-10-01T08:36:01Z event=heartbeat pid=3277 euid=501
  heartbeat=1`, 137 seconds after startup. This confirms continued execution
  after respring, with a scheduling gap; the device power state was not
  instrumented, so the gap is not assigned a specific cause.

The rootless package was left installed. UI launch requests through `uiopen
--bundleid com.altivecintelligence.Strappy` did not produce a Strappy UI process,
although the app was registered in `uicache -l`. No manual UI confirmation was
available during this run; UI foreground/background/termination and explicit
lock/unlock checks remain unverified on this device. The daemon works without
a UI process. Reboot, jailbreak reactivation, and first-unlock behavior remain
untested; respring survival is not reboot validation.

After the path fix, `make -C source clean release` passed for all required
Apple architectures with no warnings/errors, and `make -C source/linux
daemon-test` passed. Both staged package variants passed shell syntax,
interpreter/launchctl path, and plist checks. Full local logs were captured in
`/tmp/strappy-rootless-build.log` and `/tmp/strappy-rootless-device.log` and are
not committed.

## Mac follow-up: x4-vm, Tiger 10.4.11

Tested on `x4-vm`, **Mac OS X 10.4.11 build 8S165, PowerPC**, as user `me`
(**UID 501**). All timestamps here come from the VM's UTC clock; that clock
was ahead of the build host. Reference implementation inspected:
`Retro-Cloud-Sync/source/macOS-app/RCServiceController.m` and
`source/make/daemon.mk`. The Mac implementation uses the same stable per-user
installation/LaunchAgent pattern. No Retro-Cloud-Sync files or jobs were changed.

The test app was extracted under `~/Desktop/Strappy-Daemon-Step0/Strappy.app`.
The pre-existing `/Applications/Strappy.app` was not overwritten. The installed
service and log locations are documented in `README.md`.

The first device run found a Tiger loader failure (`_fcntl$UNIX2003`) caused
by defining `_POSIX_C_SOURCE` in the portable daemon source. That macro is now
supplied only by the Linux daemon build, allowing Apple SDK deployment targets
to select their own libc ABI. Tiger also revealed that a captured output pipe
can stay open when `launchctl load` spawns a per-user launchd. Load/unload now
use the null device for output; only `launchctl list` is captured. Both fixes
were tested on the real target before recording the results below.

Installation used the actual bundled app executable:

```sh
"$HOME/Desktop/Strappy-Daemon-Step0/Strappy.app/Contents/MacOS/Strappy" --install-daemon
launchctl list
cat "$HOME/Library/Logs/Strappy/heartbeat.log"
```

Without opening the UI, launchd started one daemon, PID **9974**, with these
records:

```text
2026-10-01T09:14:40Z event=startup pid=9974 euid=501 heartbeat=0
2026-10-01T09:15:40Z event=heartbeat pid=9974 euid=501 heartbeat=1
2026-10-01T09:16:40Z event=heartbeat pid=9974 euid=501 heartbeat=2
2026-10-01T09:17:40Z event=heartbeat pid=9974 euid=501 heartbeat=3
```

Repeated installation retained PID 9974. Opening the test app created UI
PID **10003**; activating Finder and subsequently terminating the UI left
PID 9974 alive with a continuing heartbeat counter. A normal AppleScript quit
request was blocked by Tiger's Keychain application-change dialog. No credential
access change was approved; the UI was terminated with SIGTERM instead. This
verifies independence from UI-process termination, but does not claim a normal
interactive quit or complete UI acceptance. Service installation now precedes
provider/Keychain bootstrap so credential dialogs cannot block service startup.

Explicit SIGTERM and unexpected SIGKILL both recovered with one replacement:

```text
2026-10-01T09:18:16Z event=shutdown pid=9974 euid=501 heartbeat=3
2026-10-01T09:18:16Z event=startup pid=10075 euid=501 heartbeat=0
2026-10-01T09:18:30Z event=startup pid=10081 euid=501 heartbeat=0
```

`--uninstall-daemon` at **09:19:09–09:19:10** unloaded the job and removed its
plist and executable. The daemon logged cooperative shutdown at **09:19:10**;
logs and user app data remained intact.

At **09:20:44**, after 94 seconds, the job and daemon were still absent.
Opening the app (without the CLI installer) then created one daemon, PID
**10113**, at **09:20:48**, before the credential prompt. It emitted its first
heartbeat at **09:21:48**. Adding a test-only key to the installed launchd
plist and running `--install-daemon` exercised configuration replacement:
PID 10113 logged shutdown at **09:21:53**, exactly one replacement, PID
**10130**, started, and the generated plist no longer contained the test key.
A second unchanged install retained PID 10130. Terminating the UI again did
not affect the service.

A missing-embedded-binary negative test temporarily moved only the test app's
bundled `strappyd` aside. Installation failed with the expected message and
left the already-running service intact. The bundled binary was restored.

Final checks:

- `make -C source clean release`: passed for iOS armv7/arm64 and Mac
  PPC/i386/x86_64/arm64, with no warnings or errors.
- `make -C source/linux daemon-test`: passed.
- Clang static analysis of `StrappyServiceController.m` and the app's
  command-line entry point: no findings.
- All four daemon Mach-O slices are present. PPC/i386 have no `UNIX2003`
  undefined symbols. The only runtime dependencies are Apple's system
  `libSystem.B.dylib` and, for legacy slices, `libgcc_s.1.dylib`.
- Full local logs are `/tmp/strappy-mac-daemon-build.log`,
  `/tmp/strappy-mac-daemon-test.log`, `/tmp/strappy-mac-daemon-analysis.log`,
  and `/tmp/strappy-mac-daemon-device.log`; generated logs are not committed.

The per-user service remains installed and running; the test app remains in
its Desktop folder. No machine reboot or logout was performed. Login loading,
newer Mac runtime behavior, and normal UI interaction past the existing
Keychain prompt remain follow-up coverage, not claims established by this run.

At **09:23:33**, the final clean-build ZIP had been recopied to the test folder
and installed. The unchanged daemon retained PID **10130**, with exactly one
launchd job and a heartbeat at **09:22:53**. The service directory/executable
were 0700, the launchd plist was 0600, and the log directory/file were 0700/0600,
all owned by `me`. The normal app remained closed after the termination test.

## Step 1: portable IPC (2026-10-01)

The service now hosts the transport described in `PROTOCOL.md`. Application
readiness remains false. No real provider, database, scanner, credential, or
prompt work was moved into the daemon in this milestone. Mutation operation
IDs return structured not-ready replies; transaction event integration and
application-state snapshots remain dependent on steps 2–4.

Offline coverage includes:

- Independent network-order wire vectors; portable C client round trips,
  expected-UID rejection, bounded monotonic receive deadlines, and writes to a
  disconnected peer with default SIGPIPE handling.
- Every header split, coalesced frames, maximum payload, invalid magic/kind,
  oversized lengths, zero IDs, invalid status, incompatible header/negotiated
  versions, commands before negotiation, unknown commands, and duplicate or
  decreasing request IDs (including rejected mutations).
- Partial-frame disconnects, slow non-reading clients, concurrent healthy
  clients, same-user authentication, a different UID despite relaxed filesystem
  permissions, socket permissions, server exclusion, stale-socket recovery,
  instance identity changes, and refusal to replace a regular file.
- Synthetic worker publication for all four event topics, a commit racing with
  atomic subscribe/snapshot, unsubscribe behavior, and resynchronization.
  Real persistent-state notifications must use this boundary after their
  database transaction commits; this milestone does not claim a database hook.
- An idle subscriber receiving an event after 31 seconds, alongside expiration
  of an incomplete handshake. This caught and fixed a stale output-deadline bug:
  the first queued message after idle must start a fresh progress deadline.
- Existing heartbeat log rotation and cooperative SIGTERM checks. Socket tests
  use private directories under `/tmp` because this workspace's mounted
  repository filesystem rejects chmod on Unix sockets with `EINVAL`.

Native runtime checks use isolated test daemons under the normal UID 501; they
leave the existing installed launchd services unchanged. No reboot, respring,
Keychain approval, provider request, or application data mutation was performed.

| Host | Observed result |
| --- | --- |
| `x4-vm`, Tiger 10.4.11 / PPC | Native C hello, snapshot, subscribe, unsubscribe; an independent Python 2.3 client validates big-endian wire bytes, byte-at-a-time framing, duplicate rejection, not-ready and unknown-operation replies. SIGKILL/stale-socket restart, reconnect, SIGTERM, and socket removal pass. |
| `pinkphone-rsa`, iOS 15 / arm64 | Native C negotiation, snapshot, subscription, unsubscribe, SIGKILL/stale-socket restart, reconnect, SIGTERM, and socket removal pass as mobile from the rootless tree. |
| `koolphone5`, iOS 8 / armv7 | SSH to the configured host refused connections on repeated attempts. Runtime IPC validation remains outstanding; the armv7 slice builds successfully. |

On `pinkphone-rsa`, the initial copy under `/tmp` was killed before service
startup (exit 137). The same signed executable ran from
`/var/jb/var/strappy-ipc-step1-20261001`. This establishes the working rootless
execution path, not a diagnosis of the temporary-path kill. The production
package already places its executable under `/var/jb/usr/libexec`. A later
in-place SCP overwrite of the test executable also produced exit 137; copying
to a fresh file and renaming it into place restored successful execution of the
final binary. Use replacement files when refreshing signed device probes.
This does not establish the exact signing-cache cause.

Final checks for this milestone:

- `make -C source clean release`: iOS armv7/arm64 and Mac
  PPC/i386/x86_64/arm64; passed with no warnings in the full output.
- `make -C source/linux clean test`: includes IPC client, daemon, and synthetic
  worker harnesses alongside the complete shared-core offline suite; all passed.
- Clang static analysis of the daemon, IPC server/client, and C XPFoundation
  compatibility module: no findings.
- Generated logs remain local: `/tmp/strappy-ipc-build.log`,
  `/tmp/strappy-ipc-tests.log`, and `/tmp/strappy-ipc-analysis.log`. Native
  command output is retained in the task transcript; test processes are stopped.

These checks establish transport behavior, not UI service-adapter integration,
provider execution, durable submission deduplication, real store snapshots,
launchd package upgrade behavior for this new transport, or continuous execution
during phone sleep. Those later milestones retain their existing checklist.

## Step 2: daemon-owned writes and read-only UI — 2026-10-01

Step 2 intentionally disables GUI mutations and in-process workers until
steps 3–4, as authorized by the user. The service initializes the store and
bundled catalog; readers wait for IPC v2 store readiness. No database version
increase or new migration was added. [STORAGE.md](STORAGE.md) records the
ownership, compatibility, and reader-lifetime contract.

### Offline and build validation

- Clean iOS armv7/arm64 and Mac PPC/i386/x86_64/arm64 release builds passed;
  full logs contain no compiler/linker warnings.
- Full `make -C source/linux clean test`, including existing history, renderer,
  options/defaults, provider/catalog, and Responses tests. The 20,000-session,
  20,000-database, and 21,000-model paging tests retain their ordering and
  indexed-page bounds. Catalog/model tests now checkpoint with old readers open.
- New `store_harness` and separately compiled `store_reader_harness` cover
  missing-store non-creation, GUI write rejection, read-only options/history,
  missing working-directory fallback without persistence, incompatible version
  rejection, readiness revocation, stable sidebar snapshots, and a child-process
  writer completing `wal_checkpoint(TRUNCATE)` while parent readers remain open.
- New `store_monitor_harness` exercises the actual asynchronous client with a
  missing service, service startup, disconnect, restart, and incompatible schema
  refusal. Existing wire/malformed/slow-reader tests were updated to IPC v2.
- Clang static analysis of daemon, IPC server, store monitor, connection/schema
  code, sidebar, and catalog; no findings. A read-path call-graph audit found and
  removed both missing-working-directory backfill writes.
- Parsed staged rootful/rootless plists: absolute database/resource paths,
  mobile identity, and bundled catalog presence. Mach-O inspection confirms
  the required daemon slices, only libSystem on iOS, and libSystem plus legacy
  libgcc on PPC/i386 Mac. SQLite/cJSON are statically linked, so the stable Mac
  service requires no copied framework.

Full generated logs remain local under `/tmp/strappy-step2-*`; none are committed.

### Native storage checks

Tests used isolated, disposable stores and the same Altivec SQLite libraries
used to build the packaged service. Existing launchd jobs and application data
were left unchanged. No provider calls, credential access, reboot, or respring
was performed.

| Host | Result |
| --- | --- |
| `x4-vm`, Tiger 10.4.11 / PPC, UID 501 (`me`) | Writer and GUI-capability reader probes pass with SQLite **3.43.2**, including a separate process writing/checkpointing while a retained sidebar snapshot remains stable; a fresh snapshot sees the committed name. |
| `pinkphone-rsa`, iOS 15.0 / arm64, UID 501 (`mobile`) | The same writer/reader checks pass with SQLite **3.43.2**, including read-only options/history, fallback non-persistence, readiness gating, cross-process WAL/checkpoint behavior, and 0600 database/WAL/SHM files. |
| `koolphone5`, configured iOS 8 / armv7 host | SSH first refused the connection, then timed out on retry. Runtime step 2 validation remains outstanding; armv7 builds pass. |

Both reachable devices also ran the packaged `strappyd` binary against a fresh
isolated store and explicit resource directory. `--ipc-probe` reported:

```text
IPC v2: same-user hello, snapshot, subscribe, unsubscribe passed; store ready; execution not ready
```

An independent GUI-capability reader then passed against the daemon-created
store. While the service was alive, DB/WAL/SHM modes were all 0600 and ownership
matched its user. SIGTERM stopped these test services cleanly.

Probe sources are `source/linux/store_harness.c` and
`source/linux/store_reader_harness.c`. Compile the writer with
`STRAPPY_DB_WRITER=1`, the reader with `STRAPPY_DB_UI=1` and no writer capability,
and link `source/daemon/store.mk`'s portable sources plus the target Altivec
SQLite/cJSON libraries. Run the writer on a nonexistent fixture path, then the
reader on that same path. On rootless iOS, copy signed probes to fresh filenames
under `/var/jb/var/strappy-step2-20261001`; copies under `/var/tmp` were killed
before creating a store, matching the earlier step 1 execution-path limitation.

These checks establish storage and readiness behavior, not end-to-end GUI
interaction, launchd upgrade/reinstallation with the new store arguments, or
provider/scanner execution. Existing step 5 limitations (including iOS 6,
remaining iOS 8 runtime checks, newer Mac runtime, and phone sleep/first unlock)
remain open. No persistent main-store read transaction survives a completed
paginated-reader open, so suspension cannot leave those row objects pinning WAL.

## Step 3 — durable prompt execution (2026-10-01)

### Implementation and automated checks

- IPC v3 / reader ABI 2 expose submission and run-specific cancellation. The
  canonical `prompt_runs` table persists retry identity, prompt, lifecycle,
  cancellation and typed processing/retry fields. SQLite `user_version` stays 1;
  there are no ALTER/migration steps.
- The daemon serializes execution globally and reuses the real C Responses
  runtime. UI submission and cancellation use background IPC clients. Reconnect
  refreshes DB-backed history/status; saved unresolved submission keys survive UI
  exit. No UI Responses-runtime callback or execution fallback remains.
- `make -C source/linux clean test` passes, including malformed transport/input,
  peer verification, slow subscribers, synthetic publication, WAL reader tests,
  the complete existing shared-core suite, and the new execution tests.
- `execution_harness.py` uses only a loopback HTTP fixture and synthetic
  credentials. It verifies lost acknowledgments, a disconnected submitting
  process, identical and conflicting retries, concurrent same-session clients,
  the cross-session concurrency bound, HTTP and Bash cancellation, SIGTERM child
  cleanup, restart interruption, and durable terminal retries. Separate
  `STRAPPY_DB_UI` processes recover running/completed/interrupted state, refuse
  writes, and revoke reads when service readiness is lost. Fixture response and
  tool IDs are unique per request, matching the provider contract.
- Clean iOS armv7/arm64 and macOS PPC/i386/x86_64/arm64 release builds pass without
  warnings. Both rootful/rootless packages contain the signed daemon, explicit
  runtime resources, and the shared app Keychain access group. Mac installs all
  runtime resources, Info.plist and CA bundle alongside the stable binary.
- Clang static analysis of execution, IPC server, daemon main, run storage, and
  store client reports no findings. Generated build/analysis logs remain under
  `/tmp/strappy-step3-*` and are not committed.

### Native checks and scope

Tests used disposable stores and service endpoints, with no replacement of the
normal app, installed launchd service, or user conversation history. Fixture
seeding read only account/model configuration from the existing store; it copied
no history or credentials. No reboot or respring was performed.

| Host | Observed result |
| --- | --- |
| `pinkphone-rsa`, iOS 15/arm64, UID 501/mobile | The signed daemon's noninteractive Keychain probe found one configured account and one accessible credential. A real prompt completed after the submitting client process exited. A later reader reported completed state and found the expected `DAEMON_STEP3_OK` assistant answer in the normalized timeline (16 visible rows in the initial fixture). The same stable key returned the same completed run instead of another provider request. IPC negotiation and explicit resource/CA/Info.plist paths passed. |
| `x4-vm`, Tiger 10.4.11/PPC, UID 501/me | Native v3 negotiation, durable acceptance and retry passed. The probe found one configured account but zero accessible credentials. The run became failed; a new client received the same failed run for the same submission identity. This establishes failure/dedup behavior, not successful Mac provider execution. |
| `koolphone5`, configured iOS 8/armv7 host | SSH refused the connection. The armv7 clean build passes, but native Step 3 execution remains unverified. |

A second iOS fixture using the complete typed run schema was first observed
running (state 2) by a separate reader; a retry returned that same running run.
A later reader observed completed state 3, found the expected assistant answer
among 14 timeline rows, and another retry returned the same completed run.
Both test daemons were stopped cooperatively after validation; the normal
installed launchd services were left unchanged. The final IPC regression also
covers a subscribed peer retaining all four invalidation topics while its own
command handler is busy.

Initial isolated endpoints were `/var/jb/var/strappy-s3/service.sock` on iOS and
`/tmp/strappy-s3/service.sock` on Tiger. The initial iOS daemon logged startup at
11:39:25 UTC as PID 3557; completion and history were queried by later client
processes. Heartbeat intervals varied while the phone was idle; this does not
establish uninterrupted execution during sleep. Device clocks differ, so cross-
host timestamps are not used for timing assertions.

Representative checks, run as the normal user:

```sh
strappyd --credential-probe /absolute/path/to/existing/strappy.sqlite
probe seed /absolute/path/to/existing/strappy.sqlite /private/fixture.sqlite /private/workdir
strappyd /private/heartbeat.log /private/fixture.sqlite /private/resources
strappyd --ipc-probe /private/service.sock
probe submit /private/service.sock 1 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa 'Reply briefly with DAEMON_STEP3_OK. Do not search the web or run Bash.'
probe state /private/fixture.sqlite 1
probe history /private/fixture.sqlite 1
```

`probe` is `source/linux/execution_device_probe.c`, compiled for the device with
writer capability for fixture seeding, the store sources, `strappy_run.c`, and
the portable IPC client/platform sources. The iOS probe must be signed and run
from a jailbreak executable path. The production daemon uses its dedicated
entitlements, not the probe's bare signature.

### Outstanding validation

The native Keychain diagnostic deliberately prints no identifiers or OS error
strings. Zero accessible items on Tiger does not identify whether the item is
missing, the Keychain is locked, or its ACL needs interactive approval. Successful
Mac credential access/refresh and a live Mac provider run remain unverified.
iOS read access and a real run passed; forced OAuth refresh/rotation, locked-phone
access and first-unlock behavior were not independently established.

Full graphical submission, UI termination and reopened-window rendering were
not driven on these devices. The automated and native tests prove client-process
independence and reopened DB state, not that complete interactive scenario.
Installation/upgrade/removal with the final execution worker, newer Mac runtimes,
iOS 6, and the remaining iOS 8/15 lifecycle matrix remain Step 5 checks. Interrupted
runs are explicit and never replayed; full interrupted-ledger repair and orphan
handling after SIGKILL remain recovery work.
