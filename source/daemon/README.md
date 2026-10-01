# Heartbeat service (step 0)

`strappyd` is a portable C diagnostic process supervised by launchd. It has no
IPC, database, credentials, provider, scanner, or UI dependencies. The UI still
owns the existing harness. Heartbeats are temporary validation instrumentation;
revisit them when migrating the real service.

The normal iOS `.deb` includes a signed armv7/arm64 executable (deployment
minimums iOS 5/7). Its only dynamic dependency is `/usr/lib/libSystem.B.dylib`.
No app resources or additional libraries are needed. Both package variants
use the job label `com.altivecintelligence.strappyd` and run as `mobile:mobile`.

| File | Rootful | Rootless |
| --- | --- | --- |
| Executable | `/usr/libexec/strappyd` | `/var/jb/usr/libexec/strappyd` |
| LaunchDaemon | `/Library/LaunchDaemons/com.altivecintelligence.strappyd.plist` | `/var/jb/Library/LaunchDaemons/com.altivecintelligence.strappyd.plist` |
| Log | `/var/log/strappy/heartbeat.log` | `/var/jb/var/log/strappy/heartbeat.log` |

Follow the rootful log with `tail -F /var/log/strappy/heartbeat.log` (prefix
`/var/jb` for rootless). If the device lacks `tail`, fetch snapshots with
`ssh koolphone5 'cat /var/log/strappy/heartbeat.log'` or copy it with `scp -O`.
Records contain UTC time, event, PID, effective UID, and a per-process counter.
Startup has counter zero; the first heartbeat follows a 60-second interruptible
wait. Every record is flushed and closed. SIGTERM wakes the wait through a
nonblocking self-pipe; only normal execution writes the shutdown record.

Before a record would exceed 65,536 bytes, the current log replaces
`heartbeat.log.1`. At most two 64 KiB files are retained (128 KiB total for
service-created logs). The directory is owned by mobile with mode 0700;
new logs have mode 0600. Logs contain no user content. Removal retains these
bounded diagnostic files, but removes the executable and job registration.

The package provisions the log directory and uses `/bin/launchctl load` and
`unload` on rootful devices, or `/var/jb/bin/launchctl` on rootless devices,
compatible with the legacy lifecycle interface. Package script interpreters
also use the installation prefix (`/bin/sh` or `/var/jb/bin/sh`); their PATH
explicitly includes the matching jailbreak tools. `RunAtLoad` starts
without opening Strappy; `KeepAlive` restarts an exited process with launchd
throttling. Pre-install and pre-removal scripts unload an existing job before
replacement/removal; configuration loads exactly one job. All runtime paths
are absolute, and the job's working directory is `/`.

Build with `make -C source clean release`. Run the offline signal/rotation
regression check with `make -C source/linux daemon-test` (also part of `test`).
For device acceptance, install the normal `.deb`, inspect `launchctl list
com.altivecintelligence.strappyd`, and follow the checklist in `PLAN.md`.
A scheduled timer does not keep iOS awake: locked-screen observations must not
be described as guaranteed execution during deep sleep. Rootless installation
has been exercised on iOS 15.0/Dopamine; see
`VALIDATION.md` for coverage and remaining screen-state/reboot checks. An
actual iOS 5 device still requires validation.
