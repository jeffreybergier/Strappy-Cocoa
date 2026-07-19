#!/usr/bin/env python3
"""Offline unit tests for hill-climbing score version 3."""

from __future__ import annotations

import io
import re
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from evaluate import (
    LINK_BONUS_LIMIT,
    extract_unique_markdown_links,
    frozen_top_five,
    price_points_for_cost,
    score_answer,
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


class PriceScoreTests(unittest.TestCase):
    def test_price_scores_one_point_per_rounded_cent_from_goal(self) -> None:
        expected = {
            0.00: (5, 0),
            0.03: (2, 3),
            0.05: (0, 5),
            0.06: (-1, 6),
            0.054: (0, 5),
            0.055: (-1, 6),
        }
        for cost, result in expected.items():
            self.assertEqual(price_points_for_cost(cost), result, cost)

    def test_failed_audit_withholds_savings_but_keeps_overspend(self) -> None:
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
        self.assertEqual((cheap.price_points, cheap.awarded_price_points), (2, 0))
        self.assertEqual(cheap.score, -5)
        self.assertEqual((costly.price_points, costly.awarded_price_points), (-1, -1))
        self.assertEqual(costly.score, -6)


class AuditFirstScoreTests(unittest.TestCase):
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
        self.assertEqual(result.potential_bonus, 8)
        self.assertEqual(result.awarded_bonus, 8)
        self.assertEqual(result.score, 8)

    def test_any_failed_audit_withholds_all_bonuses(self) -> None:
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
        self.assertEqual(result.potential_bonus, 8)
        self.assertEqual(result.awarded_bonus, 0)
        self.assertEqual(result.score, -5)

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
        self.assertEqual(result.potential_bonus, LINK_BONUS_LIMIT)
        self.assertEqual(result.score, LINK_BONUS_LIMIT)


if __name__ == "__main__":
    unittest.main()
