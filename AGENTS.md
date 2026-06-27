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
`database_query_harness` and `webview_harness`, run through
`make -C source/linux clean test`.

House style for Strappy source:

1. Objective-C classes use Objective-C naming conventions. The Objective-C class
   that manages assistant sessions is `StrappySession`, implemented in
   `StrappySession.h` and `StrappySession.m`.
2. C source and header files use lowercase snake_case names, for example
   `strappy_client.c` and `strappy_core.h`, so they are easy to distinguish from
   Objective-C classes.
3. C type and function names use lowercase snake_case. Public C functions use
   the `strappy_` prefix.
4. The shared SQLite storage module is named `strappy_db`. Do not name it
   `session_store`; it will hold app-wide database responsibilities over time.
5. C files must not import macOS-only framework headers such as
   `CoreFoundation`. Shared C code must stay portable across the iOS and macOS
   targets.
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
    Objective-C UI code.
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
16. Assistant turns and persisted messages carry `turn_key`,
    `prompt_group_key`, actor, context policy, tool-call metadata, and optional
    reasoning text. Harness and post-answer memory-audit turns should use
    `context_policy = omit` so they can render in history without being replayed
    as normal user context.
17. When changing shared C behavior, especially database, tool, prompt, client,
    or JSON parsing code, run `make -C source/linux clean test` where the host
    Linux environment has the required dependencies. Keep the harnesses updated
    as new shared behavior is added or existing behavior changes, so regressions
    can be caught without waiting for full Apple-target builds.
18. SQLite `PRAGMA user_version` is intentionally pinned at `1`. Do not
    increase it or add migration steps without explicit user permission; this
    database has not shipped yet.

The iOS App is a bit special because it's not sandboxed. It must be installed
via .deb file, not .ipa file so that it can scan the whole filesystem for
SQLite databases. This is known to only work on Jailbroken iPhones and that is
ok.

Strappy is an OpenRouter based AI Assistant that has the following basic
infrastructure:

1. C based API client for OpenRouter/OpenAI API
2. C based API JSON parsing with cJSON; SQLite JSON columns stay opaque on read
3. C based networking with libcurl
4. C based storage with sqlite
5. Web based chat interface for showing the response from the model
6. Filesystem search to search the host device for sqlite databases
7. Tools that allow the Agent to discover the schema of a sqlite database found
8. Tools that allow the Agent to answer questions the user asks from the personal context found in the sqlite databases
9. Helper tools for timestamp conversion, remembered user facts, remembered database hints, and session naming
10. Runtime prompt/tool/database guidance resources that steer database selection, SQL workflow, and memory behavior
