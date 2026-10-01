# Responsive Cocoa command and query boundary

The daemon remains the writer. UI event handlers must never wait for a socket
reply. `StrappyAsync` captures a bridge invocation and runs it on a serial command
worker; completion and notification delivery occur on the main thread. The
`StrappySession` and `FileScanner` mutation gateways reject main-thread calls
before connecting, so new callers cannot accidentally reintroduce the stall.
No wire-protocol change is needed: requests still receive correlated replies,
with the waiting confined to a worker.

## Choosing when to wait

| Operation | UI behavior | Confirmation |
| --- | --- | --- |
| Active-session model, account, tools, limits | Edit a shared local draft immediately | Send's existing worker saves captured fields before submitting the prompt |
| Future-session defaults | Show a pending edit; disable its editor while saving | Reload stored defaults on completion, including failure |
| Model/database approval or visibility, context inclusion, study reset/delete | Enqueue and return | Service notifications refresh committed values; failure reaches the common error channel |
| Conversation/account/model creation, account/model save, deletion | Keep the originating editor responsive; disable duplicate saves | Only select a returned identifier, dismiss, or remove selection after acknowledgment |
| macOS daemon install/verification | Queue launchctl work during startup | Initialize the store and show the main window on completion |
| Scan, catalog fetch, Database Study start | Enqueue acceptance request | Job status and stored run state report actual progress and completion |
| Prompt submission/cancellation, OAuth | Keep their existing background execution | Existing request keys/run state/authentication lifecycle remain authoritative |
| Model/database catalog search | Keep current results visible with loading status | Separate query worker delivers a fresh filtered snapshot; stale results are discarded |

No enqueue operation means that the daemon has succeeded. Even the proxy's BOOL
return only indicates that asynchronous completion has been scheduled. Callers
use `StrappyAsyncResult` for actual success, value, and error. Validation that
requires only form input still runs before enqueueing. Defaults are pending UI
control state, not persisted conversation state. Bulk approval changes capture
identifiers as one command and stop at the first failed item; already confirmed
items remain committed and are refreshed normally. These batches are not atomic.

## Ordering, ownership, and failure

Commands execute FIFO in this process. Queue locks protect only bookkeeping;
no IPC or database query runs under them. The bounded queue allows 128 waiting
operations. Rejection still schedules a completion, so controls are restored and
exactly one error is delivered. Ordinary commands are never implicitly retried.
A lost acknowledgment has an unknown outcome: reload stored state before retrying.
Prompt submission retains its existing durable request-key recovery behavior.
The queue is process-local and is not a durable outbox. iOS includes pending
commands in its existing background-task bookkeeping, but process termination
can still lose commands that have not reached the daemon.

The proxy is single-use. It owns a separate NSInvocation, copies NSCopying input
objects, and captures scalar arguments without narrowing. Mutable children of
collections must not be edited after submission. It installs a worker-local
NSError pointer rather than keeping a pointer to a UI stack variable. Context
and callback receivers live through delivery; callbacks must check screen and
selection relevance before navigation. It is for bridge/service calls, never
for moving a view/controller method to another thread.

Unhandled command errors publish `StrappyAsyncCommandFailedNotification` on the
main thread. Both app delegates queue their presentation, independent of the
original screen. A completion can explicitly consume an error when presenting it
locally. The payload contains an operation title and sanitized NSError, never
captured arguments, credentials, or exception details. Observed terminal scan
and catalog-fetch failures also use this channel. Prompt/Study execution errors
remain in their existing durable run/timeline state. This is not a persistent
cross-launch error inbox; reconnection reconstructs authoritative service state.

## Search and remaining local work

The query worker is independent of the command queue, so a slow daemon reply
cannot prevent a search from completing. New queries supersede old ones for the
same receiver/completion pair; queued stale work is skipped and running stale
results are discarded. Search callbacks also compare the captured search text
with the current text, covering the debounce interval.

Each query creates its own private SQLite catalog snapshot, filters and groups
it, and warms its first page before transferring exclusive ownership to the UI.
No mutable snapshot is used simultaneously by both threads. Bounded lazy paging
remains: later pages are small local SQLite reads on the UI thread. This change
does not claim that all UI disk work is asynchronous: sidebar/timeline reads,
small account/allowed-model lists, rendering, and lazy pages are still local work.
Those paths do not wait for daemon IPC.

## Native regression fixture

Run after the normal platform build:

```
make -C source/iOS -f ../linux/async_ui_device_probe.mk async-ui-probe
make -C source/macOS -f ../linux/async_ui_mac_probe.mk ALTIVECCORE_LINKAGE=static async-ui-probe
```

The phone executable is `source/iOS/build-release/async-ui-probe`; the Tiger/PPC
executable is `source/macOS/build-release/async-ui-probe-ppc`. Run as the normal
user on a test device. The fixture blocks the command worker while pumping the
main loop and completing an independent search. It checks FIFO execution,
copied mutable input, 64-bit scalar/BOOL transport, latest-only search delivery,
main-thread callbacks, handled/unhandled/detached errors, queue overflow, and production
main-thread IPC guards. It performs no database mutation or provider request.
Cold Foundation/NSProxy startup on an emulator is reported separately from the
fact that enqueue returns while command execution remains deliberately blocked.
