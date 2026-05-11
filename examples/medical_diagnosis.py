"""Medical diagnosis with negation-as-failure and exception handling.

Tests Dynabolic-RE with:
- Multi-factor diagnosis rules
- Negation-as-failure (vaccine protection check)
- Priority-based overrides (high-risk travel)

Usage:
    DYNABOLIC_RE_PROVIDER=mock python3 examples/medical_diagnosis.py
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from dynabolic_re.pipeline import Pipeline
from dynabolic_re.provider import MockProvider, ProviderError, from_env

QUESTION = (
    "Patient presents with fever and dry cough. Recent travel to Southeast Asia. "
    "No flu vaccination on record. Should patient be isolated pending further testing?"
)

# V2 schema with NAF: vaccine protection fails if NOT vaccinated
_MOCK_EXTRACTION = (
    '{"facts":['
    '{"name":"patient_has_fever","value":true},'
    '{"name":"patient_has_cough","value":true},'
    '{"name":"patient_recent_travel","value":true},'
    '{"name":"patient_vaccinated_flu","value":false}'
    '],'
    '"rules":['
    '{"name":"flu_symptoms",'
    '"antecedents":[{"name":"patient_has_fever","value":true},{"name":"patient_has_cough","value":true}],'
    '"consequent":{"name":"likely_flu","value":true},'
    '"priority":50},'
    '{"name":"travel_risk",'
    '"antecedents":[{"name":"patient_recent_travel","value":true}],'
    '"consequent":{"name":"exotic_disease_possible","value":true},'
    '"priority":70},'
    '{"name":"vaccine_protects",'
    '"antecedents":[{"name":"patient_vaccinated_flu","value":true},{"name":"likely_flu","value":true}],'
    '"consequent":{"name":"likely_flu","value":false},'
    '"priority":60},'
    '{"name":"high_risk_override",'
    '"antecedents":[{"name":"exotic_disease_possible","value":true},{"name":"patient_has_fever","value":true}],'
    '"consequent":{"name":"needs_isolation","value":true},'
    '"priority":80}'
    '],'
    '"goal":"needs_isolation"}'
)

_MOCK_VERBALIZATION = (
    "Yes, the patient should be isolated. Recent travel to an endemic region "
    "combined with fever indicates possible exotic disease. While symptoms suggest flu, "
    "the absence of vaccination means no protection is assumed. Travel risk takes priority "
    "over routine diagnosis."
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
