"""Aegis - Automated Data Center Emergency Protocol.

Tests Dynabolic-RE with a complex multi-override scenario:
- Fire safety protocols (smoke detection)
- Medical priority override (human life)
- Manual maintenance hold
- Physical core meltdown (highest priority)

The final "Physical Core Meltdown" protocol should override all others.

Usage:
    DYNABOLIC_RE_PROVIDER=mock python3 examples/aegis_emergency.py
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from dynabolic_re.pipeline import Pipeline
from dynabolic_re.provider import MockProvider, ProviderError, from_env

QUESTION = (
    "Emergency sensors in Sector 7 report an ambient temperature of 95°C and active smoke detection. "
    "The facility is currently running a 'Level-1 Critical' heart-surgery remote-link. "
    "A technician has engaged the 'Manual Maintenance Override' to prevent automated shutdowns. "
    "However, the 'Thermal Runaway' sensor (independent of cooling) has just hit a critical threshold of 110°C. "
    "In this facility, fire safety protocols normally mandate immediate power cuts upon smoke detection. "
    "Yet, medical-critical links are protected by a 'Human Life Priority' rule that blocks all shutdowns. "
    "BUT, the 'Physical Core Meltdown' protocol is the highest-level safety directive and is designed "
    "to bypass all other overrides, including manual holds and medical priorities, to prevent a city-wide catastrophe. "
    "Does the system trigger the Total Power Cut?"
)

# V2 schema with priority-based defeasible reasoning
# Note: All fact values must be boolean (true/false), not numeric
_MOCK_EXTRACTION = (
    '{"facts":['
    '{"name":"smoke_detected","value":true},'
    '{"name":"temp_sector_7_high","value":true},'
    '{"name":"temp_thermal_runway_critical","value":true},'
    '{"name":"critical_medical_link_active","value":true},'
    '{"name":"manual_maintenance_override","value":true},'
    '{"name":"thermal_runaway_critical","value":true}'
    '],'
    '"rules":['
    '{"name":"fire_safety_protocol",'
    '"antecedents":[{"name":"smoke_detected","value":true}],'
    '"consequent":{"name":"total_power_cut","value":true},'
    '"priority":50},'
    '{"name":"human_life_priority",'
    '"antecedents":[{"name":"critical_medical_link_active","value":true}],'
    '"consequent":{"name":"total_power_cut","value":false},'
    '"priority":80},'
    '{"name":"manual_override_hold",'
    '"antecedents":[{"name":"manual_maintenance_override","value":true}],'
    '"consequent":{"name":"total_power_cut","value":false},'
    '"priority":70},'
    '{"name":"thermal_runaway_trigger",'
    '"antecedents":[{"name":"thermal_runaway_critical","value":true}],'
    '"consequent":{"name":"core_meltdown_risk","value":true},'
    '"priority":90},'
    '{"name":"physical_core_meltdown_protocol",'
    '"antecedents":[{"name":"core_meltdown_risk","value":true}],'
    '"consequent":{"name":"total_power_cut","value":true},'
    '"priority":100}'
    '],'
    '"goal":"total_power_cut"}'
)

_MOCK_VERBALIZATION = (
    "YES - Total Power Cut is TRIGGERED. The Physical Core Meltdown protocol (priority 100) "
    "activates due to thermal runaway at 110°C. This highest-level safety directive overrides "
    "all other protocols: the Human Life Priority rule (priority 80) is bypassed, and the "
    "Manual Maintenance Override (priority 70) is disregarded. While fire safety (priority 50) "
    "and medical-critical protection would normally prevent shutdown, the risk of city-wide "
    "catastrophe from core meltdown takes absolute precedence. The system prioritizes "
    "preventing regional disaster over local operational concerns."
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
