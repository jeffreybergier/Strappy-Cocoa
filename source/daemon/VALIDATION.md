# Step 0 validation — 2026-10-01

The implementation is complete; the original **iOS 5 acceptance gate remains
open**. `koolphone5` is an iPhone 5 (`iPhone5,2`, armv7) running **iOS 8.4.1,
build 12H321**, with a rootful jailbreak. Its package inventory reports
`com.lukezgd.everuntether-bin 1.3.6`. Rootless packaging is built and inspected,
but has not been installed on a newer rootless device. No harness migration
has started.

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
- Install/test the rootless `.deb` on a newer rootless device; local packaging
  inspection is not rootless runtime validation.
- Establish first-unlock/jailbreak activation dependencies for each target
  configuration; this run does not resolve first-unlock timing.
- Do not infer uninterrupted locked-device work from process survival: the
  measured 188-second gap is a concrete limit of this experiment.
