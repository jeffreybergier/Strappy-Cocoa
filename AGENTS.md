AGENTS.md

This project is an iOS app and macOS app built using Altivec Intelligence. The
iOS app supports iOS 4.3 and higher and the macOS App supports Mac OS X and
higher. The Altivec Intelligence toolchain does the hard work for you so don't
worry.

For this project, the iOS app is always built for arm64 and armv7 and the macOS
app is always built for arm64, x64, x86, and PPC. After changing anything before
you finish, please run a clean build to ensure there are no warnings. Also make
sure you capture all of the build output so you can see all the warnings. In
certain cases, we also want to run Clang static analysis to be extra sure we are
not leaking memory or dereferencing null pointers. Capture build output in the
terminal transcript or a local working log, but do not commit generated build or
analysis logs unless the user explicitly asks for them.

Linux-only shared-core harnesses live under `source/linux`. They are fast
developer smoke tests for portable C code and do not replace the required
Altivec iOS/macOS clean builds. The current harness targets are
`database_query_harness`, `webview_harness`, `client_stream_harness`,
`assistant_reasoning_harness`, and `responses_harness`, run through
`make -C source/linux clean test`.
`database_query_harness` also covers
OpenRouter model catalog persistence, catalog search, default model selection,
allowed-model whitelisting, per-session model selection, and stale
session-model fallback.

House style for Strappy source:

1. Objective-C classes use Objective-C naming conventions. The Objective-C class
   that manages assistant sessions is `StrappySession`, implemented in
   `StrappySession.h` and `StrappySession.m`.
2. C source and header files use lowercase snake_case names, for example
   `strappy_client.c` and `strappy_core.h`, so they are easy to distinguish from
   Objective-C classes.
3. C type and function names use lowercase snake_case. Public C functions use
   the `strappy_` prefix.
4. The shared SQLite storage module is named `strappy.sqlite`. Do not name it
   `session_store`; it will hold app-wide database responsibilities over time.
5. C files must not import macOS-only framework headers such as
   `CoreFoundation`. Shared C code must stay portable across Apple and potential
   future targets. The exception is `source/shared/strappy_cocoa.c`, which is
   the designated Cocoa/CoreFoundation import boundary for shared C; keep any
   framework-specific imports and code isolated there with portable fallbacks
   for non-Apple targets.
6. `StrappySession.m` owns the Objective-C/C boundary for AI agent sessions.
   Keep filesystem scanning and database discovery out of `StrappySession`.
7. `FileScanner.m` owns the Objective-C/C boundary for filesystem scanning and
   database discovery. It is a singleton because Strappy scans one host
   filesystem at a time. Objective-C UI code should talk to `FileScanner`, not
   directly to scanner C headers.
8. Except for `StrappySession.m`, `FileScanner.m`, and UI code that calls the C
   webview renderer in `strappy_webview.h`, Objective-C files must not directly
   import Strappy C headers such as `strappy_client.h` or `strappy_core.h`.
   Objective-C view controllers may pass display data to the webview renderer,
   but must not hand-roll webview HTML, CSS, or JavaScript.
9. Platform and SDK compatibility `#if` / `#ifdef` checks must live in
   `XPAppKit`, `XPUIKit`, or `XPFoundation`. Call sites should use XP-prefixed
   macros, helpers, categories, or types instead of embedding compatibility
   conditionals directly in feature code.
10. XP-prefixed compatibility methods are runtime bridges, not simple aliases.
   When the modern API may exist on some target runtimes but not others,
   implement an `XP_` category method on the owning Cocoa class, check the
   modern selector with `respondsToSelector:`, and then fall back to the
   oldest supported selector. Prefer `performSelector:` for object-only
   signatures. Use `NSInvocation` for primitive or struct arguments/returns
   where `performSelector:` is unsafe; for example, `NSNumber` integer factory
   and value helpers must use `NSInvocation`, not `performSelector:`. Do not
   use raw typed function pointers in Strappy XP helpers. Foundation-only
   shims live in
   `source/shared/XPFoundation.{h,m}`; AppKit/UIKit shims live in `XPAppKit` /
   `XPUIKit`. Call sites must use the XP method and must not call newer SDK
   selectors directly.
11. SQLite JSON columns are opaque on read. Do not use cJSON to parse values
   loaded from the database. Stored custom metadata JSON should move unchanged
   from SQLite into the webview, where page JavaScript can parse it for display.
12. Webview HTML, CSS, and JavaScript strings are generated in C. Keep that
    rendering logic in `strappy_webview.{h,c}` or another C module, not in
    Objective-C view controllers.
13. Prompt, tool, and database guidance are runtime resources under
    `source/shared/Resources`: `PromptSystem.txt`, `GuidanceTools.json`, and
    `GuidanceDatabase.json`. Keep tool schemas in `GuidanceTools.json` in sync
    with the tool-name constants in `strappy_tools.h` and the executor in
    `strappy_tools.c`; do not duplicate prompt or tool guidance in
    Objective-C UI code. Strict assistant workflow rules, timestamp guidance,
    memory guidance, and database-specific instructions belong in these
    resources, not in scattered C or Objective-C strings.
14. Database tool flow is split by responsibility. `database_list_info` lists
    approved databases by assistant-visible IDs with safe metadata and short
    descriptions only. `database_context_read` returns selected database
    context, live simplified schema, and remembered database hints.
    `database_query` runs bounded read-only SQL against approved databases.
    Do not put raw filesystem paths, full schema dumps, or learned hint caches
    into `database_list_info`.
15. Memory and session-title tools persist durable assistant state.
    `memory_user_fact_*` stores small stable user facts,
    `memory_database_hint_*` stores reusable evidence-backed database hints, and
    `helper_session_name_write` names an untitled active session. Do not store
    secrets, credentials, sensitive identifiers, long copied content, or private
    row contents in memory.
16. Active assistant history uses the Responses API ledger: every HTTP attempt
    has one `response_api_calls` row, and every typed input/output item has one
    ordered `response_api_items` row with its exact raw JSON retained. A prompt
    group may append one `developer` tool-audit reminder only when no local or
    server tool-call item occurred; keep that reminder in the same Responses
    history instead of creating a synthetic harness turn.
17. OpenRouter model catalog and selection state live in shared SQLite storage.
    `strappy_db` owns `openrouter_models`, `openrouter_model_settings`, the
    default model app setting, and `sessions.model`; `StrappySession` owns the
    Objective-C bridge. `APIMODEL` is not a user configuration key. Load
    endpoint/token from `.env`, process environment, or Keychain, then resolve
    the default or per-session model through the catalog and call
    `strappy_config_set_api_model`. Model pickers and prompt options must use
    the allowed-model catalog APIs and must not bypass the whitelist or allow a
    stale disallowed session model to keep running.
18. The database is the source of truth for UI state. Do not update UI-visible
    state first and persist it later; write the database change first, then
    refresh or render the UI from the stored value.
19. Shared C/database code owns conversation-visible state. Platform UI code
    must not synthesize prompt, response item, tool, API error, or cancellation
    rows before they exist in SQLite; it should request the shared core to
    persist the state, then render the ordered DB-backed API items directly. If
    a WebView needs an incremental update, Objective-C should only pass
    shared-core, DB-derived JavaScript through the controller's single injection
    method; do not assemble conversation DOM state in platform UI code. Keep
    temporary UI-only control state, such as disabled buttons or spinners, out
    of persisted conversation rows.
20. iOS Objective-C code must use bracketed accessor/message syntax instead of
    dot syntax, even when dot syntax would be accepted by the compiler.
21. When changing shared C behavior, especially database, tool, prompt, client,
    or JSON parsing code, run `make -C source/linux clean test` where the host
    Linux environment has the required dependencies. Keep the harnesses updated
    as new shared behavior is added or existing behavior changes, so regressions
    can be caught without waiting for full Apple-target builds.
22. SQLite `PRAGMA user_version` is intentionally pinned at `1`. Do not
    increase it or add migration steps without explicit user permission; this
    database has not shipped yet.

The iOS App is a bit special because it's not sandboxed. It must be installed
via .deb file, not .ipa file so that it can scan the whole filesystem for
SQLite databases. This is known to only work on Jailbroken iPhones and that is
ok.

Strappy is an OpenRouter based AI Assistant that has the following basic
infrastructure:

1. C based non-streaming OpenRouter Responses API client
2. OpenRouter model catalog persistence with searchable/sortable browsing,
   default model selection, allowed-model whitelisting, and per-session model
   selection
3. C based API JSON parsing with cJSON; SQLite JSON columns stay opaque on read
4. C based networking with libcurl
5. C based storage with sqlite
6. Web based chat interface for showing the response from the model
7. Filesystem search to search the host device for sqlite databases
8. Tools that allow the Agent to discover the schema of a sqlite database found
9. Tools that allow the Agent to answer questions the user asks from the personal context found in the sqlite databases
10. Helper tools for timestamp conversion, remembered user facts, remembered database hints, and session naming
11. Runtime prompt/tool/database guidance resources that steer database selection, SQL workflow, and memory behavior
