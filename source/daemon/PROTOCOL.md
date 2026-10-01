# Strappy local service protocol v1

Step 1 implements transport readiness, not application readiness. The UI keeps
its current execution path until steps 2–4 transfer ownership. No provider,
credential, database, or scanner code is linked into `strappyd` yet.

## Endpoint and trust

The daemon takes its existing absolute heartbeat-log argument and creates
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
SIGPIPE-safe send APIs. The portable client and server use only system C
libraries. Server sockets, wake descriptors, and locks are close-on-exec.

## Framing

Every integer is explicitly serialized, unsigned, most-significant byte first.
No C layouts, pointers, native `long`, or native byte order are transmitted.
A frame is a 24-byte header followed by exactly `length` payload bytes:

| Offset | Width | Field |
| --- | --- | --- |
| 0 | 4 | ASCII `STPY` |
| 4 | 2 | Protocol version, currently 1 |
| 6 | 2 | Kind: request 1, reply 2, event 3 |
| 8 | 4 | Operation |
| 12 | 4 | Request ID; zero only for unsolicited events |
| 16 | 4 | Payload length, maximum 4096 |
| 20 | 4 | Status, zero in requests and events |

A connection starts with `hello`. Clients use nonzero strictly increasing
request IDs per connection, including failed requests. A repeated or lower ID
returns `duplicate_request` and is never executed, even if its payload differs.
Reconnect before exhausting the 32-bit counter. Replies echo operation and ID.
There is no durable request acceptance or replay cache in step 1. After a lost
reply, control operations can be retried on a new connection. Mutation retries
must wait for step 3's durable client-request deduplication; transport IDs are
not durable submission identities.

Malformed magic/kind/oversized frames cause immediate disconnect. Unsupported
header versions receive a v1 incompatible-version reply then disconnect. Invalid
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

The version interval must contain 1. `hello` is accepted once per connection.
Operations 2–4 are repeatable with new IDs. Every successful control reply is
exactly 32 bytes:

| Offset | Width | Field |
| --- | --- | --- |
| 0 | 16 | Opaque random service-instance identity from `/dev/urandom` |
| 16 | 8 | Instance-local committed-state revision |
| 24 | 4 | Readiness flags: bit 0 transport ready; bit 1 application ready |
| 28 | 4 | Maximum frame payload (4096) |

The current readiness value is 1: the service must not be used to open the
application store or execute work yet. The current state snapshot represents
service identity/readiness/revision, not invented session or ledger contents.
Step 2 will add the compatible store/read-layer contract; step 3 will add active
run state. Such additions require explicit negotiated protocol evolution.

## Mutation command boundary

The following stable operation IDs establish the service boundary. All return
`execution_not_ready` in step 1, without parsing or executing the supplied body.
No acknowledgement implies durable acceptance. Their required semantic inputs
below are a contract for the later typed command implementations; body encoding
and success-result schemas are deliberately not advertised as supported by v1
transport-only readiness. Negotiate an updated protocol before enabling them.

| ID | Command | Required semantic inputs for execution implementation |
| --- | --- | --- |
| 16 | submit_prompt | Stable client submission identity, session ID, UTF-8 prompt |
| 17 | cancel_run | Stable run ID |
| 18 | set_session_options | Session ID, typed changed options, expected revision |
| 19 | create_session | Stable client request identity, assistant-set ID, optional name |
| 20 | delete_session | Session ID, expected revision |
| 21 | approve_model | Model ID, approval boolean, expected revision |
| 22 | approve_database | Database ID, approval boolean, expected revision |
| 23 | start_scan | Stable client request identity, scan profile |
| 24 | start_study | Stable client request identity, approved database ID |

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

## Events, workers, and reconnect

Event IDs 32–35 mean session, ledger, catalog, and run-state changed, respectively.
They have request ID/status zero and the same 32-byte identity/revision/readiness
payload as a snapshot. They invalidate the entire named topic. No events are
emitted for rejected commands or heartbeat ticks.

`strappy_ipc_server_committed` is the worker-to-event-loop boundary. Call it only
after the corresponding transaction commits. It increments the revision under a
mutex, records pending topic bits, and wakes the loop through a nonblocking pipe.
A full pipe cannot lose pending topic bits. Multiple changes may coalesce into
one refresh hint. Workers own all future database/provider work; the loop owns
all client descriptors. Stop and join workers before destroying the server.
The offline fixture simulates commits without touching an application database.
Real transaction-hook integration belongs to steps 2–4.

Connect, authenticate, send `hello`, then send `subscribe`. Subscription and its
snapshot watermark are established in one event-loop operation, so a concurrent
commit is covered by the returned snapshot or a subsequent event. Replies and
events share one ordered stream; dispatch by kind and correlate replies by ID.
Events already queued before `unsubscribe` can arrive before its reply.

Notifications are refresh hints. On first subscription, reconnect, changed
instance identity, or a revision gap, refresh **all** daemon-owned state from its
future authoritative read layer. An event can represent coalesced changes; never
interpret it as an ordered transaction log. Events for different topics may
share a revision. Only discard events at/below a watermark after refreshing all
state through that watermark; do not discard a second topic just because the
first topic carried the same revision. Until a consistent read snapshot is
available in step 2, clients must only display transport readiness.

Do not cancel accepted future work when a client disconnects. The client API is
synchronous, has bounded monotonic deadlines, and belongs on a background thread.
After any I/O/protocol error, close it and resynchronize on a fresh connection.
Request correlation, reconnection backoff, and UI refresh scheduling remain the
responsibility of the future `StrappySession`/`FileScanner` service adapter.

## Validation

`make -C source/linux daemon-test` exercises the actual daemon plus a worker
fixture. The complete offline suite includes it. Native, provider-free smoke
checks use the same portable C client:

```sh
strappyd --ipc-probe /absolute/private/directory/service.sock
```

See `VALIDATION.md` for observed platforms and limitations. The probe does not
connect to providers or access application data.
