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
