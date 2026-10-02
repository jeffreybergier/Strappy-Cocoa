# Strappy local service protocol v5

The daemon runs the C Responses runtime, scans, study, catalog refresh and
shared mutations. GUI clients read history, run state and catalogs from SQLite.
This is the step 4 protocol; prompt semantics from step 3 are retained.

## Endpoint and trust

The daemon takes absolute log, database, and resource-directory arguments and creates
`service.sock` beside that log. Endpoints are:

- Mac: `~/Library/Logs/Strappy/service.sock` (expand the home directory first).
- Rootful iOS: `/var/log/strappy/service.sock`.
- Rootless iOS: `/var/jb/var/log/strappy/service.sock`.

The containing directory must already be owned by the daemon's effective user,
mode 0700, and not a symlink. Sockets are 0600. Both sides authenticate peer
credentials against the intended UID (`getpeereid` on Apple, `SO_PEERCRED` on
Linux); the shipped probe expects its own effective UID. iOS clients must run
as mobile, not root. Unsandboxed processes with the same UID remain trusted.
A 0600 `service.lock` serializes servers and permits recovery of stale sockets
only by the lock owner. Regular-file/symlink collisions fail closed. The lock
file is retained across shutdown; unlinking a live lock would break exclusion.

The C `XPFoundation/strappy_ipc_platform` compatibility module isolates OS-specific credential, monotonic-clock, and
SIGPIPE-safe send APIs. The transport uses only system C libraries; execution commands additionally use
the shared C runtime and transient cJSON serialization. Server sockets, wake descriptors, and locks are close-on-exec.

## Framing

Every integer is explicitly serialized, unsigned, most-significant byte first.
No C layouts, pointers, native `long`, or native byte order are transmitted.
A frame is a 24-byte header followed by exactly `length` payload bytes:

| Offset | Width | Field |
| --- | --- | --- |
| 0 | 4 | ASCII `STPY` |
| 4 | 2 | Protocol version, currently 5 |
| 6 | 2 | Kind: request 1, reply 2, event 3 |
| 8 | 4 | Operation |
| 12 | 4 | Request ID; zero only for unsolicited events |
| 16 | 4 | Payload length, maximum 4096 |
| 20 | 4 | Status, zero in requests and events |

A connection starts with `hello`. Clients use nonzero strictly increasing
request IDs per connection, including failed requests. A repeated or lower ID
returns `duplicate_request` and is never executed, even if its payload differs.
Reconnect before exhausting the 32-bit counter. Replies echo operation and ID.
Transport request IDs are connection-local. Submission retry identities are
separate 256-bit keys persisted with the work before acceptance, as below.

Malformed magic/kind/oversized frames cause immediate disconnect. Unsupported
header versions receive a v4 incompatible-version reply then disconnect. Invalid
request kind, nonzero request status, or zero request ID similarly produce an
invalid-request reply then disconnect. The server handles partial reads/writes
and multiple frames on one stream. Each client has one bounded input frame and
an 8192-byte output queue, with at most 16 simultaneous clients. Extra clients
are closed; clients should back off before reconnecting. An overflowing output
queue disconnects that client instead of silently losing events. Incomplete
handshakes/frames or blocked output with no I/O progress time out after 30 seconds.
Idle subscribed clients do not expire. Each event-loop pass performs bounded
work per client; execution never occurs on this thread.

## Control operations and snapshot

| Operation | Name | Request payload | Success reply |
| --- | --- | --- | --- |
| 1 | hello | u32 minimum version, u32 maximum version | Snapshot |
| 2 | snapshot | Empty | Snapshot |
| 3 | subscribe | Empty | Snapshot, with subscription already active |
| 4 | unsubscribe | Empty | Snapshot, with subscription disabled |

The version interval must contain 4. `hello` is accepted once per connection.
Operations 2–4 are repeatable with new IDs. Every successful control reply is
exactly 36 bytes:

| Offset | Width | Field |
| --- | --- | --- |
| 0 | 16 | Opaque random service-instance identity from `/dev/urandom` |
| 16 | 8 | Instance-local committed-state revision |
| 24 | 4 | Readiness flags: bit 0 transport ready; bit 1 store ready; bit 2 execution ready |
| 28 | 4 | Maximum frame payload (4096) |
| 32 | 4 | Store reader ABI, currently 2 |

Production readiness is 7 after store/resource configuration and the execution
worker are ready. The transport fixture remains 1. Reader ABI 2 adds
`prompt_runs`; SQLite `user_version` remains 1. Old protocol versions fail closed.
The 36-byte snapshot is an identity/readiness/revision watermark. Read current
run state using the read-only run API after subscribing, just like history.

## Mutation command boundary

Operations 16 (submit prompt) and 17 (cancel run) use the binary payloads below.
Operations 28 (combined submission) and 29 (credential refresh) are described
below. Operations 18–27 are defined in the Step 4 command table at the end of this file.

Unknown operations return `unknown_operation`. Error replies use the numeric
status in the header and an un-NUL-terminated ASCII error code as payload:

| Status | Code |
| --- | --- |
| 1 | invalid_request |
| 2 | incompatible_version |
| 3 | handshake_required |
| 4 | duplicate_request |
| 5 | execution_not_ready |
| 6 | unknown_operation |

Logs and errors never include request payloads or credentials.

## Prompt and cancellation payloads

`submit_prompt` (16): u64 positive session ID, exactly 64 lowercase ASCII hex
characters representing a random 256-bit client retry key, then 1–4024 bytes of
valid UTF-8 prompt text (no NUL). The session and account/model must already be
configured through the metadata commands below. The UI persists a pending
submission before sending and retries the same key after an uncertain reply or
reconnect. It does not create conversation rows itself.

Success is u64 run ID followed by u32 state: accepted=1, running=2, completed=3,
failed=4, cancelled=5, interrupted=6. A transaction stores the key, session and
prompt before this reply. Identical retries return the original run and current
state, including after restart. Reusing a key with different input returns
`request_conflict` (8). There is no automatic replay of interrupted work.

One run may be accepted/running per session, with up to 32 ordinary session
workers and a separate maintenance worker. Another submission for an active
session returns `service_busy` (7). Each peer reuses one command worker; up to
16 peers can be connected. Commands on a control connection execute FIFO.
Storage waits and provider/tool work never block socket servicing.

`submit_with_options` (28): u64 session ID, 64-byte retry key, u32 settings-byte
length, that many bytes of a string-valued JSON `options` command, followed by
nonempty UTF-8 prompt text. The settings length may be zero. The full payload
must fit 4096 bytes. Settings must target the same session. Validation and
settings/run writes precede acknowledgement; the writes commit atomically.
Reply/state and prompt/session retry-key conflict semantics match operation 16.
On a retry of an accepted key, captured settings are not reapplied, even if the
retry's settings differ. The original committed run remains authoritative.

`refresh_credentials` (29): the same string-valued `refresh_oauth` JSON command
as the mutation boundary, with its normal result/status. Other actions are
rejected. The UI sends it on an independent connection; its worker does not
acquire the short-command control lock. The bounded reply timeout is 120 seconds.

`cancel_run` (17): u64 session ID followed by u64 run ID. The run must be the
session's latest run, protecting a newer prompt from a stale cancel. Success has
the same 12-byte run/state reply and confirms the cancellation request was
persisted; it does not promise execution has already stopped. Repeated cancels
and cancellation of an already terminal latest run succeed without replay.
Cancellation polls cover HTTP, retry waits, OAuth refresh and tools/Bash.

Error frames contain a numeric status and a stable ASCII identifier. In addition
to the transport errors, `storage_error` (9) means acceptance/state persistence
failed. Prompt commands carry no credentials or provider response payloads. Step 4
authentication commands carry credentials only to the daemon over the private,
authenticated socket; they are never logged or persisted in SQLite. A lost
connection never cancels work.

## Events, workers, and reconnect

Event IDs 32–35 mean session, ledger, catalog, and run-state changed, respectively.
They have request ID/status zero and the same 36-byte identity/revision/readiness
payload as a snapshot. They invalidate the entire named topic. No events are
emitted for rejected commands or heartbeat ticks.

`strappy_ipc_server_committed` is the worker-to-event-loop boundary. Call it only
after the corresponding transaction commits. It increments the revision under a
mutex, records pending topic bits, and wakes the loop through a nonblocking pipe.
A full pipe cannot lose pending topic bits. Multiple changes may coalesce into
one refresh hint. Workers own database/provider work; the loop owns
all client descriptors. Stop and join workers before destroying the server.
The offline fixture simulates commits without touching an application database.
Prompt acceptance, run transitions and runtime ledger/session callbacks publish
after their associated commits. While a subscribed peer has a command in
progress, topic bits retain every pending invalidation until its reply is queued.

Connect, authenticate, send `hello`, then send `subscribe`. Subscription and its
snapshot watermark are established in one event-loop operation, so a concurrent
commit is covered by the returned snapshot or a subsequent event. Replies and
events share one ordered stream; dispatch by kind and correlate replies by ID.
Events already queued before `unsubscribe` can arrive before its reply.

Notifications are refresh hints. On first subscription, reconnect, changed
instance identity, or a revision gap, refresh **all** daemon-owned state from its
authoritative read layer. An event can represent coalesced changes; never
interpret it as an ordered transaction log. Events for different topics may
share a revision. Only discard events at/below a watermark after refreshing all
state through that watermark; do not discard a second topic just because the
first topic carried the same revision. GUI clients open read-only connections only while their service monitor has
negotiated store readiness and the matching ABI. Disconnect revokes readiness;
reconnect causes a full committed-state refresh. Ordinary events preserve their
topic bits, coalesce on the main loop, and refresh only the affected views. Session
option changes do not invalidate the ledger or reload conversation HTML. Unsent
UI option drafts survive refreshes. The monitor uses a background
thread with bounded I/O and one-second retry delay, never socket I/O on the UI
thread. Retained paginated readers hold private temporary-table snapshots, not
live WAL read transactions. See [STORAGE.md](STORAGE.md).

Do not cancel accepted work when a client disconnects. The client API is
synchronous and has bounded monotonic deadlines. Short control operations run
on the GUI main thread; prompt/authentication work uses separate threads.
After any I/O/protocol error, close it and resynchronize on a fresh connection.
The current store monitor handles control-request correlation, reconnection
backoff, and UI refresh hints. The `StrappySession` adapter sends prompts and cancels on background threads.
It renders fresh DB state on service invalidation. `FileScanner` sends commands
and reads catalog snapshots; it no longer performs scanning in the UI.
Short metadata/acceptance commands and their completions run inline on the GUI
main thread, including the socket send/receive. A permanent caller-owned control
connection preserves their FIFO order. Background callers have independent
connections and cannot hold a client-side control lock. OAuth refresh is called by
the authentication background thread. No metadata command is automatically
retried after a lost reply. See [ASYNC_UI.md](ASYNC_UI.md) for completion and
app-wide asynchronous error delivery.

## Validation

`make -C source/daemon/tests daemon-test` exercises the actual daemon plus a worker
fixture. The complete offline suite includes it. Native, provider-free smoke
checks use the same portable C client:

```sh
strappyd --ipc-probe /absolute/private/directory/service.sock
```

See `VALIDATION.md` for observed platforms and limitations. The probe does not
connect to providers or access application data.

## Step 4 command payloads (IPC v5, reader ABI 2)

Operations 18–25 and 27 accept bounded UTF-8 JSON objects with string values only.
JSON is transient wire serialization, never a database document. Integer fields
are decimal strings, preserving 64-bit identifiers on PPC/32-bit clients and
avoiding floating-point rounding. Duplicate keys, non-string values, embedded
NUL, invalid UTF-8 and trailing input are rejected. The frame limit remains 4096
bytes, including all keys and credential text. Replies contain an identifier as
UTF-8 text for creation, the refreshed account identity for OAuth refresh, or an
empty success body. Other failures use the existing structured status codes.

| Operation | Payload |
| --- | --- |
| 18 set session options | `action=options`, fields below |
| 19 create session | `action=create_session`, `directory` |
| 20 delete session | `action=delete_session`, `session` |
| 21 approve model | `action=approve_model`, `id`, `value` (0/1) |
| 22 approve database | `action=approve_database`, numeric `id`, `value` (0/1) |
| 23 start scan | `path` (absolute), `quick` (0/1) |
| 24 start study | `directory`, `key` (64 lowercase hexadecimal characters) |
| 25 mutate | One of the actions below |
| 26 work status | Empty request; reply is three network-order u32 values: active operation, last worker operation, terminal run-state enum (0 means none) |
| 27 refresh catalog | `id` (provider account ID) |

Operation 25 actions:

- `options` / `defaults`: `session` (options only), `mask`, `model`, `account`,
  `assistant`, `directory`, `fallback` (defaults), `rounds`, `web_provider`,
  `web`, `bash`, `one_tool`, `quality`. Flags are 0/1; mask is the existing
  `strappy_session_option_mask`. The Cocoa adapter sends the complete options
  record; only selected fields change.
- `create_session`, `delete_session`, `approve_model`, `approve_database`: same
  payload as their dedicated operation IDs.
- `default_model`: `id`; `hide_database`: numeric `id`, `value` (0/1).
- `create_account`: provider `id`, `name`; `update_account`: account `id`, `name`,
  `endpoint`, `output`, optional `token`; `archive_account`: account `id`.
- `create_model` / `update_model`: provider `id`, `wire`, `name`, `context`,
  `output`, `reasoning`, `image`, `functions`, `price_input`, `price_output`,
  `price_read`, `price_write`. `archive_model`: provider `id`, `wire`.
- `delete_study`: assistant database `id`; `reset_study`: no other fields.
- `include_context`: `session`, model request `id`, `value` (0/1).
- `refresh_oauth`: account `id`, expected `identity`; `delete_oauth`: `id`.
  `save_oauth` is an internal service operation and is rejected over IPC.

Only prompt submission is automatically retried by the client. A lost mutation
acknowledgment requires reading committed state before an explicit retry.

## Authentication (v5)

Operation `auth` (30) accepts the same bounded, string-valued command objects.
It uses a connection command worker independently of the metadata control lock.
Version 5 requires a matching app/service pair; the store ABI and SQLite
`user_version` are unchanged.

- `credential_status`: account `id`. Returns an object with `present` and
  `needs_refresh` booleans and an `identity` display string. It performs no
  provider request and never returns a saved token.
- `auth_start`: account `id` and 64-character lowercase hexadecimal `key`.
  Validates an active ChatGPT account before starting a service-owned worker.
  At most sixteen retained jobs exist, with one unfinished job per account.
  A repeated retained key resolves to the same job; a different account using
  that key conflicts. A new key for a busy account is refused.
- `auth_status` / `auth_cancel`: the same account `id` and `key`. Replies contain
  `state`, `url`, `code`, and `identity`. States are `requesting_code`,
  `awaiting_user`, `signed_in`, `failed`, and `cancelled`. Only the public
  verification URL/code and final display identity cross IPC. Device IDs and
  access/refresh tokens remain in daemon memory/Keychain.

Start returns before network authorization completes. The app's background
presentation worker polls status every 250 ms while sign-in is open and sends
cancel when dismissed. Transport failure attempts cancellation with the same
key. Sign-out and account archival cancel jobs before removing credentials;
credential commits serialize with cancellation so a cancelled job cannot save
late credentials. Daemon shutdown cancels and joins authorization workers.
Jobs are bounded, transient service state: terminal slots can be reused, restart
forgets them, and no authorization is automatically replayed. Existing provider
polling/HTTP deadlines bound work if the UI disappears without cancelling.
