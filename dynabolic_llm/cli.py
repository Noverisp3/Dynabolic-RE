"""`python -m dynabolic_llm "your question"` runner.

Picks the provider via DYNABOLIC_LLM_PROVIDER (default: ollama). Prints the
LLM-extracted problem, the engine's derivation chain, and the verbalised
answer so the user can see all three layers.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .pipeline import Pipeline
from .provider import ProviderError, from_env


def _print_section(title: str, body: str) -> None:
    print(f"\n=== {title} ===")
    print(body)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python -m dynabolic_llm",
        description="LLM extracts facts/rules -> dynabolic_solver reasons -> "
                    "LLM verbalises. Pick provider via DYNABOLIC_LLM_PROVIDER "
                    "env var (ollama | openai | anthropic | mock).",
    )
    parser.add_argument("question", help="Natural-language question (with context).")
    parser.add_argument(
        "--solver",
        type=Path,
        default=None,
        help="Path to dynabolic_solver binary (default: ./build/dynabolic_solver)",
    )
    parser.add_argument(
        "--show-raw",
        action="store_true",
        help="Print the LLM's raw extraction response (useful when debugging prompts).",
    )
    args = parser.parse_args(argv)

    try:
        provider = from_env()
    except ProviderError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    pipeline = Pipeline(provider=provider, solver_binary=args.solver)

    try:
        result = pipeline.run(args.question)
    except Exception as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    _print_section("Extracted problem", json.dumps(result.extracted.data, indent=2))
    if args.show_raw:
        _print_section("Raw extractor response", result.extracted.raw)
    _print_section("Derivation chain", json.dumps(result.solver.chain, indent=2))
    _print_section("Final facts", json.dumps(result.solver.final_facts, indent=2))
    _print_section("Answer", result.answer)
    return 0


if __name__ == "__main__":
    sys.exit(main())
