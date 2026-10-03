# Recovery and lifecycle

IPC v7 adds transient per-account credential jobs with query-only recovery after
an uncertain acknowledgment. Restarted or evicted jobs report unknown; no
credential mutation is automatically replayed. The larger bounded prompt
submissions introduced in v6 retain durable retry keys;
reader ABI 2 and SQLite
`user_version=1` are unchanged.
No schema migration is added.

## Restart and external effects

After taking the exclusive service lock and opening the store, before advertising
readiness or creating workers, the daemon reconciles unfinished work in one
transaction. Accepted/running prompts and study become `interrupted`, with stale
retry/status timers cleared. Pending/running HTTP attempts become transport
errors, model requests and turns become errors, and unfinished tool executions
and scans become errors with explicit interruption diagnostics. Existing terminal
records remain unchanged. Failure rolls the entire repair back and prevents
readiness. A new service identity causes clients to resnapshot and reload the
committed database; there is no interval in which clients see unreconciled work
advertised as executable.

The runtime now commits tool execution intent before calling a tool. For response
function calls in unfinished turns that lack a persisted output, recovery appends
a normalized text output explaining that the service stopped before a durable
result was recorded. This closes the call/output pair for subsequent prompts,
preserves the visible history, and explicitly says that external effects may have
occurred and must not be replayed automatically. It does not manufacture success,
a tool result, or an assertion that nothing happened. Completed execution metadata
is retained even if its output was lost. Application preflight pairs are excluded.

Neither daemon startup nor durable submission retries re-execute interrupted
work. The user must inspect external effects before making a new submission.
Filesystem changes, remote provider operations and SQLite commits cannot be made
one atomic transaction. A crash after a final answer but before run completion is
therefore conservatively reported as interrupted too.

## Owned Bash processes and shutdown

Each Bash invocation has a small group-leader guardian that parents the shell.
A close-on-exec pipe connects it to the runtime. Runtime death, including SIGKILL,
closes the pipe; the guardian kills its own process group without consulting a
persisted PID. The guardian closes inherited database, socket, lock and output
descriptors, forwards normal shell exit status, and permits cooperative group
cancellation to reap the shell. It exists only while a Bash invocation is active.
The existing timeout, output limit and TERM-to-KILL escalation still apply.

This is process-group ownership, not a sandbox: a command deliberately escaping
the group or session is outside this guarantee. A whole-system kill that also
kills the guardian cannot run its cleanup. SQLite reconciliation still records
uncertainty on the next service start. No process is killed by a saved PID during
recovery.

SIGTERM/SIGINT stop command acceptance, signal cancellation, join command and
execution workers, close/checkpoint the writer, log shutdown and release the
socket/lock. Existing HTTP, OAuth, scan and tool cancellation remain cooperative.
launchd retains its 10-second exit timeout as the hard stop for blocked system
calls; forced termination takes the same recovery path at next launch.

## Checkpoints, logs and idle health

SQLite's normal 1,000-page automatic WAL checkpoint remains enabled. Once all
workers have joined, the daemon explicitly rolls back any unfinished writer
transaction, attempts a PASSIVE checkpoint and closes its cached writer before
releasing the service lock. It never waits for UI readers with a blocking/TRUNCATE
checkpoint and never removes WAL/SHM files manually. A reader can prevent complete
checkpoint progress; committed WAL data remains recoverable. UI paginated readers
continue to release the live WAL snapshot after copying bounded view data.

The service log retains its historical `heartbeat.log` name and one `.1` backup,
each limited to 64 KiB. Only startup/shutdown identity records are emitted, with
no credentials or provider/tool payloads. Existing per-request diagnostic captures
and bounded tool-output files are separate from this lifecycle log policy.

There is no periodic heartbeat or one-second event-loop poll. The loop sleeps
until socket I/O, a command/worker completion, a signal or an incomplete peer's
30-second deadline. Command threads wake it even for rejected commands and
read-only status requests. Slow-peer deadlines remain bounded without polling
idle subscribers. No power assertion is acquired by the daemon.

`strappyd --ipc-probe /absolute/path/service.sock` checks same-user negotiation,
service identity, subscription/snapshot replies, store ABI and execution readiness.
It is an on-demand health check, not proof that credentials are accessible, a
provider is reachable, or a worker is making progress. Durable run/ledger state
and work-status commands supply those separate observations.

## Install, upgrade and platform boundaries

Mac installation keeps the complete resource set beside the stable per-user
binary. Unchanged installations keep their PID. Updates/uninstall unload first,
then acquire the old service's lock (bounded wait) before modifying its files.
Loading occurs after releasing that lock. Tiger launchctl/OnDemand compatibility
stays in XPAppKit. The service is a login-session LaunchAgent, not a system daemon
that can execute through logout.

Both iOS package variants continue to run as mobile with the same Keychain
entitlements. Stop scripts capture the job PID using shell built-ins, unload,
and wait up to 15 seconds for that process to disappear before replacing files.
They use prefixed rootless tools and do not require awk, pgrep or modern launchctl.
Removal retains the user database and bounded diagnostics. Reinstallation creates
one job and recovers the retained store.

Before first unlock or jailbreak reactivation, credentials, files or executable
paths can be unavailable. Startup failures remain non-ready and launchd retries;
a prompt credential failure is terminal rather than queued for unsafe replay.
No new root privilege, first-unlock workaround or keep-awake promise is introduced.
Actual first-unlock, logout/login, jetsam and sleep behavior requires observations
on each platform. See [VALIDATION.md](VALIDATION.md) for measured results and
explicitly deferred scenarios, including iOS 6 and newer Macs.
