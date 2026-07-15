#!/usr/bin/env python3
"""Deterministic ledger scorer for hill-climbing runs.

The score is a regression signal, not a substitute for checking public facts.
"""

from __future__ import annotations

import argparse
import json
import re
import sqlite3
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


REQUIRED_GROUP_COUNT = 3
AUDIT_HIT_PENALTY = 3
COST_BUDGET_USD = 0.01

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


def resolve_database_manifest(metadata: dict[str, Any]) -> Path:
    recorded_value = metadata.get("database_manifest")
    if not isinstance(recorded_value, str):
        raise RuntimeError("Run metadata does not identify its database manifest.")

    recorded = Path(recorded_value)
    harness_dir = Path(__file__).resolve().parent
    candidates = (recorded, harness_dir / "private/gomadango/databases.json")
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise RuntimeError("The run's database manifest is unavailable.")


def resolve_ranking_database(metadata: dict[str, Any]) -> Path:
    if "database_manifest" not in metadata:
        recorded_value = metadata.get("media_db")
        if not isinstance(recorded_value, str):
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
            if candidate.is_file():
                return candidate
        raise RuntimeError("The legacy run's ranking database is unavailable.")

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
    if not isinstance(local_path, str):
        raise RuntimeError(f"Invalid ranking database entry in {manifest_path}")
    fixture_root = manifest_path.parent.resolve()
    database = (fixture_root / local_path).resolve()
    if fixture_root not in database.parents:
        raise RuntimeError(f"Ranking database escapes private root: {database}")
    if not database.is_file():
        raise RuntimeError(
            f"The ranking database is unavailable: {database}"
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


def safe_json_array(value: str | None) -> list[Any]:
    if not value:
        return []
    try:
        parsed = json.loads(value)
    except json.JSONDecodeError:
        return []
    return parsed if isinstance(parsed, list) else []


def structured_value(
    database: sqlite3.Connection,
    owner_item_id: int,
    purpose: str,
) -> object | None:
    document = database.execute(
        """
        SELECT id FROM structured_documents
        WHERE owner_item_id = ? AND purpose = ?
        """,
        (owner_item_id, purpose),
    ).fetchone()
    if document is None:
        return None
    rows = database.execute(
        """
        SELECT node_id, parent_node_id, ordinal, member_name, value_type,
               text_value, number_value, boolean_value
        FROM structured_nodes WHERE document_id = ? ORDER BY node_id
        """,
        (document["id"],),
    ).fetchall()
    if not rows:
        return None
    nodes = {int(row["node_id"]): row for row in rows}
    children: dict[int, list[sqlite3.Row]] = {}
    for row in rows:
        parent = row["parent_node_id"]
        if parent is not None:
            children.setdefault(int(parent), []).append(row)
    for values in children.values():
        values.sort(key=lambda row: int(row["ordinal"]))

    def build(node_id: int) -> object | None:
        row = nodes[node_id]
        value_type = str(row["value_type"])
        child_rows = children.get(node_id, [])
        if value_type == "object":
            return {
                str(child["member_name"]): build(int(child["node_id"]))
                for child in child_rows
            }
        if value_type == "array":
            return [build(int(child["node_id"])) for child in child_rows]
        if value_type == "string":
            return str(row["text_value"] or "")
        if value_type == "number":
            number = str(row["number_value"] or "0")
            try:
                return json.loads(number)
            except json.JSONDecodeError:
                return number
        if value_type == "boolean":
            return bool(row["boolean_value"])
        return None

    return build(0)


def item_text(database: sqlite3.Connection, item_id: int) -> str:
    rows = database.execute(
        """
        SELECT text FROM item_text_parts
        WHERE item_id = ? AND collection_name = 'content'
        ORDER BY ordinal
        """,
        (item_id,),
    ).fetchall()
    return "\n".join(str(row["text"]) for row in rows)


def output_json(database: sqlite3.Connection, row: sqlite3.Row) -> str:
    item_id = row["output_item_id"]
    if item_id is None:
        return ""
    if row["output_format"] == "structured":
        value = structured_value(database, int(item_id), "output")
        return json.dumps(value, ensure_ascii=False)
    return str(row["text_output"] or "")


def fact_matches(candidate: Any, expected: dict[str, Any]) -> bool:
    if not isinstance(candidate, dict):
        return False
    return all(
        str(candidate.get(field, "")).casefold()
        == str(expected.get(field, "")).casefold()
        for field in ("kind", "subject", "predicate", "value")
    )


def preflight_fact_matches(candidate: Any, expected: dict[str, Any]) -> bool:
    if not isinstance(candidate, dict):
        return False
    return str(candidate.get("fact", "")).casefold() == str(
        expected.get("value", "")
    ).casefold()


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
    result.add("Cost budget", cost <= COST_BUDGET_USD, 5, f"${cost:.4f}")
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
            "SELECT id, model_id FROM sessions ORDER BY id DESC LIMIT 1"
        ).fetchone()
        if session is None:
            raise RuntimeError(f"No session in {session_db}")
        session_id = int(session["id"])
        model = str(session["model_id"] or slug)
        answer_item = database.execute(
            """
            SELECT i.id FROM conversation_items i
            JOIN message_items m ON m.item_id = i.id
            WHERE i.session_id = ? AND m.role = 'assistant'
            ORDER BY i.sequence DESC LIMIT 1
            """,
            (session_id,),
        ).fetchone()
        answer = (
            item_text(database, int(answer_item["id"]))
            if answer_item
            else ""
        )
        answer_lower = answer.lower()

        calls_row = database.execute(
            """
            SELECT COUNT(*) AS calls,
                   COALESCE(SUM(CASE
                     WHEN a.state != 'completed' OR a.http_status >= 400
                       OR ar.error_type IS NOT NULL
                       OR ar.error_message IS NOT NULL
                       OR ar.parse_error IS NOT NULL
                     THEN 1 ELSE 0 END), 0) AS errors,
                   COALESCE(SUM(CASE WHEN r.request_kind = 'tool_audit'
                            THEN 1 ELSE 0 END), 0) AS audit_hits,
                   COALESCE(SUM(u.cost_nano_usd), 0) / 1000000000.0 AS cost,
                   COALESCE((MAX(COALESCE(a.completed_at_ms,
                                         a.started_at_ms)) -
                             MIN(a.started_at_ms)) / 1000.0, 0) AS wall
            FROM http_attempts a
            JOIN model_requests r ON r.id = a.request_id
            JOIN turns t ON t.id = r.turn_id
            LEFT JOIN api_results ar ON ar.attempt_id = a.id
            LEFT JOIN api_usage u ON u.attempt_id = a.id
            WHERE t.session_id = ?
            """,
            (session_id,),
        ).fetchone()
        tool_rows = database.execute(
            """
            SELECT e.id, f.tool_name, f.item_id AS call_item_id,
                   e.state AS status, e.error_message AS error_text,
                   fo.item_id AS output_item_id, fo.output_format,
                   fo.text_output
            FROM tool_executions e
            JOIN function_calls f ON f.item_id = e.function_call_item_id
            JOIN conversation_items i ON i.id = f.item_id
            LEFT JOIN function_outputs fo
              ON fo.function_call_item_id = f.item_id
            WHERE i.session_id = ? ORDER BY e.id
            """,
            (session_id,),
        ).fetchall()
        tools = []
        for row in tool_rows:
            arguments = structured_value(
                database, int(row["call_item_id"]), "arguments"
            )
            tools.append(
                {
                    "tool_name": row["tool_name"],
                    "arguments_json": json.dumps(
                        arguments if arguments is not None else {},
                        ensure_ascii=False,
                    ),
                    "status": row["status"],
                    "output_json": output_json(database, row),
                    "error_text": row["error_text"],
                }
            )
        media_row = database.execute(
            """
            SELECT d.assistant_database_id
            FROM databases d
            JOIN database_locations l
              ON l.database_id = d.id AND l.active = 1
            WHERE l.path LIKE
              '%/Media/iTunes_Control/iTunes/MediaLibrary.sqlitedb'
            LIMIT 1
            """
        ).fetchone()
        web = int(
            database.execute(
                """
                SELECT COUNT(*) FROM conversation_items
                WHERE session_id = ? AND kind = 'openrouter:web_search'
                  AND source_attempt_id IS NOT NULL
                """,
                (session_id,),
            ).fetchone()[0]
        )
        preflight_pairs = int(
            database.execute(
                """
                SELECT COUNT(DISTINCT calls.tool_name)
                FROM function_calls AS calls
                JOIN conversation_items AS call_items
                  ON call_items.id = calls.item_id
                WHERE call_items.session_id = ?
                  AND calls.tool_name IN ('database_list_info',
                                          'memory_user_fact_read')
                  AND EXISTS (
                    SELECT 1 FROM function_outputs AS outputs
                    WHERE outputs.function_call_item_id = calls.item_id
                  )
                """,
                (session_id,),
            ).fetchone()[0]
        )
        approved_databases = int(
            database.execute(
                """
                SELECT COUNT(DISTINCT d.id)
                FROM databases d
                JOIN database_locations l
                  ON l.database_id = d.id AND l.active = 1
                JOIN database_permissions p ON p.database_id = d.id
                WHERE p.decision = 'allowed'
                  AND l.validation_state = 'valid'
                """
            ).fetchone()[0]
        )
        stored_user_facts = [
            dict(row)
            for row in database.execute(
                """
                SELECT id, kind, subject, predicate, value,
                       confidence_basis_points / 10000.0 AS confidence,
                       'model_observed' AS source
                FROM user_facts ORDER BY id
                """
            ).fetchall()
        ]
        memory_rows = database.execute(
            """
            SELECT outputs.item_id AS output_item_id,
                   outputs.output_format, outputs.text_output
            FROM function_calls AS calls
            JOIN conversation_items AS call_items
              ON call_items.id = calls.item_id
            JOIN function_outputs AS outputs
              ON outputs.function_call_item_id = calls.item_id
            WHERE call_items.session_id = ?
              AND calls.tool_name = 'memory_user_fact_read'
            """,
            (session_id,),
        ).fetchall()
        memory_preflight_outputs = [
            output_json(database, row) for row in memory_rows
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
    completed_user_fact_write_values: set[str] = set()
    for row in tools:
        if (
            row["tool_name"] != "memory_user_fact_remember"
            or row["status"] != "completed"
        ):
            continue
        fact = safe_json(row["arguments_json"]).get("fact")
        if isinstance(fact, str) and fact:
            completed_user_fact_write_values.add(fact.casefold())

    calls = int(calls_row["calls"] or 0)
    errors = int(calls_row["errors"] or 0)
    audit_hits = int(calls_row["audit_hits"] or 0)
    cost = float(calls_row["cost"] or 0.0)
    wall = float(calls_row["wall"] or 0.0)
    artists = expected_artists(ranking_db)
    top_three = artists[:REQUIRED_GROUP_COUNT]
    favorite_memory_facts = [
        fact
        for fact in stored_user_facts
        if str(fact.get("subject", "")).casefold() == "user"
        and str(fact.get("value", "")).casefold()
        in completed_user_fact_write_values
        and all(
            artist_position(str(fact.get("value", "")).lower(), artist) >= 0
            for artist, _ in top_three
        )
    ]
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
            preflight_fact_matches(fact, expected_user_fact)
            for fact in safe_json_array(output)
        )
        for output in memory_preflight_outputs
    )
    expected_user_name = (
        str(
            expected_user_fact.get(
                "expected_name", expected_user_fact.get("value", "")
            )
        )
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
        "completed_user_fact_writes": len(completed_user_fact_write_values),
        "favorite_memory_facts": len(favorite_memory_facts),
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
        "Stored favorite bands in durable memory",
        bool(favorite_memory_facts),
        7,
        (
            f"{len(favorite_memory_facts)} matching active fact"
            f"{'s' if len(favorite_memory_facts) != 1 else ''}"
        ),
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
