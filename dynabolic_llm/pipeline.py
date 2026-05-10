"""End-to-end: NL question -> NL answer with verifiable derivation."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .extractor import ExtractedProblem, extract
from .provider import LLMProvider
from .solver import SolverResult, solve
from .verbalizer import verbalize


@dataclass
class PipelineResult:
    question: str
    extracted: ExtractedProblem    # facts/rules/goal as the LLM extracted them
    solver: SolverResult           # chain + final facts from the C++ engine
    answer: str                    # human-readable answer from the LLM verbaliser

    @property
    def chain(self) -> list[dict[str, Any]]:
        return self.solver.chain

    @property
    def derived(self) -> bool | None:
        return self.solver.derived


@dataclass
class Pipeline:
    """Orchestrates extract -> solve -> verbalize.

    `extractor_provider` and `verbalizer_provider` can be the same provider
    or different ones (e.g. cheap model for extraction, stronger model for
    verbalisation). If only `provider` is given, both stages share it.
    """

    provider: LLMProvider | None = None
    extractor_provider: LLMProvider | None = None
    verbalizer_provider: LLMProvider | None = None
    solver_binary: Path | None = None

    def __post_init__(self) -> None:
        ep = self.extractor_provider or self.provider
        vp = self.verbalizer_provider or self.provider
        if ep is None or vp is None:
            raise ValueError(
                "Pipeline needs at least one provider (set `provider=`, or both "
                "`extractor_provider=` and `verbalizer_provider=`)"
            )
        self._extractor = ep
        self._verbalizer = vp

    def run(self, question: str) -> PipelineResult:
        extracted = extract(self._extractor, question)
        result = solve(extracted.data, binary=self.solver_binary)
        answer = verbalize(self._verbalizer, question, extracted.data, result)
        return PipelineResult(
            question=question,
            extracted=extracted,
            solver=result,
            answer=answer,
        )
