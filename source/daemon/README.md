# Local service

## Source and build ownership

`source/daemon` contains the provider client/SSE parser, Responses runtime,
execution tools, scanner, catalog fetcher, OAuth and Keychain implementations.
The app targets do not compile these sources or include their private headers.
`strappy_session_execution`, `strappy_run_execution`, and
`strappy_study_execution` contain the write/execution halves of the shared read
interfaces. `strappy_tool_metadata` retains descriptions, schemas and display
metadata for UI rendering and prompt previews. Shared option/event types no
longer require client or credential-configuration headers.

The app's `StrappyAuthentication` classes display sign-in state and verification
codes. IPC v5 delegates device authorization, polling, token exchange, storage,
refresh decisions and cancellation to the daemon. Credential status exposes
only presence, display identity and whether refresh is needed. Saved bearer and
OAuth tokens are never returned to the UI. API-key editors start blank: leaving
the field untouched preserves the saved key; entering a replacement updates it.
Clearing an edited optional bearer field removes that credential.

Portable and native harness sources now live in `source/daemon/tests`.
`make -C source/daemon/tests clean test` runs the offline suite, including the
app-boundary and authorization-manager regressions. The old
`make -C source/linux ...` entry point forwards to this directory. Test targets
may explicitly compile daemon sources; normal iOS/macOS app targets may not.
Native `auth_device_probe.mk` runs the same authorization-manager tests with
synthetic provider/credential callbacks; it never accesses user credentials.


`strappyd` has a portable C runtime and a small Cocoa credential boundary, and is a service supervised by launchd. It owns store
initialization, bundled catalog seeding, and all database writes. The app opens
existing stores read-only after IPC v8 readiness. The service also owns scans,
study, catalog refresh, account changes and shared mutations. See [STORAGE.md](STORAGE.md) for
ownership, snapshot lifetimes, startup/upgrade behavior, and runtime validation;
[PROTOCOL.md](PROTOCOL.md) describes the wire contract.
[ASYNC_UI.md](ASYNC_UI.md) describes background Cocoa commands, search snapshots,
and asynchronous error delivery.

The normal iOS `.deb` includes a signed armv7/arm64 executable (deployment
minimums iOS 5/7). Foundation, Security, CoreFoundation and SystemConfiguration are system dependencies.
The networking, TLS, SQLite and cJSON libraries are statically linked from Altivec.
Daemon entitlements include the app's Keychain group while execution remains mobile.
The daemon reads prompt resources, `Info.plist` and `cacert.pem` from its explicit
resource directory. It executes one prompt at a time independently of UI lifetime.
See [EXECUTION.md](EXECUTION.md) for retry, cancellation and credential behavior. Both package variants
use the job label `com.altivecintelligence.strappyd` and run as `mobile:mobile`.

| File | Rootful | Rootless |
| --- | --- | --- |
| Executable | `/usr/libexec/strappyd` | `/var/jb/usr/libexec/strappyd` |
| LaunchDaemon | `/Library/LaunchDaemons/com.altivecintelligence.strappyd.plist` | `/var/jb/Library/LaunchDaemons/com.altivecintelligence.strappyd.plist` |
| Log | `/var/log/strappy/heartbeat.log` | `/var/jb/var/log/strappy/heartbeat.log` |

Follow the rootful log with `tail -F /var/log/strappy/heartbeat.log` (prefix
`/var/jb` for rootless). If the device lacks `tail`, fetch snapshots with
`ssh koolphone5 'cat /var/log/strappy/heartbeat.log'` or copy it with `scp -O`.
Records contain UTC time, event, PID and effective UID. The historical filename
is retained, but periodic heartbeats and their counter were removed in step 5.
Every startup/shutdown record is flushed and closed. SIGTERM/SIGINT wake the
event loop through a nonblocking self-pipe; only normal execution writes the
shutdown record. Use `--ipc-probe /absolute/path/service.sock` for on-demand
readiness. See [RECOVERY.md](RECOVERY.md) for recovery, health and idle policy.

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
regression check with `make -C source/daemon/tests daemon-test` (also part of `test`).
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
LaunchAgent that runs the same durable execution and recovery code as iOS.

On app launch, before provider/Keychain bootstrap, `StrappyServiceController`
installs or updates these files:

- `~/Library/Application Support/Strappy/Service/strappyd` (mode 0700).
- `~/Library/LaunchAgents/com.altivecintelligence.strappyd.plist` (mode 0600).
- Log: `~/Library/Logs/Strappy/heartbeat.log`, with the same 64 KiB rotation
  and `.1` backup as iOS. New log directories/files are 0700/0600.

The agent runs as the logged-in user, without root privileges. It statically links Altivec SQLite/cJSON and uses a stable copy of
the complete prompt catalogs, `Info.plist` and CA bundle beside the installed service. It has no bundled framework
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

## Response diagnostics during the refactor

`STRAPPY_RAW_JSON_DEBUG_CAPTURE` currently defaults to `1` in
`strappy_debug_capture.h`. The existing capture mechanism writes request JSON
and the unprocessed response body beneath the store's `debug/sessions` directory,
keyed by session, turn, request and attempt. Authorization headers are not
captured; JSON remains outside SQLite. Define the macro as `0` to disable it.

Provider tool-call IDs are scoped to their response rather than globally unique.
Tool outputs and memory provenance resolve the corresponding call within the
active session; calls repeated in later rounds or sessions retain distinct
internal item IDs. Duplicate IDs within one response are rejected as ambiguous.
If response normalization/storage fails, the runtime saves a terminal
`response_persistence_failed` API result without the rejected output. The
existing API-error timeline renders that diagnostic in the WebView, including
when the conversation is reopened.

This changes the creation schema only. `user_version` remains `1`, and no
migration is provided. Existing stores retain their old global unique constraint;
installing a new build does not remove it or reset user data.
