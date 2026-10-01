# Store ownership (step 2)

Only `strappyd` and explicit offline fixture builds define `STRAPPY_DB_WRITER`.
App builds define `STRAPPY_DB_UI` and cannot open a writer through the shared
storage API. All query families—history, renderer timeline, session/default
options, accounts, models, database catalog, and Database Study status—open
existing files with `SQLITE_OPEN_READONLY`. They never enable WAL, initialize
schema, seed missing defaults, repair tables, or checkpoint. Default/fallback
values are computed without persistence. Temporary view tables are writable;
SQLite still rejects mutations to the persistent main database.

The GUI's prompt, study, scan, catalog-refresh, and mutation entrypoints fail
with an unavailable-action error. The removed UI bootstrap no longer imports
catalogs or converts saved account credentials. Those responsibilities must be
implemented in the daemon in steps 3–4; there is no in-process execution
fallback. Local UI presentation files/preferences are unaffected.

## Startup and compatibility

The production daemon requires three absolute arguments:

```
strappyd /absolute/heartbeat.log /absolute/strappy.sqlite /absolute/resources
```

It takes the existing service-instance lock before opening the database,
creates its final store directory as its normal user, initializes schema in a
transaction, and imports the bundled catalog. It publishes store readiness only
after these commits. iOS uses
`/var/mobile/Library/Application Support/Strappy/strappy.sqlite` for both package
layouts; resource paths include the rootless prefix where applicable. Mac uses
`~/Library/Application Support/Strappy/strappy.sqlite`. The Mac installer copies
`BundledModels.json` beside its stable service binary and reloads the service if
that resource changes. It does not depend on the app remaining at its old path.

New store directories are 0700; database, WAL, and SHM files are 0600 and owned
by the service user. Existing owned store files are tightened to those modes.
The service rejects symlink or foreign-owned store files. Normal ownership is
UID 501/mobile on iOS and the logged-in user on Mac. This is an ownership rule,
not isolation from other unsandboxed programs with the same identity.

A background client monitor negotiates IPC v2, subscribes, and checks reader
ABI 1 before allowing GUI readers. The reader independently verifies the stored
schema identity and `user_version = 1`. A missing store is an error, never an
invitation for a reader to create it. Startup races, service loss, old transport
versions, and mismatched reader ABI fail closed. The monitor retries once per
second and invalidates views on readiness changes, reconnect, and events.
Already materialized view snapshots can remain displayed while disconnected.
The next store read fails until readiness returns. No new database migration or
SQLite version increase is introduced. Incompatible stores are not reset or
silently converted. Upgrade must install matching app/service/resource files;
old v1 transports cannot grant readiness to this app.

## Read snapshots and WAL

One-shot readers close connections after their query. Sidebar and catalog/model
readers copy only the tables needed for their view into SQLite private temporary
tables in one short read transaction. They commit before returning, index their
private copies, and preserve stable pagination/search semantics. They never copy
conversation/response ledgers. Temporary storage uses SQLite's file-backed
policy, avoiding a full Objective-C row array. The existing row-object lifetime
owns and releases that temp storage.

Invalidation reconstructs the reader. Keeping an old row object alive, leaving
the UI idle, or suspending iOS cannot retain a main-store WAL transaction: it has
already ended. This avoids reliance on a background timer running while iOS is
suspended. View capture costs are proportional to the selected tables; page
queries remain indexed and bounded. The existing 20,000-session,
20,000-database, and 21,000-model harnesses test ordering and paging costs.

The store stays in WAL mode; no reader uses `immutable`. Daemon connections keep
WAL/SHM available for concurrent read-only connections. Altivec's shipped SQLite
3.43.2 was tested across processes on Tiger/PPC and iOS 15/arm64, including a
TRUNCATE checkpoint while an old sidebar snapshot remained alive. Separate
GUI-capability executables verified read-only APIs, denied writes, and readiness
revocation. Linux additionally tests catalog/model retained snapshots,
reconnect, startup races, and incompatible schema refusal. See
[VALIDATION.md](VALIDATION.md) for coverage limits.
