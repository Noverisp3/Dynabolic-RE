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

V1 note: the canonical "birds fly, penguins are birds, penguins don't fly"
formulation is non-monotonic and the V1 solver doesn't support negation or
rule priorities. We sidestep this by extracting "penguins cannot fly" as a
single positive predicate (`tweety_cannot_fly`) rather than negating
`tweety_can_fly`. The end-user-facing answer is the same; only the
intermediate predicates differ.
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
_MOCK_EXTRACTION = (
    '{"facts":[{"name":"tweety_is_penguin","value":true}],'
    '"rules":['
    '{"name":"r1","antecedents":["tweety_is_penguin"],'
    '"consequent":"tweety_is_bird"},'
    '{"name":"r2","antecedents":["tweety_is_penguin"],'
    '"consequent":"tweety_cannot_fly"}'
    '],'
    '"goal":"tweety_can_fly"}'
)

_MOCK_VERBALIZATION = (
    "Answer: No, Tweety cannot fly. "
    "Reasoning: Tweety is a penguin (given), and from the rule that penguins "
    "are flightless, Tweety cannot fly. The goal `tweety_can_fly` was not "
    "derived by any rule, so we cannot conclude that Tweety can fly."
)


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
        ants = " AND ".join(r["antecedents"])
        print(f"  - {r['name']}: {ants} -> {r['consequent']}")
    print(f"Goal: {goal}")

    print("\n--- Engine derivation ---")
    if not result.chain:
        print("(no rules fired)")
    for step in result.chain:
        ants = ", ".join(step["fired_because"])
        print(f"  step {step['step']}: rule {step['rule']} fired "
              f"because [{ants}] -> {step['concluded']}={step['value']}")
    print(f"Goal derived? {result.derived}")

    print("\n--- LLM answer ---")
    print(result.answer)
    return 0


if __name__ == "__main__":
    sys.exit(main())
