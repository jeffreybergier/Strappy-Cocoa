# Step 0 validation — 2026-10-01

The implementation is complete; the original **iOS 5 acceptance gate remains
open**. `koolphone5` is an iPhone 5 (`iPhone5,2`, armv7) running **iOS 8.4.1,
build 12H321**, with a rootful jailbreak. Its package inventory reports
`com.lukezgd.everuntether-bin 1.3.6`. Rootless iOS 15.0/Dopamine was subsequently tested on `pinkphone-rsa`; see
the rootless section below. No harness migration has started.

## Build and offline checks

- `make -C source clean release`: passed, no compiler warnings or errors;
  armv7/arm64 iOS `.deb` packages and PPC/i386/x86_64/arm64 macOS app.
- `make -C source/daemon/tests clean test`: passed, including `daemon_harness.py`.
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
scp -O build/iOS/release/Strappy/Strappy-rootful.deb koolphone5:/var/tmp/strappy-step0.deb
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
scp -O build/iOS/release/Strappy/Strappy-rootless.deb pinkphone-rsa:/var/tmp/strappy-rootless-test.deb
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
Apple architectures with no warnings/errors, and `make -C source/daemon/tests
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
- `make -C source/daemon/tests daemon-test`: passed.
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
- `make -C source/daemon/tests clean test`: includes IPC client, daemon, and synthetic
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
- Full `make -C source/daemon/tests clean test`, including existing history, renderer,
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

Probe sources are `source/daemon/tests/store_harness.c` and
`source/daemon/tests/store_reader_harness.c`. Compile the writer with
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
- `make -C source/daemon/tests clean test` passes, including malformed transport/input,
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

`probe` is `source/daemon/tests/execution_device_probe.c`, compiled for the device with
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

## Step 4: remaining workers and mutations — 2026-10-01

Implemented service commands for scanning, Database Study, model refresh,
accounts/credentials, manual models, approvals, session changes, context
inclusion and shared defaults. The UI bridges now contain only command clients,
readers and presentation work; the old in-process scan/study/refresh paths and
unused UI run registry were removed. IPC is v4, reader ABI remains 2, and SQLite
`user_version` remains 1. See [MUTATIONS.md](MUTATIONS.md) for command behavior and
limitations, including unknown mutation acknowledgments and transient scan state.

### Local checks

- Clean universal iOS build: armv7 and arm64, rootful/rootless packages.
- Clean Mac build: PPC, i386, x86_64 and arm64, including daemon slices.
- Full offline Linux shared-core suite, original execution/IPC/WAL/read-only
  regressions, and the new mutation harness. No live provider credentials used.
- The mutation harness verifies account/model/default/session commands with
  independent read-only SQLite queries, malformed command rejection, real
  filesystem scanning, approvals/visibility, mutation conflicts during work,
  Database Study cancellation and three-batch no-progress failure. A successful
  study executes actual `database_context`, a real-table `database_query`, and
  `database_study` tools through the loopback Responses provider. It also verifies
  model catalog download/persistence, cancellation of a blocked catalog download
  on SIGTERM, status reset on reconnect after service restart, and durable prompt retry
  lookup while a different worker is busy.
- Clang static analysis of execution, mutations, IPC server, store client, HTTP client and durable run storage produced no
  findings.

Generated build/test/analysis logs are local under `/tmp/strappy-step4-*` and are
not committed. Warnings are reviewed in the full captured build output.

### Isolated native checks

The same `source/daemon/tests/mutation_device_probe.c` ran against freshly created test
stores. The probe uses a new authenticated connection for every command. It
creates a fixture account and manual model, approves the model, sets defaults,
creates/deletes a session, asks the daemon to scan a real fixture SQLite file,
polls completion from separate connections, approves/hides/unhides the database,
and archives the fixture model/account. It never sends a provider request or
uses an existing account's credentials. The production launchd job and live
Strappy store were not replaced or modified.

| Host | Verified result |
| --- | --- |
| `x4-vm`, Tiger 10.4.11/PPC | Probe passed; daemon PID 10833, euid 501. Device log startup 13:05:33Z and cooperative shutdown 13:05:40Z. |
| `koolphone5`, iOS 8.4.1/armv7 | SSH was reachable. Probe and daemon ran as mobile via `su` from the root SSH account. Probe passed; PID 1006, euid 501; startup 12:49:28Z and shutdown 12:49:32Z. |
| `pinkphone-rsa`, iOS 15.0/arm64 | Probe passed from the mobile SSH account, using signed executables under the jailbreak prefix; PID 3642, euid 501; startup 12:49:24Z and shutdown 12:49:40Z. No reboot or respring. |

Device clocks differ; these timestamps are not used for cross-device timing.
The isolated paths were `/tmp/strappy-s4-20261001` on Tiger and iOS 8, and
`/var/jb/var/strappy-s4-20261001` on iOS 15. After orderly shutdown, each fixture
store was copied back and independently checked: integrity `ok`, schema version
1, zero remaining sessions, one archived fixture account, and one approved,
unhidden discovered database. All test daemons were stopped.

These are native command/storage/worker checks, not a full graphical UI test.
New sign-in/refresh/archive Keychain behavior has build coverage and reuses the
previously validated daemon credential boundary, but a fresh interactive OAuth
sign-in and live provider catalog refresh were not exercised in this milestone.
The existing Tiger Keychain ACL limitation remains. Newer Mac runtime, iOS 6,
graphical foreground/background/reopen flows, first unlock, sleep, package
upgrade/removal of the complete harness, and broader recovery remain step 5.

The final binaries were rerun on all three hosts. On both phones, overwriting the
previously executed test binary in place caused termination before startup
logging; uploading to a new file and renaming it over the old executable restored
successful execution. This was a deployment observation, not a daemon crash after
readiness. Use replacement-file semantics when copying signed test executables.

Representative native invocation (normal user, private fresh fixture):

```sh
strappyd /private/fixture/heartbeat.log /private/fixture/strappy.sqlite /private/fixture/resources &
pid=$!
probe /private/fixture/service.sock /private/fixture
kill "$pid"
wait "$pid"
```

The fixture contains `work/` and `scan/fixture.sqlite`, plus explicit resources.
The probe has no provider or live-store dependency. Temporary remote fixtures
were removed after capturing the final stores and logs locally.

## Step 5: recovery and lifecycle — 2026-10-01

See [RECOVERY.md](RECOVERY.md) for the implemented contract. IPC v4, reader ABI 2
and SQLite `user_version=1` are unchanged. No migration or automatic replay was
introduced. iOS 6 is explicitly deferred at the user's request because no device
is ready. No newer Mac host was supplied.

### Local validation

- Clean iOS armv7/arm64 release builds and both Debian variants passed.
- Clean Mac PPC/i386/x86_64/arm64 app and daemon builds passed.
- Full `make -C source/daemon/tests clean test` passed. Full captured build output was
  reviewed; there were no compiler warnings.
- Execution tests now kill a runtime during both HTTP and real Bash execution,
  recover interrupted runs and scans, verify durable tool intent and missing
  output diagnostics, and reject automatic retry/replay. A deliberately failing
  recovery trigger verifies transaction rollback and refusal of readiness.
  Existing duplicate submissions, contention, reconnect, cancellation, reader/WAL,
  mutation/study, malformed IPC and slow-peer tests still pass.
- The Bash suite retains exit-status, timeout, cancellation and output-limit
  coverage with the guardian enabled. A daemon SIGKILL test verifies its tool
  stops; Linux container zombies count as stopped, not live processes.
- Signal/rotation tests now cover SIGINT as well as SIGTERM. Lifecycle logs keep
  one 64 KiB backup and owner-only permissions, without heartbeat counters.
- Clang static analysis of Bash supervision, durable recovery, IPC server and
  daemon main produced no findings. Shell syntax checks passed.

Logs are local `/tmp/strappy-step5-*` files and are not committed. Final clean
build/suite output is in the `*-verified.log` files; analysis is in
`strappy-step5-analysis-final.log`.

### Native isolated fixtures

`source/daemon/tests/recovery_device_probe.c` seeds unfinished prompt, turn, request,
HTTP, tool and scan records in a private fixture. It checks terminal recovery,
a normalized interruption output, foreign keys and unchanged schema version.
`recovery_device_harness.sh` starts/stops the actual daemon, checks IPC before and
after more than a minute idle, then SIGKILLs a native Bash-owning probe and verifies
its shell no longer exists. The probe uses the same Bash/storage code and the
Altivec library shipped in the app. It makes no provider requests and touches no
live account/store. Run the script only in a fresh private directory containing
`strappyd`, `probe`, and the full `resources/` directory.

All final native fixtures passed as UID 501:

| Host | Final fixture daemon | Observations |
| --- | --- | --- |
| `x4-vm`, Tiger 10.4.11/PPC | PID 11021, startup 13:32:31Z, shutdown 13:33:35Z | Recovery/output pairing, idle IPC, cooperative shutdown and owner-death Bash cleanup passed. |
| `koolphone5`, iOS 8.4.1/armv7 | PID 1223, startup 13:15:26Z, shutdown 13:16:30Z | Same checks passed as mobile, launched with `su` from root SSH. |
| `pinkphone-rsa`, iOS 15.0/arm64 | PID 3851, startup 13:15:29Z, shutdown 13:18:41Z | Same checks passed as mobile, using signed binaries under `/var/jb`. |

Only startup/shutdown records appeared during these idle intervals; on-demand IPC
was healthy. The iOS 15 interval took 192 seconds despite 64 seconds of scripted
waits. Its cause was not assigned to a particular power state. Device clocks
also differ; these timestamps are not cross-device timing measurements. This
is not evidence of continuous execution during sleep.

Final fixture stores were copied back after shutdown for independent integrity,
foreign-key, schema-version and repaired-output checks. Temporary test daemons
were stopped and their fixture directories removed; the production services
were left installed and healthy. No reboot or respring was performed.

### Complete app/package service lifecycle

Both production iOS packages (version 1.1.2) installed the real worker/resource
configuration and answered IPC readiness without opening the UI. Reinstall
replaced the old PID, removal left no job or old process after a 12-second wait,
and reinstall restored one healthy launchd service with retained user data.
Rootful/mobile and rootless/mobile paths were both exercised. The initial test
exposed an unavailable `awk` command in the stop script; the final script uses
only shell built-ins to extract the PID and wait for exit. The final packages
were tested again over the corrected installed scripts without package-script
warnings. Final production PIDs were 1360 (iOS 8) and 3985 (iOS 15), both healthy.

Tiger installed the complete worker and resources, preserved its PID on unchanged
installation, uninstalled its registration/executable, and reinstalled a healthy
service. Replacing the app/service build then produced a new PID. The final app
is installed at `/Applications/Strappy.app`; its prior app bundle was retained
at `/tmp/strappy-pre-step5.app`. Opening the final UI produced PID 11065; killing
that UI left daemon PID 11001 and IPC readiness intact. Normal interactive quit,
logout/login and Keychain authorization were not simulated by that process test.

On iOS 8, Activator reported `com.altivecintelligence.Strappy` as the current app
and `application` mode after opening the final UI. Returning home and killing
Strappy left daemon PID 1254 and readiness intact. An explicit lock command
reported `lockscreen`; a later check still reported that mode, and the test
restored `springboard` with the normal dismiss command. These observations
establish UI-process independence;
they do not establish a completed graphical prompt/reopen workflow.

The signed production credential diagnostic reported one account and one
accessible credential on each phone, without printing identifiers, values or
making a provider request. Tiger reported one account and zero accessible
credentials, as before; missing items versus lock/ACL denial remain
indistinguishable with this deliberately bounded diagnostic. OAuth rotation and
first-unlock access are not established by a read probe.

### Remaining platform observations

The iOS 15 `uiopen` attempts did not establish a live Strappy UI process;
subsequent termination found no matching process. Its daemon stayed healthy.
Full graphical foreground/background/termination, explicit observed lock/unlock,
and graphical prompt/close/reopen remain unverified on that phone. A simulated
memory-pressure command was attempted, but produced no diagnostic confirming
delivery before it was stopped; only subsequent daemon PID/readiness were
verified. This is not real jetsam or memory-pressure validation.

Actual iOS first-unlock timing, jailbreak reactivation, iOS 15 reboot, forced
OAuth refresh, true memory pressure, newer Mac runtime and logout/login remain
open. `pinkphone-rsa` remains respring-only for any future disruptive testing.
iOS 6 remains deferred until the user supplies a device. No phone was kept awake
by a new service power assertion, and no additional execution privilege was added.

## Active-session option latency correction (2026-10-01)

The production Cocoa bridge now keeps active-session edits in a process-local
shared draft and saves it on the prompt worker before submission. The iOS options
panel reuses its loaded catalogs on edits. Session-only daemon notifications no
longer invalidate conversation HTML or unrelated catalogs, and queued topic bits
are combined before main-loop delivery. Future-session default and other metadata
mutation APIs retained their synchronous contracts at that revision; the
following change removes those waits from UI handlers.

`session_options_device_probe.m`, built with
`make -C source/iOS -f ../daemon/tests/session_options_device_probe.mk session-options-probe`,
ran as `mobile` on `koolphone5` (iOS 8.4.1 / armv7). It links the production Cocoa
bridge with only the settings and submission transport functions replaced. Its
loaded session is an in-memory fixture; it never contacts the real daemon or
provider and never writes the application database.

The passing run reported:

- 1,000 option edits in **515.444 ms**, averaging **0.515 ms per edit**, with zero
  mutation/submission IPC calls.
- Send returned in **3.280 ms** on the main thread while the settings transport
  deliberately waited **300 ms**. The main loop advanced five times during the
  failed-save scenario.
- Drafts survived controller replacement and invalidation; field masks preserved
  an earlier Bash edit when another controller changed the round limit.
- A rejected settings save retained the draft and sent no prompt. Concurrent
  edits during submission were rejected.
- A lost submission reply retained the durable retry key and saved-settings
  marker. Retrying submitted the identical key without repeating the settings
  mutation. Edits were rejected while acceptance was unresolved.

These are bridge timings and behavioral assertions, not graphical tap-to-display
measurements. Unsent drafts are discarded at UI process exit. No application
package was installed as part of this probe.

Final clean builds passed for iOS armv7/arm64 and macOS PPC/i386/x86_64/arm64;
full build output contained no warnings. `make -C source/daemon/tests clean test` passed,
including new subscribed-client assertions that option mutations emit only the
session topic and model approvals emit only the catalog topic. Generated build
and test logs were retained locally outside the checkout and are not committed.


## Asynchronous Cocoa commands and searches (2026-10-01)

`StrappyAsync` provides serial background commands, an independent query worker,
main-thread completions, and a common command-error notification consumed by both
app delegates. The production mutation gateways reject main-thread calls.
[ASYNC_UI.md](ASYNC_UI.md) records the operation policy and remaining local reads.

The provider-free `async_ui_device_probe.m` fixture ran successfully on
`x4-vm` (Tiger 10.4.11 / PPC), linked against the production dispatcher and Cocoa
bridges. It verified FIFO commands while the first command deliberately blocked,
an independent search completing during that block, copied mutable arguments,
64-bit integer and BOOL arguments, suppression of stale running and queued
searches, main-thread callbacks, exactly-once errors, explicit local handling,
errors without an originating view, and asynchronous queue overflow. Both
production mutation gateways rejected main-thread calls before transport.
The cold first enqueue measured 146.038 ms on the emulated PPC machine; this is
not a phone latency measurement. No database mutation or provider call occurred.

After initially refusing SSH, `koolphone5` became available again. Both native
fixtures passed as `mobile` on iOS 8.4.1 / armv7:

- The dispatcher returned from its cold first enqueue in **6.616 ms**, with the
  command worker deliberately blocked. Independent queries and the main loop
  continued; all the FIFO, stale-result, argument, error, overflow, and IPC-guard
  assertions passed.
- The existing session-options regression fixture completed 1,000 local edits in
  **531.259 ms** (**0.531 ms/edit**) with zero IPC. Send returned in **3.999 ms**
  while its settings transport delayed 300 ms. All draft, save-failure, and
  durable-retry assertions passed.

These are native bridge tests, not graphical tap-to-display measurements. No
updated app package was installed. Graphical navigation, backgrounding, and
error presentation still need on-device acceptance testing.

Final clean app builds passed for iOS armv7/arm64 and macOS
PPC/i386/x86_64/arm64, with zero warnings in the captured logs. The complete
`make -C source/daemon/tests clean test` offline suite passed. Both phone fixtures and
the PPC dispatcher fixture also cross-built without warnings. Build/test logs
remain outside the repository. The macOS launchctl installation/verification
path now executes on the same background command queue; CLI maintenance
commands retain their synchronous behavior.

## Persistent control and concurrent sessions (2026-10-02)

The UI client now owns a permanent FIFO control worker/connection, with an
independent notification/status path. Daemon command workers persist with their
connections. Prompt workers and cancellation belong to individual sessions;
short mutations no longer reserve the execution worker. Combined submissions
commit captured option changes and run acceptance together. Explicit OAuth
refresh uses an independent connection/worker so network latency does not occupy
the control FIFO. The IPC reader now permits the existing bounded 120-second
credential reply timeout instead of rejecting it before reading.

The offline execution fixture holds a loopback provider response open while two
sessions execute, verifies create/delete for an idle session, refuses deletion
of an executing session, and cancels one session without cancelling the other.
It also tests settings rollback when run insertion fails, retry preservation of
accepted settings, and control mutations during a delayed credential refresh.
Existing HTTP/Bash cancellation, service restart, durable retry, and shutdown
checks remain part of the suite.

`control_client_harness` runs the production C client against a deterministic
socket peer. It verifies one connection for repeated commands, increasing
request IDs, no replay after a lost mutation reply, reconnect, and an OAuth
refresh blocked until an independent control command arrives. Clang static
analysis of the client, IPC server, and execution dispatcher reports no findings.

Native `control_device_probe` runs against a fresh isolated database and creates
only synthetic account/model configuration. Thirty committed create/delete pairs
using the persistent client measured:

| Host | Mean per pair | Maximum per pair |
| --- | ---: | ---: |
| `koolphone5`, iOS 8.4.1 / armv7 | 21.579 ms | 44.992 ms |
| `pinkphone-rsa`, iOS 15 / arm64 | 4.174 ms | 4.500 ms |
| `x4-vm`, Tiger / PPC VM | 32.351 ms | 147.191 ms |

These are IPC-plus-commit fixture timings, not graphical tap-to-display timings
or before/after speedup measurements. The probes ran as UID 501, used no provider
credentials, made no provider requests, and did not replace installed apps or
services. The test scripts stopped and waited for their daemons; temporary fixtures were removed.
The rootless phone again required executing signed probes under `/var/jb/var`;
its initial `/var/tmp` attempt was killed before startup. The Mac probe used the
matching AltivecCore framework via `DYLD_FRAMEWORK_PATH`.

The updated native options fixture passed on both phones. Send returned in
3.998 ms on armv7 and 1.148 ms on arm64 while combined acceptance deliberately
waited 300 ms; the main loop advanced 6 and 25 times respectively. One thousand
local option edits still generated zero IPC calls. Rejected combined acceptance
retained the draft and created no submission; an uncertain reply retained the
same retry key and captured settings. No graphical navigation or rendering
acceptance test was performed.

Build the native fixtures after the normal platform build:

```
make -C source/iOS -f ../daemon/tests/control_device_probe.mk control-probe
make -C source/macOS -f ../daemon/tests/control_device_probe.mk control-probe
make -C source/iOS -f ../daemon/tests/session_options_device_probe.mk session-options-probe
```

Final clean builds passed for iOS armv7/arm64 and macOS PPC/i386/x86_64/arm64
with no compiler warnings. The complete offline Linux suite passed, including
the final heap-owned control-request implementation and independent OAuth lane.
Build and analysis logs remain outside the checkout and are not committed.


## Main-thread control correction

The earlier background-control validation above describes the superseded design.
Short Cocoa commands now invoke their bridge and completion synchronously on the
GUI main thread. The C transport performs socket I/O on the caller thread, with
no control worker, request queue, or cross-thread completion wait. Background
callers have independent connections. Add Session navigates using the committed
returned session before the separate sidebar query finishes.

`control_client_harness` wraps the actual send primitive and asserts that control
mutations reach it on the bootstrap/main thread. Its synthetic peer simultaneously
holds an ordinary background mutation and OAuth refresh until a main-thread
command arrives, proving neither owns the main control lane. The existing checks
still cover persistent connection reuse, reply ordering, uncertain-acknowledgment
no-replay behavior, and reconnect. The native dispatcher fixture asserts that a
main control method and its callback finish before the call returns even while
the explicit background worker is blocked. Database loading labels are unchanged.


## Daemon source boundary and authentication ownership (2026-10-02)

Provider HTTP/SSE, Responses execution, tools, filesystem execution, credential
configuration, OAuth and Keychain sources now live under `source/daemon` and are
absent from both application source lists. Session/run/study execution was split
from shared readers; tool metadata and option/event types remain shared. The
portable/native harnesses moved to `source/daemon/tests`; the old Linux Makefile
forwards commands. App startup configures presentation only, without HTTP CA
configuration. IPC v5 owns device login and exposes only public status metadata.
Saved tokens never return to the app. An untouched API-key editor preserves the
saved key, and a supplied replacement is sent to the daemon.

Validation completed:

- `make -C source/daemon/tests clean`, then `make -C source/daemon/tests -j4 test`:
  complete offline suite passed. Existing loopback OAuth/Responses tests remain
  unchanged in behavior. New authorization-manager tests cover accepted starts,
  repeated keys, account isolation, cancellation, sign-out cancellation, shutdown,
  provider failure and secret-free replies. Socket tests cover credential status,
  invalid accounts, and refusal of the formerly exposed `save_oauth` action.
- `make -C source/iOS clean`, then `make -C source/iOS -j4`: armv7/arm64 and both
  Debian package variants passed with zero warnings.
- `make -C source/macOS clean`, then `make -C source/macOS -j4`: PPC/i386/x86_64/
  arm64 passed with zero warnings.
- `app-boundary-test` checks both app source manifests and transitive local
  headers. `llvm-nm` inspection found no moved provider client/SSE, Responses
  sender, OAuth, tool executor or Keychain implementation in either app binary.
- Clang static analysis of the authorization manager, execution dispatcher and
  split session/study/run execution units reported no findings.

Native fixtures ran over SSH in fresh private directories, without installing an
app, replacing a service, using provider credentials or making provider requests:

| Host | Observed result |
| --- | --- |
| `x4-vm`, Tiger 10.4.11 / PPC, UID 501 | Authorization manager, Cocoa dispatch, IPC v5 readiness, daemon Keychain presence status for a synthetic account, and 30 committed create/delete pairs passed. Mean 36.568 ms/pair; maximum 214.270 ms. |
| `koolphone5`, iOS 8.4.1 / armv7, mobile | Same probes passed. Mean 111.275 ms/pair; maximum 1525.890 ms. The draft fixture also passed: 1,000 local edits in 501.130 ms with zero IPC; Send returned in 4.135 ms during a deliberately delayed 300 ms acceptance. |

The native authorization-manager fixture uses synthetic provider and credential
store callbacks. The separate control probe uses the real daemon and native
Keychain status path for a newly created account with no credential. This does
not validate live browser sign-in, real token refresh, locked-device Keychain
access or graphical acceptance. The timing samples include device scheduling
and storage latency; they are not a before/after comparison. All fixture daemons
were stopped and waited for, and their temporary directories were removed. Build,
analyzer and device logs stay outside Git.


## IPC v6 large-prompt validation (2026-10-03)

Tested the production daemon from `88247ad` over SSH/SCP in fresh private
fixture directories, without installing an app or replacing a launchd job.
`large_prompt_device_probe.c` uses the production C IPC client and a native
loopback HTTP provider. It creates a synthetic `other` account without a bearer
credential and checks that no Authorization header reaches the fixture. It
archives the fixture account afterward. No live application store or existing
provider credential is copied or used.

| Host | Runtime and identity | Result |
| --- | --- | --- |
| `x4-vm` | Tiger 10.4.11 / 32-bit PowerPC, UID 501 (`me`) | Production-daemon and Cocoa bridge probes passed. Large Send returned in 102.117 ms; accepted retry observed after 0.481 s. |
| `koolphone5` | iOS 8.4.1 build 12H321 / 32-bit armv7, UID 501 (`mobile`, via `su` from root SSH) | Same probes passed. Large Send returned in 42.805 ms; accepted retry observed after 0.440 s. |

The native daemon probe verified:

- Exactly 1 MiB of multibyte UTF-8 text plus exactly 4 KiB of settings, including
  atomic persistence of the changed round limit.
- A transfer missing its final byte creates no run and does not change options;
  an independent client remains responsive during that incomplete transfer.
- Independent rejection of oversized text, oversized options, malformed UTF-8,
  and an oversized ordinary control header.
- Lost-acknowledgment retry returns the original accepted work without another
  provider request; the plain submission operation also accepts 1 MiB and the
  connection remains usable afterward.
- Both runs complete, the local provider receives the exact prompt text, stored
  prompt bytes match, and SQLite integrity is `ok`. Exactly two provider requests
  occur, all to the device's own loopback listener.

The Cocoa probe links the production `StrappySession` bridge with only its
transport replaced. It checks rejection above 1 MiB, exact 1 MiB Unicode bytes
in all three send attempts, editable drafts after rejection, persistence after
an uncertain reply, retry-key preservation, and cleanup after acceptance. Its
main-loop callback assertion now uses the XP compatibility bridge, since Tiger
lacks `+[NSThread isMainThread]`. Timings above measure the method return and
fixture acceptance, not graphical paste/render performance or provider latency.

The first Tiger Cocoa run failed on that unsupported selector. The first iPhone
Cocoa run exceeded the old five-second retry wait; the final probe uses a
30-second completion allowance for large defaults writes, and the final run
completed its retry in 0.440 seconds. This does not establish the cause of the
initial timeout. Overwriting the signed iPhone probe in place also produced one
SIGKILL before output; copying the final binary under a fresh filename ran
successfully. The production-daemon tests passed on both hosts on the initial
run; these fixture issues did not require production-code changes.

Build after the normal platform build:

```sh
make -C source/iOS -f ../daemon/tests/large_prompt_device_probe.mk large-prompt-probe
make -C source/macOS -f ../daemon/tests/large_prompt_device_probe.mk large-prompt-probe
make -C source/iOS -f ../daemon/tests/session_options_device_probe.mk session-options-probe
make -C source/macOS -f ../daemon/tests/session_options_device_probe.mk session-options-probe ALTIVECCORE_LINKAGE=static
```

Prepare a fresh mode-0700 directory owned by the testing user containing
`strappyd`, `large-prompt-probe`, `session-options-probe`, and `resources/` (the
bundled JSON catalogs, `Info.plist`, and CA bundle). Use the PPC probe outputs
under those names on Tiger. Run `large_prompt_device_harness.sh /absolute/fixture`
as the normal user, or mobile on iOS. It refuses an existing fixture store,
starts a private daemon, and stops/waits for that daemon before the Cocoa probe.
All fixture daemons were stopped and their sockets and temporary directories
removed after this validation. Generated logs remain local under
`/tmp/strappy-native-*` and are not committed. Graphical paste/Send, modern Mac,
and other iOS runtime validation remain separate coverage.


## Credential daemon jobs / IPC v7 (2026-10-03)

Short non-credential main-thread control calls remain synchronous. Credential
reads, account saves, refresh, sign-out and archival now use transient daemon
jobs; public status is cached in Cocoa and completions return to the main loop.
IPC v7 requires matching app/service builds. Reader ABI 2 and schema version 1
are unchanged.

Validation:

- The full offline Linux suite passes. The authorization harness gates a
  credential refresh/save and proves another account remains responsive,
  per-account FIFO, sign-out after an already-started save, cancellation during
  device approval, queued-job shutdown, retained-key conflicts, restart unknown,
  and replies without secrets. Provider and Keychain boundaries are synthetic.
- The production C transport test drops a credential acceptance reply, observes
  query-only recovery and exactly one submission, and checks restart unknown and
  terminal failure. Ordinary mutations still do not replay. The daemon execution
  test accepts a two-second credential refresh promptly, queues a same-account
  read, and completes unrelated controls and another account's read meanwhile.
- Clean iOS armv7/arm64 and macOS PPC/x86/x64/arm64 builds pass without warnings.
  Clang static analysis of the auth manager and shared transport reports no
  diagnostics. Complete logs are under `/tmp/strappy-credential-*` in the work
  environment, not committed.
- x4-vm (Tiger 10.4.11, PPC, UID 501) passes the native authorization harness,
  shared Cocoa dispatcher, and real Cocoa credential-cache bridge fixture. The
  dispatcher proves an intentionally blocked credential queue leaves both the
  main loop and another account usable, maintains same-account ordering, and
  returns completions on main. The cache fixture blocks one read, completes
  another account, coalesces 1,000 getters, and distinguishes unavailable from
  absent credentials. Its production daemon passes the isolated loopback/account
  job and 1 MiB prompt regression fixture too, then shuts down cleanly. The final
  dispatcher cold enqueue was 140 ms and delayed Send returned in 30 ms on main.
- koolphone5 (iOS 8.4.1, armv7, mobile UID 501) passes the same authorization,
  dispatcher and credential-cache fixtures. The blocked-worker dispatcher enqueue
  returned in 5.5 ms. Its production daemon also passes the isolated loopback
  fixture: account update/archival use credential jobs, two exact 1 MiB prompts
  complete, and size/UTF-8 rejection, partial transfer, lost-ack deduplication and
  SQLite integrity pass. The temporary daemon logs clean shutdown. Initial SSH
  attempts timed out; a later retry connected successfully.

Concurrency fixtures use synthetic provider/credential boundaries. The separate
production-daemon fixture uses a fresh private store and a synthetic account
with no provider token; its two requests go only to its own loopback HTTP server.
These tests do not demonstrate a live provider login or locked-Keychain ACL
behavior. Temporary stores, binaries and sockets were removed from both devices after
verified daemon shutdown. No production service was installed or changed. The first Tiger
launch incorrectly used `su me` on a connection already running as `me`; rerunning
the fixtures directly as UID 501 passed.

Job status is transient: restart or completed-slot eviction can make a committed
operation's result unknown. Clients report that condition and do not resubmit.
A UI polling deadline likewise does not cancel already accepted daemon work.

## Scoped conversation invalidations / IPC v8 (2026-10-03)

Normal events carry the affected session ID and, for ledger changes, the model
request ID. The UI keeps independent per-document revisions in a bounded local
journal. Latest-request changes reconstruct only that request's rows; earlier
edits reconcile the affected suffix to keep cumulative metrics correct. DOM
updates retain unchanged rows and delete absent rows only in the supplied range.
Study batches invalidate their whole prompt group because completion excludes
all of its requests from context. Status-only events skip history. Reconnect, missing detail, and journal overflow
fall back to complete snapshots. SQLite version 1 and reader ABI 2 are unchanged;
IPC v8 requires matching app/daemon builds.

Validation:

- `make -C source/daemon/tests clean test`: complete offline suite, including new
  scope wire fields with IDs above JavaScript's exact numeric range, production
  mutation scopes (including whole-group study exclusions), per-session journal
  isolation/deduplication, independent
  cursors, missing-detail/overflow recovery, request-range cumulative metrics,
  late tool insertion, scoped tombstones and preservation of unchanged DOM rows.
- Clean iOS release build: armv7 and arm64; clean macOS release build: PPC, x86,
  x64 and arm64. Build logs checked for compiler warnings and errors.
- Native x4-vm (Tiger 10.4.11, PPC, UID 501): production server/monitor scoped
  events; two real `StrappySession` instances verify unrelated notifications
  skip status/history reads; independent revisions; global detail-loss recovery;
  socket disconnect/reconnect; production daemon IPC v8 readiness probe;
  read-only normalized history; native WebKit
  unchanged-row identity, disclosure-state retention, late insertion and scoped
  deletion. Fixtures use private `/tmp` paths and synthetic history only.
- The native fixture has two sessions with 300 completed requests / 600 rows
  each and 4 KiB answer text per request. In a representative run, full refresh
  took 946.7 ms and produced 2,084,917 bytes; latest-request refresh took 100.6 ms
  and produced 6,977 bytes (two rows). The unrelated session had zero dirty
  requests and generated no history JavaScript. Timing includes reader setup,
  resource loading and rendering; it is not an end-to-end UI latency benchmark.
- No phone runtime test was run: only x4-vm was available for this change.

Remaining cost: prefix numeric aggregation for cumulative totals still visits
prior requests, and a change to an older request can require a longer suffix.
This removes full-history message reconstruction/HTML transfer for ordinary
latest-request updates; it does not claim constant-time total database work.

Native fixture reproduction (after the platform build):

```sh
make -C source/daemon/tests build-linux/responses_harness
build/tests/linux/daemon/responses_harness --incremental-fixture /tmp/incremental.sqlite
make -C source/macOS -f ../daemon/tests/incremental_device_probe.mk ALTIVECCORE_LINKAGE=static incremental-probe
```

Copy the probe, synthetic SQLite file, shared JSON resources, and
`source/daemon/tests/webview_device_probe.js` to a private mode-0700 directory on
x4-vm. Run `incremental-probe-ppc SOCKET_DIRECTORY DATABASE RESOURCE_DIRECTORY
WEBVIEW_TEST_JS` as the logged-in user. The native WebView fixture intercepts
`strappy-action://webview-ready`, matching the application's navigation policy.

## Existing function-call schema constraints (2026-10-03)

Daemon startup and GUI readers now inspect `main.function_calls` index metadata
before accepting a store. Main's inline global `UNIQUE(provider_call_id)` and
unsupported named/composite/partial uniqueness are rejected with an actionable
compatibility error. Quoted index names work; nonunique lookup indexes and
redundant unique indexes containing `item_id` remain supported. No migration,
constraint removal, database reset, `user_version` change, reader ABI change, or
IPC version change was introduced.

Validation:

- `make -C source/daemon/tests clean test` passed, including fresh/legacy schema
  fixtures, GUI-reader rejection, daemon refusal before readiness, preservation
  of existing schema/rows, repeated provider IDs across responses/sessions, and
  rejection/rollback of duplicate IDs within a single response.
- Clean iOS armv7/arm64 and macOS PPC/x86/x64/arm64 release builds passed with
  no compiler warnings or errors. The native schema probes also compiled cleanly.
- `pinkphone-rsa`, iOS 15 / arm64, UID 501 (`mobile`), SQLite 3.43.2: fresh-store
  IPC v8 readiness, inline/named/composite/partial constraint detection, quoted
  names, redundant-index acceptance, writer and UI-reader rejection, legacy
  daemon refusal, absent service socket after refusal, and byte-for-byte
  preservation of the rejected SQLite file all passed. Tests ran in a private
  directory under `/var/jb/tmp`; installed stores and services were untouched.
  No reboot or respring was performed.
- `koolphone5`, iOS 8.4.1 / armv7, UID 501 (`mobile` via root SSH), SQLite
  3.43.2: after SSH became available, the same native schema suite passed,
  including fresh daemon readiness, UI-reader/daemon rejection of the legacy
  constraint, absent socket after refusal, and byte-for-byte preservation of
  the rejected database. The installed service remained running.
- Read-only inspection of the installed stores found koolphone5 already has
  only the nonunique provider-ID lookup index. pinkphone-rsa also retains the
  legacy unique provider-ID autoindex. Inspection did not read conversation
  content or change either store.

Reproduce native probes after the platform build:

```sh
make -C source/iOS -f ../daemon/tests/schema_device_probe.mk ALTIVECCORE_LINKAGE=static schema-probes
```

Copy `schema-probe`, `schema-reader-probe`, the built daemon as `strappyd`, and
`schema_device_harness.sh` into a fresh private directory; include the shared
JSON resources, platform `Info.plist`, and CA bundle in its `resources` directory.
Run `sh schema_device_harness.sh ABSOLUTE_FIXTURE_DIRECTORY` as mobile. On a
rootless phone use the jailbreak prefix for the signed executables. The script
refuses to reuse fixture stores and starts/stops only its own daemon. An
uncompressed archive avoids depending on `gzip` in the phone's shell environment.

## Session deletion cleanup (2026-10-15)

Deletion now removes `prompt_runs` and the session in one transaction. This
works with existing branch stores that have no run/session foreign key; it
adds no migration, schema change, or `user_version` change. After confirmed
service deletion, the UI removes that session's process-local option draft and
pending prompt retry identity. Failed or unacknowledged deletion preserves both.

Regression coverage:

- `session_delete_harness` is part of the offline suite. It checks completed,
  failed, cancelled and interrupted run cleanup, rollback when session deletion
  fails after run deletion, preservation of another session, missing-session
  failure, reuse of the deleted ID, and unchanged schema/version.
- `session_options_device_probe` exercises the production Cocoa bridge with
  simulated service replies. Failed and uncertain deletion preserve drafts and
  retries; successful deletion clears both; a new controller with the reused ID
  gets defaults; another conversation retains its own edits and retry identity.
  Existing field-merge, failed-send, 1 MiB Unicode retry and credential-cache
  checks also passed.
- The full offline suite and clean iOS armv7/arm64 and Mac
  PPC/i386/x86_64/arm64 debug builds passed without compiler warnings. Project
  and both platforms' debug package validation passed. Targeted Clang analysis
  of the changed Cocoa and database modules (UI and writer configurations)
  completed without diagnostics.

Native tests were transferred with `scp -O` and run over SSH:

| Host | Runtime | Result |
| --- | --- | --- |
| `koolphone5` | iOS 8.4.1, armv7, mobile (UID 501), SQLite 3.43.2 | Both probes passed. |
| `x4-vm` | Mac OS X 10.4.11, PPC, user me (UID 501), SQLite 3.43.2 | Both probes passed. |

Fixture directories were `/var/mobile/tmp_altivec/strappy-delete-20261015-1`
and `/Users/me/Desktop/strappy-delete-20261015-1`. Logs were retrieved under
`build/deletion-fix/{koolphone5,x4-vm}/`. No installed app/service, real
conversation store, credentials, or live provider was used. The first phone
launch mistakenly used the unsigned output and was killed before running;
transferring `Intermediates/signed/` outputs under fresh filenames resolved it.
These tests establish native storage and Cocoa behavior, not an installed-app
UI interaction or live-provider test.

Reproduce builds from the repository root:

```sh
make -C source/daemon/tests session-delete-test
make -C source/daemon/tests -f native.mk PLATFORM=iOS TEST_PRODUCTS=delete-probe ARCHS=armv7 debug
make -C source/daemon/tests -f native.mk PLATFORM=macOS TEST_PRODUCTS=delete-probe ARCHS=ppc debug
make -C source/iOS -f ../daemon/tests/session_options_device_probe.mk ARCHS=armv7 debug
make -C source/macOS -f ../daemon/tests/session_options_device_probe.mk ARCHS=ppc debug
```

Transfer the signed phone binaries from
`build/tests/iOS/daemon/debug/delete-probe/Intermediates/signed/delete-probe`
and `build/iOS/debug/options-probe/Intermediates/signed/options-probe`.
Mac outputs are `build/tests/macOS/daemon/debug/delete-probe/delete-probe`
and `build/macOS/debug/options-probe/options-probe`. Run `delete-probe` with
an absolute, nonexistent fixture database path and `options-probe` without
arguments. Keep each run's output logs and use a new staging directory.

## Empty account endpoint override (2026-10-16)

Both platform daemons compile `strappy_db_catalog.c`. Account updates now bind
an empty optional endpoint override as SQL NULL, matching provider-default
resolution and the existing schema constraint. Endpoint validation still runs
first, so `other` accounts must supply their required Responses endpoint. No
schema migration, key-format validation, or provider request was added.

The multi-account database regression failed before the fix with `Provider
account could not be updated.` It now covers empty overrides for OpenRouter
and ChatGPT, SQL NULL persistence, and rejection of an empty required endpoint.
The complete `make -C source/daemon/tests clean test` suite passes. Clean debug
app/daemon builds pass for iOS armv7/arm64 and Mac PPC/i386/x86_64/arm64 with no
compiler warnings. `make validate` and both debug `validate-package` targets
pass. This checkout has no `check-project` target; `validate` is its project
validation entry point.

`account_endpoint_device_probe.c` sends real credential jobs over IPC to the
production daemon and reads committed rows through read-only SQLite. It checks:

- Saving a synthetic OpenRouter key with an empty endpoint and output limit;
  the endpoint is SQL NULL and Keychain presence is true.
- Editing the name/limit without replacing the key; presence remains true.
- Rejecting an empty endpoint for `other` without changing its saved name.
- Saving and preserving an explicit endpoint and output limit for `other`.
- Archiving the fixture accounts through the daemon's credential cleanup path.

Native results:

| Host | Runtime / launch context | Result |
| --- | --- | --- |
| `koolphone5` | iOS 8.4.1 / armv7, mobile via `su` | Passed |
| `x4-vm` | Mac OS X 10.4.11 / PPC, logged-in desktop Terminal | Passed |

The first Mac runs launched directly from SSH could not save the fixture key:
`SecKeychainGetStatus` in that SSH context reported unlocked=0, writable=0.
Launching the same daemon/probe from the normal desktop Terminal session passed.
Do not treat an SSH Keychain denial as a storage regression or native success.

Build the native probe from `source/daemon/tests` using:

```sh
make -f native.mk PLATFORM=iOS TEST_PRODUCTS=account-endpoint-probe debug
make -f native.mk PLATFORM=macOS TEST_PRODUCTS=account-endpoint-probe debug
```

Stage `account_endpoint_device_harness.sh` as `run.sh`, the production daemon
as `strappyd`, and the probe as `account-endpoint-probe` in a fresh private
fixture directory. Use the `Intermediates/signed/` outputs on iOS. Include shared
JSON catalogs, platform `Info.plist`, and `cacert.pem` under `resources/`.
Invoke `sh run.sh ABSOLUTE_FIXTURE_DIRECTORY` as the normal app user. On Mac,
launch that command from a desktop Terminal, including when orchestration uses
SSH; a `.command` wrapper can capture output and exit status in the fixture.
The harness refuses reused stores and stops only its own daemon.

Passing fixtures are `/var/mobile/tmp_altivec/strappy-endpoint-20261016-2` and
`/Users/me/Desktop/strappy-endpoint-20261016-3`. Build and runtime logs are retained
locally under `build/account-endpoint-fix/`. Tests used synthetic credentials and
isolated stores, made no provider requests, and did not replace installed apps
or services. This verifies native daemon account saving, not manual UI taps.

## OpenRouter catalog default endpoint (2026-10-16)

After account saving was fixed, `strappy_service_refresh_catalog` still copied
`responses_endpoint` directly into the HTTP configuration. That field is an
optional override, so a normal OpenRouter account has SQL NULL there. The HTTP
client rejected the unconfigured request before sending it. The shared Cocoa
daemon boundary now resolves the endpoint through
`strappy_provider_definition_responses_endpoint`, preserving provider validation
and using the built-in URL when no override exists. Endpoint-resolution failure
stops the fetch. Both Mac and iOS compile this same implementation.

The complete offline suite, clean debug builds for all six app/daemon slices,
project validation, and both platform package checks passed without compiler
warnings. `catalog_refresh_device_probe.c` provides an explicit live diagnostic:

```sh
make -C source/daemon/tests -f native.mk PLATFORM=iOS \
  TEST_PRODUCTS=catalog-refresh-probe debug
# Stage the signed probe, then run as mobile with an explicit socket/store:
STRAPPY_ALLOW_LIVE_PROVIDER_TESTS=YES ./catalog-refresh-probe \
  /var/log/strappy/service.sock \
  '/var/mobile/Library/Application Support/Strappy/strappy.sqlite'
```

The probe requires exactly one active OpenRouter account, submits one catalog
refresh, polls its completion, and reads the active model count. It does not
read or print credentials or provider payloads and is not part of offline tests.
It refreshes the specified store, so use it only for authorized live validation.

On gomadango (iOS 6.1.3 / armv7), the installed pre-fix daemon returned terminal
state 4 (failed) for the account with its default endpoint. After installing the
rebuilt rootful 1.1.3 package through dpkg, the same diagnostic returned terminal
state 3 (completed) and **462 active OpenRouter models**. Launchd started daemon
PID 8584 as mobile and IPC v8 readiness passed. No prompt/model inference was
submitted. Logs are under `build/catalog-endpoint-fix/`; native probe staging is
`/var/mobile/tmp_altivec/strappy-catalog-20261016`, and package staging is
`/var/root/tmp_altivec/strappy-catalog-install-20261016`. This verifies the native
production refresh path on the affected phone; Mac coverage for this follow-up
is compilation and shared resolver tests, not a live Mac provider request.

## Keyed maintenance completion (2026-10-16)

Scan and model-catalog submissions now carry opaque job keys. The daemon retains
64 keyed states, and a UI worker reads the particular job's terminal result.
Main-thread completion no longer depends on observing a running activity hint.
The aggregate monitor cannot finish locally pending jobs. Missing results after
restart or eviction are unknown outcomes and never trigger automatic replay.

The production-daemon mutation harness covers first observation after successful
scan completion, injected scan-storage failure, immediate catalog failures,
consecutive job isolation, duplicate-key rejection, malformed status queries,
bounded retention, running status, and restart loss. The production-client
transport harness covers immediate success/failure, unknown outcome, a lost
status reply followed by a read-only reconnect, and running-to-terminal polling.

`make -C source/daemon/tests clean test`, `make validate`, and
`make validate-package CONFIG=debug` passed. Final `make clean debug` builds
passed for Mac PPC/i386/x86_64/arm64 and iOS armv7/arm64, app and daemon, without
compiler warnings. The first build exposed a missing private scan-completion
method declaration; that declaration was added before the final clean build.
Logs are local `/tmp/strappy-maintenance-*` files, not committed artifacts.
Native GUI/device scenarios and live provider calls were not run for this fix.
