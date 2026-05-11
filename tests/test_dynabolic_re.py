"""Unit tests for the LLM-symbolic hybrid orchestrator.

Uses MockProvider so no LLM is required. The C++ solver binary IS required
and must be built first (`make` from the repo root).

Run: python3 -m unittest tests/test_dynabolic_re.py
"""

from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path

# Make the repo root importable.
_REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_REPO_ROOT))

from dynabolic_re.extractor import ExtractionError, extract
from dynabolic_re.pipeline import Pipeline
from dynabolic_re.provider import (
    AnthropicProvider,
    MockProvider,
    OpenAIProvider,
    ProviderError,
)
from dynabolic_re.solver import SolverError, solve
from dynabolic_re.verbalizer import verbalize


_SOLVER = _REPO_ROOT / "build" / "dynabolic_solver"


def _require_solver() -> None:
    if not _SOLVER.exists():
        raise unittest.SkipTest(
            f"solver not built (expected at {_SOLVER}); run `make` first"
        )


def _is_extract_prompt(system: str) -> bool:
    return "knowledge extractor" in system


def _is_verbalize_prompt(system: str) -> bool:
    return "explanation writer" in system


class TestProvider(unittest.TestCase):
    def test_openai_requires_key(self) -> None:
        # Make sure the constructor refuses to silently ship empty creds.
        original = {k: __import__("os").environ.pop(k, None) for k in ("OPENAI_API_KEY",)}
        try:
            with self.assertRaises(ProviderError):
                OpenAIProvider()
        finally:
            for k, v in original.items():
                if v is not None:
                    __import__("os").environ[k] = v

    def test_anthropic_requires_key(self) -> None:
        original = {k: __import__("os").environ.pop(k, None) for k in ("ANTHROPIC_API_KEY",)}
        try:
            with self.assertRaises(ProviderError):
                AnthropicProvider()
        finally:
            for k, v in original.items():
                if v is not None:
                    __import__("os").environ[k] = v

    def test_mock_callable(self) -> None:
        p = MockProvider(responder=lambda s, u: f"sys={s[:3]} user={u[:3]}")
        self.assertEqual(p.complete("system_prompt", "user_prompt"),
                         "sys=sys user=use")

    def test_mock_dict_lookup_miss_raises(self) -> None:
        p = MockProvider(responder={("s", "u"): "ok"})
        self.assertEqual(p.complete("s", "u"), "ok")
        with self.assertRaises(ProviderError):
            p.complete("other", "missing")


class TestExtractor(unittest.TestCase):
    _GOOD = (
        '{"facts":[{"name":"a","value":true}],'
        '"rules":[{"name":"r1","antecedents":["a"],"consequent":"b"}],'
        '"goal":"b"}'
    )

    def test_happy_path(self) -> None:
        provider = MockProvider(responder=lambda s, u: self._GOOD)
        result = extract(provider, "If a then b. Is b?")
        self.assertEqual(result.data["goal"], "b")
        self.assertEqual(result.data["facts"][0]["name"], "a")

    def test_strips_code_fences(self) -> None:
        provider = MockProvider(
            responder=lambda s, u: f"```json\n{self._GOOD}\n```"
        )
        result = extract(provider, "anything")
        self.assertEqual(result.data["goal"], "b")

    def test_rejects_non_json(self) -> None:
        provider = MockProvider(responder=lambda s, u: "not json at all")
        with self.assertRaises(ExtractionError):
            extract(provider, "anything")

    def test_rejects_bad_shape(self) -> None:
        bad = '{"facts": "not a list", "rules": []}'
        provider = MockProvider(responder=lambda s, u: bad)
        with self.assertRaises(ExtractionError):
            extract(provider, "anything")

    def test_rejects_empty_question(self) -> None:
        provider = MockProvider(responder=lambda s, u: self._GOOD)
        with self.assertRaises(ExtractionError):
            extract(provider, "   ")


class TestSolverWrapper(unittest.TestCase):
    def setUp(self) -> None:
        _require_solver()

    def test_simple_chain(self) -> None:
        problem = {
            "facts": [{"name": "a", "value": True}],
            "rules": [{"name": "r1", "antecedents": ["a"], "consequent": "b"}],
            "goal": "b",
        }
        result = solve(problem)
        self.assertTrue(result.ok)
        self.assertTrue(result.derived)
        self.assertTrue(result.goal_value)
        self.assertEqual(len(result.chain), 1)
        self.assertEqual(result.chain[0]["concluded"], "b")

    def test_three_hop_chain(self) -> None:
        problem = {
            "facts": [{"name": "a", "value": True}],
            "rules": [
                {"name": "r1", "antecedents": ["a"], "consequent": "b"},
                {"name": "r2", "antecedents": ["b"], "consequent": "c"},
                {"name": "r3", "antecedents": ["c"], "consequent": "d"},
            ],
            "goal": "d",
        }
        result = solve(problem)
        self.assertTrue(result.derived)
        self.assertEqual([s["concluded"] for s in result.chain], ["b", "c", "d"])

    def test_goal_not_derivable(self) -> None:
        problem = {
            "facts": [{"name": "a", "value": True}],
            "rules": [{"name": "r1", "antecedents": ["a", "missing"], "consequent": "z"}],
            "goal": "z",
        }
        result = solve(problem)
        self.assertFalse(result.derived)
        self.assertEqual(result.chain, [])

    def test_missing_binary(self) -> None:
        with self.assertRaises(SolverError):
            solve({"facts": [], "rules": []}, binary=Path("/nonexistent/dynabolic_solver"))

    def test_v3_ground_first_order(self) -> None:
        # PR-9 smoke test: ground first-order atoms round-trip through the
        # Python wrapper. Variables / unification land in PR-10.
        problem = {
            "facts": [
                {"name": "parent", "args": ["alice", "bob"],     "value": True},
                {"name": "parent", "args": ["bob",   "charlie"], "value": True},
            ],
            "rules": [{
                "name": "grandparent_alice_charlie",
                "antecedents": [
                    {"name": "parent", "args": ["alice", "bob"],     "value": True},
                    {"name": "parent", "args": ["bob",   "charlie"], "value": True},
                ],
                "consequent": {"name": "grandparent",
                               "args": ["alice", "charlie"],
                               "value": True},
            }],
            "goal": {"name": "grandparent", "args": ["alice", "charlie"]},
        }
        result = solve(problem)
        self.assertTrue(result.derived)
        self.assertTrue(result.goal_value)
        self.assertEqual(len(result.chain), 1)
        self.assertEqual(result.chain[0]["concluded"], "grandparent(alice,charlie)")
        self.assertIn("grandparent(alice,charlie)", result.final_facts)

    def test_v3_arg_mismatch_does_not_fire(self) -> None:
        # parent(alice,bob) does NOT satisfy parent(alice,charlie).
        problem = {
            "facts": [{"name": "parent", "args": ["alice", "bob"], "value": True}],
            "rules": [{
                "name": "r1",
                "antecedents": [
                    {"name": "parent", "args": ["alice", "charlie"], "value": True}
                ],
                "consequent": {"name": "x", "args": [], "value": True},
            }],
            "goal": {"name": "x"},
        }
        result = solve(problem)
        self.assertFalse(result.derived)
        self.assertEqual(result.chain, [])

    def test_v3_variables_not_yet_supported(self) -> None:
        # PR-10 will add this. Until then the solver must reject cleanly.
        problem = {
            "facts": [{"name": "p", "args": ["a"], "value": True}],
            "rules": [{
                "name": "r1",
                "antecedents": [{"name": "p", "args": [{"var": "X"}], "value": True}],
                "consequent": {"name": "q", "args": [{"var": "X"}], "value": True},
            }],
            "goal": "q",
        }
        with self.assertRaises(SolverError) as ctx:
            solve(problem)
        self.assertIn("variables are not yet supported", str(ctx.exception))


class TestVerbalizer(unittest.TestCase):
    def setUp(self) -> None:
        _require_solver()

    def test_calls_verbaliser_with_chain(self) -> None:
        captured: dict[str, str] = {}

        def responder(system: str, user: str) -> str:
            captured["system"] = system
            captured["user"] = user
            return "Answer: yes. Reasoning: a implies b."

        problem = {
            "facts": [{"name": "a", "value": True}],
            "rules": [{"name": "r1", "antecedents": ["a"], "consequent": "b"}],
            "goal": "b",
        }
        solver_result = solve(problem)
        text = verbalize(MockProvider(responder=responder),
                         "Is b?", problem, solver_result)
        self.assertEqual(text, "Answer: yes. Reasoning: a implies b.")
        self.assertIn("explanation writer", captured["system"])
        self.assertIn("Is b?", captured["user"])
        # The verbaliser must see the chain so it can describe it.
        payload = json.loads(captured["user"].split("symbolic engine:\n", 1)[1])
        self.assertEqual(payload["chain"][0]["concluded"], "b")


class TestV2Schema(unittest.TestCase):
    """V2 features: NAF antecedents, value=false consequents, priorities."""

    def setUp(self) -> None:
        _require_solver()

    def test_classical_tweety_priority_wins(self) -> None:
        problem = {
            "facts": [
                {"name": "is_bird", "value": True},
                {"name": "is_penguin", "value": True},
            ],
            "rules": [
                {"name": "default_birds_fly",
                 "antecedents": [{"name": "is_bird", "value": True}],
                 "consequent":  {"name": "can_fly", "value": True},
                 "priority":    0},
                {"name": "penguins_dont_fly",
                 "antecedents": [{"name": "is_penguin", "value": True}],
                 "consequent":  {"name": "can_fly", "value": False},
                 "priority":    10},
            ],
            "goal": "can_fly",
        }
        result = solve(problem)
        self.assertTrue(result.derived)
        self.assertFalse(result.goal_value)
        self.assertFalse(result.final_facts["can_fly"])
        # Only the winning rule should be in the chain.
        rule_names = [s["rule"] for s in result.chain]
        self.assertEqual(rule_names, ["penguins_dont_fly"])
        self.assertEqual(result.chain[0]["priority"], 10)

    def test_default_fires_when_exception_absent(self) -> None:
        problem = {
            "facts": [{"name": "is_bird", "value": True}],
            "rules": [
                {"name": "default_birds_fly",
                 "antecedents": [{"name": "is_bird", "value": True}],
                 "consequent":  {"name": "can_fly", "value": True},
                 "priority":    0},
                {"name": "penguins_dont_fly",
                 "antecedents": [{"name": "is_penguin", "value": True}],
                 "consequent":  {"name": "can_fly", "value": False},
                 "priority":    10},
            ],
            "goal": "can_fly",
        }
        result = solve(problem)
        self.assertTrue(result.goal_value)

    def test_naf_antecedent_fires_when_target_unknown(self) -> None:
        problem = {
            "facts": [{"name": "is_bird", "value": True}],
            "rules": [{"name": "naf_default",
                       "antecedents": [{"name": "is_bird",    "value": True},
                                       {"name": "is_penguin", "value": False}],
                       "consequent":  {"name": "can_fly", "value": True}}],
            "goal": "can_fly",
        }
        result = solve(problem)
        self.assertTrue(result.goal_value)

    def test_naf_antecedent_blocks_when_target_asserted(self) -> None:
        problem = {
            "facts": [{"name": "is_bird", "value": True},
                      {"name": "is_penguin", "value": True}],
            "rules": [{"name": "naf_default",
                       "antecedents": [{"name": "is_bird",    "value": True},
                                       {"name": "is_penguin", "value": False}],
                       "consequent":  {"name": "can_fly", "value": True}}],
            "goal": "can_fly",
        }
        result = solve(problem)
        self.assertFalse(result.derived)
        self.assertEqual(result.chain, [])

    def test_equal_priority_disagreement_is_tie_skipped(self) -> None:
        problem = {
            "facts": [{"name": "p", "value": True}, {"name": "q", "value": True}],
            "rules": [
                {"name": "r_true",
                 "antecedents": [{"name": "p", "value": True}],
                 "consequent":  {"name": "x", "value": True},
                 "priority":    5},
                {"name": "r_false",
                 "antecedents": [{"name": "q", "value": True}],
                 "consequent":  {"name": "x", "value": False},
                 "priority":    5},
            ],
            "goal": "x",
        }
        result = solve(problem)
        self.assertFalse(result.derived)
        ties = result.raw.get("tie_skipped", [])
        self.assertEqual(len(ties), 1)
        self.assertEqual(ties[0]["predicate"], "x")
        self.assertEqual(ties[0]["priority"], 5)
        rule_names = sorted(r["name"] for r in ties[0]["rules"])
        self.assertEqual(rule_names, ["r_false", "r_true"])

    def test_override_in_chain(self) -> None:
        # `low` fires first (derives x=true), then `derive_b` derives `b`,
        # then `high` (priority 10) overrides x to false.
        problem = {
            "facts": [{"name": "a", "value": True}],
            "rules": [
                {"name": "low",
                 "antecedents": [{"name": "a", "value": True}],
                 "consequent":  {"name": "x", "value": True},
                 "priority":    0},
                {"name": "derive_b",
                 "antecedents": [{"name": "a", "value": True}],
                 "consequent":  {"name": "b", "value": True},
                 "priority":    0},
                {"name": "high",
                 "antecedents": [{"name": "b", "value": True}],
                 "consequent":  {"name": "x", "value": False},
                 "priority":    10},
            ],
            "goal": "x",
        }
        result = solve(problem)
        self.assertTrue(result.derived)
        self.assertFalse(result.goal_value)
        # `high` step should carry overrides_previous=True.
        high_step = next(s for s in result.chain if s["rule"] == "high")
        self.assertTrue(high_step.get("overrides_previous", False))


class TestPipelineEndToEnd(unittest.TestCase):
    def setUp(self) -> None:
        _require_solver()

    def test_tweety_penguin_with_mock(self) -> None:
        extraction = (
            '{"facts":[{"name":"tweety_is_penguin","value":true}],'
            '"rules":['
            '{"name":"r1","antecedents":["tweety_is_penguin"],'
            '"consequent":"tweety_is_bird"},'
            '{"name":"r2","antecedents":["tweety_is_penguin"],'
            '"consequent":"tweety_cannot_fly"}'
            '],'
            '"goal":"tweety_can_fly"}'
        )
        verbalization = ("Answer: No, Tweety cannot fly. "
                         "Reasoning: Tweety is a penguin (given), so by the "
                         "flightless rule, Tweety cannot fly.")

        def responder(system: str, _user: str) -> str:
            if _is_extract_prompt(system):
                return extraction
            if _is_verbalize_prompt(system):
                return verbalization
            raise AssertionError(f"unexpected system prompt: {system[:60]!r}")

        pipeline = Pipeline(provider=MockProvider(responder=responder))
        result = pipeline.run(
            "Penguins are flightless birds. Tweety is a penguin. Can Tweety fly?"
        )

        # Extracted shape
        self.assertEqual(result.extracted.data["goal"], "tweety_can_fly")
        # Engine fires both rules in order, derives bird and cannot_fly,
        # but does NOT derive can_fly (no rule produces it).
        concluded = [s["concluded"] for s in result.chain]
        self.assertIn("tweety_is_bird", concluded)
        self.assertIn("tweety_cannot_fly", concluded)
        self.assertFalse(result.derived)
        self.assertTrue(result.solver.final_facts.get("tweety_cannot_fly"))
        self.assertNotIn("tweety_can_fly", result.solver.final_facts)
        # Answer comes from the verbaliser verbatim
        self.assertEqual(result.answer, verbalization)

    def test_pipeline_requires_provider(self) -> None:
        with self.assertRaises(ValueError):
            Pipeline()


if __name__ == "__main__":
    unittest.main()
