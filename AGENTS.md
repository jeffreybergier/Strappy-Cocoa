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
not leaking memory or dereferencing null pointers.

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
6. `StrappySession.m` is the only Objective-C file that may directly import
   Strappy C headers such as `strappy_client.h` or `strappy_core.h`. Other
   Objective-C files should talk to the C layer through `StrappySession`.
7. Platform and SDK compatibility `#if` / `#ifdef` checks must live in
   `XPAppKit`, `XPUIKit`, or `XPFoundation`. Call sites should use XP-prefixed
   macros, helpers, categories, or types instead of embedding compatibility
   conditionals directly in feature code.
8. XP-prefixed compatibility methods are runtime bridges, not simple aliases.
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

The iOS App is a bit special because its not sandboxed. It must be installed via
.deb file, not .ipa file so that it can scan the whole filesystem for SQLite
database. This is known to only work on Jailbroken iPhones and that is ok.

Strappy is an OpenRouter based AI Assistant that has the following basic
infrastructure:

1. C based API client for OpenRouter/OpenAI API
2. C based JSON parsing with cJSON
3. C based networking with libcurl
4. C based storage with sqlite
5. Web based chat interface for showing the response from the model
6. Filesystem search to search the host device for sqlite databases
7. Tools that allow the Agent to discover the schema of a sqlite database found
8. Tools that allow the Agent to answer questions the user asks from the personal context found in the sqlite databases
