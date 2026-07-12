#!/usr/bin/env python3
"""Deterministic ledger scorer for hill-climbing runs.

The score is a regression signal, not a substitute for checking public facts.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sqlite3
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


REQUIRED_GROUP_COUNT = 3
AUDIT_HIT_PENALTY = 3

# This is an alias map, not a checklist. Any of these groups may occupy one of
# the three database-derived ranking slots, and only those three slots are
# required for full coverage credit.
ALLOWED_ARTIST_ALIASES: dict[str, tuple[str, ...]] = {
    "the boyz": ("the boyz", "boyz"),
    "zerobaseone": ("zerobaseone", "zerobase", "zb1"),
    "tomorrow x together": ("tomorrow x together", "txt"),
    "&team": ("&team",),
    "enhypen": ("enhypen",),
    "seventeen": ("seventeen",),
}


@dataclass
class Result:
    model: str
    slug: str
    score: int = 0
    grade: str = "F"
    checks: list[dict[str, Any]] = field(default_factory=list)
    metrics: dict[str, Any] = field(default_factory=dict)

    def add(self, name: str, passed: bool, points: int, note: str = "") -> None:
        earned = points if passed else 0
        self.score += earned
        self.checks.append(
            {
                "name": name,
                "passed": passed,
                "earned": earned,
                "possible": points,
                "note": note,
            }
        )

    def penalize(self, name: str, hits: int, points_per_hit: int) -> None:
        penalty = hits * points_per_hit
        self.score -= penalty
        self.checks.append(
            {
                "name": name,
                "passed": hits == 0,
                "earned": -penalty,
                "possible": 0,
                "penalty": penalty,
                "note": (
                    f"{hits} hit{'s' if hits != 1 else ''} × "
                    f"{points_per_hit} points"
                ),
            }
        )

    def normalize_positive_score(self, target: int = 100) -> None:
        possible = sum(
            int(check["possible"])
            for check in self.checks
            if int(check["possible"]) > 0
        )
        earned = self.score
        normalized = round(earned * target / possible) if possible else 0
        self.metrics["positive_points_earned"] = earned
        self.metrics["positive_points_possible"] = possible
        self.metrics["normalized_score_before_penalties"] = normalized
        self.score = normalized


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--run-dir", required=True, type=Path)
    return parser.parse_args()


def connect_readonly(path: Path) -> sqlite3.Connection:
    connection = sqlite3.connect(f"file:{path.resolve()}?mode=ro", uri=True)
    connection.row_factory = sqlite3.Row
    return connection


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def resolve_database_manifest(metadata: dict[str, Any]) -> Path:
    recorded_value = metadata.get("database_manifest")
    expected_hash = metadata.get("database_manifest_sha256")
    if not isinstance(recorded_value, str) or not isinstance(expected_hash, str):
        raise RuntimeError("Run metadata does not identify its database manifest.")

    recorded = Path(recorded_value)
    harness_dir = Path(__file__).resolve().parent
    candidates = (recorded, harness_dir / "private/gomadango/databases.json")
    for candidate in candidates:
        if candidate.is_file() and sha256(candidate) == expected_hash:
            return candidate
    raise RuntimeError(
        "The run's database manifest is unavailable or its SHA-256 changed."
    )


def resolve_ranking_database(metadata: dict[str, Any]) -> Path:
    if "database_manifest" not in metadata:
        recorded_value = metadata.get("media_db")
        expected_hash = metadata.get("media_db_sha256")
        if not isinstance(recorded_value, str) or not isinstance(
            expected_hash, str
        ):
            raise RuntimeError(
                "Legacy run metadata does not identify its ranking database."
            )
        recorded = Path(recorded_value)
        harness_dir = Path(__file__).resolve().parent
        candidates = (
            recorded,
            harness_dir
            / "private/gomadango/root/var/mobile/Media/iTunes_Control/iTunes/MediaLibrary.sqlitedb",
            harness_dir
            / "private/Media/iTunes_Control/iTunes/MediaLibrary.sqlitedb",
        )
        for candidate in candidates:
            if candidate.is_file() and sha256(candidate) == expected_hash:
                return candidate
        raise RuntimeError(
            "The legacy run's ranking database is unavailable or its "
            "SHA-256 changed."
        )

    suffix = "/Media/iTunes_Control/iTunes/MediaLibrary.sqlitedb"
    manifest_path = resolve_database_manifest(metadata)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    entries = manifest.get("databases")
    if manifest.get("format_version") != 1 or not isinstance(entries, list):
        raise RuntimeError(f"Invalid database manifest: {manifest_path}")

    matches = [
        entry
        for entry in entries
        if isinstance(entry, dict)
        and isinstance(entry.get("remote_path"), str)
        and entry["remote_path"].endswith(suffix)
    ]
    if len(matches) != 1:
        raise RuntimeError(
            "The run manifest must contain exactly one ranking database; "
            f"found {len(matches)}."
        )
    entry = matches[0]
    local_path = entry.get("local_path")
    expected_hash = entry.get("sha256")
    if not isinstance(local_path, str) or not isinstance(expected_hash, str):
        raise RuntimeError(f"Invalid ranking database entry in {manifest_path}")
    database = (manifest_path.parent / local_path).resolve()
    if not database.is_file() or sha256(database) != expected_hash:
        raise RuntimeError(
            "The ranking database is unavailable or its SHA-256 changed: "
            f"{database}"
        )
    return database


def expected_artists(ranking_db: Path, limit: int = 7) -> list[tuple[str, int]]:
    sql = """
        SELECT ia.item_artist AS artist,
               SUM(COALESCE(s.play_count_user, 0)) AS total_plays
        FROM item i
        JOIN item_artist ia ON ia.item_artist_pid = i.item_artist_pid
        JOIN genre g ON g.genre_id = i.genre_id
        LEFT JOIN item_stats s ON s.item_pid = i.item_pid
        WHERE LOWER(REPLACE(REPLACE(g.genre, '-', ''), ' ', '')) = 'kpop'
        GROUP BY ia.item_artist
        HAVING total_plays > 0
        ORDER BY total_plays DESC, COUNT(*) DESC
    """
    ignored_fragments = (" feat. ", "jonas brothers")
    with connect_readonly(ranking_db) as database:
        rows = database.execute(sql).fetchall()
    primary = []
    for row in rows:
        artist = str(row["artist"])
        if any(fragment in artist.lower() for fragment in ignored_fragments):
            continue
        primary.append((artist, int(row["total_plays"])))
        if len(primary) == limit:
            break
    return primary


def safe_json(value: str | None) -> dict[str, Any]:
    if not value:
        return {}
    try:
        parsed = json.loads(value)
    except json.JSONDecodeError:
        return {}
    return parsed if isinstance(parsed, dict) else {}


def fact_matches(candidate: Any, expected: dict[str, Any]) -> bool:
    if not isinstance(candidate, dict):
        return False
    return all(
        str(candidate.get(field, "")).casefold()
        == str(expected.get(field, "")).casefold()
        for field in ("kind", "subject", "predicate", "value")
    )


def artist_aliases(artist: str) -> tuple[str, ...]:
    lowered = artist.lower()
    return ALLOWED_ARTIST_ALIASES.get(lowered, (lowered,))


def artist_position(answer_lower: str, artist: str) -> int:
    positions = [
        answer_lower.find(alias)
        for alias in artist_aliases(artist)
        if answer_lower.find(alias) >= 0
    ]
    return min(positions) if positions else -1


def efficiency_points(result: Result, cost: float, wall: float, calls: int, web: int) -> None:
    result.add("Cost budget", cost <= 0.20, 5, f"${cost:.4f}")
    result.add("Latency budget", wall <= 360.0, 4, f"{wall:.1f}s")
    if calls <= 8:
        call_points = 3
    elif calls <= 12:
        call_points = 2
    elif calls <= 16:
        call_points = 1
    else:
        call_points = 0
    result.score += call_points
    result.checks.append(
        {
            "name": "API-call budget",
            "passed": call_points == 3,
            "earned": call_points,
            "possible": 3,
            "note": str(calls),
        }
    )
    result.add("Web-search budget", web <= 10, 3, str(web))


def score_session(
    session_db: Path,
    ranking_db: Path,
    slug: str,
    expected_database_count: int,
    expected_user_fact: dict[str, Any] | None,
) -> Result:
    with connect_readonly(session_db) as database:
        session = database.execute(
            "SELECT id, model, response FROM sessions ORDER BY id DESC LIMIT 1"
        ).fetchone()
        if session is None:
            raise RuntimeError(f"No session in {session_db}")
        session_id = int(session["id"])
        model = str(session["model"] or slug)
        answer = str(session["response"] or "")
        answer_lower = answer.lower()

        calls_row = database.execute(
            """
            SELECT COUNT(*) AS calls,
                   SUM(is_error) AS errors,
                   SUM(CASE WHEN request_kind = 'tool_audit'
                            THEN 1 ELSE 0 END) AS audit_hits,
                   COALESCE(SUM(response_usage_cost), 0) AS cost,
                   COALESCE((MAX(transport_completed_at_ms) -
                             MIN(request_started_at_ms)) / 1000.0, 0) AS wall
            FROM response_api_calls WHERE session_id = ?
            """,
            (session_id,),
        ).fetchone()
        tools = database.execute(
            """
            SELECT tool_name, arguments_json, status, error_text
            FROM response_tool_executions
            WHERE session_id = ? ORDER BY id
            """,
            (session_id,),
        ).fetchall()
        media_row = database.execute(
            """
            SELECT assistant_database_id FROM discovered_databases
            WHERE path LIKE '%/Media/iTunes_Control/iTunes/MediaLibrary.sqlitedb'
            LIMIT 1
            """
        ).fetchone()
        web = int(
            database.execute(
                """
                SELECT COUNT(*) FROM response_api_items
                WHERE session_id = ? AND direction = 'response'
                  AND type = 'openrouter:web_search'
                """,
                (session_id,),
            ).fetchone()[0]
        )
        preflight_pairs = int(
            database.execute(
                """
                SELECT COUNT(DISTINCT calls.name)
                FROM response_api_items AS calls
                WHERE calls.session_id = ?
                  AND calls.direction = 'request'
                  AND calls.is_canonical = 1
                  AND calls.type = 'function_call'
                  AND calls.name IN ('database_list_info',
                                     'memory_user_fact_read')
                  AND EXISTS (
                    SELECT 1 FROM response_api_items AS outputs
                    WHERE outputs.session_id = calls.session_id
                      AND outputs.direction = 'request'
                      AND outputs.is_canonical = 1
                      AND outputs.type = 'function_call_output'
                      AND outputs.call_id = calls.call_id
                  )
                """,
                (session_id,),
            ).fetchone()[0]
        )
        approved_databases = int(
            database.execute(
                """
                SELECT COUNT(*) FROM discovered_databases
                WHERE user_decision = 'allowed' AND is_valid_sqlite = 1
                """
            ).fetchone()[0]
        )
        stored_user_facts = [
            dict(row)
            for row in database.execute(
                """
                SELECT kind, subject, predicate, value, confidence, source
                FROM helper_user_info
                WHERE status = 'active'
                """
            ).fetchall()
        ]
        memory_preflight_outputs = [
            str(row[0] or "")
            for row in database.execute(
                """
                SELECT outputs.output
                FROM response_api_items AS calls
                JOIN response_api_items AS outputs
                  ON outputs.response_call_id = calls.response_call_id
                 AND outputs.session_id = calls.session_id
                 AND outputs.call_id = calls.call_id
                WHERE calls.session_id = ?
                  AND calls.direction = 'request'
                  AND calls.is_canonical = 1
                  AND calls.type = 'function_call'
                  AND calls.name = 'memory_user_fact_read'
                  AND outputs.direction = 'request'
                  AND outputs.is_canonical = 1
                  AND outputs.type = 'function_call_output'
                """,
                (session_id,),
            ).fetchall()
        ]
        if expected_database_count <= 0:
            expected_database_count = approved_databases

    media_id = str(media_row[0]) if media_row else ""
    parsed_tools = [(row, safe_json(row["arguments_json"])) for row in tools]
    media_queries = [
        args
        for row, args in parsed_tools
        if row["tool_name"] == "database_query"
        and args.get("database_id") == media_id
    ]
    sql_queries = [str(args.get("sql", "")).lower() for args in media_queries]
    sql_text = "\n".join(sql_queries)
    tool_errors = [row for row in tools if row["status"] == "error"]
    favorite_writes = [
        args
        for row, args in parsed_tools
        if row["tool_name"] == "memory_user_fact_remember"
        and "favorite" in str(args.get("predicate", "")).lower()
    ]

    calls = int(calls_row["calls"] or 0)
    errors = int(calls_row["errors"] or 0)
    audit_hits = int(calls_row["audit_hits"] or 0)
    cost = float(calls_row["cost"] or 0.0)
    wall = float(calls_row["wall"] or 0.0)
    artists = expected_artists(ranking_db)
    top_three = artists[:REQUIRED_GROUP_COUNT]
    mentioned_top_three = [
        name
        for name, _ in top_three
        if artist_position(answer_lower, name) >= 0
    ]
    user_fact_stored = bool(expected_user_fact) and any(
        fact_matches(fact, expected_user_fact) for fact in stored_user_facts
    )
    user_fact_preflight = bool(expected_user_fact) and any(
        any(
            fact_matches(fact, expected_user_fact)
            for fact in safe_json(output).get("facts", [])
        )
        for output in memory_preflight_outputs
    )
    expected_user_name = (
        str(expected_user_fact.get("value", ""))
        if expected_user_fact
        else ""
    )
    user_name_mentioned = bool(expected_user_name) and bool(
        re.search(
            rf"(?<![A-Za-z]){re.escape(expected_user_name)}(?![A-Za-z])",
            answer,
            flags=re.IGNORECASE,
        )
    )

    result = Result(model=model, slug=slug)
    result.metrics = {
        "calls": calls,
        "api_errors": errors,
        "audit_hits": audit_hits,
        "local_tools": len(tools),
        "tool_errors": len(tool_errors),
        "web_searches": web,
        "preflight_tool_pairs": preflight_pairs,
        "approved_databases": approved_databases,
        "expected_databases": expected_database_count,
        "cost": cost,
        "wall_seconds": wall,
        "answer_characters": len(answer),
        "expected_artists": [
            {"name": name, "total_plays": plays} for name, plays in artists
        ],
        "expected_top_three": [
            {"name": name, "total_plays": plays} for name, plays in top_three
        ],
        "mentioned_top_three": mentioned_top_three,
        "seeded_user_fact_stored": user_fact_stored,
        "seeded_user_fact_in_preflight": user_fact_preflight,
        "seeded_user_name_mentioned": user_name_mentioned,
    }

    result.add(
        "Loaded all approved fixtures",
        preflight_pairs == 2
        and approved_databases == expected_database_count,
        5,
        f"{approved_databases}/{expected_database_count}",
    )
    if expected_user_fact:
        result.add(
            "Loaded seeded user fact",
            user_fact_stored and user_fact_preflight,
            4,
            (
                f"stored={'yes' if user_fact_stored else 'no'}, "
                f"preflight={'yes' if user_fact_preflight else 'no'}"
            ),
        )
        result.add(
            "Used remembered user name in answer",
            user_fact_stored and user_fact_preflight and user_name_mentioned,
            6,
            expected_user_name,
        )
    result.add("Queried MediaLibrary", bool(media_queries), 8)
    aggregate_queries = [
        sql
        for sql in sql_queries
        if all(token in sql for token in ("sum(", "play_count_user", "group by"))
    ]
    kpop_lookup = any(
        ("from genre" in sql) and (("kpop" in sql) or ("k-pop" in sql))
        for sql in sql_queries
    )
    aggregate_filters_genre = any(
        ("genre_id" in sql) or (("genre" in sql) and ("kpop" in sql))
        for sql in aggregate_queries
    )
    result.add(
        "Filtered the ranking to KPOP records",
        kpop_lookup and aggregate_filters_genre,
        5,
    )
    result.add(
        "Aggregated play counts instead of ranking one track",
        bool(aggregate_queries),
        12,
    )
    result.add("No local tool errors", not tool_errors, 5, str(len(tool_errors)))

    coverage_points = round(
        14 * len(mentioned_top_three) / REQUIRED_GROUP_COUNT
    )
    result.score += coverage_points
    result.checks.append(
        {
            "name": "Covered the dynamically calculated top three",
            "passed": len(mentioned_top_three) == REQUIRED_GROUP_COUNT,
            "earned": coverage_points,
            "possible": 14,
            "note": (
                f"{len(mentioned_top_three)}/{REQUIRED_GROUP_COUNT}: "
                f"{', '.join(mentioned_top_three)}"
            ),
        }
    )
    requested_attributes = ("blood type" in answer_lower) and (
        ("personality" in answer_lower) or ("mbti" in answer_lower)
    )
    result.add("Addressed personality and blood type", requested_attributes, 6)
    result.add(
        "Linked public sources",
        bool(re.search(r"https?://", answer, flags=re.IGNORECASE)),
        5,
    )
    positions = [artist_position(answer_lower, name) for name, _ in top_three]
    correct_order = (
        len(positions) == 3
        and all(position >= 0 for position in positions)
        and positions == sorted(positions)
    )
    result.add("Presented the top three in descending order", correct_order, 3)
    top_artist = top_three[0][0] if top_three else ""
    result.add(
        "Included the top aggregate-play artist",
        bool(top_artist) and artist_position(answer_lower, top_artist) >= 0,
        4,
    )

    result.add(
        "Did not persist inferred favorites as durable memory",
        not favorite_writes,
        7,
        str(len(favorite_writes)),
    )
    result.add("No failed API attempts", errors == 0, 3, str(errors))
    efficiency_points(result, cost, wall, calls, web)
    result.normalize_positive_score()
    result.penalize(
        "Tool-audit interventions",
        audit_hits,
        AUDIT_HIT_PENALTY,
    )
    result.score = max(0, result.score)

    if result.score >= 90:
        result.grade = "A"
    elif result.score >= 80:
        result.grade = "B"
    elif result.score >= 70:
        result.grade = "C"
    elif result.score >= 60:
        result.grade = "D"
    return result


def render_markdown(results: list[Result]) -> str:
    lines = [
        "# Hill-climbing evaluation",
        "",
        "This deterministic score measures database workflow, evidence coverage, "
        "state safety, and efficiency. Tool-audit interventions directly reduce "
        "the score. Positive checks are normalized to 100 points before audit "
        "penalties. Public roster, blood-type, and personality claims still "
        "require human/source review.",
        "",
        "| Model | Score | Grade | Time | Cost | Calls | Local tools | Web | Audits |",
        "|---|---:|:---:|---:|---:|---:|---:|---:|---:|",
    ]
    for result in sorted(results, key=lambda item: item.score, reverse=True):
        metrics = result.metrics
        lines.append(
            f"| {result.model} | {result.score}/100 | {result.grade} | "
            f"{metrics['wall_seconds']:.1f}s | ${metrics['cost']:.4f} | "
            f"{metrics['calls']} | {metrics['local_tools']} | "
            f"{metrics['web_searches']} | {metrics['audit_hits']} |"
        )
    for result in results:
        lines.extend(["", f"## {result.model}", ""])
        metrics = result.metrics
        lines.extend(
            [
                "- Positive-check subtotal: "
                f"{metrics['positive_points_earned']}/"
                f"{metrics['positive_points_possible']}, normalized to "
                f"{metrics['normalized_score_before_penalties']}/100 before "
                "audit penalties.",
                "",
            ]
        )
        for check in result.checks:
            marker = "PASS" if check["passed"] else "MISS"
            note = f" — {check['note']}" if check["note"] else ""
            if "penalty" in check:
                penalty = int(check["penalty"])
                penalty_text = (
                    f"-{penalty} points" if penalty else "0 point penalty"
                )
                lines.append(
                    f"- {marker} {check['name']}: {penalty_text}{note}"
                )
            else:
                lines.append(
                    f"- {marker} {check['name']}: "
                    f"{check['earned']}/{check['possible']}{note}"
                )
    lines.extend(
        [
            "",
            "## Human review checklist",
            "",
            "- Are current group rosters checked against current authoritative sources?",
            "- Are blood types supported by the linked sources rather than recalled?",
            "- Are personality descriptions qualified as subjective and time-sensitive?",
            "- Is the answer proportionate, readable, and free of exposed planning text?",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    run_dir = args.run_dir.resolve()
    metadata_path = run_dir / "run.json"
    if not metadata_path.is_file():
        raise SystemExit(f"Missing run metadata: {metadata_path}")
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    ranking_db = resolve_ranking_database(metadata)
    expected_database_count = int(metadata.get("database_count", 0))
    expected_user_fact = metadata.get("seeded_user_fact")
    if not isinstance(expected_user_fact, dict):
        expected_user_fact = None

    results = []
    for model, run in metadata.get("results", {}).items():
        slug = str(run["slug"])
        session_db = run_dir / slug / "strappy.sqlite"
        if not session_db.is_file():
            continue
        result = score_session(
            session_db,
            ranking_db,
            slug,
            expected_database_count,
            expected_user_fact,
        )
        if result.model != model:
            result.metrics["requested_model"] = model
        results.append(result)

    if not results:
        raise SystemExit("No completed session databases were available to score.")

    report_json = {
        "run_dir": str(run_dir),
        "results": [
            {
                "model": result.model,
                "slug": result.slug,
                "score": result.score,
                "grade": result.grade,
                "metrics": result.metrics,
                "checks": result.checks,
            }
            for result in results
        ],
    }
    (run_dir / "report.json").write_text(
        json.dumps(report_json, indent=2) + "\n", encoding="utf-8"
    )
    (run_dir / "report.md").write_text(
        render_markdown(results), encoding="utf-8"
    )
    print(render_markdown(results))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
