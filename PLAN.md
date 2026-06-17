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

- Shared source layout for Strappy core modules under `source/shared`.
- C API boundaries for logging, errors, memory ownership, and string buffers.
- cJSON integration with small wrapper helpers for safe object/array/string
  access.
- libcurl integration with a reusable HTTP client abstraction.
- sqlite integration with read-only connection helpers, statement lifecycle
  helpers, and result serialization helpers.
- Configuration loading for OpenRouter API key, base URL, model, timeout, and
  request limits.
- Build system updates so shared core code and third-party libraries link into
  both iOS and macOS targets.

Validation:

- Clean debug build for macOS.
- Clean debug build for iOS.
- Small shared-core smoke tests or command-style test harness where the
  Altivec environment allows it.
- No memory ownership warnings from Clang static analysis on the shared C core
  once non-trivial allocation code exists.

## Phase 2: OpenRouter Assistant Client

Goal: make the app able to send chat requests to OpenRouter/OpenAI-compatible
APIs and parse assistant responses reliably.

Deliverables:

- C request builder for chat messages, model settings, and tool definitions.
- C response parser for assistant text, tool calls, finish reasons, and API
  errors.
- Retry and timeout policy for transient network failures.
- Request cancellation hook for the UI layer.
- Redacted diagnostic logging that never prints API keys or full personal
  database contents.
- Minimal Objective-C bridge functions for both apps to submit a prompt and
  receive streamed or complete response text.

Validation:

- Mocked API responses covering success, malformed JSON, HTTP errors, and tool
  calls.
- Manual end-to-end request from both apps using a non-sensitive test prompt.
- Clean builds with full warning logs captured.

## Phase 3: SQLite Discovery And Catalog

Goal: discover local SQLite databases and maintain a safe catalog of what the
assistant is allowed to inspect.

Deliverables:

- Filesystem scanner that searches likely user data locations on macOS and the
  full filesystem on jailbroken iOS.
- SQLite file detection that does not rely only on file extensions.
- Read-only database open path with timeouts and defensive error handling.
- Local Strappy catalog database for discovered paths, file metadata, scan
  status, and user allow/deny decisions.
- Basic UI state for scan progress, found databases, and ignored locations.
- Platform-specific safeguards for permission failures, symlinks, loops, large
  directories, and unreadable files.

Validation:

- Scanner test fixtures with valid SQLite files, non-SQLite files, corrupt
  files, symlinks, and permission failures.
- Manual scan on macOS against a constrained test directory.
- Manual scan on jailbroken iOS package install once `.deb` packaging exists.
- Clean builds with full warning logs captured.

## Phase 4: Database Tools For The Agent

Goal: expose controlled tools that let the assistant inspect database schemas
and query user-approved databases.

Deliverables:

- Tool registry in C with stable tool names, JSON schemas, argument parsing,
  and result serialization.
- Schema discovery tool that returns tables, columns, indexes, foreign keys,
  row counts, and sample-safe metadata.
- Query tool that permits read-only SQL only and enforces statement timeouts,
  row limits, and result size limits.
- Database selection tool that maps assistant-visible database IDs to cataloged
  local paths without leaking unnecessary filesystem details.
- Prompt context builder that summarizes available databases and tool usage
  rules.
- Audit log of tool calls, query text, row counts, errors, and truncation.

Validation:

- Unit tests for tool argument validation and SQL safety checks.
- Fixture databases for common schemas, empty databases, large tables, and
  malformed requests.
- Static analysis pass focused on JSON parsing, sqlite statement cleanup, and
  error paths.
- Clean builds with full warning logs captured.

## Phase 5: Chat Interface And User Workflow

Goal: replace the placeholder screen with a usable web-based chat interface
that works on old iOS and macOS targets.

Deliverables:

- Embedded web chat UI suitable for legacy WebKit/UIWebView-era platforms.
- Native bridge between the web UI and Objective-C/C assistant core.
- Conversation list, active conversation view, message composer, loading state,
  error state, and cancel action.
- Tool activity display showing when Strappy is scanning, inspecting schema, or
  querying a database.
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
