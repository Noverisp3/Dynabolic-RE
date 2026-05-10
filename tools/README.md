# dynabolic_solver

A JSON-in / JSON-out forward-chaining solver. Wraps `LogicProcessor` +
`RuleNode` and adds derivation-chain tracking so the output is something a
Python orchestrator (or LLM) can consume directly.

## Build

```bash
make tools           # builds build/dynabolic_solver
# or just: make
```

## Use

```bash
./build/dynabolic_solver < problem.json > result.json
```

Reads a single JSON object from stdin, writes a single JSON object to stdout,
status messages (none today) to stderr. Exit code `0` on success, `1` on
input or solver error.

## Input schema

```json
{
  "facts": [
    {"name": "is_bird", "value": true},
    {"name": "has_wings", "value": true}
  ],
  "rules": [
    {
      "name": "r1",
      "antecedents": ["is_bird", "has_wings"],
      "consequent": "can_fly",
      "weight": 1.0
    }
  ],
  "goal": "can_fly"
}
```

- `facts[]` — initial facts. `value` is required and must be a bool.
- `rules[]` — forward-chaining rules. Antecedents are conjunctive (all must be
  true for the rule to fire). `weight` is optional and stored on the rule but
  does not currently affect firing semantics.
- `goal` — optional. If present, the result reports whether the goal was
  derived and, if so, its boolean value.

## Output schema

Success:

```json
{
  "ok":          true,
  "derived":     true,
  "goal_value":  true,
  "chain": [
    {
      "step":          1,
      "rule":          "r1",
      "fired_because": ["is_bird", "has_wings"],
      "concluded":     "can_fly",
      "value":         true
    }
  ],
  "final_facts": {
    "is_bird":   true,
    "has_wings": true,
    "can_fly":   true
  }
}
```

`derived` and `goal_value` are only present if the input had a `goal` field.
`chain` lists rule firings in the order they fired during the fixed-point
loop, so a downstream verbaliser can render them as a step-by-step
explanation.

Error:

```json
{ "ok": false, "error": "..." }
```

## V1 limitations

- Antecedents are positive only — no negation. `"!is_bird"` is treated as the
  literal predicate name, not a negated antecedent.
- Rules conclude with `value: true` only.
- Each rule fires at most once per solve.
- The bundled `JsonParser` does not escape special characters on serialise,
  so callers should keep predicate names plain (alphanumeric + underscores).
  This is fine for LLM-extracted predicates but may bite if you hand-craft
  payloads with quotes or backslashes.

## Examples

Three-hop chain:

```bash
echo '{
  "facts": [{"name": "is_mammal", "value": true},
            {"name": "lives_in_water", "value": true}],
  "rules": [
    {"name": "r1", "antecedents": ["is_mammal", "lives_in_water"],
     "consequent": "is_aquatic_mammal"},
    {"name": "r2", "antecedents": ["is_aquatic_mammal"],
     "consequent": "is_dolphin_whale_or_seal"},
    {"name": "r3", "antecedents": ["is_dolphin_whale_or_seal"],
     "consequent": "needs_to_surface_for_air"}
  ],
  "goal": "needs_to_surface_for_air"
}' | ./build/dynabolic_solver
```

Returns a `chain` of three steps, `derived: true`, `goal_value: true`.

## Why a separate tool

Decouples the Python orchestrator from C++ build / linking concerns. The
orchestrator just shells out via `subprocess.run(["./build/dynabolic_solver"],
input=problem_json, ...)`. When pybind11 bindings land later, this same code
path is what they'll wrap.
