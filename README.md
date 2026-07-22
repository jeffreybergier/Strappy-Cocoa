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
  application-owned first-prompt preflight always runs `uname -a` and seeds its
  result, even when model access to Bash is disabled. `file_read` reads bounded
  UTF-8 text ranges, while `bash` runs a fresh non-interactive child shell with
  a hard 120-second ceiling; both start in the per-session working directory.
  Bash results expose `output_truncated` so the model can distinguish complete
  output from a bounded tail.

An assistant set is selected per session and can be changed between prompts.
The prompt-options button is disabled while a prompt is in progress, so model,
assistant-set, web-search, and Bash changes cannot overlap an active request.

Generate every assistant-set prompt with web search set to none, auto, native,
Exa, and Parallel:

```sh
make -C source/linux prompts
```

The fifteen review files are written under
`source/linux/build-linux/system-prompts`. Use `review-prompts` instead of
`prompts` to print them to standard output.
