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
  helpers, and result serialization helpers. Session persistence exists, but
  read-only local database helpers are still open.
- [ ] Configuration loading for OpenRouter API key, base URL, model, timeout,
  and request limits. API key/base URL/model exist; timeout and request-limit
  configuration are still open.
- [x] Build system updates so shared core code and third-party libraries link
  into both iOS and macOS targets.

Validation:

- [x] Clean debug build for macOS.
- [x] Clean debug build for iOS.
- [ ] Small shared-core smoke tests or command-style test harness where the
  Altivec environment allows it.
- [ ] No memory ownership warnings from Clang static analysis on the shared C
  core once non-trivial allocation code exists.

## Phase 2: OpenRouter Assistant Client

Goal: make the app able to send chat requests to OpenRouter/OpenAI-compatible
APIs and parse assistant responses reliably.

Deliverables:

- [x] C request builder for chat messages and model selection.
- [ ] C request builder support for model settings and tool definitions.
- [x] C response parser for assistant text, finish reasons, and API errors.
- [ ] C response parser support for assistant tool calls as a stable parsed
  result, not only preserved message JSON.
- [x] Timeout policy for network requests.
- [ ] Retry policy for transient network failures.
- [ ] Request cancellation hook for the UI layer.
- [ ] Redacted diagnostic logging that never prints API keys or full personal
  database contents.
- [x] Minimal Objective-C bridge functions to submit a prompt and receive
  streamed or complete response text.

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
- [ ] Catalog schema for deterministic database facts: assistant-visible
  database ID, tables, columns, indexes, foreign keys, row counts where cheap,
  file metadata, and scan timestamps.
- [x] Removed the database summary-cache catalog path. The assistant now relies
  on deterministic schema facts from `database_list_info` and uses bounded
  read-only `database_query` calls for concrete data lookups.
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
catalog rather than creating executable tools dynamically for each discovered
database. Do not make one tool per whitelisted database; tools should accept an
assistant-visible database ID and resolve it through the catalog. Deterministic
schema facts such as tables, columns, indexes, foreign keys, and row counts are
the only catalog-provided database facts. The assistant can inspect contents
with bounded read-only queries when the user asks a concrete question.

Deliverables:

- [x] Tool registry in C with stable tool names, JSON schemas, argument
  parsing, and result serialization. The registry exposes
  `database_list_info` and `database_query`.
- [x] Stable database tools named `database_list_info` and `database_query`,
  both using assistant-visible database IDs.
- [x] `database_list_info` defines the availability states `error`,
  `possible_scan_needed`, `possible_whitelist_needed`, and `available`. Empty
  success results route scanning and approval to the user-clicked
  `database_manage` app action through next-step hints; `database_manage` is not
  an LLM tool.
- [x] `database_list_info` tool that returns all approved databases with
  deterministic schema facts: tables, columns, indexes, foreign keys, cheap row
  counts, filename-based file metadata, and per-database recommended next
  steps.
- [x] `database_query` tool that permits read-only SQL only and enforces
  statement timeouts, row limits, and result size limits.
- [ ] Database ID resolution that maps assistant-visible database IDs to
  cataloged local paths without leaking unnecessary filesystem details. The
  `database_list_info` result uses assistant-visible IDs and omits raw filesystem
  paths.
- [ ] `database_manage` app action link, intercepted by the WebView/native
  bridge, that opens `PreferencesWindowController` for scanning, approval,
  denial, forgetting, and rescanning databases.
- [ ] Prompt context builder that summarizes available databases and tool usage
  rules. Basic tool-use prompt context exists for `database_list_info`;
  database summaries remain open.
- [x] Bounded multi-round tool execution loop for chat completions: capture
  assistant `tool_calls`, execute registered local tools, send `role: "tool"`
  results back to the model, and repeat until the model returns final text or
  reaches the 20-round tool limit.
- [x] Persisted tool-call and tool-result messages in `session_messages`, with
  `tool_call` and `tool` roles replaying through stored raw message JSON.
- [x] Webview rendering for tool-call inputs and tool outputs as full-width
  tool activity rows.
- [ ] Audit log of tool calls, query text, row counts, errors, and truncation.

Validation:

- [ ] Unit tests for tool argument validation and SQL safety checks.
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

- Embedded web chat UI suitable for legacy WebKit/UIWebView-era platforms.
- Native bridge between the web UI and Objective-C/C assistant core.
- Conversation list, active conversation view, message composer, loading state,
  error state, and cancel action.
- Tool activity display showing when Strappy is scanning, inspecting schema, or
  querying a database. Basic tool-call and tool-result rows now render in the
  chat log; scan/schema/query-specific activity remains open.
- Database permission flow that lets the user approve, deny, or forget a found
  database.
- Local conversation persistence in sqlite.
- Localized strings for English and Japanese for new visible UI.

Validation:

- Manual UI pass on macOS and iOS build outputs.
- Browser/webview compatibility pass for the oldest supported platform APIs.
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
