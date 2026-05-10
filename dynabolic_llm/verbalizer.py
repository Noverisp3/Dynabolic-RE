"""Solver chain -> natural-language explanation via the LLM."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from .provider import LLMProvider, ProviderError
from .solver import SolverResult

_PROMPT_PATH = Path(__file__).parent / "prompts" / "verbalize.txt"


class VerbalizationError(RuntimeError):
    """The LLM verbaliser failed."""


def _load_system_prompt() -> str:
    return _PROMPT_PATH.read_text(encoding="utf-8")


def _build_user_message(question: str, problem: dict[str, Any], result: SolverResult) -> str:
    """Bundle the question + structured derivation into a single user message."""
    payload = {
        "question": question,
        "goal": problem.get("goal"),
        "derived": result.derived,
        "goal_value": result.goal_value,
        "chain": result.chain,
        "final_facts": result.final_facts,
    }
    return (
        "User question:\n"
        f"  {question}\n\n"
        "Structured derivation produced by the symbolic engine:\n"
        f"{json.dumps(payload, indent=2)}\n"
    )


def verbalize(provider: LLMProvider,
              question: str,
              problem: dict[str, Any],
              result: SolverResult) -> str:
    system = _load_system_prompt()
    user = _build_user_message(question, problem, result)
    try:
        return provider.complete(system=system, user=user, temperature=0.0).strip()
    except ProviderError as e:
        raise VerbalizationError(f"LLM call failed: {e}") from e
