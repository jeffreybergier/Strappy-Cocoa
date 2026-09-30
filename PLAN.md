# Strappy daemon migration plan

Status: proposed implementation checklist. All tasks are unchecked; no daemon
implementation or device validation has been completed as part of writing this
plan.

## Objective

Run Strappy's harness in a launchd-managed service so accepted work can continue
when the UI closes. The daemon owns execution and all writes to `strappy.sqlite`;
the UI sends commands, receives change notifications, and reads stored state
through a dedicated read-only database layer.

The first milestone is deliberately smaller: install one heartbeat daemon with
Strappy and prove that it runs independently on a jailbroken iPhone running
iOS 5. Complete that device experiment before moving harness responsibilities.

## Step 0: install and verify a heartbeat daemon on iOS 5

- [ ] Complete the heartbeat implementation and real-device acceptance checks
  below before starting harness migration.

### Deliverable

Add one small daemon executable, provisionally named `strappyd`, to Strappy's
build and installation package. Use portable C and the existing Altivec
toolchain, preserving the required armv7/arm64 builds. Install it through the
same `.deb` as the app, with one launchd job per device. Launchd starts and
supervises it; opening the UI is not required to start it.

The daemon has only three behaviors:

- [ ] Log startup, including timestamp, PID, and effective UID.
- [ ] Log a heartbeat every 60 seconds while the process is scheduled, including
   timestamp, PID, and a monotonically increasing heartbeat counter.
- [ ] Handle SIGTERM cooperatively and log shutdown from normal execution context.

Use an interruptible wait rather than a busy loop. Flush each log record so it
can be observed immediately. Do not perform logging inside a signal handler.
Keep the diagnostic log bounded with a simple rotation policy and document its
exact on-device path and the command used to follow it. Log no credentials or
user content. Treat the recurring heartbeat as a validation feature to revisit
once the real service exists.

### Packaging and launchd lifecycle

- [ ] Extend `source/iOS/Makefile` to build, sign, and package the executable and
  launchd plist alongside the app. Include all required runtime dependencies.
- [ ] Support the existing rootful and rootless package layouts. The iOS 5 gate is
  specifically the rootful package; rootless support requires a separate,
  newer-device test. Resolve executable, resource, library, and log paths
  explicitly instead of relying on a shell environment or current directory.
- [ ] Start by configuring the service to run as `mobile`. Confirm the effective
  identity in its startup log and provision writable log directories for it.
- [ ] Use launchd configuration compatible with the target OS to start on load and
  restart after unexpected exit. Keep lifecycle compatibility handling in the
  appropriate platform boundary; modern launchctl commands cannot be assumed
  to exist on iOS 5.
- [ ] Package scripts load the job after installation and unload it before upgrade
  or removal. Upgrade replaces the executable and reloads exactly one job.
  Removal stops the process and removes its registration.
- [ ] Do not rely on modern XPC, ServiceManagement, or unified logging APIs in this
  first executable. Do not fork or detach it from launchd.

### Acceptance checks on a real iOS 5 device

Record the device/OS/jailbreak, package version, service identity, commands,
timestamps, and relevant log excerpts for these checks:

- [ ] Install the normal Strappy package. Without opening Strappy, verify one
   daemon process and observe startup plus at least three heartbeats roughly
   60 seconds apart while the device is awake.
- [ ] Open, background, and terminate the UI. Verify the daemon keeps the same PID
   and its heartbeat counter continues.
- [ ] Lock the device for several minutes, then unlock it and inspect the log.
   Record whether heartbeats continued, paused during sleep, or the process
   restarted. A timer does not guarantee CPU execution during device sleep;
   observed gaps must be understood before claiming locked-device execution.
- [ ] Terminate the daemon unexpectedly. Verify launchd starts one replacement,
   with a new startup record and PID, rather than duplicate processes.
- [ ] Reboot and, where the jailbreak requires it, reactivate the jailbreak.
   Verify automatic startup without launching Strappy. Document any dependency
   on jailbreak activation or first unlock.
- [ ] Upgrade/reinstall the package and verify only one job/process remains.
   Remove the package and verify the service stops and stays unloaded.

Build success is not device validation. If no iOS 5 device is available, leave
this milestone explicitly awaiting verification. Do not proceed to harness
migration until the basic service behavior is established.

Step 0 introduces no IPC, database access, provider requests, scanning, tools,
or changes to how the UI currently runs the harness.

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

- [ ] Implement and validate the service protocol described below.

Use one versioned protocol over a local Unix-domain socket as the initial
transport. C XPC requires macOS 10.7 and NSXPCConnection requires 10.8, so neither
can be the only transport for Tiger/PPC support. An optional XPC adapter can be
considered later if it offers a concrete benefit.

- [ ] Define framed, explicitly serialized messages with bounded payload sizes,
  protocol versions, request IDs, structured errors, and replies. Never send
  raw C structures across PPC/Intel/ARM or 32/64-bit boundaries.
- [ ] Restrict socket access and validate peers against the intended local user.
  Keep the IPC event loop responsive while execution happens on workers.
- [ ] Add readiness and compatibility negotiation, service-instance identity,
  subscriptions, and a state snapshot operation.
- [ ] Establish commands for submitting prompts, cancelling runs, changing session
  options, creating/deleting sessions, managing model/database approvals, and
  starting scans or database study.
- [ ] Establish events for session, ledger, catalog, and run-state changes. Send
  persistent-state notifications only after the associated transaction commits.
- [ ] Define reconnect semantics: subscribe and obtain a snapshot/revision without
  losing changes between those operations. Treat notifications as refresh hints,
  not the only record of state. Resynchronize after a gap or service restart.

- [ ] Validate framing, malformed input, disconnects, duplicate requests,
  incompatible versions, and slow subscribers with offline tests before
  connecting providers.

## Step 2: separate database readers from writers

- [ ] Establish daemon-only writes and a dedicated UI read-only layer.

The current `strappy_db_open` opens read/write with creation enabled and sets
WAL mode. Many query paths ensure the schema. Even the read-only sidebar reader
calls `strappy_db_initialize` first. These paths must be separated explicitly.

- [ ] Only the daemon creates the store, initializes the schema, seeds catalogs,
  performs maintenance, or writes shared state.
- [ ] UI readers open an existing compatible database read-only after service
  readiness. Audit history, options, models, catalogs, and renderer queries for
  hidden writes and fallback/default mutations.
- [ ] Keep SQLite WAL and verify the SQLite library actually shipped by Altivec,
  including cross-process access and permissions for the database, WAL, and
  shared-memory files. Never open the live store as `immutable`.
- [ ] Bound snapshot lifetimes. Refresh/release sidebar and catalog read
  transactions on invalidation and before iOS background suspension so they
  cannot indefinitely prevent checkpoint progress.
- [ ] Define behavior for startup races, a missing store, incompatible app/service
  versions, upgrades, and service unavailability.

This deliberately couples the UI reader to the schema of the daemon shipped
with it. Read-only access is an ownership rule, not strong security isolation
between unsandboxed processes sharing a user identity.

Preserve normalized semantic storage and existing DB-first UI rules. The
repository pins `PRAGMA user_version` at 1 and requires explicit permission for
migration steps or a version increase; this plan does not authorize either.

## Step 3: move one prompt through the daemon

- [ ] Complete a prompt through the daemon independently of the UI lifetime.

Reuse `strappy_session_send_prompt_with_events_and_load` and the existing C
Responses runtime. Extract execution from `StrappySession` without moving
WebView rendering into the service.

- [ ] Persist accepted work and its stable client request ID before acknowledging
  acceptance. Distinguish accepted, running, completed, failed, cancelled, and
  interrupted states. Deduplicate submission retries durably.
- [ ] Move active-run tracking, processing status, and cancellation ownership out
  of UI objects. Closing the UI or losing IPC does not cancel accepted work.
- [ ] Preserve one active prompt per session initially; define bounded concurrency
  across sessions and audit process-global configuration before expanding it.
- [ ] Keep cancellation responsive during provider requests and tool execution.
- [ ] Give the daemon explicit resource, CA certificate, database, and working
  directory paths rather than relying on the app's main bundle.
- [ ] Move provider credential retrieval/refresh behind the service boundary.
  Validate Keychain identity/access separately on Mac and jailbroken iOS;
  authentication presentation stays in the UI and secrets stay out of logs.

- [ ] Submit one prompt, close the UI, reopen it, and recover the correct stored
  history and active/completed state.
- [ ] Test lost acknowledgments, reconnects, cancellation, and two clients
  attempting to submit to the same session.

## Step 4: transfer remaining workers and mutations

- [ ] Move filesystem scanning and database study behind daemon commands.
- [ ] Move model catalog refresh and provider account changes behind commands.
- [ ] Move model/database approvals, session changes, and shared defaults behind
  commands.
- [ ] Audit both Objective-C bridges for remaining workers and database writes.
- [ ] Verify there is no competing in-process harness fallback.

Move filesystem scanning, database study, model catalog refresh, provider
account changes, model/database approvals, session changes, and shared defaults
behind daemon commands. Retain local UI-only preferences where appropriate.

Audit `StrappySession.m` and `FileScanner.m` for every write and background
worker. Convert runtime events into UI refreshes through their existing public
interfaces. Ensure the completed migration has one execution owner and no
remaining UI database writes or competing in-process harness fallback.

## Step 5: recovery and platform lifecycle

- [ ] On restart, reconcile persisted accepted/running work and publish truthful
  run state. Initially mark interrupted work explicitly instead of blindly
  replaying side-effecting Bash or file tools. Conversation history alone does
  not prove whether an interrupted external operation already happened.
- [ ] Implement cooperative shutdown, cancellation of owned child processes,
  bounded logging, checkpoint policy, and clean upgrade/uninstall behavior.
- [ ] On Mac, install one per-user LaunchAgent using Retro-Cloud-Sync as a reference
  for service installation, resources, paths, and logs. Use suitable modern
  registration where supported while preserving the legacy compatibility path.
- [ ] On iPhone, validate credentials, entitlements, filesystem access, rootful and
  rootless packaging, first unlock, sleep, and memory-pressure behavior. Start
  with `mobile`; any need for additional privilege must be established rather
  than making all Bash/file execution root by default.
- [ ] Revisit heartbeat logging after validation. Define real service health and
  idle behavior without keeping a phone awake merely to emit log messages.

## Validation and completion

- [ ] Complete clean iOS/macOS builds for all required architectures and review
  the full output for warnings.
- [ ] Run shared-core offline tests and add IPC/recovery tests as applicable.
- [ ] Complete the device scenarios below and record verified outcomes.
- [ ] Keep generated build and analysis logs out of commits.

For implementation changes, run the required clean iOS/macOS builds for all
repository architectures and capture the full output, resolving warnings.
Run `make -C source/linux clean test` for shared C/database/runtime changes and
add meaningful offline IPC/recovery tests. Keep generated logs out of commits.

Device tests must cover UI termination, daemon termination during a provider
request and during a tool call, duplicate submission, reconnect, cancellation,
WAL readers during ongoing writes, installation, upgrade, and removal. Include
the iOS 5 device and a legacy Mac as well as newer supported configurations.
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
