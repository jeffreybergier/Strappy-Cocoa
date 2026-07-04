# Strappy Cocoa Audit

## House Rule: No C in Objective-C Code

Audit scope: Objective-C headers and implementations under `source` (`*.h`,
`*.m`, `*.mm`). This does not include the standalone C files under
`source/shared`, `source/linux`, or `inspiration/openai-c`, except where their
headers are imported by Objective-C files.

Status: not compliant. The Objective-C layer directly imports and manipulates a
`strappy_*` C backend, and several platform compatibility files call C/runtime
APIs directly.

### C Header Imports

- `source/shared/StrappySession.m:3` imports `strappy_assistant.h`,
  `strappy_client.h`, `strappy_core.h`, `strappy_db.h`,
  `strappy_model_catalog.h`, `strappy_prompt.h`, and `strappy_webview.h`.
- `source/shared/FileScanner.m:4` imports `strappy_core.h`, `strappy_db.h`,
  and `strappy_file_scanner.h`; also includes `<stdlib.h>` and `<string.h>`.
- `source/shared/StrappyKeychain.m:1` imports `strappy_keychain.h`; also
  includes `strappy_config.h`, `strappy_core.h`, and `<stdlib.h>`.
- `source/macOS/PreferencesWindowController.m:10` imports
  `strappy_keychain.h`.
- `source/macOS/StrappyPreferencesAuthenticationView.m:4` imports
  `strappy_config.h` and `strappy_keychain.h`.
- `source/shared/XPFoundation.m:3` imports `<Security/Security.h>` and
  `<TargetConditionals.h>`; also includes `<string.h>`.
- `source/macOS/XPAppKit.m:2` and `source/iOS/XPUIKit.m:2` import
  `<objc/message.h>`.

Notes:

- Normal Cocoa framework imports such as `Foundation`, `UIKit`, and `AppKit`
  were not counted as C-header violations.
- `AltivecCore/AltivecCore.h` was not counted as a C-header violation because
  the visible use is Objective-C message syntax (`[AltivecCore certPath]`).
- The shared `strappy_*.h` headers themselves are C API headers: they use
  `extern "C"`, `typedef struct`, `char *`, `size_t`, and C function
  prototypes.

### Obviously C Code in Objective-C Files

- `source/shared/StrappySession.m` is the largest violation. It defines C
  structs and helpers, uses `char *`, `const char *`, `size_t`, `memset`,
  `record->field`, `list->count`, and many direct `strappy_db_*`,
  `strappy_webview_*`, `strappy_assistant_*`, and `strappy_client_*` calls.
  Representative locations:
  - `source/shared/StrappySession.m:30` defines
    `StrappySessionStreamContext`.
  - `source/shared/StrappySession.m:35` defines a `const char *` helper.
  - `source/shared/StrappySession.m:73` converts C database records into
    `strappy_webview_message`.
  - `source/shared/StrappySession.m:600` calls `strappy_client_set_cainfo`.
  - `source/shared/StrappySession.m:1143` calls `strappy_db_initialize`.
  - `source/shared/StrappySession.m:1180`, `1541`, `1671`, `1753`, `2060`,
    and `2146` allocate/use C record/list structs in Objective-C methods.
- `source/shared/FileScanner.m` bridges dictionaries to C structs and C arrays.
  It uses `calloc`, `free`, `strlen`, `char *` errors, `size_t` indexes, and
  `strappy_file_scanner_*` / `strappy_db_*` APIs.
  Representative locations:
  - `source/shared/FileScanner.m:48` converts filesystem `const char *` paths.
  - `source/shared/FileScanner.m:205` stores
    `strappy_discovered_database_input *inputs`.
  - `source/shared/FileScanner.m:238` allocates the input array with `calloc`.
  - `source/shared/FileScanner.m:317` calls
    `strappy_db_save_discovered_databases`.
  - `source/shared/FileScanner.m:345` uses
    `strappy_file_scanner_options` and `strappy_file_scanner_record_list`.
- `source/shared/StrappyKeychain.m` exports C functions from Objective-C:
  `char *strappy_keychain_copy_api_endpoint`,
  `char *strappy_keychain_copy_api_token`, and
  `int strappy_keychain_save_api_credentials(const char *, const char *)`.
  It also uses `getenv` for environment variables.
- `source/shared/XPFoundation.m` uses CoreFoundation and Security C APIs
  directly: `CFTypeRef`, `CFRelease`, `OSStatus`, `SecItem*`, `SecKeychain*`,
  pointer casts, `UInt32`, `UInt16`, and `strlen`.
- `source/macOS/XPAppKit.m` and `source/iOS/XPUIKit.m` cast and call
  `objc_msgSend` directly.
- `source/macOS/StrappyPreferencesWhitelistView.m:119` defines a local
  `typedef struct` plus a comparator callback with `void *context`.
- `source/iOS/main.m:4` and `source/macOS/main.m:4` contain normal C app
  entry points: `int main(int argc, char *argv[])`.

### Likely Remediation Direction

- Put an Objective-C wrapper facade around each `strappy_*` C subsystem and
  keep C types private to `.c` or tightly isolated adapter files.
- Remove `strappy_*` imports from UI/controller files first, especially
  `PreferencesWindowController` and `StrappyPreferencesAuthenticationView`.
- Decide whether platform/runtime compatibility shims (`XPFoundation`,
  `XPAppKit`, `XPUIKit`, and `main.m`) are explicit exceptions to the rule or
  must also be rewritten/isolated.

### Resolved

- `source/macOS/MessageListViewController.h` no longer exposes
  `struct strappy_webview_script_batch *`.
- `source/macOS/MessageListViewController.m` no longer imports
  `strappy_webview.h`, includes `<string.h>`, builds `strappy_webview_*`
  structs, or owns C webview batch lifetimes. It now calls Objective-C
  rendering/batching methods on `StrappySession`.
