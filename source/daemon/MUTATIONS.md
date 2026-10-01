# Remaining service ownership (step 4)

The daemon now owns all shared-store mutations and all harness workers. The
`StrappySession` and `FileScanner` Cocoa interfaces send commands and read the
committed store. They contain no writer-capability guards, database mutation
calls, provider execution fallback, filesystem traversal, catalog download, or
Database Study loop. The old UI run registry and unused execution callbacks were
removed. UI-only drafts, pending prompt retry keys, appearance preferences,
rendering and font staging remain local.

One runtime owner serializes prompts, study, scanning, catalog refresh, and
metadata/account changes. Conflicting commands return `busy`; clients must not
optimistically update persisted state. Database calls commit before persistent
invalidation events. Catalog invalidations also hint that transient worker status
changed; consumers query `work_status`, rather than interpreting events as rows.
IPC socket handling and worker status/cancellation stay responsive during work.

## Workers

A scan command accepts an absolute root and full/quick mode. The daemon chooses
its own platform profile, validates SQLite candidates, and commits batches of
100 through the existing scanner. Committed batches invalidate the UI catalog,
including when later traversal fails. Shutdown cooperatively interrupts traversal.
The UI scan start/finish notifications follow the service's status; reopening or
reconnecting can observe an already-running scan. The legacy instance scan entry
points now request service acceptance; catalog results are snapshots and final
completion arrives through the existing notifications.

Database Study creates its session, inherits the default account, sets the
internal assistant set and fixed name, then persists an accepted `prompt_runs`
row before replying. Its isolated batch loop runs in the daemon. Each batch uses
the existing study tools and normalized ledger; three batches without progress
fail the run. Cancellation, completion, failure and restart interruption use the
same durable states as prompts. A new client reads the study session and run;
closing the initiating UI does not stop study. Unfinished study is not replayed.

Model refresh accepts an account ID. The daemon reads that active account's
endpoint and Keychain credential and downloads/persists its catalog. It uses
explicit configuration, without a process-environment credential fallback. Curl
progress cancellation interrupts a download during shutdown.

Scan and catalog status are transient per service instance. `work_status` reports
the active operation and the last worker's operation/terminal state. Restart
clears that status; the UI reports an observed in-flight operation as failed or
interrupted rather than reporting success. Startup marks persisted unfinished
scan runs as errors without claiming a complete inventory or sweeping unseen
files. Scans/catalog refresh are not durably scheduled or automatically replayed.
Study and prompt state are durable. See [RECOVERY.md](RECOVERY.md).

## Accounts, credentials, and settings

Commands cover provider account creation/update/archive; manual model
creation/update/archive; allowed/default models; database approval/visibility;
session creation/deletion/options; future-session defaults; round context
inclusion; study-value deletion/reset; credential save, refresh and sign-out.
The same shared validation and typed tables remain authoritative. No schema
migration or `user_version` change was added.

Interactive OAuth device authorization and user input remain in the UI. The UI
passes completed OAuth credentials or a user-entered bearer token to the daemon
over its owner-only, peer-authenticated socket. Keychain writes, token refresh,
identity checks and credential deletion happen in the daemon. UI Keychain reads
remain for authentication presentation and account editors. Daemon errors expose
only stable statuses, never credential/provider diagnostics. Archiving commits
first, then attempts credential removal; an inaccessible leftover Keychain item
cannot reactivate an archived account. Metadata updates restore the previous
bearer credential if the database update fails. Keychain and SQLite cannot form
one atomic transaction.

Mutations use a single connection/attempt. If a reply is lost, clients refresh
stored state and explicitly report an unknown acknowledgment; they do not
implicitly duplicate creation, study, or authentication changes. Prompt submission
retains its durable retry-key behavior from step 3. Study's key is stored with the
run, but starting study is not a retryable create-session transaction.

Active-session options use a process-local draft shared by session identity.
Editing a loaded option does no IPC, database work, or conversation rendering.
The options panel reuses its account/model/assistant catalogs until explicit
catalog invalidation or presentation, and run-state checks use the service-fed
cache. Only changed fields are merged, so stale controller copies cannot undo
another field's local edit. Service refreshes preserve the draft. Navigating away
and back preserves it; terminating the UI before Send discards it.

Send captures the draft and its field mask, then a worker persists the stable
retry request, saves the options, and submits the prompt. A rejected or
unacknowledged settings save prevents submission and retains the draft for an
explicit retry. The pending request records a durable `optionsFlushed` flag
before submission; lost submission replies reuse the original prompt/key without
reapplying options to an already accepted run. Option editing is disabled while
acceptance remains unresolved. No database schema or wire-version change is
needed. The save and submission are separate commands: the daemon still arbitrates
other clients and rejects conflicting operations with `busy`.

Successful options mutations invalidate only sessions; model/account/default
mutations invalidate catalogs, and context inclusion invalidates the ledger.
The client preserves topic bits and combines queued callbacks on the main loop.
Session-only invalidation does not regenerate conversation HTML or reload the
WebView. Reconnect refreshes all committed state while preserving local drafts.

Other metadata mutation APIs, including future-session defaults, retain their
existing synchronous contracts. Authentication refresh uses its background
thread. The active-session draft path does not change those API contracts.

## Verification

`make -C source/linux clean test` includes `mutation_harness.py`. It creates
accounts/models/defaults/sessions through IPC, verifies stored changes through a
separate read-only connection, rejects malformed values/duplicate JSON keys,
scans a real fixture database, checks conflicting mutations during study, cancels
study, exercises no-progress failure and tool-backed study completion, downloads
an offline catalog, interrupts a blocked download on shutdown, and reconnects.
The original prompt, IPC, read-only and WAL regression coverage remains enabled.

`mutation_device_probe.c` is a provider-free native probe for a fresh isolated
service store. It exercises metadata mutations, a real scan, approvals,
visibility, and status recovery using separate client connections. It must never
be pointed at a user's live store. See [VALIDATION.md](VALIDATION.md) for evidence
and the distinction between command-level device tests and graphical lifecycle
validation.

`session_options_device_probe.m` tests the production Cocoa edit/send bridge
against delayed/failing transport replacements. From `source/iOS`, build with
`make -f ../linux/session_options_device_probe.mk session-options-probe` after a
normal build. Run `build-release/session-options-probe` as mobile on a test
phone. The probe uses an already-loaded in-memory session, private process
preferences, and no live daemon, database writes, credentials, or provider calls.
It covers shared drafts, field-mask merging, invalidation, main-loop responsiveness,
failed saves, and durable submission retries. This measures the bridge rather
than simulating graphical taps.
