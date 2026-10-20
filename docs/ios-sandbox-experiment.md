# iOS sandbox / daemon experiment

Branch: `experiment/ios-sandbox-ipc`, started 2026-10-17.
Target: koolphone5 (expected iOS 8.4.1 / armv7).

Follow-up: the [DEB notification experiment](deb-notifications-experiment.md)
demonstrated visible local notifications from an unsandboxed DEB app using
`SBAppUsesLocalNotifications`. An IPA/storage conversion is not required for
that demonstrated result.

The experiment is authorized to depart from AGENTS.md's unsandboxed DEB-only
rule. Production packaging and the installed service are unchanged.

## Evidence scope

Isolated permission variants established native IPC/SQLite access and visible
local notification delivery. Actual filesystem access succeeded despite policy
denials, so sandbox enforcement was not established. The subsequent DEB
experiment showed that an IPA/storage conversion is unnecessary for the
notification route. Conditional conversion research is tracked in
[TODO.md](../TODO.md); this document records the probe and its technical limits.

## Questions and research

Can a container-installed IPA use the existing authenticated Unix socket at
`/var/log/strappy/service.sock`? Can it also read the daemon's WAL-backed store
at `/var/mobile/Library/Application Support/Strappy/strappy.sqlite`? Does local
notification display work with that installation?

These are separate questions. The production UI uses IPC v8 for commands and
notifications, but still opens SQLite directly for reads. Its
`+[StrappySession sessionsDatabasePath]` uses Application Support under the
process home directory; a container app would need an explicit shared-store
path even if the socket works. Direct UI database reads are a requirement for
the eventual conversion; the daemon remains the sole writer.

Research sources:

- [Apple's sandbox entitlement reference](https://developer.apple.com/library/archive/documentation/Miscellaneous/Reference/EntitlementKeyReference/Chapters/EnablingAppSandbox.html)
  describes macOS sandbox keys and explicitly distinguishes iOS. Do not assume
  macOS temporary-exception keys apply to an iPhone.
- [Trail of Bits' 2011 iOS security evaluation](https://media.blackhat.com/bh-us-11/DaiZovi/BH_US_11_DaiZovi_iOS_Security_WP.pdf),
  pages 19–21, describes the older container profile's filesystem and Unix
  socket restrictions. It is historical evidence, not proof for iOS 8.4.1.
- [SandScout's extracted sandbox rules](https://github.com/malus-security/sandscout/blob/master/factsAndRules.pl)
  include iOS absolute-path file exceptions and separate outbound socket rules.
  File access and socket access must be tested independently; granting a file
  exception does not by itself establish socket permission.
- [WebKit's sandbox SPI declarations](https://github.com/WebKit/WebKit/blob/main/Source/WTF/wtf/spi/darwin/SandboxSPI.h)
  provide the diagnostic function signatures and filter values used here.
  These private APIs belong only to this experiment.

## Isolated probe

`source/daemon/tests/sandbox_probe.py` reuses the production iOS Makefile and
builds a small native test delegate with the production IPC implementation,
Altivec SQLite and `AILocalNotificationCenter`. It does not launch production
bootstrap, read credentials, submit prompts, or mutate the database. The only
SQL is a read-only schema count, whose value is not recorded.

Each permission variant has its own bundle ID and URL scheme, so all can coexist with
the installed Strappy app:

| Variant | Entitlements beyond application identity |
| --- | --- |
| `baseline` | None |
| `read-only` | Read/write exception for Strappy's log/socket directory; read-only exception for its store directory |
| `read-write` | Read/write exceptions for those two directories |

Both `/var` and canonical `/private/var` spellings are included. There are no
platform-application, no-sandbox, no-container, or root-directory exceptions.
The read/write store variant diagnoses WAL/SHM access needs; it still opens
SQLite with `SQLITE_OPEN_READONLY`. It is not a recommendation to grant the
eventual UI write access. No rootless or newer-OS result is implied.

`--ipc-version 5` generates a private copy of the IPC codec/header with only the
wire version changed. HELLO/SNAPSHOT/SUBSCRIBE and their 36-byte replies have
the same layout in v5 and v8 (checked against `88247ad^`). This is a fixture-only
adapter for the installed older daemon, not production protocol negotiation.
The default remains v8. Never use the adapted codec to send newer commands.

`--seatbelt` adds `seatbelt-profiles = [container]` and a separate `.seatbelt`
identity. `--self-sandbox` adds a separate `.self` identity and explicitly calls
the dynamically resolved `sandbox_init("container", SANDBOX_NAMED, ...)`
before starting IPC. The latter is diagnostic: applying a profile after UIKit
startup is not equivalent to a normally sandboxed launch. Each process reports
`sandbox_check`, policy decisions, and actual filesystem operations separately.

Build each variant from the checkout root:

```sh
python3 source/daemon/tests/sandbox_probe.py baseline
python3 source/daemon/tests/sandbox_probe.py read-only
python3 source/daemon/tests/sandbox_probe.py read-write
python3 source/daemon/tests/sandbox_probe.py baseline analyze
python3 source/daemon/tests/sandbox_probe.py baseline validate-package
python3 source/daemon/tests/sandbox_probe.py baseline --ipc-version 5 --self-sandbox
```

Repeat package validation for the other variants. Builds are clean and include
both armv7 and arm64. Artifacts and matching symbols are under
`build/experiments/ios-sandbox/<variant>/output/iOS/release/Strappy/`.
The extra flags insert `ipc5/`, `seatbelt/`, and/or `self-sandbox/` between the
variant and `output`. Use the same flags for analysis/package validation.

## Device procedure

First verify SSH, OS, mobile identity, installed daemon health, and installer
availability. Do not replace the production DEB just to run the probe. A protocol
mismatch is distinct from a sandbox failure.

Create a benign outside-container control file owned by mobile, mode 0644, at
`/var/mobile/tmp_altivec/strappy-sandbox-control.txt`, containing exactly
`sandbox control fixture` followed by a newline. Verify mobile can read/write it
over SSH before interpreting the probe's denial. The probe rewrites the first
byte with the same `s` to test write enforcement without changing the fixture.
Policy queries use its canonical `/private/var/...` path.
An ENOENT result or a missing
fixture is inconclusive; an EACCES/EPERM denial with that verified fixture is
evidence that the process still has filesystem restrictions. A container home
path and absence of broad entitlements alone do not prove enforcement on a
jailbroken device.

Stage IPAs under `~/tmp_altivec/strappy-sandbox-20261017` using distinct filenames.
Use the device's existing `appinst`, or `ipainstaller` if appinst is unavailable.
Do not use clean-install or force flags. Launch from SpringBoard or as mobile
with `uiopen strappy-sandbox-baseline://` (substitute the variant).
Launching an executable directly through SSH does not test container launch.

The screen and `Documents/sandbox-result.txt` in each app's data container show:

- Variant, OS, effective UID, and container home.
- Outside-control open result and errno.
- Actual one-byte fixture write, runtime sandbox state, and policy queries for
  process-fork, fixture file-write-data, and store file-read-data.
- Production authenticated socket connection (expected peer UID is mobile).
- Validated HELLO, SNAPSHOT, and SUBSCRIBE replies, including readiness and ABI.
- SQLite open/query return codes, without any user rows.
- Local notification authorization and submission result.

Accept notification permission, press Home, and wait at least three seconds.
The probe submits a generic alert during a short background task. Record actual
banner/Notification Center visibility separately; submission code zero is not
proof of display. Reopen to check alert/badge cleanup. Keep reports and relevant
sandbox denial logs locally under the ignored experiment build directory.

Successful SUBSCRIBE validates registration, not delivery of a later change
event, reconnect behavior, or execution while suspended. Those, plus the real
UI's shared-store path and background completion flow, need a follow-up full
app test after a working permission configuration is established.

## Results

Executed on koolphone5, iPhone5,2 / iOS 8.4.1 (12H321) / armv7, as mobile UID
501. After charging, SSH returned and the existing launchd daemon passed its
own v5 probe. No production app, daemon, account, or database was replaced.

| Probe | Runtime sandbox | IPC | Direct SQLite read |
| --- | --- | --- | --- |
| Plain IPA, v8 | Not measured in initial binary | Socket connected; v8 reply rejected against v5 daemon | Passed |
| Plain IPA, v5 | 0 (none) | HELLO/SNAPSHOT/SUBSCRIBE passed | Passed |
| Seatbelt entitlement, v5 | 0 (none) | All three passed | Passed |
| Explicit container initialization, v5 | 1 (attached), init returned 0 | All three passed | Passed |
| Read-only path exceptions plus explicit initialization, v5 | 1 (attached) | All three passed | Passed |

The owner confirmed visible local notifications for the probes, specifically
including `baseline-self`. That process also recorded authorization 2
(Authorized) and submission 0 (Submitted) after backgrounding. This is a real
display observation, distinct from the API return value. Foreground cleanup,
completion of an actual production prompt, suspended change-event delivery,
and reconnection were not validated.

**Sandbox enforcement is not established.** In the explicit-profile baseline,
`sandbox_check` returned 1 for process-fork, fixture file-write-data, and store
file-read-data (policy denies). Nonetheless the canonical-path fixture write
succeeded (one byte), and SQLite queries succeeded. The read-only-exception
variant also reported the store read denied while the actual read succeeded.
This is consistent with jailbreak policy bypass; the exact patch responsible
was not identified or changed. Therefore the observed IPC/store success cannot
prove that either permission variant will work with an enforcing sandbox.
The attached-profile test is useful evidence, not a production sandbox recipe.

Several first launches timed out before the delegate ran. Terminating only the
identified test-app PID and relaunching through `uiopen` produced the reports;
the production daemon was not restarted. Reinstallations changed the test data
container UUIDs. Reports and diagnostics remain in the ignored
`build/experiments/ios-sandbox/logs/` directory; test IPAs remain installed for
inspection. The read-write variant was cross-built, not installed/tested.

Validation: clean dual-architecture iOS probe builds and package checks passed;
Clang analysis of the probe passed. The offline Linux suite and clean production
builds passed for iOS armv7/arm64 and macOS PPC/i386/x86_64/arm64, with no compiler
warnings reported. Native arm64, other iOS versions, and native Mac execution
were not tested by this experiment.

## Path ownership for a future production conversion

The daemon must not depend on the IPA's randomized bundle/data path. Its DEB
should own a stable executable and resource directory; the current launchd
resource argument `/Applications/Strappy.app` must be replaced with the daemon's
own copy of prompts, metadata and CA certificates. The UI uses bundle/container
APIs for its own presentation resources and connects outward to the service.

Keep direct SQLite reads and the existing sole-writer daemon design. "Writer
ownership" does not mean an exclusive directory: the database must be in shared
storage accessible to both mobile processes, including required WAL/SHM access.
The app must resolve that shared location explicitly instead of assuming its
private Application Support directory equals the daemon's directory.

An app-specific stable directory with verified narrow sandbox access is one
candidate for these jailbroken targets. An app-group container is another
candidate on compatible OS versions, resolved by group identifier through
[NSFileManager](https://developer.apple.com/documentation/Foundation/FileManager/containerURL(forSecurityApplicationGroupIdentifier:)),
never by caching its UUID. Group provisioning/daemon membership and old-OS
support need their own tests; this is not an assumed solution for iOS 5.
Do not move the database into the IPA's private container or change the
production storage layout based on this probe alone.
