"""Natural language -> dynabolic_solver problem JSON.

Calls the LLM with the strict extraction prompt, parses the response into a
problem dict, and validates the shape so a malformed LLM response is caught
*before* it hits the C++ solver.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .provider import LLMProvider, ProviderError

_PROMPT_PATH = Path(__file__).parent / "prompts" / "extract.txt"
_FENCE_RE = re.compile(r"^```(?:json)?\s*\n(.*)\n```\s*$", re.DOTALL)


class ExtractionError(RuntimeError):
    """The LLM returned something that doesn't look like a valid problem."""


@dataclass
class ExtractedProblem:
    """Result of extraction. `data` is the JSON dict ready to pipe to the solver."""

    data: dict[str, Any]
    raw: str  # the raw LLM response, kept for debugging


def _load_system_prompt() -> str:
    return _PROMPT_PATH.read_text(encoding="utf-8")


def _strip_code_fences(text: str) -> str:
    r"""Tolerate ```json ... ``` fences even though the prompt forbids them."""
    text = text.strip()
    m = _FENCE_RE.match(text)
    if m:
        return m.group(1).strip()
    return text


def _validate(problem: Any) -> dict[str, Any]:
    """Cheap shape check. Returns the problem on success, raises on failure."""
    if not isinstance(problem, dict):
        raise ExtractionError(f"top-level value must be an object, got {type(problem).__name__}")

    facts = problem.get("facts", [])
    if not isinstance(facts, list):
        raise ExtractionError("'facts' must be a list")
    for i, f in enumerate(facts):
        if not isinstance(f, dict) or "name" not in f or "value" not in f:
            raise ExtractionError(f"facts[{i}] must be {{name, value}}, got {f!r}")
        if not isinstance(f["name"], str) or not f["name"]:
            raise ExtractionError(f"facts[{i}].name must be a non-empty string")
        if not isinstance(f["value"], bool):
            raise ExtractionError(f"facts[{i}].value must be a bool")

    rules = problem.get("rules", [])
    if not isinstance(rules, list):
        raise ExtractionError("'rules' must be a list")
    for i, r in enumerate(rules):
        if not isinstance(r, dict):
            raise ExtractionError(f"rules[{i}] must be an object")
        for key in ("name", "antecedents", "consequent"):
            if key not in r:
                raise ExtractionError(f"rules[{i}] missing '{key}'")
        if not isinstance(r["name"], str) or not r["name"]:
            raise ExtractionError(f"rules[{i}].name must be a non-empty string")
        if not isinstance(r["antecedents"], list):
            raise ExtractionError(f"rules[{i}].antecedents must be a list")
        for j, a in enumerate(r["antecedents"]):
            # Accept V1 string OR V2 {name, value} object.
            if isinstance(a, str):
                if not a:
                    raise ExtractionError(
                        f"rules[{i}].antecedents[{j}] must be a non-empty string"
                    )
            elif isinstance(a, dict):
                if "name" not in a or "value" not in a:
                    raise ExtractionError(
                        f"rules[{i}].antecedents[{j}] must be {{name, value}}"
                    )
                if not isinstance(a["name"], str) or not a["name"]:
                    raise ExtractionError(
                        f"rules[{i}].antecedents[{j}].name must be a non-empty string"
                    )
                if not isinstance(a["value"], bool):
                    raise ExtractionError(
                        f"rules[{i}].antecedents[{j}].value must be a bool"
                    )
            else:
                raise ExtractionError(
                    f"rules[{i}].antecedents[{j}] must be a string or {{name, value}}"
                )
        # Consequent: V1 string OR V2 {name, value} object.
        c = r["consequent"]
        if isinstance(c, str):
            if not c:
                raise ExtractionError(f"rules[{i}].consequent must be non-empty")
        elif isinstance(c, dict):
            if "name" not in c or "value" not in c:
                raise ExtractionError(f"rules[{i}].consequent must be {{name, value}}")
            if not isinstance(c["name"], str) or not c["name"]:
                raise ExtractionError(f"rules[{i}].consequent.name must be a non-empty string")
            if not isinstance(c["value"], bool):
                raise ExtractionError(f"rules[{i}].consequent.value must be a bool")
        else:
            raise ExtractionError(
                f"rules[{i}].consequent must be a string or {{name, value}}"
            )
        # Priority is optional; if present must be an int.
        if "priority" in r and not isinstance(r["priority"], int):
            raise ExtractionError(f"rules[{i}].priority must be an int if present")

    if "goal" in problem and not isinstance(problem["goal"], str):
        raise ExtractionError("'goal' must be a string if present")

    return problem


def extract(provider: LLMProvider, question: str) -> ExtractedProblem:
    """Run the LLM extractor on a natural-language question."""
    if not question.strip():
        raise ExtractionError("question is empty")

    system = _load_system_prompt()
    try:
        raw = provider.complete(system=system, user=question, temperature=0.0)
    except ProviderError as e:
        raise ExtractionError(f"LLM call failed: {e}") from e

    body = _strip_code_fences(raw)
    try:
        parsed = json.loads(body)
    except json.JSONDecodeError as e:
        raise ExtractionError(
            f"LLM did not return valid JSON. Raw response was: {raw!r}"
        ) from e

    return ExtractedProblem(data=_validate(parsed), raw=raw)
