"""Security clearance with conflicting rules and manager override.

Tests Dynabolic-RE with:
- Conflicting clearance levels (basic vs enhanced)
- Manager override authority (higher priority)
- Multiple rule interactions

Usage:
    DYNABOLIC_RE_PROVIDER=mock python3 examples/security_clearance.py
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from dynabolic_re.pipeline import Pipeline
from dynabolic_re.provider import MockProvider, ProviderError, from_env

QUESTION = (
    "Employee Jones passed background check and security training. "
    "Has foreign contacts in allied countries. Not in national security role. "
    "Manager approved enhanced clearance due to project needs. "
    "Does Jones qualify for enhanced security clearance?"
)

# Complex rule interactions: foreign contacts normally block enhanced clearance,
# but manager approval with good standing provides override
_MOCK_EXTRACTION = (
    '{"facts":['
    '{"name":"employee_background_check_passed","value":true},'
    '{"name":"employee_security_training_completed","value":true},'
    '{"name":"employee_foreign_contacts","value":true},'
    '{"name":"employee_national_security_role","value":false},'
    '{"name":"employee_manager_approval","value":true}'
    '],'
    '"rules":['
    '{"name":"basic_clearance",'
    '"antecedents":[{"name":"employee_background_check_passed","value":true},{"name":"employee_security_training_completed","value":true}],'
    '"consequent":{"name":"basic_security_clearance","value":true},'
    '"priority":10},'
    '{"name":"foreign_contact_flag",'
    '"antecedents":[{"name":"employee_foreign_contacts","value":true}],'
    '"consequent":{"name":"enhanced_clearance_blocked","value":true},'
    '"priority":20},'
    '{"name":"national_security_boost",'
    '"antecedents":[{"name":"employee_national_security_role","value":true}],'
    '"consequent":{"name":"top_secret_eligible","value":true},'
    '"priority":50},'
    '{"name":"manager_override_enhanced",'
    '"antecedents":[{"name":"employee_manager_approval","value":true},{"name":"employee_foreign_contacts","value":true},{"name":"employee_background_check_passed","value":true}],'
    '"consequent":{"name":"enhanced_clearance_granted","value":true},'
    '"priority":40},'
    '{"name":"manager_override_block",'
    '"antecedents":[{"name":"employee_manager_approval","value":true},{"name":"enhanced_clearance_blocked","value":true}],'
    '"consequent":{"name":"enhanced_clearance_blocked","value":false},'
    '"priority":45}'
    '],'
    '"goal":"enhanced_clearance_granted"}'
)

_MOCK_VERBALIZATION = (
    "Yes, Jones qualifies for enhanced security clearance. Although foreign contacts "
    "normally block enhanced clearance, the manager's override authority supersedes this "
    "restriction when the employee has passed background checks. The override is granted "
    "based on project needs and verified good standing."
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
