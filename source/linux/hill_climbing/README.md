# Hill-climbing harness

This is a manual, live OpenRouter evaluation harness for Strappy. It runs the
same personal-media question against the currently enabled models while exercising
the real shared Responses API, tool executor, database catalog, and ledger code.

The checked-in model list currently enables only `google/gemma-4-31b-it`; GLM,
DeepSeek, and Qwen remain commented beside it for easy re-enabling.

The default evaluation prompt is:

> Please list out the names, blood types, and personalities of the members of
> my top 3 listened to KPOP bands

The shared runtime resources themselves are intentionally reduced to the
minimum baseline:

- `source/shared/Resources/PromptSystem.txt` is used directly;
- all normal tool names and JSON argument schemas remain available;
- tool and parameter descriptions are removed;
- display metadata and behavioral tool instructions are removed;
- database descriptions, matching rules, related-database hints, and recovery
  instructions are removed;
- the one approved fixture is described only as `Database.`;
- web search and web fetch remain available as unannotated server tools.

The small remaining control strings and generic database labels are structural
runtime requirements, not task guidance.

It is intentionally isolated from `source/linux/Makefile`; neither the normal
`test` target nor a plain invocation of this Makefile performs network calls.

## Private inputs and outputs

- `private/` contains the copied `MediaLibrary.sqlitedb` main/WAL/SHM files.
- `runs/` contains answers, per-model Strappy databases, costs, and reports.
- `.env` files are ignored. The default live run reads the repository-root
  `.env` through the normal Strappy configuration loader.
- All of those paths are ignored by this directory's `.gitignore`. Never force
  add them.

The fixture retains its Apple path suffix for stable run metadata. Each model
gets a new session database, preventing model runs from sharing remembered
facts or other mutable assistant state.

## Commands

Build without contacting OpenRouter:

```sh
make -C source/linux/hill_climbing
```

Validate that the private fixture exists, is ignored, and passes SQLite's quick
check:

```sh
make -C source/linux/hill_climbing verify-fixture
```

Refresh the complete main/WAL/SHM fixture from `gomadango`:

```sh
make -C source/linux/hill_climbing refresh-fixture
```

Run every currently enabled model. The confirmation is deliberately required
because this makes billable network requests:

```sh
make -C source/linux/hill_climbing run CONFIRM_LIVE=yes
```

Run one model:

```sh
make -C source/linux/hill_climbing run CONFIRM_LIVE=yes \
  MODEL=google/gemma-4-31b-it
```

Every run writes an ignored timestamped directory under `runs/` containing the
raw per-model ledgers, final answers, `report.json`, and `report.md`. Each model
directory also contains:

- `answer.md`: Strappy's extracted final answer;
- `output.json`: every raw Responses API response object in call order, plus
  the final answer selected from the session;
- `strappy.sqlite`: the complete queryable request, response, item, and tool
  ledger.

`output.json` intentionally excludes request payloads and authorization
headers.

## Scoring and iteration

The evaluator derives the top three K-pop artists dynamically from aggregate
play counts in the private fixture. It scores whether the model:

- discovers and queries MediaLibrary;
- filters the ranking to KPOP records and aggregates total play counts rather
  than ranking groups by one song;
- covers the dynamically calculated top three in descending order;
- explains that the ranking comes from listening data;
- provides the requested public details with links;
- avoids persisting inferred favorites as durable facts;
- stays within API-call, web-search, latency, and cost budgets.

The score deliberately does not declare public biographical facts correct.
Roster freshness, blood types, and subjective personality descriptions remain
part of the human review checklist in each report.

A useful hill-climbing cycle is:

1. Preserve a baseline run directory.
2. Make one focused shared prompt, tool-guidance, retrieval, or query change.
3. Run the enabled-model comparison again against the unchanged fixture hash.
4. Compare deterministic scores, costs, tool traces, and human-review notes.
5. Keep the change only if it improves behavior across models without harming
   the normal offline Linux harnesses.
