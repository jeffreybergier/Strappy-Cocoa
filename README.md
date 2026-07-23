# Strappy

Minimal Cocoa application scaffold based on the ENIL-cocoa build layout.

## Build

macOS:

```sh
cd source/macOS
make debug
```

iOS:

```sh
cd source/iOS
make debug
```

Both targets use the Altivec build engine from `/altivec` and the Apple SDKs
from `/osxcross`.

## Assistant sets

`source/shared/Resources/AssistantSets.json` defines the goal, tool allowlist,
preflight calls, and answer-quality checks for each assistant set. The shared C
prompt builder combines those selections with the matching descriptions from
`GuidanceTools.json`, the code-owned audit behavior, and the structured section
copy, audit guidance, invariant personality, and hard rules in
`SystemPrompt.json`:

- World Knowledge exposes only universal web, user-memory, date, Font Awesome,
  and session-name tools.
- Personal Assistant is the default and adds the personal-database tools and
  database-specific checks.
- Coding Assistant is available with the set-only `file_read` tool and an
  opt-in `bash` tool. Bash starts disabled for every session and can be enabled
  from the iOS prompt options only while Coding Assistant is selected; changing
  assistants disables it again. This setting controls model access only: the
  application-owned first-prompt preflight always runs a bounded environment
  probe and seeds its result, even when model access to Bash is disabled. The
  probe reports the iOS/system identity, current directory and safe shell-path
  variables, disk/header roots, relevant C and jailbroken-device tool paths and
  versions, and `ls -al`; it does not dump the full environment. `file_read`
  reads bounded UTF-8 text ranges, while `bash` runs a fresh non-interactive
  child shell with a hard 120-second ceiling. The Coding Assistant file tools
  and Bash share a per-session working directory. New sessions default to
  `~/Developer`; the iOS prompt options can instead select `~/` or
  `~/Library/Application Support/Strappy/Developer`. Selecting a missing
  directory creates it before the database setting is changed. Bash results
  expose `output_truncated` so the model can distinguish complete output from a
  bounded tail.
- Database Study is an internal assistant with `database_list`,
  `database_context`, `database_query`, `database_study`, and both datetime
  conversion tools. The application persists its fixed session name as
  `Database Study`; the model does not receive session-renaming or Font Awesome
  tools or guidance.

An assistant set is selected per session and can be changed between prompts.
User memories are shared by sessions using the same assistant set and isolated
from sessions using other assistant sets; switching a session changes which
memories it can read, save, and delete.
The prompt-options button is disabled while a prompt is in progress, so model,
assistant-set, web-search, and Bash changes cannot overlap an active request.

Generate every assistant-set prompt with web search set to none, auto, native,
Exa, and Parallel:

```sh
make -C source/linux prompts
```

The twenty review files are written under
`source/linux/build-linux/system-prompts`. Use `review-prompts` instead of
`prompts` to print them to standard output.
