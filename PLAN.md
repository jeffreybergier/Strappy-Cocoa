# Strappy daemon migration plan

Status: steps 0–4 are implemented. The daemon owns prompt execution, scanning,
Database Study, catalog refresh, shared settings, and account mutations. Step 4
has offline coverage and isolated native command/storage validation on Tiger/PPC,
iOS 8/armv7, and iOS 15/arm64. Full graphical lifecycle and remaining recovery
validation are step 5; iOS 6 device compatibility remains unverified.
Step 6 tracks the architectural follow-ups from the refactor review against
`main`; prompt capacity is addressed, with the remaining items still open.

See `source/daemon/README.md` for operation and `source/daemon/VALIDATION.md`
for device evidence. This plan supersedes the earlier iOS 5 acceptance gate
mentioned in those implementation-era notes.

## Objective

Run Strappy's harness in a launchd-managed service so accepted work can continue
when the UI closes. The daemon owns execution and all writes to `strappy.sqlite`;
the UI sends commands, receives change notifications, and reads stored state
through a dedicated read-only database layer.

The first milestone is deliberately smaller: install one heartbeat daemon with
Strappy and prove that it runs independently on rootful iOS 8 and rootless
iOS 15. That initial device experiment is complete. Preserve the legacy armv7
build and APIs for eventual iOS 6 testing: the iOS 8 build is expected to work
there, but compatibility remains unverified until tested on an actual device.

## Step 0: install and verify the heartbeat service on iOS 8/15 and Mac

- [x] Complete the heartbeat implementation and initial iOS 8/15 device
  experiment before starting harness migration. Carry the explicitly deferred
  coverage below into later platform validation.

### Deliverable

Add one small daemon executable, provisionally named `strappyd`, to Strappy's
build and installation package. Use portable C and the existing Altivec
toolchain, preserving the required armv7/arm64 builds. Install it through the
same `.deb` as the app, with one launchd job per device. Launchd starts and
supervises it; opening the UI is not required to start it.

The daemon has only three behaviors:

- [x] Log startup, including timestamp, PID, and effective UID.
- [x] Log a heartbeat every 60 seconds while the process is scheduled, including
   timestamp, PID, and a monotonically increasing heartbeat counter.
- [x] Handle SIGTERM cooperatively and log shutdown from normal execution context.

Use an interruptible wait rather than a busy loop. Flush each log record so it
can be observed immediately. Do not perform logging inside a signal handler.
Keep the diagnostic log bounded with a simple rotation policy and document its
exact on-device path and the command used to follow it. Log no credentials or
user content. Treat the recurring heartbeat as a validation feature to revisit
once the real service exists.

### Packaging and launchd lifecycle

- [x] Extend `source/iOS/Makefile` to build, sign, and package the executable and
  launchd plist alongside the app. Include all required runtime dependencies.
- [x] Support the existing rootful and rootless package layouts, validated on
  iOS 8.4.1 and iOS 15.0 respectively. Resolve executable, resource, library,
  and log paths explicitly instead of relying on a shell environment or
  current directory.
- [x] Start by configuring the service to run as `mobile`. Confirm the effective
  identity in its startup log and provision writable log directories for it.
- [x] Use launchd configuration compatible with the target OS to start on load and
  restart after unexpected exit. Keep lifecycle compatibility handling in the
  appropriate platform boundary; modern launchctl commands cannot be assumed
  to exist on the legacy iOS targets, including eventual iOS 6 support.
- [x] Package scripts load the job after installation and unload it before upgrade
  or removal. Upgrade replaces the executable and reloads exactly one job.
  Removal stops the process and removes its registration.
- [x] Do not rely on modern XPC, ServiceManagement, or unified logging APIs in this
  first executable. Do not fork or detach it from launchd.

### Accepted iOS 8/15 device checks

Device/OS/jailbreak, package version, identity, commands, timestamps, and log
excerpts are recorded in `source/daemon/VALIDATION.md`. Completion here means
the initial service experiment is accepted with these specific coverage limits;
it does not mean every scenario has been tested on both devices.

- [x] Install the normal Strappy package without opening the UI; verify one
  daemon running as `mobile` and at least three heartbeats roughly 60 seconds
  apart. Verified on both devices. Awake state was established on iOS 8;
  iOS 15 screen/power state was not independently verified.
- [x] Open, background, and terminate the UI while the daemon retains its PID
  and continues its counter. Verified on iOS 8. The iOS 15 UI launch attempt
  was inconclusive and remains a follow-up.
- [x] Observe locking for several minutes and inspect the log after unlocking.
  On iOS 8 the PID survived with a 188-second heartbeat gap. This establishes
  process survival, not continuous execution during sleep. An explicit iOS 15
  lock/unlock observation remains a follow-up.
- [x] Terminate the daemon unexpectedly and verify exactly one replacement
  with a new startup record and PID. Verified on both devices. Explicit
  SIGTERM also logged cooperative shutdown on both devices.
- [x] Verify automatic startup after reboot on iOS 8 without opening Strappy.
  On iOS 15, use the user-requested respring instead: SpringBoard restarted,
  the daemon kept its PID, and a later heartbeat confirmed continued execution.
  The post-respring heartbeat arrived 137 seconds after daemon startup; its
  delay was not assigned a specific power-state cause. Respring is not reboot
  validation. Do not reboot `pinkphone-rsa` without a new user instruction.
- [x] Upgrade/reinstall and verify one daemon; remove and verify no process or
  job remains for over a minute. Verified on both devices, then restored the
  normal package and left the daemon running.

Build success alone is not device validation. The recorded iOS 8/15 results
establish the basic service behavior needed to begin step 1. iOS 6 device
coverage, remaining iOS 15 UI/screen-state checks, and reboot/first-unlock
questions are tracked under step 5 rather than blocking this initial milestone.

Step 0 introduces no IPC, database access, provider requests, scanning, tools,
or changes to how the UI currently runs the harness.


### Mac implementation and acceptance

Mac background-service support is an established platform capability. Follow
Retro-Cloud-Sync's working `RCServiceController` pattern rather than treating
this as another feasibility experiment: embed the daemon in the app, install
a stable per-user copy in Application Support, and register a LaunchAgent.
The heartbeat remains the initial workload; harness migration is still step 1
onward.

- [x] Build and bundle `strappyd` for PPC, i386, x86_64, and arm64 alongside the
  Mac app, retaining Mac OS X 10.4 support for the legacy slices.
- [x] Install/update the per-user service on app launch before credential
  bootstrap. Keep it running when Strappy quits, preserve its PID on unchanged
  relaunch, and provide explicit install/uninstall maintenance commands.
- [x] Keep Tiger/newer launchd compatibility in `XPAppKit`; use `OnDemand` on
  Tiger, `KeepAlive` on newer systems, and the appropriate login-session flags.
- [x] Verify startup, three heartbeats, UI-process independence, SIGTERM,
  unexpected-exit recovery, update, and uninstall on `x4-vm` (Tiger 10.4.11,
  PowerPC). Record results and any UI test limitations in
  `source/daemon/VALIDATION.md`. The GUI Keychain prompt was not approved;
  process termination was tested, while normal interactive quit remains
  unverified.

Actual logout/login and newer Mac runtime checks remain later platform
validation. A LaunchAgent runs for its user session; do not describe it as a
system daemon that continues through logout. The per-user installation and
bounded diagnostic logs do not introduce IPC, database ownership, or provider
execution.

## Target ownership after migration

| Responsibility | Owner |
| --- | --- |
| Prompt execution, provider calls, tools, scheduling, cancellation | Daemon |
| Database creation, schema setup, shared preferences and catalog writes | Daemon |
| Credential refresh, filesystem scanning, database study | Daemon |
| Read-only history and paginated catalog queries | Shared read library used by UI |
| Navigation, windows, selection, draft text, conversation rendering | UI |
| Authentication presentation and user input | UI coordinating with daemon |

Retain `StrappySession` and `FileScanner` as the UI-facing Objective-C
boundaries. Move their worker responsibilities behind a service client while
keeping view-controller changes contained. The daemon emits semantic events;
the UI uses the existing shared C renderer to generate HTML and JavaScript.

## Step 1: define the service boundary and portable IPC

- [x] Implement and validate the service protocol described below.

Use one versioned protocol over a local Unix-domain socket as the initial
transport. C XPC requires macOS 10.7 and NSXPCConnection requires 10.8, so neither
can be the only transport for Tiger/PPC support. An optional XPC adapter can be
considered later if it offers a concrete benefit.

- [x] Define framed, explicitly serialized messages with bounded payload sizes,
  protocol versions, request IDs, structured errors, and replies. Never send
  raw C structures across PPC/Intel/ARM or 32/64-bit boundaries.
- [x] Restrict socket access and validate peers against the intended local user.
  Keep the IPC event loop responsive while execution happens on workers.
- [x] Add readiness and compatibility negotiation, service-instance identity,
  subscriptions, and a state snapshot operation.
- [x] Establish commands for submitting prompts, cancelling runs, changing session
  options, creating/deleting sessions, managing model/database approvals, and
  starting scans or database study.
- [x] Establish events for session, ledger, catalog, and run-state changes. Send
  persistent-state notifications only after the associated transaction commits.
- [x] Define reconnect semantics: subscribe and obtain a snapshot/revision without
  losing changes between those operations. Treat notifications as refresh hints,
  not the only record of state. Resynchronize after a gap or service restart.

- [x] Validate framing, malformed input, disconnects, duplicate requests,
  incompatible versions, and slow subscribers with offline tests before
  connecting providers.

Step 1 is transport-only: mutation command IDs are established and return
structured `execution_not_ready` errors until steps 2–4 supply store ownership
and workers. The service snapshot reports identity, revision, and readiness;
it does not claim application state is available. The thread-safe post-commit
publication boundary is exercised by an offline synthetic worker. See
`source/daemon/PROTOCOL.md` and `source/daemon/VALIDATION.md` for the exact
contract, coverage, and remaining device limitations.

## Step 2: separate database readers from writers

- [x] Establish daemon-only writes and a dedicated UI read-only layer.

The current `strappy_db_open` opens read/write with creation enabled and sets
WAL mode. Many query paths ensure the schema. Even the read-only sidebar reader
calls `strappy_db_initialize` first. These paths must be separated explicitly.

- [x] Only the daemon creates the store, initializes the schema, seeds catalogs,
  performs maintenance, or writes shared state.
- [x] UI readers open an existing compatible database read-only after service
  readiness. Audit history, options, models, catalogs, and renderer queries for
  hidden writes and fallback/default mutations.
- [x] Keep SQLite WAL and verify the SQLite library actually shipped by Altivec,
  including cross-process access and permissions for the database, WAL, and
  shared-memory files. Never open the live store as `immutable`.
- [x] Bound snapshot lifetimes. Refresh/release sidebar and catalog read
  transactions on invalidation and before iOS background suspension so they
  cannot indefinitely prevent checkpoint progress.
- [x] Define behavior for startup races, a missing store, incompatible app/service
  versions, upgrades, and service unavailability.

This deliberately couples the UI reader to the schema of the daemon shipped
with it. Read-only access is an ownership rule, not strong security isolation
between unsandboxed processes sharing a user identity.

Preserve normalized semantic storage and existing DB-first UI rules. The
repository pins `PRAGMA user_version` at 1 and requires explicit permission for
migration steps or a version increase; this plan does not authorize either.

Step 2 validation and the temporary GUI restrictions are documented in
`source/daemon/STORAGE.md` and `source/daemon/VALIDATION.md`. Readiness uses IPC
v2 and a separate reader ABI; SQLite `user_version` remains 1. Paginated readers
copy only their view tables to private SQLite temp storage, releasing the live
WAL snapshot before returning to Cocoa. Runtime WAL tests passed on Tiger/PPC
and iOS 15/arm64; iOS 8 SSH was unavailable and remains deferred.

## Step 3: move one prompt through the daemon

- [x] Complete a prompt through the daemon independently of the UI lifetime.

Reuse `strappy_session_send_prompt_with_events_and_load` and the existing C
Responses runtime. Extract execution from `StrappySession` without moving
WebView rendering into the service.

- [x] Persist accepted work and its stable client request ID before acknowledging
  acceptance. Distinguish accepted, running, completed, failed, cancelled, and
  interrupted states. Deduplicate submission retries durably.
- [x] Move active-run tracking, processing status, and cancellation ownership out
  of UI objects. Closing the UI or losing IPC does not cancel accepted work.
- [x] Preserve one active prompt per session initially; define bounded concurrency
  across sessions and audit process-global configuration before expanding it.
- [x] Keep cancellation responsive during provider requests and tool execution.
- [x] Give the daemon explicit resource, CA certificate, database, and working
  directory paths rather than relying on the app's main bundle.
- [x] Move provider credential retrieval/refresh behind the service boundary.
  Validate Keychain identity/access separately on Mac and jailbroken iOS;
  authentication presentation stays in the UI and secrets stay out of logs.

- [ ] Submit one prompt, close the UI, reopen it, and recover the correct stored
  history and active/completed state.
- [x] Test lost acknowledgments, reconnects, cancellation, and two clients
  attempting to submit to the same session.

Implementation and wire semantics are documented in
`source/daemon/EXECUTION.md` and `source/daemon/PROTOCOL.md`. The worker is globally
serialized; configured existing sessions can submit/cancel, while creation and
other mutations remain step 4. IPC v3 / reader ABI 2 add typed durable run state;
SQLite `user_version` remains 1 and no migration steps were introduced.

Offline tests exercise the actual Responses runtime with a loopback provider,
including lost replies, concurrent clients, HTTP/tool cancellation, child cleanup
on SIGTERM, and restart interruption without replay. Native iOS 15/mobile
Keychain access and a live prompt completed independently of the submitting
client, with the expected answer recovered by a new reader and durable retry
returning the same run. Tiger/PPC submission and terminal retry work, but its
existing account credential was inaccessible to the noninteractive daemon.
The probe does not distinguish missing credentials from Keychain lock/ACL denial.
`koolphone5` refused SSH. Full graphical submit/close/reopen validation remains
unchecked; client-process independence is not claimed as an interactive GUI test.
See `source/daemon/VALIDATION.md` for exact coverage and remaining work.

## Step 4: transfer remaining workers and mutations

- [x] Move filesystem scanning and database study behind daemon commands.
- [x] Move model catalog refresh and provider account changes behind commands.
- [x] Move model/database approvals, session changes, and shared defaults behind
  commands.
- [x] Audit both Objective-C bridges for remaining workers and database writes.
- [x] Verify there is no competing in-process harness fallback.

Move filesystem scanning, database study, model catalog refresh, provider
account changes, model/database approvals, session changes, and shared defaults
behind daemon commands. Retain local UI-only preferences where appropriate.

Audit `StrappySession.m` and `FileScanner.m` for every write and background
worker. Convert runtime events into UI refreshes through their existing public
interfaces. Ensure the completed migration has one execution owner and no
remaining UI database writes or competing in-process harness fallback.

Implementation and command semantics are documented in
`source/daemon/MUTATIONS.md` and `source/daemon/PROTOCOL.md`. IPC v4 retains reader
ABI 2 and SQLite `user_version` 1. One daemon runtime owner serializes prompts,
study, scans, catalog refresh and account changes; conflicting mutations return
busy. UI-only presentation, drafts and authentication presentation remain local.
See `source/daemon/VALIDATION.md` for verified outcomes and explicit limitations.

The Cocoa performance follow-up is implemented: metadata commands run on a
serial background queue, catalog searches use an independent query worker, and
unhandled command failures reach an app-wide error channel. Active-session
options remain local drafts until Send. See `source/daemon/ASYNC_UI.md` for
operation policy and `source/daemon/VALIDATION.md` for test limits.

## Step 5: recovery and platform lifecycle

- [x] On restart, reconcile persisted accepted/running work and publish truthful
  run state. Initially mark interrupted work explicitly instead of blindly
  replaying side-effecting Bash or file tools. Conversation history alone does
  not prove whether an interrupted external operation already happened.
- [x] Implement cooperative shutdown, cancellation of owned child processes,
  bounded logging, checkpoint policy, and clean upgrade/uninstall behavior.
- [x] Extend the step 0 Mac LaunchAgent integration for the real harness,
  including resources, recovery and safe replacement on Tiger.
- [ ] Validate actual logout/login and newer Mac runtimes.
- [ ] On iPhone, validate credentials, entitlements, filesystem access, rootful and
  rootless packaging, first unlock, sleep, and memory-pressure behavior. Start
  with `mobile`; any need for additional privilege must be established rather
  than making all Bash/file execution root by default.
- [ ] Test on a real iOS 6 device using the legacy armv7 build (deferred by
  user instruction; no device available yet). Compatibility
  with the iOS 8 implementation is expected, not yet demonstrated. Cover
  installation, heartbeats, UI independence, shutdown/restart, and removal.
- [ ] Complete iOS 15 UI foreground/background/termination and explicit
  lock/unlock checks. Preserve observed scheduling gaps as limitations; do
  not claim uninterrupted work during sleep.
- [ ] Establish first-unlock and jailbreak-reactivation dependencies. iOS 8
  reboot startup and iOS 15 respring survival are recorded, but first-unlock
  timing and iOS 15 reboot behavior remain unverified. Keep `pinkphone-rsa`
  respring-only unless the user explicitly authorizes a reboot later.
- [x] Revisit heartbeat logging after validation. Define real service health and
  idle behavior without keeping a phone awake merely to emit log messages.

Step 5 implementation and health/checkpoint policy are documented in
`source/daemon/RECOVERY.md`. Atomic reconciliation repairs unfinished ledger and
scan records and closes missing tool-output pairs without replay; Bash guardians
stop owned groups after runtime death. Native recovery checks passed on Tiger,
iOS 8 and iOS 15. Platform coverage and remaining physical-device scenarios are
recorded separately in `source/daemon/VALIDATION.md`; unchecked items above are
not implied complete by implementation or build success.

## Step 6: address architectural review findings

The review of `exp/daemon` against `main` identified the following follow-ups.
Preserve daemon-owned execution and credentials, durable submission retries,
read-only UI storage, and conservative recovery while addressing them.

### Prompt capacity (high priority)

The review found that the 4,096-byte IPC payload limited prompt text to at most
4,020 UTF-8 bytes, with less space when session options accompanied it. This
restriction was absent from `main` and rejected ordinary pasted code, logs, and
documents before provider execution.

- [x] Define an explicit supported prompt-size limit and adapt IPC framing or
  bounded chunked submission to carry it, independently of control-message size.
  Keep prompt/options acceptance atomic and preserve retry-key deduplication.
- [x] Test prompts above 4 KB, multibyte UTF-8, option-bearing submissions, exact
  size boundaries, oversized-input rejection, and lost acknowledgments. Partial
  transfer must never start execution.

Implemented in IPC v6: 1 MiB of UTF-8 prompt text plus an independent 4 KiB
options allowance. Large submissions use heap buffers; control requests, replies,
and events remain capped at 4 KiB. Offline regressions cover exact limits,
partial transfers without acceptance, provider delivery, and durable retries.
Native runtime validation of large prompts remains unverified.

Relevant code: `source/shared/strappy_ipc.h`,
`source/shared/StrappySession.m`, and `source/daemon/strappy_execution.c`.

### GUI responsiveness during control commands (high priority)

`StrappyAsync commandTarget:` currently executes synchronously on the GUI
thread, including socket send/receive. Ordinary reply waits can take two
seconds, with additional reconnect overhead. Account operations can also wait
behind the account credential lock held during a network refresh; a client
timeout does not mean the server-side mutation was cancelled.

- [ ] Restore asynchronous, ordered control commands with main-thread
  completions. Audit callers that currently depend on inline completion and
  preserve committed-state rendering and explicit pending UI state.
- [ ] Keep credential serialization without allowing a blocked account command
  to stall unrelated control work. Preserve unknown-acknowledgment handling and
  never automatically replay ordinary mutations.
- [ ] Verify main-loop responsiveness during delayed replies, reconnects,
  database contention, and an account save/delete concurrent with credential
  refresh. Check completion ordering and late server-side commits after timeout.

Relevant code: `source/shared/StrappyAsync.m`,
`source/shared/strappy_store_client.c`, and `source/daemon/StrappySession.m`.
Update `source/daemon/ASYNC_UI.md` and the step 4 status notes to match the final
command policy.

### Scoped, incremental conversation invalidation (medium priority)

Current events identify only a topic. Every live `StrappySession` observes the
same notification, and run/ledger changes trigger full-history regeneration
even for unaffected conversations. DOM identity preservation avoids replacing
unchanged elements but does not avoid the database, rendering, and transfer cost.

- [ ] Include affected session identities in normal invalidations so unrelated
  conversations do not refresh. Preserve complete refresh on reconnect or when
  event detail is unavailable.
- [ ] Add revision-based row updates that cover changed and deleted rows behind
  the append cursor, including tool outputs and context-inclusion changes.
- [ ] Measure long-history updates with multiple open conversations on legacy
  targets. Verify isolated refreshes, bounded incremental work, coalescing,
  reconnect recovery, and preserved WebView disclosure/scroll state.

Relevant code: `source/daemon/strappy_ipc_server.c`,
`source/shared/StrappySession.m`, and `source/shared/strappy_session.c`.

### Detect incompatible existing schema shapes (medium priority)

Fresh stores no longer enforce global uniqueness of provider tool-call IDs,
but existing stores retain that constraint because `CREATE TABLE IF NOT EXISTS`
does not alter it. Both shapes currently pass the same schema identity/version
checks, so an upgraded store can reject valid repeated IDs while a fresh store
works.

- [ ] Validate the relevant schema constraints before advertising readiness and
  report an actionable incompatibility for unsupported existing stores. Do not
  silently reset user data or claim that initialization removed old constraints.
- [ ] Test fresh stores and stores created with `main`'s unique constraint,
  including repeated provider IDs across responses and sessions. Keep rejecting
  ambiguous duplicate IDs within one response.

Relevant code: `source/shared/strappy_db.c` and
`source/shared/strappy_db_responses.c`. Keep `PRAGMA user_version` pinned at 1.
This step authorizes planning detection, not a migration or version increase;
either still requires explicit user permission under `AGENTS.md`.

The review's targeted Linux app-boundary, authentication, control-client,
execution, and mutation checks passed. They do not establish completion of
these follow-ups or native GUI/device validation. Apply the implementation
validation requirements below when carrying out this step.

## Validation and completion

- [x] Complete clean iOS/macOS builds for all required architectures and review
  the full output for warnings.
- [x] Run shared-core offline tests and add IPC/recovery tests as applicable.
- [ ] Complete the device scenarios below and record verified outcomes.
- [ ] Keep generated build and analysis logs out of commits.

For implementation changes, run the required clean iOS/macOS builds for all
repository architectures and capture the full output, resolving warnings.
Run `make -C source/daemon/tests clean test` for shared C/database/runtime changes and
add meaningful offline IPC/recovery tests. Keep generated logs out of commits.

Device tests must cover UI termination, daemon termination during a provider
request and during a tool call, duplicate submission, reconnect, cancellation,
WAL readers during ongoing writes, installation, upgrade, and removal. Include
rootful iOS 8, rootless iOS 15, eventual iOS 6 device validation, and a legacy
Mac as well as newer supported configurations. Honor the respring-only
restriction on `pinkphone-rsa`.
Separate verified outcomes from untested assumptions in milestone notes.

## Effort and uncertainties

Initial planning estimates for one engineer familiar with this codebase and
with target devices available:

| Milestone | Estimate |
| --- | --- |
| Step 0 heartbeat packaging and initial device experiment | 1–3 engineering days |
| Prompt-service vertical slice after the heartbeat gate | 2–4 additional days |
| Useful Mac integration with reconnect and cancellation | Roughly 2–4 weeks |
| Reliable Mac/iPhone integration across the legacy matrix | Roughly 4–8 weeks |

The later ranges describe the broader implementation, not additive fixed bids.
Re-estimate after step 0 and the prompt slice. Device availability, signing and
Keychain behavior, jailbreak lifecycle differences, and recovery semantics are
the largest uncertainties. Launchd ownership does not guarantee execution
through sleep, reboot, or memory pressure.

## Research references

- Existing Strappy boundaries: `source/shared/strappy_session.c`,
  `StrappySession.m`, `FileScanner.m`, `strappy_db.c`, and
  `strappy_db_sidebar.c`; iOS packaging is in `source/iOS/Makefile`.
- Sibling Retro-Cloud-Sync: `source/macOS-app/RCServiceController.m` demonstrates
  per-user service installation; `DaemonStatusView.m` polls daemon-owned status.
  Reuse the lifecycle lessons, while building command/reply IPC for Strappy.
- [Apple: Creating launch daemons and agents](https://developer.apple.com/library/archive/documentation/MacOSX/Conceptual/BPSystemStartup/Chapters/CreatingLaunchdJobs.html)
- [Apple: Designing daemons and services](https://developer.apple.com/library/archive/documentation/MacOSX/Conceptual/BPSystemStartup/Chapters/DesigningDaemons.html)
- [Apple: Creating XPC services and API availability](https://developer.apple.com/library/archive/documentation/MacOSX/Conceptual/BPSystemStartup/Chapters/CreatingXPCServices.html)
- [Apple: SMAppService](https://developer.apple.com/documentation/servicemanagement/smappservice)
- [SQLite: Write-ahead logging and read-only access](https://sqlite.org/wal.html)
- [Theos: Rootless layout considerations](https://theos.dev/docs/rootless)
