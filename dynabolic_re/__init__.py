"""LLM-symbolic hybrid orchestrator for Dynabolic-RE.

Pipeline:
    natural-language question
        -> [LLM extractor]   facts + rules + goal as JSON
        -> [dynabolic_solver] forward chaining + chain tracking
        -> [LLM verbalizer]  natural-language answer with derivation

The LLM does *only* extraction and verbalization. All multi-step reasoning
runs in the C++ solver, so the answer is grounded in a verifiable chain of
rule firings.
"""

from .pipeline import Pipeline, PipelineResult
from .provider import (
    AnthropicProvider,
    LLMProvider,
    MockProvider,
    OllamaProvider,
    OpenAIProvider,
    ProviderError,
)

__all__ = [
    "Pipeline",
    "PipelineResult",
    "LLMProvider",
    "OllamaProvider",
    "OpenAIProvider",
    "AnthropicProvider",
    "MockProvider",
    "ProviderError",
]
