# Local service (steps 0–2)

`strappyd` is a portable C service supervised by launchd. It owns store
initialization, bundled catalog seeding, and all database writes. The app opens
existing stores read-only after IPC v2 readiness. Unmigrated mutation/worker
entrypoints fail explicitly until steps 3–4. See [STORAGE.md](STORAGE.md) for
ownership, snapshot lifetimes, startup/upgrade behavior, and runtime validation;
[PROTOCOL.md](PROTOCOL.md) describes the wire contract.

The normal iOS `.deb` includes a signed armv7/arm64 executable (deployment
minimums iOS 5/7). Its only dynamic dependency is `/usr/lib/libSystem.B.dylib`.
SQLite and cJSON are statically linked from the shipped Altivec libraries.
The daemon reads `BundledModels.json` from the explicitly supplied app directory. Both package variants
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

## Mac installation and lifecycle

The Mac app embeds a PPC/i386/x86_64/arm64 `strappyd` under
`Contents/Library/LaunchServices`. Like Retro-Cloud-Sync's
`RCServiceController`, Strappy installs a stable per-user copy and a
LaunchAgent. This is standard macOS background-service support; the heartbeat
is the step 0 validation workload, not a separate daemon feasibility experiment.

On app launch, before provider/Keychain bootstrap, `StrappyServiceController`
installs or updates these files:

- `~/Library/Application Support/Strappy/Service/strappyd` (mode 0700).
- `~/Library/LaunchAgents/com.altivecintelligence.strappyd.plist` (mode 0600).
- Log: `~/Library/Logs/Strappy/heartbeat.log`, with the same 64 KiB rotation
  and `.1` backup as iOS. New log directories/files are 0700/0600.

The agent runs as the logged-in user, without root privileges. It statically links Altivec SQLite/cJSON and uses a stable copy of
`BundledModels.json` beside the installed service. It has no bundled framework
dependency; the legacy compiler also links
Apple's system `libgcc_s.1.dylib`. Paths in the plist are absolute. A stable copy
outside the bundle lets the service survive closing, moving, or replacing the
app. A per-user file lock serializes installation; reopening an unchanged app
keeps the existing daemon. Changed binary/plist/catalog content unloads the old job,
atomically replaces the files, and loads one replacement.

`XPAppKit` owns launchd compatibility: Tiger uses `OnDemand = false` and plain
`load`/`unload`; newer systems use `KeepAlive = true` and the Aqua session flag.
The plist registers startup at login. Actual logout/login and newer Mac runtime
coverage remain separate from the recorded Tiger tests.

Explicit maintenance commands (run as the same user, not with sudo):

```sh
"/Applications/Strappy.app/Contents/MacOS/Strappy" --install-daemon
"/Applications/Strappy.app/Contents/MacOS/Strappy" --uninstall-daemon
tail -F "$HOME/Library/Logs/Strappy/heartbeat.log"
```

Quit Strappy and run `--uninstall-daemon` before removing the app if the service
should also be removed. It unloads the job and deletes the plist and installed
executable, retaining logs, the installation lock file, and all user app data.
Deleting the app alone leaves the installed service; launching Strappy again
installs it again. The maintenance command-line options do not initialize the UI or provider
credentials. Installing starts the service, which initializes its store. Provider and scanner workers remain unavailable; store ownership has moved.
