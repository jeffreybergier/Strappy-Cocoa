# Prompt execution

The daemon accepts prompts for configured sessions, persists their
stable client request keys, and runs `strappy_session_send_prompt_with_events_and_load`
on its own worker. Closing the UI, terminating a client or losing a reply leaves
accepted work owned by the service. Reopened clients read normalized conversation
history and `prompt_runs` through read-only APIs.

The UI saves an unresolved submission and its random key in local defaults before
sending. It reuses that key after reconnect, including when the session is opened
again. An identical retry returns the original run; a different prompt/session
under the same key is a conflict. Transport IDs are unrelated to these keys.
The combined submission carries an 8-byte session ID, 64-byte retry key,
4-byte settings length, optional transient settings JSON, and UTF-8 prompt.
IPC v6 permits up to 1 MiB (1,048,576 bytes) of UTF-8 prompt text and a separate
4 KiB of settings JSON. The UI reports either limit explicitly; options do not
reduce prompt capacity. Submission buffers live on the heap. The daemon validates
the header before allocating a large frame and waits for its complete body
before validating/persisting options and accepting the run atomically. Partial
transfers never execute; a retry after a lost reply returns the same durable run.
Prompt-only submissions remain supported with the same text limit. Control
requests, replies, and events retain their 4 KiB cap. App and daemon must both
speak v7. Acknowledgment means durable acceptance, not completion.

The run lifecycle is accepted, running, completed, failed, cancelled or
interrupted. The daemon persists processing/retry fields as typed columns; the
UI reconstructs transient status JSON for the existing C WebView renderer.
Session/ledger/run events invalidate DB-backed views. WebView work stays in the
client, and no UI prompt execution fallback remains. Submission-pending spinners
are temporary presentation state, not evidence that a daemon run is active.

Each active session owns an execution worker and cancellation state. There is
at most one active run per session and at most 32 ordinary prompt workers per
service. A separate maintenance worker runs scan/catalog/study jobs. Short
control mutations do not reserve an execution slot; create/delete for an idle
session remain available during prompts and maintenance. Deleting or changing
options/context on an executing session returns busy. There is no durable queue
of future prompts: acceptance starts the session worker, and repeated keys
resolve to the original run even when capacity is full.

The control dispatcher serializes mutations and acceptance. Combined submissions
validate settings, then save changed fields and accept the run in one SQLite
transaction. A failure rolls both back; a durable retry never reapplies settings.
Configuration/CA paths and credential callbacks are initialized before workers.
The writer cache serializes short DB operations; network and tool execution do
not hold its lock. Thread-local execution ownership routes cancellation through
HTTP, tools and credential refresh to the correct session. Account credential
locks continue to serialize refreshes for a shared account.

IPC handlers reuse bounded per-connection command workers separate from execution and from
the socket loop. Cancel requests name both session and run, persist their intent,
and set a mutex-protected cancellation flag. HTTP, retry waits, OAuth refresh and
tools cooperate through the existing cancellation callbacks. Bash terminates and
reaps its owned child group. SIGTERM stops acceptance, drains command handlers,
cancels/joins execution and then logs shutdown. Unfinished persisted work becomes
interrupted at startup and is never automatically replayed. [RECOVERY.md](RECOVERY.md)
describes atomic ledger repair, missing tool-result diagnostics, owned Bash
group cleanup after SIGKILL, and remaining platform validation.

## Paths and credentials

All daemon arguments are absolute: log file, store file, and resource directory.
The process working directory is explicitly `/`; tools use the persisted session
working directory. Runtime resources include all prompt/assistant/tool/skill/DB
JSON catalogs, Font Awesome metadata, `Info.plist` (for User-Agent version), and
`cacert.pem`. Mac installs these beside the stable service binary and reloads if
any resource changes. iOS uses the package's explicit app resource directory.
The stable daemon has no dependency on the app's main bundle or a copied dynamic
Altivec framework.

`source/daemon/StrappySession.m` is the daemon-side Objective-C/C session boundary.
It uses the existing Keychain account formats and owns provider credential
retrieval and OAuth refresh, including refresh cancellation and identity checks.
Prompt execution never returns secrets through IPC. Step 4 authentication
commands transfer user-entered credentials over the authenticated private socket;
credentials never enter diagnostic logs or the run table. The Mac app-owned
service preserves default Keychain interaction so the logged-in user can approve
access, including Leopard's authorization dialogs. No prompt suppression occurs
at normal daemon startup; permission denial still fails the operation. The
implementation does not bypass an existing Keychain ACL.
iOS signs the daemon with `com.altivecintelligence.Strappy` as its Keychain access
group, preserving the normal mobile identity. Locked-device accessibility and
first unlock are still platform validation work.

A read-only access probe explicitly disables interaction only in its dedicated
command-line process (not the normal daemon):

```
strappyd --credential-probe /absolute/path/strappy.sqlite
```

It prints only account and accessible counts, never identifiers, tokens or
expiry data. It performs no refresh or provider request. An accessible count of
zero does not distinguish a locked Keychain, an ACL denial, or a missing item.

Step 4 supplies account changes, model refresh/approvals, session
creation/options/deletion, scanning and Database Study. Authentication
networking and credentials stay in the daemon; presentation stays in the UI; see [MUTATIONS.md](MUTATIONS.md).

## Verification

`make -C source/daemon/tests clean test` includes actual Unix-socket, SQLite and Responses
runtime tests against an offline loopback HTTP fixture. The execution harness
covers lost acknowledgments, durable retries, input conflicts, simultaneous
clients, parallel sessions with isolated cancellation, create/delete during blocked
provider execution, atomic settings rollback and retry preservation, cancellation in HTTP and Bash, SIGTERM
child cleanup, restart interruption without replay, and reopened read-only state.
A separate reader-capability executable checks denied writes and readiness
revocation. No host provider credentials are used by these tests.

`execution_device_probe.c` supports isolated device fixtures. `seed` copies only
account/model configuration from an existing read-only source into a fresh target;
it copies no conversation history or credentials. `submit` uses real IPC;
`state` and `history` inspect persisted results without printing user content.
See [VALIDATION.md](VALIDATION.md) for observed native results and explicit gaps.
