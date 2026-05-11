"""Subprocess wrapper around build/dynabolic_solver.

Pipes a problem JSON to the solver's stdin, parses its stdout, and surfaces
errors with enough context to debug.
"""

from __future__ import annotations

import json
import os
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any


class SolverError(RuntimeError):
    """The solver exited non-zero or returned something we can't parse."""


@dataclass
class SolverResult:
    ok: bool
    derived: bool | None        # None if the input had no goal
    goal_value: bool | None     # None if the goal wasn't derived (or absent)
    chain: list[dict[str, Any]]
    final_facts: dict[str, bool]
    raw: dict[str, Any]


def _default_binary() -> Path:
    """Default solver path: build/dynabolic_solver under the repo root."""
    # dynabolic_re/solver.py -> repo root
    return Path(__file__).resolve().parent.parent / "build" / "dynabolic_solver"


def solve(problem: dict[str, Any], *,
          binary: Path | None = None,
          timeout: float = 30.0) -> SolverResult:
    """Run the C++ solver on `problem` and return a typed result."""

    bin_path = Path(binary) if binary else _default_binary()
    if not bin_path.exists():
        raise SolverError(
            f"solver binary not found at {bin_path}. "
            f"Run `make` from the repo root to build it."
        )
    if not os.access(bin_path, os.X_OK):
        raise SolverError(f"solver binary at {bin_path} is not executable")

    payload = json.dumps(problem)
    try:
        proc = subprocess.run(
            [str(bin_path)],
            input=payload,
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as e:
        raise SolverError(f"solver timed out after {timeout}s") from e

    stdout = proc.stdout.strip()
    if not stdout:
        raise SolverError(
            f"solver produced no output (exit {proc.returncode}). stderr: {proc.stderr!r}"
        )

    try:
        data = json.loads(stdout)
    except json.JSONDecodeError as e:
        raise SolverError(f"solver returned non-JSON: {stdout!r}") from e

    if not data.get("ok", False):
        raise SolverError(f"solver reported error: {data.get('error', '<no message>')}")

    return SolverResult(
        ok=True,
        derived=data.get("derived"),
        goal_value=data.get("goal_value"),
        chain=data.get("chain", []),
        final_facts=data.get("final_facts", {}),
        raw=data,
    )
