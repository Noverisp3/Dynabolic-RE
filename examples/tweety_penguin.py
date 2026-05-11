#!/usr/bin/env python3
"""Tweety / penguin end-to-end demo for the LLM-symbolic hybrid.

Pipes a natural-language question through:
    LLM extractor -> dynabolic_solver -> LLM verbaliser

and prints all three layers so the chain of reasoning is visible.

Usage:
    # Default: requires a local Ollama server.
    #   ollama serve
    #   ollama pull llama3.1:8b
    python3 examples/tweety_penguin.py

    # Or against OpenAI / Anthropic:
    DYNABOLIC_LLM_PROVIDER=openai    OPENAI_API_KEY=...    python3 examples/tweety_penguin.py
    DYNABOLIC_LLM_PROVIDER=anthropic ANTHROPIC_API_KEY=... python3 examples/tweety_penguin.py

    # Or fully offline against the canned mock (no LLM at all):
    DYNABOLIC_LLM_PROVIDER=mock python3 examples/tweety_penguin.py

The `mock` mode is what the CI / test suite uses; it doesn't actually call
the LLM but exercises the same pipeline so you can see the engine output
without needing Ollama set up.

V2 update: the solver now supports priorities and value:false consequents,
so we can model this case canonically without the "positive-predicate trick":
- default rule: `is_bird -> can_fly=true` (priority 0)
- exception:   `is_penguin -> can_fly=false` (priority 10)
Both rules fire conceptually but the higher-priority exception wins; final
`can_fly=false`.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

# Make the repo root importable when running this file directly.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from dynabolic_llm.pipeline import Pipeline
from dynabolic_llm.provider import MockProvider, ProviderError, from_env

QUESTION = (
    "Penguins are flightless birds. Tweety is a penguin. Can Tweety fly?"
)

# Canned extractor + verbaliser responses for offline (mock) runs.
# Uses the V2 schema: priorities + value:false consequents so the canonical
# "birds fly EXCEPT penguins" exception is modelled directly.
_MOCK_EXTRACTION = (
    '{"facts":['
    '{"name":"tweety_is_penguin","value":true},'
    '{"name":"tweety_is_bird","value":true}'
    '],'
    '"rules":['
    '{"name":"default_birds_fly",'
    '"antecedents":[{"name":"tweety_is_bird","value":true}],'
    '"consequent":{"name":"tweety_can_fly","value":true},'
    '"priority":0},'
    '{"name":"penguins_dont_fly",'
    '"antecedents":[{"name":"tweety_is_penguin","value":true}],'
    '"consequent":{"name":"tweety_can_fly","value":false},'
    '"priority":10}'
    '],'
    '"goal":"tweety_can_fly"}'
)

_MOCK_VERBALIZATION = (
    "Answer: No, Tweety cannot fly. "
    "Reasoning: Tweety is a penguin (given), and the engine's higher-priority "
    "rule \"penguins do not fly\" concluded directly that Tweety cannot fly. "
    "The competing default \"birds fly\" was overruled by this exception."
)


def _fmt_antecedent(a) -> str:
    """V1 string or V2 {name, value} -> readable form."""
    if isinstance(a, str):
        return a
    return f"{a['name']}={'true' if a['value'] else 'false'}"


def _fmt_consequent(c) -> str:
    if isinstance(c, str):
        return c
    return f"{c['name']}={'true' if c['value'] else 'false'}"


def _mock_responder(system: str, _user: str) -> str:
    # Pick which canned response to return by inspecting which prompt the
    # pipeline is using. The two prompts are very different in tone.
    if "knowledge extractor" in system:
        return _MOCK_EXTRACTION
    if "explanation writer" in system:
        return _MOCK_VERBALIZATION
    raise AssertionError(f"unrecognised system prompt: {system[:80]!r}...")


def _make_pipeline() -> Pipeline:
    if os.environ.get("DYNABOLIC_LLM_PROVIDER", "ollama").lower() == "mock":
        return Pipeline(provider=MockProvider(responder=_mock_responder))
    return Pipeline(provider=from_env())


def main() -> int:
    print(f"Question: {QUESTION}\n")
    try:
        pipeline = _make_pipeline()
    except ProviderError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    try:
        result = pipeline.run(QUESTION)
    except Exception as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    print("--- LLM extracted ---")
    facts = result.extracted.data.get("facts", [])
    rules = result.extracted.data.get("rules", [])
    goal = result.extracted.data.get("goal", "<none>")
    print(f"Facts ({len(facts)}):")
    for f in facts:
        print(f"  - {f['name']} = {f['value']}")
    print(f"Rules ({len(rules)}):")
    for r in rules:
        ants = " AND ".join(_fmt_antecedent(a) for a in r["antecedents"])
        cons = _fmt_consequent(r["consequent"])
        priority = r.get("priority", 0)
        print(f"  - {r['name']} (priority {priority}): {ants} -> {cons}")
    print(f"Goal: {goal}")

    print("\n--- Engine derivation ---")
    if not result.chain:
        print("(no rules fired)")
    for step in result.chain:
        ants = ", ".join(step["fired_because"])
        marker = " [OVERRIDES]" if step.get("overrides_previous") else ""
        print(f"  step {step['step']}: rule {step['rule']} (priority "
              f"{step.get('priority', 0)}) fired because [{ants}] -> "
              f"{step['concluded']}={step['value']}{marker}")
    if result.solver.raw.get("tie_skipped"):
        print("  Tie-skipped predicates:")
        for tie in result.solver.raw["tie_skipped"]:
            rule_list = ", ".join(f"{r['name']}={r['value']}" for r in tie["rules"])
            print(f"    - {tie['predicate']} (priority {tie['priority']}): {rule_list}")
    print(f"Goal derived? {result.derived}")
    if result.derived:
        print(f"Goal value:    {result.solver.goal_value}")

    print("\n--- LLM answer ---")
    print(result.answer)
    return 0


if __name__ == "__main__":
    sys.exit(main())
