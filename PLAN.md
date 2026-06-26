# Strappy Implementation Plan

This plan builds Strappy from the current Cocoa scaffold into the app described
in `AGENTS.md`: a cross-platform OpenRouter-based assistant that can find local
SQLite databases, inspect their schemas, and answer user questions using
personal context from those databases.

## Constraints

- Keep shared business logic in C or Objective-C compatible with the existing
  Altivec build layout.
- Support iOS 4.3 and newer, built for `armv7` and `arm64`.
- Support macOS across `ppc`, `x86`, `x64`, and `arm64`.
- Treat iOS as a jailbreak-only deployment target and ship it as a `.deb`, not
  an `.ipa`, so it can scan the whole filesystem.
- Vendor or configure third-party dependencies so clean builds work for every
  required architecture.
- After source changes, run clean builds and capture full build output so
  warnings are visible.
- Keep prompt, tool, and database guidance resources in
  `source/shared/Resources` synchronized with the C tool registry and prompt
  loader.

## Phase 1: Core Runtime Foundation

Goal: create the shared C foundation that both platform apps can call without
duplicating assistant logic.

Deliverables:

- [x] Shared source layout for Strappy core modules under `source/shared`.
- [ ] C API boundaries for logging, errors, memory ownership, and string
  buffers. Errors, memory ownership, and internal buffers exist; a logging
  boundary is still open.
- [ ] cJSON integration with small wrapper helpers for safe object/array/string
  access. cJSON is integrated, but the safe helpers are still mostly local to
  client code instead of reusable shared wrappers.
- [x] libcurl integration with a reusable HTTP client abstraction.
- [ ] sqlite integration with read-only connection helpers, statement lifecycle
  helpers, and result serialization helpers. Session/catalog persistence and
  bounded read-only `database_query` execution exist; reusable shared sqlite
  helper APIs are still mostly local to `strappy_db.c` and `strappy_tools.c`.
- [ ] Configuration loading for OpenRouter API key, base URL, model, timeout,
  and request limits. API key/base URL/model exist; timeout and request-limit
  configuration are still open.
- [x] Build system updates so shared core code and third-party libraries link
  into both iOS and macOS targets.

Validation:

- [x] Clean debug build for macOS.
- [x] Clean debug build for iOS.
- [x] Linux shared-core smoke harnesses under `source/linux` for database/tool
  behavior and webview rendering, run with `make -C source/linux clean test`.
- [ ] No memory ownership warnings from Clang static analysis on the shared C
  core once non-trivial allocation code exists.

## Phase 2: OpenRouter Assistant Client

Goal: make the app able to send chat requests to OpenRouter/OpenAI-compatible
APIs and parse assistant responses reliably.

Deliverables:

- [x] C request builder for chat messages and model selection.
- [x] C request builder support for loading tool definitions from
  `GuidanceTools.json`, including a tool allowlist for memory-audit turns.
- [ ] Additional model-setting configuration beyond endpoint, model, streaming,
  reasoning, and stream usage options.
- [x] C response parser for assistant text, finish reasons, and API errors.
- [x] C client preservation of non-streamed and streamed assistant
  `tool_calls`, streamed tool-call deltas, and reasoning text for the assistant
  loop.
- [ ] Public stable parsed tool-call result API outside the assistant loop.
- [x] Timeout policy for network requests.
- [ ] Retry policy for transient network failures.
- [ ] Request cancellation hook for the UI layer. The streaming callback can
  reject/cancel a stream, but a user-facing native cancel action is still open.
- [ ] Redacted diagnostic logging that never prints API keys or full personal
  database contents.
- [x] Minimal Objective-C bridge functions to submit a prompt and receive
  streamed or complete response text, reasoning deltas, tool events, and turn
  events.

Validation:

- [ ] Mocked API responses covering success, malformed JSON, HTTP errors, and
  tool calls.
- [ ] Manual end-to-end request from both apps using a non-sensitive test
  prompt.
- [x] Clean builds with full warning logs captured.

## Phase 3: SQLite Discovery And Catalog

Goal: discover local SQLite databases and maintain a safe catalog of what the
assistant is allowed to inspect.

Architecture decision: use deterministic native scanning, cataloging, and
allow/deny approval as the foundation. The assistant may guide the user through
the process conversationally later, but permission state and filesystem access
must be owned by Strappy UI/core code, not inferred from model output.

Deliverables:

- [x] Shared C filesystem scanner with an Objective-C `FileScanner` singleton
  boundary, keeping filesystem discovery out of `StrappySession`.
- [x] Filesystem scanner that searches a requested root path and is wired to
  scan the user's home directory from the macOS Preferences UI. Full-filesystem
  jailbroken iOS scan roots are still open.
- [x] SQLite file detection that does not rely only on file extensions.
- [x] Read-only database validation path with a short busy timeout and
  defensive error handling for invalid candidates.
- [x] Local Strappy catalog database for discovered paths, file metadata, scan
  status, and user allow/deny decisions.
- [ ] Catalog schema for persisted deterministic database facts:
  assistant-visible database ID, tables, columns, indexes, foreign keys, row
  counts where cheap, file metadata, and scan timestamps. Live simplified schema
  is available through `database_context_read`, but it is not yet stored as
  catalog facts.
- [x] Removed the database summary-cache catalog path. The assistant now relies
  on `database_list_info` for database availability, `database_context_read` for
  selected live schema and remembered hints, and bounded read-only
  `database_query` calls for concrete data lookups.
- [x] Native macOS Preferences allow checkbox whitelisting for valid cataloged
  SQLite databases.
- [ ] Fixed native UI state for deny decisions and ignored locations.
- [ ] Platform-specific safeguards for permission failures, symlinks, loops,
  large directories, and unreadable files. Symlink avoidance, unreadable path
  handling, and validation errors exist in the C scanner; ignored locations and
  large-directory policy are still open.

Validation:

- [ ] Scanner test fixtures with valid SQLite files, non-SQLite files, corrupt
  files, symlinks, and permission failures.
- [x] Manual scan on macOS from Preferences against the user's home directory.
- [ ] Manual scan on jailbroken iOS package install once `.deb` packaging exists.
- [x] Clean builds with full warning logs captured.

## Phase 4: Database Tools For The Agent

Goal: expose controlled tools that let the assistant inspect database schemas
and query user-approved databases.

Architecture decision: expose a small stable tool set backed by the Strappy
catalog and helper memory tables rather than creating executable tools
dynamically for each discovered database. Do not make one tool per whitelisted
database; tools should accept an assistant-visible database ID and resolve it
through the catalog. `database_list_info` is for selection and availability,
`database_context_read` is for selected live schema plus remembered hints, and
`database_query` is for bounded read-only SQL. Durable learned user facts and
database hints live in helper memory tables, not in a schema-summary cache.

Deliverables:

- [x] Tool registry in C with stable tool names, JSON schemas, argument
  parsing, and result serialization. The registry exposes `database_list_info`,
  `database_context_read`, `database_query`, timestamp helpers, remembered user
  fact helpers, remembered database hint helpers, and
  `helper_session_name_write`.
- [x] Stable database tools named `database_list_info`,
  `database_context_read`, and `database_query`, all using assistant-visible
  database IDs when a database is selected.
- [x] `database_list_info` defines `available` and `no_approved_databases`
  success states, with tool failure representing errors. Empty success results
  include a `database_manage` user action hint; `database_manage` is not an LLM
  tool.
- [x] `database_list_info` returns approved databases with assistant-visible
  IDs, safe filename metadata, short descriptions, and availability state. It
  intentionally omits raw filesystem paths, schema, remembered hints, and query
  results.
- [x] `database_context_read` returns selected database metadata, full
  description, simplified live schema, and remembered database hints; without a
  database ID it can search remembered hints only.
- [x] `database_query` tool that permits read-only SQL only and enforces
  statement timeouts, row limits, and result size limits.
- [x] Database ID resolution that maps assistant-visible database IDs to
  cataloged local paths without leaking unnecessary filesystem details. The
  `database_list_info` result uses assistant-visible IDs and omits raw filesystem
  paths.
- [ ] `database_manage` app action link. The tool result now emits
  `strappy://database-manage`; WebView/native bridge interception that opens
  `PreferencesWindowController` remains open.
- [x] Runtime prompt, tool, and database guidance resources:
  `PromptSystem.txt`, `GuidanceTools.json`, and `GuidanceDatabase.json`.
- [ ] Proactive prompt context builder that injects available-database summaries
  before a tool call. Current behavior relies on tool guidance and
  `database_list_info` / `database_context_read`.
- [x] Bounded multi-round tool execution loop for chat completions: capture
  assistant `tool_calls`, execute registered local tools, send `role: "tool"`
  results back to the model, and repeat until the model returns final text or
  reaches the 20-round tool limit.
- [x] Post-answer memory-audit turn that can call `helper_session_name_write`,
  `memory_user_fact_remember`, and `memory_database_hint_remember` with an
  allowlisted tool set and `context_policy = omit`.
- [x] Persisted prompt, assistant, tool-call, tool-result, and harness messages
  in `session_messages`, with `session_turns`, `turn_key`, `prompt_group_key`,
  raw message JSON, reasoning text, and context inclusion flags.
- [x] Webview rendering for tool-call inputs and tool outputs as full-width
  tool activity rows, with dynamic JSON object display and improved tool-error
  visualization.
- [ ] Audit log of tool calls, query text, row counts, errors, and truncation.

Validation:

- [x] Linux `database_query_harness` coverage for tool schema loading/filtering,
  database-list output shape, SQL safety checks, timestamp helpers, remembered
  user/database helper memory, session naming, and message persistence.
- [x] Linux `webview_harness` coverage for webview rendering of JSON objects,
  tool events, reasoning, and harness turns.
- [ ] Fixture databases for common schemas, empty databases, large tables, and
  malformed requests.
- [ ] Static analysis pass focused on JSON parsing, sqlite statement cleanup, and
  error paths.
- [x] Clean builds with full warning logs captured for the current
  `database_list_info` vertical slice.

## Phase 5: Chat Interface And User Workflow

Goal: replace the placeholder screen with a usable web-based chat interface
that works on old iOS and macOS targets.

Deliverables:

- [x] Embedded web chat UI suitable for legacy WebKit/UIWebView-era platforms.
- [x] Native bridge between the web UI and Objective-C/C assistant core for
  prompt submission, streaming text, reasoning, tool events, and turn events.
- [ ] Conversation list, active conversation view, message composer, loading
  state, error state, and cancel action. Conversation list/view/composer and
  error states exist; a user-facing cancel action remains open.
- [x] Local conversation persistence in sqlite using `sessions`,
  `session_turns`, and `session_messages`; the old empty/non-started session
  path has been removed.
- [x] Session titles written by `helper_session_name_write` when the active
  session is untitled.
- [x] Tool activity display for tool-call inputs and tool outputs, including
  dynamic JSON object rendering and tool-error visualization.
- [x] Reasoning display for streamed and persisted assistant/harness messages,
  with collapsed rendering for completed reasoning blocks.
- [ ] Scan/schema/query-specific activity labels beyond generic tool rows.
- [ ] Database permission flow that lets the user approve, deny, or forget a
  found database. macOS approval checkboxes exist; deny, forget, rescan, and the
  WebView `database_manage` bridge remain open.
- [x] Localized strings for English and Japanese for new visible UI touched by
  the current chat/session flow.

Validation:

- Manual UI pass on macOS and iOS build outputs.
- Browser/webview compatibility pass for the oldest supported platform APIs.
- Linux `webview_harness` smoke coverage for generated webview HTML/JS.
- Linux `database_query_harness` smoke coverage for session message
  persistence.
- Persistence test covering app restart and failed request recovery.
- Clean builds with full warning logs captured.

## Phase 6: Packaging, Security, And Release Hardening

Goal: make the app deployable in the required formats and safe enough to handle
personal local data.

Deliverables:

- macOS release artifact remains a zipped `.app`.
- iOS release pipeline produces a jailbreak-installable `.deb` package instead
  of an `.ipa`.
- Update `.altivec-release.yml` and iOS Makefile/package scripts to publish the
  `.deb` artifact.
- Clear local configuration path for API keys that avoids committing secrets.
- Data retention controls for scanned database catalog, conversation history,
  and tool audit logs.
- Release checklist for clean builds, warning review, static analysis, and
  basic manual smoke tests.
- README update documenting setup, build, packaging, and first-run behavior.

Validation:

- Clean release build for macOS with full logs captured.
- Clean release build for iOS with full logs captured.
- Install and launch macOS `.app`.
- Install and launch iOS `.deb` on a jailbroken test device.
- Confirm iOS filesystem scan behavior from the `.deb` install context.
- Confirm no secrets appear in logs, app bundle resources, release artifacts,
  or git status.

## Suggested Build Order

1. Implement Phase 1 enough to link cJSON, libcurl, and sqlite everywhere.
2. Implement Phase 2 with mocked responses before using live OpenRouter calls.
3. Implement Phase 3 with a constrained scanner before full-device iOS scans.
4. Implement Phase 4 against fixture databases before connecting it to the
   model loop.
5. Implement Phase 5 as a thin UI over already-tested core behavior.
6. Finish Phase 6 before treating iOS behavior as representative, because the
   `.deb` install context affects filesystem access.
