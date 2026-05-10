"""Unit tests for the LLM orchestrator.

Uses MockProvider so no LLM is required. The C++ solver binary IS required
and must be built first (`make` from the repo root).

Run: python3 -m unittest tests/test_dynabolic_llm.py
"""

from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path

# Make the repo root importable.
_REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_REPO_ROOT))

from dynabolic_llm.extractor import ExtractionError, extract
from dynabolic_llm.pipeline import Pipeline
from dynabolic_llm.provider import (
    AnthropicProvider,
    MockProvider,
    OpenAIProvider,
    ProviderError,
)
from dynabolic_llm.solver import SolverError, solve
from dynabolic_llm.verbalizer import verbalize


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
