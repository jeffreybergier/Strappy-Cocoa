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

`source/shared/Resources/AssistantSets.json` defines the prompt, tool allowlist,
preflight calls, and answer-quality checks for each assistant set:

- World Knowledge exposes only universal web, user-memory, date, Font Awesome,
  and session-name tools.
- Personal Assistant is the default and adds the personal-database tools and
  database-specific checks.
- Coding Assistant is visible as Coming Soon and cannot be selected until its
  coding tools are implemented.

An assistant set is selected per session and can be changed between prompts.
The prompt-options button is disabled while a prompt is in progress, so model,
assistant-set, and web-search changes cannot overlap an active request.
