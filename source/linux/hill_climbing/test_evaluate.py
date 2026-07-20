#!/usr/bin/env python3
"""Offline unit tests for hill-climbing score version 4."""

from __future__ import annotations

import io
import re
import sqlite3
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from evaluate import (
    LINK_BONUS_LIMIT,
    Result,
    extract_unique_markdown_links,
    frozen_top_five,
    price_points_for_cost,
    render_markdown,
    score_answer,
    score_session,
)
from hill_climbing_ground_truth import (
    REQUIRED_GROUP_COUNT,
    matched_roster_members,
    standalone_alias_pattern,
    validate_member_roster,
)
from run_hill_climbing import MODELS, print_pending, run_model


def sample_roster() -> dict[str, object]:
    names = ("K", "Alpha", "Beta", "Gamma", "Delta")
    groups = []
    for index, name in enumerate(names, 1):
        groups.append(
            {
                "artist": f"Artist {index}",
                "source": {
                    "title": f"Artist {index} roster",
                    "url": f"https://example.com/artist-{index}",
                },
                "members": [
                    {
                        "name": name,
                        "patterns": [
                            standalone_alias_pattern(
                                name,
                                case_sensitive=(name == "K"),
                            )
                        ],
                    }
                ],
            }
        )
    roster = {
        "format_version": 1,
        "required_group_count": REQUIRED_GROUP_COUNT,
        "retrieved_at": "2026-07-19T00:00:00Z",
        "groups": groups,
    }
    expected = [(f"Artist {index}", index) for index in range(1, 6)]
    return validate_member_roster(roster, expected)


class MemberRegexTests(unittest.TestCase):
    def test_ambiguous_uppercase_k_requires_a_standalone_token(self) -> None:
        pattern = standalone_alias_pattern("K", case_sensitive=True)
        for text in ("K", "(K)", "**K**", "member K."):
            self.assertIsNotNone(re.search(pattern, text), text)
        for text in (
            "k",
            "KPOP",
            "K-pop",
            "SK",
            "pre-K",
            "OKAY",
            "éK",
            "Ké",
        ):
            self.assertIsNone(re.search(pattern, text), text)

    def test_long_member_alias_is_case_insensitive_and_bounded(self) -> None:
        pattern = standalone_alias_pattern("Sangyeon", case_sensitive=False)
        self.assertIsNotNone(re.search(pattern, "**SANGYEON**"))
        self.assertIsNotNone(re.search(pattern, "sangyeon,"))
        self.assertIsNone(re.search(pattern, "NotSangyeon"))

    def test_each_expected_member_scores_only_once(self) -> None:
        roster = sample_roster()
        matches = matched_roster_members("K, K, Alpha and ALPHA", roster)
        self.assertEqual([match["name"] for match in matches], ["K", "Alpha"])

    def test_kpop_does_not_award_member_k(self) -> None:
        roster = sample_roster()
        self.assertEqual(matched_roster_members("Five K-pop bands", roster), [])


class LinkTests(unittest.TestCase):
    def test_only_unique_non_image_markdown_http_links_count(self) -> None:
        answer = (
            "[One](https://example.com/a) "
            "[Duplicate](https://EXAMPLE.com/a#section) "
            "![Image](https://example.com/image.png) "
            "https://example.com/plain "
            "[FTP](ftp://example.com/file) "
            "[Two](http://example.com/b)"
        )
        links = extract_unique_markdown_links(answer)
        self.assertEqual(
            [link["url"] for link in links],
            ["https://example.com/a", "http://example.com/b"],
        )


class FrozenTopFiveTests(unittest.TestCase):
    def test_run_metadata_must_freeze_exactly_five_groups(self) -> None:
        values = [
            {"name": f"Artist {index}", "total_plays": index}
            for index in range(1, REQUIRED_GROUP_COUNT + 1)
        ]
        self.assertEqual(
            frozen_top_five({"expected_top_five": values}),
            [(f"Artist {index}", index) for index in range(1, 6)],
        )
        with self.assertRaises(RuntimeError):
            frozen_top_five({"expected_top_five": values[:3]})


class ModelListTests(unittest.TestCase):
    def test_all_seven_models_are_enabled(self) -> None:
        model_ids = [model_id for model_id, _ in MODELS]
        slugs = [slug for _, slug in MODELS]
        self.assertEqual(len(model_ids), 7)
        self.assertEqual(len(set(model_ids)), 7)
        self.assertEqual(len(set(slugs)), 7)
        self.assertTrue(
            {
                "google/gemini-3.1-flash-lite",
                "openai/gpt-oss-120b",
                "openai/gpt-5.6-luna-pro",
            }.issubset(model_ids)
        )


class ParallelRunnerTests(unittest.TestCase):
    def test_noninteractive_pending_output_is_compact(self) -> None:
        output = io.StringIO()
        with mock.patch("run_hill_climbing.sys.stdout", output):
            print_pending(7, 7)
            print_pending(6, 7)
        self.assertEqual(
            output.getvalue(),
            "Hill climbs pending: 7/7\nHill climbs pending: 6/7\n",
        )

    def test_failed_runner_output_is_captured_per_model(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            run_dir = Path(temporary_directory)
            runner = run_dir / "fake-runner"
            runner.write_text(
                "#!/bin/sh\n"
                "printf 'runner stdout\\n'\n"
                "printf 'runner stderr\\n' >&2\n"
                "exit 9\n",
                encoding="utf-8",
            )
            runner.chmod(0o755)
            result = run_model(
                runner,
                "openai/gpt-5.6-luna-pro",
                "luna-pro",
                run_dir,
                [],
                Path("/dev/null"),
                Path("/dev/null"),
                "prompt",
            )

            self.assertEqual(result["exit_code"], 9)
            self.assertEqual(
                result["database_registration_error"],
                "Session database was not created.",
            )
            self.assertEqual(result["runner_log"], "luna-pro/runner.log")
            self.assertEqual(
                (run_dir / "luna-pro" / "runner.log").read_text(
                    encoding="utf-8"
                ),
                "runner stdout\nrunner stderr\n",
            )


class EmptyAnswerPersistenceTests(unittest.TestCase):
    def test_final_audit_attempt_outranks_earlier_assistant_text(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            database_path = Path(temporary_directory) / "session.sqlite"
            with sqlite3.connect(database_path) as database:
                database.executescript(
                    """
                    CREATE TABLE sessions (id INTEGER, model_id TEXT);
                    CREATE TABLE turns (id INTEGER, session_id INTEGER);
                    CREATE TABLE model_requests (id INTEGER, turn_id INTEGER);
                    CREATE TABLE http_attempts (
                      id INTEGER, request_id INTEGER, state TEXT,
                      http_status INTEGER, started_at_ms INTEGER,
                      completed_at_ms INTEGER
                    );
                    CREATE TABLE api_results (
                      attempt_id INTEGER, error_type TEXT,
                      error_message TEXT, parse_error TEXT
                    );
                    CREATE TABLE api_usage (
                      attempt_id INTEGER, cost_nano_usd INTEGER
                    );
                    CREATE TABLE answer_quality_audits (
                      id INTEGER, response_attempt_id INTEGER,
                      outcome TEXT, guidance_version TEXT
                    );
                    CREATE TABLE answer_quality_checks (
                      audit_id INTEGER, ordinal INTEGER, check_key TEXT,
                      check_kind TEXT, label TEXT, status TEXT,
                      tool_name TEXT, detail TEXT
                    );
                    CREATE TABLE conversation_items (
                      id INTEGER, session_id INTEGER, source_attempt_id INTEGER,
                      sequence INTEGER, kind TEXT
                    );
                    CREATE TABLE message_items (item_id INTEGER, role TEXT);
                    CREATE TABLE item_text_parts (
                      item_id INTEGER, collection_name TEXT,
                      ordinal INTEGER, text TEXT
                    );
                    CREATE TABLE function_calls (item_id INTEGER, tool_name TEXT);
                    CREATE TABLE tool_executions (
                      function_call_item_id INTEGER, state TEXT
                    );

                    INSERT INTO sessions VALUES (1, 'test/model');
                    INSERT INTO turns VALUES (1, 1);
                    INSERT INTO model_requests VALUES (1, 1), (2, 1);
                    INSERT INTO http_attempts VALUES
                      (1, 1, 'completed', 200, 1000, 1100),
                      (2, 2, 'completed', 200, 1200, 1300);
                    INSERT INTO api_usage VALUES (1, 0), (2, 0);
                    INSERT INTO conversation_items VALUES
                      (1, 1, 1, 1, 'message');
                    INSERT INTO message_items VALUES (1, 'assistant');
                    INSERT INTO item_text_parts VALUES
                      (1, 'content', 0, 'Jeff and every expected member.');
                    INSERT INTO answer_quality_audits VALUES
                      (1, 2, 'failed', '3');
                    INSERT INTO answer_quality_checks VALUES
                      (1, 0, 'answer_non_empty', 'answer_content',
                       'Answer provided', 'failed', NULL,
                       'The response did not include a non-empty assistant answer.');
                    """
                )

            result = score_session(
                database_path,
                "test-model",
                sample_roster(),
                [(f"Artist {index}", index) for index in range(1, 6)],
                {"expected_name": "Jeff"},
            )

            self.assertEqual(result.metrics["answer_characters"], 0)
            self.assertEqual(result.failed_audit_count, 1)
            self.assertEqual(result.bonus_points, 0)
            self.assertEqual(result.price_points, 25)
            self.assertEqual(result.score, 20)


class PriceScoreTests(unittest.TestCase):
    def test_price_scores_five_points_per_rounded_cent_from_goal(self) -> None:
        expected = {
            0.00: (25, 0),
            0.03: (10, 3),
            0.05: (0, 5),
            0.06: (-5, 6),
            0.054: (0, 5),
            0.055: (-5, 6),
        }
        for cost, result in expected.items():
            self.assertEqual(price_points_for_cost(cost), result, cost)

    def test_price_and_bonus_points_apply_despite_failed_audit(self) -> None:
        roster = sample_roster()
        cheap = score_answer(
            "Jeff and K.",
            [{"check_key": "required", "status": "failed"}],
            roster,
            "Jeff",
            0.03,
        )
        costly = score_answer(
            "Jeff and K.",
            [{"check_key": "required", "status": "failed"}],
            roster,
            "Jeff",
            0.06,
        )
        self.assertEqual((cheap.bonus_points, cheap.price_points), (2, 10))
        self.assertEqual(cheap.score, 7)
        self.assertEqual((costly.bonus_points, costly.price_points), (2, -5))
        self.assertEqual(costly.score, -8)


class ReportRenderingTests(unittest.TestCase):
    def test_report_uses_only_pass_fail_and_hit_miss_states(self) -> None:
        result = Result(
            model="test/model",
            slug="test-model",
            score=-4,
            audit_penalty=-5,
            failed_audit_count=1,
            bonus_points=1,
            price_points=0,
            audit_checks=[
                {
                    "name": "Passing audit",
                    "passed": True,
                    "earned": 0,
                    "detail": "",
                },
                {
                    "name": "Failing audit",
                    "passed": False,
                    "earned": -5,
                    "detail": "",
                },
            ],
            bonus_checks=[
                {
                    "name": "Bonus hit",
                    "state": "HIT",
                    "earned": 1,
                    "possible": 1,
                    "note": "",
                },
                {
                    "name": "Bonus miss",
                    "state": "MISS",
                    "earned": 0,
                    "possible": 1,
                    "note": "",
                },
            ],
            metrics={
                "matched_member_count": 0,
                "expected_member_count": 0,
                "unique_markdown_link_count": 0,
                "wall_seconds": 0.0,
                "cost": 0.05,
                "calls": 1,
                "price_goal_usd": 0.05,
                "rounded_cost_cents": 5,
            },
        )

        report = render_markdown([result])

        self.assertIn("- PASS Passing audit: +0", report)
        self.assertIn("- FAIL Failing audit: -5", report)
        self.assertIn("- HIT Bonus hit: +1", report)
        self.assertIn("- MISS Bonus miss: +0", report)
        self.assertNotIn("WITHHELD", report)


class ScoreTests(unittest.TestCase):
    def test_empty_answer_accumulates_normal_audit_failures(self) -> None:
        roster = sample_roster()
        result = score_answer(
            "",
            [
                {
                    "check_key": "answer_non_empty",
                    "status": "failed",
                },
                {
                    "check_key": "database_context_read",
                    "status": "failed",
                },
                {
                    "check_key": "web_reference",
                    "status": "not_applicable",
                },
            ],
            roster,
            "Jeff",
            0.05,
        )
        self.assertEqual(result.failed_audit_count, 2)
        self.assertEqual(result.audit_penalty, -10)
        self.assertEqual(result.price_points, 0)
        self.assertEqual(result.score, -10)

    def test_compliant_answer_receives_all_recognized_bonuses(self) -> None:
        roster = sample_roster()
        audit_checks = [
            {"check_key": "required", "status": "passed"},
            {"check_key": "web", "status": "not_applicable"},
        ]
        result = score_answer(
            "Jeff met **K** and Alpha. [Source](https://example.com/source)",
            audit_checks,
            roster,
            "Jeff",
            0.05,
        )
        self.assertEqual(result.failed_audit_count, 0)
        self.assertEqual(result.audit_penalty, 0)
        self.assertEqual(result.bonus_points, 4)
        self.assertEqual(result.score, 4)

    def test_failed_audit_does_not_withhold_bonus_hits(self) -> None:
        roster = sample_roster()
        result = score_answer(
            "Jeff, K, Alpha. [Source](https://example.com/source)",
            [{"check_key": "required", "status": "failed"}],
            roster,
            "Jeff",
            0.05,
        )
        self.assertEqual(result.failed_audit_count, 1)
        self.assertEqual(result.audit_penalty, -5)
        self.assertEqual(result.bonus_points, 4)
        self.assertEqual(result.score, -1)

    def test_each_failed_or_error_audit_costs_five_points(self) -> None:
        roster = sample_roster()
        result = score_answer(
            "No bonuses.",
            [
                {"check_key": "one", "status": "failed"},
                {"check_key": "two", "status": "error"},
                {"check_key": "three", "status": "not_applicable"},
            ],
            roster,
            "Jeff",
            0.05,
        )
        self.assertEqual(result.failed_audit_count, 2)
        self.assertEqual(result.score, -10)

    def test_link_bonus_is_capped(self) -> None:
        roster = sample_roster()
        links = " ".join(
            f"[Source {index}](https://example.com/{index})"
            for index in range(LINK_BONUS_LIMIT + 3)
        )
        result = score_answer(
            links,
            [{"check_key": "required", "status": "passed"}],
            roster,
            "Jeff",
            0.05,
        )
        self.assertEqual(len(result.links), LINK_BONUS_LIMIT + 3)
        self.assertEqual(result.bonus_points, LINK_BONUS_LIMIT)
        self.assertEqual(result.score, LINK_BONUS_LIMIT)


if __name__ == "__main__":
    unittest.main()
