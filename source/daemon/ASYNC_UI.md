# Cocoa control and query boundary

The daemon remains the writer. Short control operations execute synchronously
on the GUI main thread. `StrappyAsync commandTarget:` invokes the bridge and
its completion inline: there is no command-worker hop and no run-loop pumping.
The C client sends and receives on that same thread through its permanent control
connection. UI event order supplies FIFO ordering; the next event cannot submit
another command before the current reply returns. The main thread intentionally
waits for the daemon acknowledgment, including bounded failure/reconnect waits.

Bootstrap establishes the control socket on the GUI main thread. Each background
thread has an independent persistent connection, closed when that thread exits.
Prompt submission/cancellation, authentication, and the service monitor therefore
cannot occupy the GUI's socket or a shared client-side transport mutex. The
monitor also retains its subscribed event connection. The daemon retains a
command worker per connection and independent per-session prompt workers.

Replies use increasing request IDs. Reconnect revalidates version, ABI, and
readiness. Ordinary mutations never replay after an uncertain acknowledgment;
durable prompt/cancel commands retain their existing request-key retry behavior.
Explicit OAuth refresh remains on a separate network worker/connection.

## Choosing when to wait

| Operation | Execution | Confirmation |
| --- | --- | --- |
| Active-session model, account, tools, limits | Shared local draft; no IPC | Settings submit atomically with prompt acceptance |
| Future-session defaults | Main-thread control call | Read committed defaults in inline completion |
| Model/database approval or visibility, context inclusion, study reset/delete | Main-thread control call | Notifications refresh committed values |
| Conversation/account/model creation, save, deletion | Main-thread control call | Completion receives committed identifier/value before handler returns |
| macOS daemon installation/verification | Explicit `backgroundTarget:` startup work | Main window opens on completion |
| Scan, catalog fetch, Database Study start | Main-thread acceptance call | Actual long-running job remains in the daemon worker |
| Prompt submission/cancellation and authentication presentation | Independent background/session paths | Existing durable run and authentication lifecycles |
| Catalog search, sidebar refresh, conversation refresh | Existing query worker | Latest-only results return to the main thread |

Session creation opens the committed session directly in its inline completion;
it does not wait for the asynchronous sidebar refresh before navigation. Catalog
loading labels and query-worker behavior are unchanged.

## Ordering, ownership, and failure

The command proxy is single-use and captures invocation arguments, including
64-bit scalars and copied NSCopying objects. Pass NULL for NSError**; the proxy
provides invocation-local error storage. `StrappyAsyncResult` holds actual success,
value, and error. The immediate BOOL return means the invocation was handled,
including failure already delivered through completion/the error channel; it is
not the bridge's success value. Object returns are intentionally ignored.

Short command completions run before the proxy returns. Callers must initialize
completion state before invoking it. Receivers remain retained through delivery.
A completion can handle an error locally; otherwise the common command-error
notification reports it exactly once. No provisional conversation rows are made.
Bulk approval operations stop on first failure; earlier committed items remain.

`backgroundTarget:` is reserved for slow local work such as launchctl startup.
Its bounded FIFO, pending-count/background-task bookkeeping, asynchronous errors,
and argument ownership remain unchanged. It is independent of the synchronous
control path and the latest-only query worker. Background queries and startup
work cannot prevent a short control invocation from executing on the main thread.

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
does not claim that all UI disk work is asynchronous: initial timeline page reads/rendering, small account/allowed-model lists,
and lazy pages are still local work.
Those paths do not wait for daemon IPC. Sidebar snapshot construction and section
building now run on the query worker. Main-thread completion applies the snapshot
and performs any pending selection after the committed row is available. Newer
reloads supersede stale results. No provisional conversation rows are synthesized.

## Native regression fixture

Run after the normal platform build:

```
make -C source/iOS -f ../daemon/tests/async_ui_device_probe.mk async-ui-probe
make -C source/macOS -f ../daemon/tests/async_ui_mac_probe.mk ALTIVECCORE_LINKAGE=static async-ui-probe
```

The phone executable is `source/iOS/build-release/async-ui-probe`; the Tiger/PPC
executable is `source/macOS/build-release/async-ui-probe-ppc`. Run as the normal
user on a test device. The fixture blocks the command worker while pumping the
main loop and completing an independent search. It checks FIFO execution,
copied mutable input, 64-bit scalar/BOOL transport, latest-only search delivery,
main-thread callbacks, handled/unhandled/detached errors, queue overflow, and
inline main-thread control execution while the background command worker is blocked. It performs no database mutation or provider request.
Cold Foundation/NSProxy startup on an emulator is reported separately from the
fact that enqueue returns while command execution remains deliberately blocked.

## Conversation invalidations

Ledger and run notifications coalesce for 300 ms before a query-worker refresh.
The shared bridge reads committed timeline rows and run status from SQLite and
produces one JavaScript batch. The GUI does not reload the document for these
notifications or for normal prompt completion. The snapshot includes rows behind
the append cursor, since request status, tool output, and context inclusion can
change in place. This still renders the full local timeline per refresh; it does
not require another daemon request. A future revision cursor could reduce that
work for very long histories.

`syncMessages` compares each row's committed markup by stable identity, preserving
unchanged DOM objects and their disclosure state. Changed rows are replaced in
place, new rows are inserted in database order through the existing decoration
and animated scrolling path, and absent committed rows are removed. Repeated
snapshots do not restart scrolling. Processing status is read from the stored
run, including notifications that change run state without adding ledger rows.

Only one refresh per controller runs at a time. Notifications during a refresh
schedule another pass. Results are checked against session identity and document
generation before injection. Notifications during page loading stay pending;
iOS flushes them on document-load completion, and macOS uses the shared page's
`strappy-action://webview-ready` signal. A failed background read retains the
current document and retries on a later notification.

The Linux WebView behavior harness covers identity preservation, changed rows,
late insertion order, animated additions, duplicate snapshots, and removal.
For an actual iOS WebView, build `webview-device-probe` with
`../daemon/tests/webview_device_probe.mk` from `source/iOS`. Copy the signed executable,
`source/daemon/tests/build-linux/webview-page.html` generated by the offline suite, and
`source/daemon/tests/webview_device_probe.js` to a test phone. Run the executable as
mobile with the HTML and JavaScript paths as its two arguments. The fixture uses
no application database or provider request; it checks one document load, the
ready signal, stable live DOM identities, in-place updates, insertion, and removal.
