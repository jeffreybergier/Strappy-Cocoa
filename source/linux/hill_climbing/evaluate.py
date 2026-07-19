#!/usr/bin/env python3
"""Audit-first deterministic scorer for private hill-climbing runs."""

from __future__ import annotations

import argparse
import json
import re
import sqlite3
from dataclasses import dataclass, field
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path
from typing import Any
from urllib.parse import urlsplit, urlunsplit

from hill_climbing_ground_truth import (
    REQUIRED_GROUP_COUNT,
    matched_roster_members,
    roster_member_count,
    standalone_alias_pattern,
    validate_member_roster,
)


SCORE_VERSION = 3
AUDIT_FAILURE_POINTS = 5
NAME_BONUS_POINTS = 5
LINK_BONUS_LIMIT = 10
PRICE_GOAL_CENTS = 5

MARKDOWN_LINK = re.compile(
    r"(?<!!)\[([^\]\r\n]+)\]\((https?://[^\s)\r\n]+)\)",
    flags=re.IGNORECASE,
)


@dataclass
class ScoreBreakdown:
    score: int
    audit_penalty: int
    failed_audit_count: int
    awarded_bonus: int
    potential_bonus: int
    price_points: int
    awarded_price_points: int
    rounded_cost_cents: int
    name_mentioned: bool
    matched_members: list[dict[str, str]] = field(default_factory=list)
    links: list[dict[str, str]] = field(default_factory=list)
    audit_checks: list[dict[str, Any]] = field(default_factory=list)


@dataclass
class Result:
    model: str
    slug: str
    score: int
    audit_penalty: int
    failed_audit_count: int
    awarded_bonus: int
    potential_bonus: int
    price_points: int
    awarded_price_points: int
    audit_checks: list[dict[str, Any]] = field(default_factory=list)
    bonus_checks: list[dict[str, Any]] = field(default_factory=list)
    metrics: dict[str, Any] = field(default_factory=dict)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--run-dir", required=True, type=Path)
    return parser.parse_args()


def frozen_top_five(metadata: dict[str, Any]) -> list[tuple[str, int]]:
    values = metadata.get("expected_top_five")
    if not isinstance(values, list) or len(values) != REQUIRED_GROUP_COUNT:
        raise RuntimeError(
            f"Run metadata must freeze exactly {REQUIRED_GROUP_COUNT} top groups."
        )
    result: list[tuple[str, int]] = []
    for index, value in enumerate(values, 1):
        if not isinstance(value, dict):
            raise RuntimeError(f"Frozen top-five entry {index} is invalid.")
        name = value.get("name")
        plays = value.get("total_plays")
        if not isinstance(name, str) or not name.strip():
            raise RuntimeError(f"Frozen top-five entry {index} has no name.")
        if isinstance(plays, bool) or not isinstance(plays, int) or plays < 0:
            raise RuntimeError(
                f"Frozen top-five entry {index} has an invalid play count."
            )
        result.append((name, plays))
    return result


def connect_readonly(path: Path) -> sqlite3.Connection:
    connection = sqlite3.connect(f"file:{path.resolve()}?mode=ro", uri=True)
    connection.row_factory = sqlite3.Row
    return connection


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


def _normalized_link(url: str) -> str | None:
    try:
        parsed = urlsplit(url)
    except ValueError:
        return None
    if parsed.scheme.casefold() not in ("http", "https") or not parsed.hostname:
        return None
    return urlunsplit(
        (
            parsed.scheme.casefold(),
            parsed.netloc.casefold(),
            parsed.path,
            parsed.query,
            "",
        )
    )


def extract_unique_markdown_links(answer: str) -> list[dict[str, str]]:
    links: list[dict[str, str]] = []
    seen: set[str] = set()
    for match in MARKDOWN_LINK.finditer(answer):
        title = match.group(1).strip()
        url = match.group(2)
        normalized = _normalized_link(url)
        if not title or normalized is None or normalized in seen:
            continue
        seen.add(normalized)
        links.append({"title": title, "url": url})
    return links


def audit_check_failed(check: dict[str, Any]) -> bool:
    return str(check.get("status", "error")) not in (
        "passed",
        "not_applicable",
    )


def price_points_for_cost(cost_usd: float) -> tuple[int, int]:
    cost = Decimal(str(cost_usd))
    if not cost.is_finite() or cost < 0:
        raise ValueError("Run cost must be a finite non-negative amount.")
    rounded_cost_cents = int(
        (cost * Decimal(100)).quantize(Decimal("1"), rounding=ROUND_HALF_UP)
    )
    return PRICE_GOAL_CENTS - rounded_cost_cents, rounded_cost_cents


def score_answer(
    answer: str,
    audit_checks: list[dict[str, Any]],
    roster: dict[str, Any],
    expected_user_name: str,
    cost_usd: float,
) -> ScoreBreakdown:
    failed_audits = [check for check in audit_checks if audit_check_failed(check)]
    audit_penalty = -AUDIT_FAILURE_POINTS * len(failed_audits)

    name_pattern = (
        standalone_alias_pattern(expected_user_name, case_sensitive=False)
        if expected_user_name
        else None
    )
    name_mentioned = bool(name_pattern and re.search(name_pattern, answer))
    matched_members = matched_roster_members(answer, roster)
    links = extract_unique_markdown_links(answer)
    potential_bonus = (
        (NAME_BONUS_POINTS if name_mentioned else 0)
        + len(matched_members)
        + min(len(links), LINK_BONUS_LIMIT)
    )
    awarded_bonus = potential_bonus if not failed_audits else 0
    price_points, rounded_cost_cents = price_points_for_cost(cost_usd)
    awarded_price_points = (
        price_points if not failed_audits else min(price_points, 0)
    )
    score = audit_penalty + awarded_bonus + awarded_price_points
    return ScoreBreakdown(
        score=score,
        audit_penalty=audit_penalty,
        failed_audit_count=len(failed_audits),
        awarded_bonus=awarded_bonus,
        potential_bonus=potential_bonus,
        price_points=price_points,
        awarded_price_points=awarded_price_points,
        rounded_cost_cents=rounded_cost_cents,
        name_mentioned=name_mentioned,
        matched_members=matched_members,
        links=links,
        audit_checks=audit_checks,
    )


def _missing_audit_check(detail: str) -> dict[str, Any]:
    return {
        "check_key": "answer_quality_audit_missing",
        "check_kind": "answer_content",
        "label": "Answer quality audit available",
        "status": "error",
        "tool_name": None,
        "detail": detail,
    }


def _answer_audit(
    database: sqlite3.Connection,
    answer_attempt_id: int | None,
) -> tuple[str, str | None, list[dict[str, Any]]]:
    if answer_attempt_id is None:
        return (
            "error",
            None,
            [_missing_audit_check("The final answer has no source attempt.")],
        )
    audit = database.execute(
        """
        SELECT id, outcome, guidance_version
        FROM answer_quality_audits WHERE response_attempt_id = ?
        """,
        (answer_attempt_id,),
    ).fetchone()
    if audit is None:
        return (
            "error",
            None,
            [_missing_audit_check("The final answer has no quality audit.")],
        )
    rows = database.execute(
        """
        SELECT check_key, check_kind, label, status, tool_name, detail
        FROM answer_quality_checks WHERE audit_id = ? ORDER BY ordinal
        """,
        (audit["id"],),
    ).fetchall()
    checks = [dict(row) for row in rows]
    if not checks:
        checks = [_missing_audit_check("The answer quality audit has no checks.")]
    if str(audit["outcome"]) != "passed" and not any(
        audit_check_failed(check) for check in checks
    ):
        checks.append(
            _missing_audit_check(
                "The answer quality audit outcome is not passed but has no "
                "failed check."
            )
        )
    return str(audit["outcome"]), audit["guidance_version"], checks


def _runtime_metrics(
    database: sqlite3.Connection,
    session_id: int,
) -> dict[str, Any]:
    calls = database.execute(
        """
        SELECT COUNT(*) AS calls,
               COALESCE(SUM(CASE
                 WHEN a.state != 'completed' OR a.http_status >= 400
                   OR ar.error_type IS NOT NULL
                   OR ar.error_message IS NOT NULL
                   OR ar.parse_error IS NOT NULL
                 THEN 1 ELSE 0 END), 0) AS errors,
               COALESCE(SUM(u.cost_nano_usd), 0) / 1000000000.0 AS cost,
               COALESCE((MAX(COALESCE(a.completed_at_ms, a.started_at_ms)) -
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
    tools = database.execute(
        """
        SELECT COUNT(*) AS count,
               COALESCE(SUM(CASE WHEN e.state = 'error' THEN 1 ELSE 0 END), 0)
                 AS errors
        FROM tool_executions e
        JOIN function_calls f ON f.item_id = e.function_call_item_id
        JOIN conversation_items i ON i.id = f.item_id
        WHERE i.session_id = ?
        """,
        (session_id,),
    ).fetchone()
    web = int(
        database.execute(
            """
            SELECT COUNT(*) FROM conversation_items
            WHERE session_id = ?
              AND kind IN ('openrouter:web_search', 'openrouter:web_fetch')
              AND source_attempt_id IS NOT NULL
            """,
            (session_id,),
        ).fetchone()[0]
    )
    return {
        "calls": int(calls["calls"] or 0),
        "api_errors": int(calls["errors"] or 0),
        "local_tools": int(tools["count"] or 0),
        "tool_errors": int(tools["errors"] or 0),
        "web_activities": web,
        "cost": float(calls["cost"] or 0.0),
        "wall_seconds": float(calls["wall"] or 0.0),
    }


def score_session(
    session_db: Path,
    slug: str,
    roster: dict[str, Any],
    top_five: list[tuple[str, int]],
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
        audited_attempt = database.execute(
            """
            SELECT q.response_attempt_id
            FROM answer_quality_audits q
            JOIN http_attempts a ON a.id = q.response_attempt_id
            JOIN model_requests r ON r.id = a.request_id
            JOIN turns t ON t.id = r.turn_id
            WHERE t.session_id = ?
            ORDER BY q.id DESC LIMIT 1
            """,
            (session_id,),
        ).fetchone()
        answer_attempt_id = (
            int(audited_attempt["response_attempt_id"])
            if audited_attempt is not None
            else None
        )
        if answer_attempt_id is not None:
            answer_item = database.execute(
                """
                SELECT i.id, i.source_attempt_id
                FROM conversation_items i
                JOIN message_items m ON m.item_id = i.id
                WHERE i.session_id = ? AND m.role = 'assistant'
                  AND i.source_attempt_id = ?
                ORDER BY i.sequence DESC LIMIT 1
                """,
                (session_id, answer_attempt_id),
            ).fetchone()
        else:
            answer_item = database.execute(
                """
                SELECT i.id, i.source_attempt_id
                FROM conversation_items i
                JOIN message_items m ON m.item_id = i.id
                WHERE i.session_id = ? AND m.role = 'assistant'
                ORDER BY i.sequence DESC LIMIT 1
                """,
                (session_id,),
            ).fetchone()
        answer = item_text(database, int(answer_item["id"])) if answer_item else ""
        if answer_attempt_id is None and answer_item is not None:
            source_attempt_id = answer_item["source_attempt_id"]
            if source_attempt_id is not None:
                answer_attempt_id = int(source_attempt_id)
        audit_outcome, audit_guidance_version, audit_checks = _answer_audit(
            database, answer_attempt_id
        )
        runtime_metrics = _runtime_metrics(database, session_id)

    expected_user_name = ""
    if expected_user_fact:
        expected_user_name = str(
            expected_user_fact.get(
                "expected_name", expected_user_fact.get("value", "")
            )
        )
    breakdown = score_answer(
        answer,
        audit_checks,
        roster,
        expected_user_name,
        runtime_metrics["cost"],
    )
    member_names = [match["name"] for match in breakdown.matched_members]
    member_possible = roster_member_count(roster)
    link_points = min(len(breakdown.links), LINK_BONUS_LIMIT)

    audit_records = []
    for check in breakdown.audit_checks:
        failed = audit_check_failed(check)
        audit_records.append(
            {
                "name": str(check.get("label") or check.get("check_key") or "Audit"),
                "check_key": str(check.get("check_key", "")),
                "status": str(check.get("status", "error")),
                "passed": not failed,
                "earned": -AUDIT_FAILURE_POINTS if failed else 0,
                "detail": str(check.get("detail") or ""),
                "tool_name": check.get("tool_name"),
            }
        )
    bonus_eligible = breakdown.failed_audit_count == 0
    if breakdown.price_points > 0:
        price_state = "AWARDED" if bonus_eligible else "WITHHELD"
    elif breakdown.price_points < 0:
        price_state = "APPLIED"
    else:
        price_state = "NEUTRAL"
    bonus_records = [
        {
            "name": "Mentioned remembered user name",
            "earned": NAME_BONUS_POINTS if breakdown.name_mentioned else 0,
            "possible": NAME_BONUS_POINTS,
            "awarded": bonus_eligible and breakdown.name_mentioned,
            "state": (
                "AWARDED"
                if bonus_eligible and breakdown.name_mentioned
                else ("WITHHELD" if breakdown.name_mentioned else "MISS")
            ),
            "note": expected_user_name,
        },
        {
            "name": "Mentioned expected active members",
            "earned": len(member_names),
            "possible": member_possible,
            "awarded": bonus_eligible and bool(member_names),
            "state": (
                "AWARDED"
                if bonus_eligible and member_names
                else ("WITHHELD" if member_names else "MISS")
            ),
            "note": f"{len(member_names)}/{member_possible}",
        },
        {
            "name": "Included unique Markdown links",
            "earned": link_points,
            "possible": LINK_BONUS_LIMIT,
            "awarded": bonus_eligible and link_points > 0,
            "state": (
                "AWARDED"
                if bonus_eligible and link_points > 0
                else ("WITHHELD" if link_points > 0 else "MISS")
            ),
            "note": (
                f"{len(breakdown.links)} unique; first {LINK_BONUS_LIMIT} score"
            ),
        },
        {
            "name": "Price against $0.05 goal",
            "earned": breakdown.price_points,
            "possible": PRICE_GOAL_CENTS,
            "awarded": breakdown.awarded_price_points == breakdown.price_points,
            "state": price_state,
            "note": (
                f"${runtime_metrics['cost']:.4f} rounds to "
                f"{breakdown.rounded_cost_cents} cents"
            ),
        },
    ]
    metrics = {
        **runtime_metrics,
        "answer_characters": len(answer),
        "audit_outcome": audit_outcome,
        "audit_guidance_version": audit_guidance_version,
        "expected_top_five": [
            {"name": name, "total_plays": plays} for name, plays in top_five
        ],
        "expected_member_count": member_possible,
        "matched_member_count": len(member_names),
        "matched_members": member_names,
        "unique_markdown_link_count": len(breakdown.links),
        "unique_markdown_links": breakdown.links,
        "name_mentioned": breakdown.name_mentioned,
        "bonuses_eligible": bonus_eligible,
        "price_goal_usd": PRICE_GOAL_CENTS / 100.0,
        "rounded_cost_cents": breakdown.rounded_cost_cents,
        "price_points": breakdown.price_points,
        "awarded_price_points": breakdown.awarded_price_points,
    }
    return Result(
        model=model,
        slug=slug,
        score=breakdown.score,
        audit_penalty=breakdown.audit_penalty,
        failed_audit_count=breakdown.failed_audit_count,
        awarded_bonus=breakdown.awarded_bonus,
        potential_bonus=breakdown.potential_bonus,
        price_points=breakdown.price_points,
        awarded_price_points=breakdown.awarded_price_points,
        audit_checks=audit_records,
        bonus_checks=bonus_records,
        metrics=metrics,
    )


def render_markdown(results: list[Result]) -> str:
    lines = [
        "# Hill-climbing evaluation",
        "",
        "Score version 3 is audit-first: every failed applicable audit check "
        "scores -5. A fully compliant answer starts at 0 and earns bonuses for "
        "the remembered user name, expected active members of the database-derived "
        "top five K-pop bands, unique Markdown links, and cost below the $0.05 "
        "goal. Each rounded cent below or above the goal is worth +1 or -1. "
        "Any audit failure withholds positive bonuses; negative cost points remain.",
        "",
        "| Model | Score | Audit | Answer bonus | Price | Members | Links | Time | Cost | Calls |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for result in sorted(results, key=lambda item: item.score, reverse=True):
        metrics = result.metrics
        bonus_text = (
            str(result.awarded_bonus)
            if result.failed_audit_count == 0
            else f"0 (+{result.potential_bonus} withheld)"
        )
        lines.append(
            f"| {result.model} | {result.score:+d} | "
            f"{result.audit_penalty:+d} | {bonus_text} | "
            f"{result.awarded_price_points:+d} | "
            f"{metrics['matched_member_count']}/{metrics['expected_member_count']} | "
            f"{metrics['unique_markdown_link_count']} | "
            f"{metrics['wall_seconds']:.1f}s | ${metrics['cost']:.4f} | "
            f"{metrics['calls']} |"
        )

    for result in results:
        lines.extend(
            [
                "",
                f"## {result.model}",
                "",
                f"- Final score: {result.score:+d}",
                f"- Audit penalty: {result.audit_penalty:+d} "
                f"({result.failed_audit_count} failed)",
                f"- Bonus: +{result.awarded_bonus} awarded; "
                f"+{result.potential_bonus} recognized",
                f"- Price: {result.awarded_price_points:+d} awarded; "
                f"{result.price_points:+d} calculated",
                "",
                "### Audit checks",
                "",
            ]
        )
        for check in result.audit_checks:
            marker = "PASS" if check["passed"] else "FAIL"
            points = "0" if check["passed"] else f"{check['earned']:+d}"
            detail = f" — {check['detail']}" if check["detail"] else ""
            lines.append(f"- {marker} {check['name']}: {points}{detail}")
        lines.extend(["", "### Bonuses", ""])
        for check in result.bonus_checks:
            note = f" — {check['note']}" if check["note"] else ""
            lines.append(
                f"- {check['state']} {check['name']}: "
                f"{check['earned']}/{check['possible']}{note}"
            )
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    run_dir = args.run_dir.resolve()
    metadata_path = run_dir / "run.json"
    if not metadata_path.is_file():
        raise SystemExit(f"Missing run metadata: {metadata_path}")
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    try:
        top_five = frozen_top_five(metadata)
    except RuntimeError as error:
        raise SystemExit(str(error)) from error
    roster_value = metadata.get("member_roster")
    try:
        roster = validate_member_roster(roster_value, top_five)
    except RuntimeError as error:
        raise SystemExit(str(error)) from error
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
            slug,
            roster,
            top_five,
            expected_user_fact,
        )
        if result.model != model:
            result.metrics["requested_model"] = model
        results.append(result)

    if not results:
        raise SystemExit("No completed session databases were available to score.")

    report_json = {
        "score_version": SCORE_VERSION,
        "run_dir": str(run_dir),
        "roster_retrieved_at": roster["retrieved_at"],
        "results": [
            {
                "model": result.model,
                "slug": result.slug,
                "score": result.score,
                "audit_penalty": result.audit_penalty,
                "failed_audit_count": result.failed_audit_count,
                "awarded_bonus": result.awarded_bonus,
                "potential_bonus": result.potential_bonus,
                "price_points": result.price_points,
                "awarded_price_points": result.awarded_price_points,
                "metrics": result.metrics,
                "audit_checks": result.audit_checks,
                "bonus_checks": result.bonus_checks,
            }
            for result in results
        ],
    }
    (run_dir / "report.json").write_text(
        json.dumps(report_json, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    rendered = render_markdown(results)
    (run_dir / "report.md").write_text(rendered, encoding="utf-8")
    print(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
