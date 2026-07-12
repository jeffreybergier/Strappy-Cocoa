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
- [x] libcurl integration with a reusable HTTP client abstraction.
- [x] Configuration loading for OpenRouter API key, base URL, and model.
  API key/base URL exist with `.env` and process-environment overrides; macOS
  can save endpoint and token credentials in Keychain. Model choice is
  catalog-backed through default/per-session settings and an allowed-model
  whitelist.
- [x] Build system updates so shared core code and third-party libraries link
  into both iOS and macOS targets.

## Phase 2: OpenRouter Responses Client

Goal: send typed Responses API requests through OpenRouter and persist the
complete request/response ledger reliably.

Deliverables:

- [x] C request builder for Responses input items, tools, instructions, and
  model selection.
- [x] OpenRouter `/models/user` catalog fetch, local persistence, searchable
  and sortable macOS model picker UI, default model selection for new chats,
  user-managed allowed-model whitelist, and per-session model selection from
  allowed models.
- [x] C request builder support for loading Responses tool definitions from
  `GuidanceTools.json`.
- [x] Typed Responses parser for output items, reasoning, function calls,
  function outputs, usage, and API errors.
- [x] Exact raw request/response persistence for every Responses API attempt.
- [x] Timeout policy for network requests.
- [x] Request cancellation hook for the UI layer that aborts active Responses
  transfers and retry waits.
- [x] Minimal Objective-C bridge for prompt submission, processing status, and
  committed Responses-ledger updates.

## Phase 3: SQLite Discovery And Catalog

Goal: discover local SQLite databases and maintain a safe catalog of what the
assistant is allowed to inspect.

Architecture decision: use deterministic native scanning, cataloging, and
whitelist approval as the foundation. The assistant may guide the user through
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
  status, and user whitelist decisions.
- [x] Catalog schema for deterministic discovered-database metadata:
  assistant-visible database ID, path, file size, modified time, device/inode,
  validation state, scan status, user decision, scan root, first/last seen
  timestamps, and last scanned timestamp.
- [x] Removed the database summary-cache catalog path. The assistant now relies
  on `database_list_info` for database availability, `database_context_read` for
  selected live schema and remembered hints, and bounded read-only
  `database_query` calls for concrete data lookups.
- [x] Native macOS Preferences allow checkbox whitelisting for valid cataloged
  SQLite databases, with a polished database table showing Use, Database,
  Location, and Size columns, disabled invalid rows, validation tooltips, and
  multi-row spacebar toggling.

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
- [x] Round-zero application preflight executes `database_list_info` and
  `memory_user_fact_read` for each user request and injects their fresh results
  as application-seeded, matched `function_call` / `function_call_output`
  input pairs without creating response tool-execution audit rows.
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
  `PromptSystem.txt`, `GuidanceTools.json`, and `GuidanceDatabase.json`,
  synchronized with the stable tool names and stricter current guidance that
  supplies `database_list_info` as a typed preflight tool output, requires
  `database_context_read` before querying, uses explicit timestamp units, and
  forbids invented schema or private facts.
- [x] Bounded multi-round Responses tool loop: execute typed function calls,
  append `function_call_output` items, and continue until final output or the
  round limit.
- [x] Single combined post-answer audit message that lists every applicable
  missing-tool check once, permits normal tool continuations, and asks for the
  corrected standalone answer. A tool-disabled finalization recovery runs only
  when the post-audit response contains no non-whitespace assistant answer.
- [x] Persisted prompt, assistant, tool-call, tool-result, and harness messages
  in `session_messages`, with `session_turns`, `turn_key`, `prompt_group_key`,
  raw message JSON, reasoning text, context inclusion flags, tool names,
  tool-call IDs, tool arguments, tool results, and tool-error state.
- [x] Webview rendering for tool-call inputs and tool outputs as full-width
  tool activity rows, with dynamic JSON object display and improved tool-error
  visualization.
- [x] Session-backed audit persistence for tool calls, query text, row counts,
  errors, and truncation. Tool invocations are stored in `session_messages`
  using `tool_name`, `tool_call_id`, `arguments_json`, `result_json`, and
  `is_error`; `database_query` result JSON includes `row_count`, `truncated`,
  and specific truncation flags.

## Phase 5: Chat Interface And User Workflow

Goal: replace the placeholder screen with a usable web-based chat interface
that works on old iOS and macOS targets.

Deliverables:

- [x] Embedded web chat UI suitable for legacy WebKit/UIWebView-era platforms.
- [x] Native bridge between the web UI and Objective-C/C Responses core for
  prompt submission, processing status, cancellation, and ledger updates.
- [x] Conversation list, active conversation view, message composer, loading
  state, error state, and cancel action. The iOS prompt options expose model
  selection and web search; the obsolete streaming option has been removed.
- [x] macOS Preferences tabs for API credentials, sortable model catalog
  browsing/search with allowed-model checkboxes and a default-model picker,
  database scanning/approval, and read-only system prompt inspection.
- [x] Local conversation persistence in sqlite using `sessions`,
  `response_api_calls`, `response_api_items`, response item parts, and tool
  executions.
- [x] Session titles written by `helper_session_name_write` when the active
  session is untitled.
- [x] Tool activity display for tool-call inputs and tool outputs, including
  dynamic JSON object rendering and tool-error visualization.
- [x] Reasoning display for persisted typed Responses reasoning items, with
  collapsed rendering for completed reasoning blocks.
- [x] Database approval flow that lets the user whitelist found databases.
  macOS home-folder scan/rescan and approval checkboxes exist; explicit deny
  and forget states are intentionally out of scope because unapproved databases
  remain unavailable.
- [x] Localized strings for English and Japanese for new visible UI touched by
  the current chat/session, Preferences, model picker/default/whitelist,
  database scanning, and menu flows.

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
  macOS now stores endpoint and token credentials in Keychain while leaving
  `.env` and process-environment overrides available for endpoint/token
  development and automation. Model choice is catalog-backed instead of
  `APIMODEL`-driven.
- Data retention controls for scanned database catalog, conversation history,
  and tool audit logs.
- Release checklist for clean builds, warning review, static analysis, and
  basic manual smoke tests.
- README update documenting setup, build, packaging, and first-run behavior.

## Testing

Goal: keep automated, manual, build, static-analysis, and release validation
centralized so phase deliverables do not duplicate test-plan details.

- [x] Build: clean debug build for macOS.
- [x] Build: clean debug build for iOS.
- [x] Build: clean builds with full warning logs captured for completed
  core, assistant, scanner, and database-tool slices.
- [ ] Build: clean release build for macOS with full logs captured.
- [ ] Build: clean release build for iOS with full logs captured.
- [x] Static analysis: no memory ownership warnings from Clang static analysis
  on the shared C core once non-trivial allocation code exists.
- [ ] Static analysis: pass focused on JSON parsing, sqlite statement cleanup,
  and error paths.
- [x] Linux shared-core smoke harnesses under `source/linux` for database/tool
  behavior and webview rendering, run with `make -C source/linux clean test`.
- [x] Linux `database_query_harness` coverage for OpenRouter model catalog
  persistence, catalog search, default model persistence, allowed-model
  whitelisting, per-session model selection, and stale session-model fallback.
- [x] Linux `database_query_harness` coverage for tool schema loading/filtering,
  database-list output shape, SQL safety checks, timestamp helpers, remembered
  user/database helper memory, session naming, session-backed tool audit fields,
  and message persistence.
- [x] Linux `webview_harness` coverage for webview rendering of generated
  HTML/JS, JSON objects, tool events, reasoning, and harness turns.
- [ ] Mocked API responses covering success, malformed JSON, HTTP errors, and
  tool calls.
- [ ] Linux scanner harness with temporary fixtures for valid SQLite files,
  non-SQLite files, corrupt SQLite candidates, symlink avoidance, and
  unreadable-path handling.
- [ ] Fixture databases for common schemas, empty databases, large tables, and
  malformed requests.
- [ ] Persistence test covering app restart and failed request recovery.
- [ ] Manual end-to-end request from both apps using a non-sensitive test
  prompt.
- [x] Manual scan on macOS from Preferences against the user's home directory.
- [ ] Manual scan on jailbroken iOS package install once `.deb` packaging exists.
- [ ] Manual UI pass on macOS and iOS build outputs.
- [ ] Browser/webview compatibility pass for the oldest supported platform APIs.
- [ ] Install and launch macOS `.app`.
- [ ] Install and launch iOS `.deb` on a jailbroken test device.
- [ ] Confirm iOS filesystem scan behavior from the `.deb` install context.
- [ ] Confirm no secrets appear in logs, app bundle resources, release artifacts,
  or git status.

## Won't Do

These ideas were considered and are intentionally out of scope for the current
implementation plan:

- Shared cJSON wrapper helpers. Existing local safe access helpers are enough;
  adding a global wrapper layer would mainly be refactor churn.
- Shared sqlite helper API. SQLite lifecycle and serialization helpers should
  stay local to `strappy_db.c` and `strappy_tools.c` until another module needs
  a stable shared API.
- Configuration loading for timeout and request-limit tunables. Network timeout
  policies and database/query limits stay code constants so safety behavior is
  deterministic across deployments.
- Additional generation-setting configuration. Endpoint, model selection,
  allowed-model whitelisting, and web search are enough for now; extra provider
  knobs would add UI/config noise.
- Public stable parsed tool-call result API outside the Responses loop. Typed
  persisted response items are the supported surface.
- Persisted deterministic schema facts for approved databases. Live schema via
  `database_context_read` avoids stale catalog facts and migration burden.
- Proactive prompt context builder for available-database summaries. Tool-first
  discovery keeps prompts smaller and avoids stale injected summaries.
- Scan/schema/query-specific activity labels beyond generic tool rows. Generic
  tool activity rows already expose tool names, inputs, outputs, and errors.
- Explicit native UI state for database deny decisions, forget actions, and
  ignored locations. Database and model access are controlled by whitelists, so
  unapproved databases remain unavailable without a separate deny state.
- Additional platform-specific scanner policy for ignored locations and large
  directory trees. The current C scanner already avoids symlink traversal,
  handles unreadable path errors, validates candidates defensively, and leaves
  access gated by database whitelist approval.
- Shared C logging boundary. Shared C stays quiet for now; diagnostics remain at
  the app or harness layer unless a concrete cross-platform logging need appears.
- Redacted diagnostic logging feature. Strappy avoids logging API keys and full
  personal database contents by keeping diagnostics minimal; release validation
  still checks that secrets do not appear in logs, resources, artifacts, or git
  status.

## Suggested Build Order

1. Implement Phase 1 enough to link cJSON, libcurl, and sqlite everywhere.
2. Implement Phase 2 with mocked responses before using live OpenRouter calls.
3. Implement Phase 3 with a constrained scanner before full-device iOS scans.
4. Implement Phase 4 against fixture databases before connecting it to the
   model loop.
5. Implement Phase 5 as a thin UI over already-tested core behavior.
6. Finish Phase 6 before treating iOS behavior as representative, because the
   `.deb` install context affects filesystem access.
