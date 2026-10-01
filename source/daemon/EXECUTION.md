# Prompt execution (step 3)

The daemon accepts prompts for configured sessions, persists their
stable client request keys, and runs `strappy_session_send_prompt_with_events_and_load`
on its own worker. Closing the UI, terminating a client or losing a reply leaves
accepted work owned by the service. Reopened clients read normalized conversation
history and `prompt_runs` through read-only APIs.

The UI saves an unresolved submission and its random key in local defaults before
sending. It reuses that key after reconnect, including when the session is opened
again. An identical retry returns the original run; a different prompt/session
under the same key is a conflict. Transport IDs are unrelated to these keys.
The wire limit currently permits 4024 UTF-8 prompt bytes; the UI rejects larger
prompts explicitly. Acknowledgment means durable acceptance, not completion.

The run lifecycle is accepted, running, completed, failed, cancelled or
interrupted. The daemon persists processing/retry fields as typed columns; the
UI reconstructs transient status JSON for the existing C WebView renderer.
Session/ledger/run events invalidate DB-backed views. WebView work stays in the
client, and no UI prompt execution fallback remains. Submission-pending spinners
are temporary presentation state, not evidence that a daemon run is active.

There is one execution worker and no pending-work queue across sessions. Other
submissions get a busy error, whereas retries of the same key succeed. This
bounds concurrency while process-global curl CA configuration, OAuth CA state,
credential callbacks, identity and localization remain configured once per
process. The database writer cache already serializes connections. Increasing
concurrency requires another audit; it is not a matter of adding threads.

IPC handlers run on bounded command threads separate from that worker and from
the socket loop. Cancel requests name both session and run, persist their intent,
and set a mutex-protected cancellation flag. HTTP, retry waits, OAuth refresh and
tools cooperate through the existing cancellation callbacks. Bash terminates and
reaps its owned child group. SIGTERM stops acceptance, drains command handlers,
cancels/joins execution and then logs shutdown. Unfinished persisted work becomes
interrupted at startup and is never automatically replayed. Full crash-ledger
repair, orphan cleanup after SIGKILL, and power/lifecycle recovery remain step 5.

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
credentials never enter diagnostic logs or the run table. macOS disables
Keychain interaction for the background executable; inaccessible credentials
fail the run. Unlock/access authorization belongs to the user-facing application
or Keychain UI. The implementation does not bypass an existing Keychain ACL.
iOS signs the daemon with `com.altivecintelligence.Strappy` as its Keychain access
group, preserving the normal mobile identity. Locked-device accessibility and
first unlock are still platform validation work.

A read-only, noninteractive access probe is available:

```
strappyd --credential-probe /absolute/path/strappy.sqlite
```

It prints only account and accessible counts, never identifiers, tokens or
expiry data. It performs no refresh or provider request. An accessible count of
zero does not distinguish a locked Keychain, an ACL denial, or a missing item.

Step 4 supplies account changes, model refresh/approvals, session
creation/options/deletion, scanning and Database Study. Authentication
presentation stays in the UI; see [MUTATIONS.md](MUTATIONS.md).

## Verification

`make -C source/linux clean test` includes actual Unix-socket, SQLite and Responses
runtime tests against an offline loopback HTTP fixture. The execution harness
covers lost acknowledgments, durable retries, input conflicts, simultaneous
clients, cross-session busy behavior, cancellation in HTTP and Bash, SIGTERM
child cleanup, restart interruption without replay, and reopened read-only state.
A separate reader-capability executable checks denied writes and readiness
revocation. No host provider credentials are used by these tests.

`execution_device_probe.c` supports isolated device fixtures. `seed` copies only
account/model configuration from an existing read-only source into a fresh target;
it copies no conversation history or credentials. `submit` uses real IPC;
`state` and `history` inspect persisted results without printing user content.
See [VALIDATION.md](VALIDATION.md) for observed native results and explicit gaps.
