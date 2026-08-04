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

`source/shared/Resources/AssistantSets.json` defines the goal, tool and skill
allowlists, preflight calls, and answer-quality checks for each assistant set.
The shared C prompt builder combines those selections with the matching
descriptions from `GuidanceTools.json`, instruction skills from
`GuidanceSkills.json`, the code-owned audit behavior, and the structured
section copy, audit guidance, optional assistant-set guidance, invariant
personality, and hard rules in `SystemPrompt.json`:

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
  probe reports compact system and effective-identity information, the current
  directory and `PATH`, targeted cross-platform, iOS, and macOS developer-tool
  locations, and the canonical iOS Make include. When installed, `altivec-sdk`
  and `altivec-lib` also provide their guarded read-only inventories. The probe
  omits the iOS system-version plist, raw package-architecture queries,
  disk/header inventories, working-directory listings, complete `PATH`
  directory inventories, broad tool-version sweeps, and the full environment.
  `file_read` reads bounded UTF-8 text ranges, while `bash` runs a fresh
  non-interactive child shell with a hard 120-second ceiling. File tools and
  Bash share a per-session working directory. New
  sessions default to `~/Developer`; the iOS prompt options' Debug submenu can
  instead select `~/` or `~/Library/Application Support/Strappy/Developer`.
  The same submenu can disable parallel tool calls for a session, asking the
  model to make at most one tool call per response. Selecting a missing
  directory creates it before the database setting is changed. Bash results
  expose `output_truncated` so the model can distinguish complete output from a
  bounded tail. Its assistant-set guidance also keeps source artifacts
  professionally neutral, requires evidence-backed API and command claims,
  requires relevant tests/builds/runtime checks after changes, and prohibits
  commits and pushes.
- Database Study is an internal assistant with `database_list`,
  `database_context`, `database_query`, `database_study`, and both datetime
  conversion tools. The application persists its fixed session name as
  `Database Study`; the model does not receive session-renaming or Font Awesome
  tools or guidance. A study save requires successful database context and
  real-table query results from the same isolated study batch; schema-only or
  earlier-batch queries do not satisfy that guard. Study guidance preserves
  compact retrieval recipes with exact successfully executed SQL rather than
  table or column inventories that can be generated on demand. Each database's
  description and context are required together and saved atomically by one
  `database_study` call. After a study batch ends, its rounds are excluded
  through the same context-inclusion state used by normal sessions.

An assistant set is selected per session and can be changed between prompts.
User memories are shared by sessions using the same assistant set and isolated
from sessions using other assistant sets; switching a session changes which
memories it can read, save, and delete.
Answer-quality checking is also persisted per session and initially defaults
to off. On iOS it can be enabled for an active session under Debug → Limits or
for future sessions under Preferences → Session Defaults → Debug → Limits.
When disabled, the prompt omits audit guidance and Strappy creates no
answer-quality audit, check, or timeline entry; enabling it preserves the full
current evaluation and report behavior.
The prompt-options button is disabled while a prompt is in progress, so model,
assistant-set, web-search, and Bash changes cannot overlap an active request.

Instruction skills are deliberately small and resource-backed. Add entries to
`source/shared/Resources/GuidanceSkills.json` with a stable lowercase `id`, a
display `title`, a short routing `description`, and `instructions`. Enable IDs
through `universal.skills` or a set's `additional_skills` in
`AssistantSets.json`. On the first prompt, `skills_list` supplies only the
allowed metadata; the model calls `skill_read` when a description matches the
current request. Skills provide instructions only and cannot add tools.

Generate every assistant-set prompt with web search set to none, auto, native,
Exa, and Parallel:

```sh
make -C source/linux prompts
```

The twenty review files are written under
`source/linux/build-linux/system-prompts`. Use `review-prompts` instead of
`prompts` to print them to standard output.
