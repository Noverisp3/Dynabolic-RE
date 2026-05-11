"""Legal inheritance reasoning demo with multi-step defeasible logic.

Tests Dynabolic-RE with a 4-step inference chain and priority-based exceptions.

Usage:
    DYNABOLIC_RE_PROVIDER=mock python3 examples/legal_inheritance.py
    # Or with real LLM (slower):
    OLLAMA_MODEL=phi3 python3 examples/legal_inheritance.py
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from dynabolic_re.pipeline import Pipeline
from dynabolic_re.provider import MockProvider, ProviderError, from_env

QUESTION = (
    "John Smith died without a will, leaving a $500,000 estate. "
    "His daughter Mary is his eldest child and a direct descendant. "
    "Under intestacy law, descendants inherit when there is no will. "
    "The eldest child has priority among multiple heirs. "
    "Is Mary the primary heir to the estate?"
)

# Canned responses for mock mode
_MOCK_EXTRACTION = (
    '{"facts":['
    '{"name":"estate_has_assets","value":true},'
    '{"name":"will_exists","value":false},'
    '{"name":"mary_is_descendant","value":true},'
    '{"name":"mary_is_eldest_child","value":true}'
    '],'
    '"rules":['
    '{"name":"intestacy_applies",'
    '"antecedents":[{"name":"estate_has_assets","value":true},{"name":"will_exists","value":false}],'
    '"consequent":{"name":"intestacy_law_applies","value":true},'
    '"priority":100},'
    '{"name":"descendant_inherits",'
    '"antecedents":[{"name":"intestacy_law_applies","value":true},{"name":"mary_is_descendant","value":true}],'
    '"consequent":{"name":"mary_has_inheritance_rights","value":true},'
    '"priority":90},'
    '{"name":"eldest_priority",'
    '"antecedents":[{"name":"mary_has_inheritance_rights","value":true},{"name":"mary_is_eldest_child","value":true}],'
    '"consequent":{"name":"mary_is_primary_heir","value":true},'
    '"priority":80}'
    '],'
    '"goal":"mary_is_primary_heir"}'
)

_MOCK_VERBALIZATION = (
    "Yes, Mary is the primary heir. The estate has assets and no will exists, "
    "so intestacy law applies. As a direct descendant, Mary has inheritance rights. "
    "Being the eldest child gives her priority status as the primary heir."
)


def _mock_responder(system: str, user: str) -> str:
    # Pick which canned response to return by inspecting which prompt the
    # pipeline is using. The two prompts are very different in tone.
    if "knowledge extractor" in system:
        return _MOCK_EXTRACTION
    if "explanation writer" in system:
        return _MOCK_VERBALIZATION
    raise AssertionError(f"unrecognised system prompt: {system[:80]!r}...")


def _make_pipeline() -> Pipeline:
    if os.environ.get("DYNABOLIC_RE_PROVIDER", "ollama").lower() == "mock":
        return Pipeline(provider=MockProvider(responder=_mock_responder))
    return Pipeline(provider=from_env())


def main() -> int:
    print(f"Question: {QUESTION}\n")
    try:
        pipeline = _make_pipeline()
    except ProviderError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    result = pipeline.run(QUESTION)

    print("--- LLM extracted ---")
    facts = result.extracted.data.get("facts", [])
    rules = result.extracted.data.get("rules", [])
    goal = result.extracted.data.get("goal", "<none>")
    print(f"Facts ({len(facts)}):")
    for f in facts:
        print(f"  - {f['name']} = {f['value']}")
    print(f"Rules ({len(rules)}):")
    for r in rules:
        ants = ", ".join(a["name"] for a in r["antecedents"])
        print(f"  - {r['name']} (priority {r.get('priority', 0)}): {ants} -> {r['consequent']['name']}={r['consequent']['value']}")
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
