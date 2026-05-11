# dynabolic_solver for Dynabolic-RE

A JSON-in / JSON-out forward-chaining solver. Wraps `LogicProcessor` +
`RuleNode` and adds prioritised defeasible reasoning with negation-as-failure
plus derivation-chain tracking so the output is something a Python
orchestrator (or the RE orchestrator) can consume directly.

## Build

```bash
make tools           # builds build/dynabolic_solver
# or just: make
```

## Use

```bash
./build/dynabolic_solver < problem.json > result.json
```

Reads a single JSON object from stdin, writes a single JSON object to stdout.
Exit code `0` on success, `1` on input or solver error.

## Input schema (V3)

V3 introduces ground first-order atoms — facts, antecedents, consequents,
and goals can carry an `args` list of string constants:

```json
{
  "facts": [
    {"name": "parent", "args": ["alice", "bob"], "value": true},
    {"name": "parent", "args": ["bob", "charlie"], "value": true}
  ],
  "rules": [
    {
      "name": "grandparent_alice_charlie",
      "antecedents": [
        {"name": "parent", "args": ["alice", "bob"], "value": true},
        {"name": "parent", "args": ["bob", "charlie"], "value": true}
      ],
      "consequent": {"name": "grandparent", "args": ["alice", "charlie"], "value": true}
    }
  ],
  "goal": {"name": "grandparent", "args": ["alice", "charlie"]}
}
```

Internally the solver canonicalises `(name, args)` to a single string key
(`parent(alice,bob)`, `grandparent(alice,charlie)`) and the existing
reasoner is unchanged — facts are still indexed by string. Variables and
unification are reserved for PR-10; PR-9 supports ground constants only.

`args` is optional; omitting it (or passing `[]`) yields the V2 0-arity
behaviour. Arg entries must be non-empty strings without `(`, `)`, or `,`.

The pre-PR-9 V2 schema (no `args`, no first-order atoms) still works:

```json
{
  "facts": [
    {"name": "is_bird",    "value": true},
    {"name": "is_penguin", "value": true}
  ],
  "rules": [
    {
      "name":        "default_birds_fly",
      "antecedents": [{"name": "is_bird", "value": true}],
      "consequent":  {"name": "can_fly", "value": true},
      "priority":    0
    },
    {
      "name":        "penguins_dont_fly",
      "antecedents": [{"name": "is_penguin", "value": true}],
      "consequent":  {"name": "can_fly", "value": false},
      "priority":    10
    }
  ],
  "goal": "can_fly"
}
```

### Antecedents

Each antecedent is one of:

- A string (V1, back-compat): `"is_bird"` is shorthand for
  `{"name":"is_bird","value":true}`.
- A `{name, value}` object (V2): explicit name and required value.

An antecedent with `value: false` is **negation-as-failure (NAF)** under
closed-world semantics: it is satisfied iff `name` is *not* known to be
true. "Not known to be true" means the predicate is absent from the fact
set OR present with `value: false`.

### Consequents

Each consequent is one of:

- A string (V1, back-compat): `"can_fly"` is shorthand for
  `{"name":"can_fly","value":true}`.
- A `{name, value}` object (V2): can now set a predicate to `false`.

### Priority

`priority` (int, default `0`) controls conflict resolution. When two rules
want to set the same predicate to different values, the rule with the
strictly higher priority wins. Equal priority with disagreement is a tie:
neither rule fires for that predicate; the tie is recorded under
`tie_skipped`.

### Goal

`goal` is an optional predicate name. If present, the result reports
`derived` (whether the goal predicate is in `final_facts`) and `goal_value`
(its boolean value).

## Output schema

Success:

```json
{
  "ok":          true,
  "derived":     true,
  "goal_value":  false,
  "chain": [
    {
      "step":               1,
      "rule":               "penguins_dont_fly",
      "fired_because":      ["is_penguin=true"],
      "concluded":          "can_fly",
      "value":              false,
      "priority":           10,
      "overrides_previous": true
    }
  ],
  "tie_skipped": [
    {"predicate": "x", "priority": 5,
     "rules": [{"name": "r1", "value": true},
               {"name": "r2", "value": false}]}
  ],
  "final_facts": {
    "is_bird":    true,
    "is_penguin": true,
    "can_fly":    false
  },
  "oscillated":  false
}
```

- `chain` is the ordered list of rule firings. Each entry's
  `fired_because` is the literal antecedent list in the form
  `"name=true"` or `"name=false"`. `priority` mirrors the rule's priority.
  `overrides_previous: true` is set when this rule's consequent flipped a
  previously-derived value (only on the first such occurrence per
  consequent).
- `tie_skipped` (omitted when empty) lists `{predicate, priority, rules[]}`
  entries for equal-priority disagreements that could not be resolved.
- `final_facts` is the post-reasoning state.
- `oscillated: true` is set if the fixed-point loop hit the iteration cap
  (1024). This indicates a non-monotonic NAF interaction with no stable
  model; the chain is still returned but may be incomplete.
- `derived` / `goal_value` are only present when `goal` was supplied.

Error:

```json
{ "ok": false, "error": "..." }
```

## V1 back-compat

V1 payloads (string antecedents, string consequents, no `priority`) continue
to work unchanged — they are interpreted as `{name, value:true}` for both
antecedent and consequent, with `priority = 0`. The PR-6 test suite still
passes against the V2 binary.

## V2 limitations

- Each rule fires at most once and is recorded once in `chain`. Suppressed
  losing rules (e.g. the default in Tweety) are not in the chain. If you
  want to see *all* satisfied rules including the losers, that's a future
  extension.
- NAF is closed-world only. There's no third "unknown" value distinct from
  `false`.
- Predicate names should stay alphanumeric + underscores. The bundled
  `JsonParser` does not escape special characters on serialise.

## Examples

Three-hop chain (V1 syntax, still works):

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

Classical Tweety (V2 syntax, priority-resolved exception):

```bash
echo '{
  "facts": [{"name": "is_bird", "value": true},
            {"name": "is_penguin", "value": true}],
  "rules": [
    {"name": "default_birds_fly",
     "antecedents": [{"name": "is_bird", "value": true}],
     "consequent":  {"name": "can_fly", "value": true},
     "priority":    0},
    {"name": "penguins_dont_fly",
     "antecedents": [{"name": "is_penguin", "value": true}],
     "consequent":  {"name": "can_fly", "value": false},
     "priority":    10}
  ],
  "goal": "can_fly"
}' | ./build/dynabolic_solver
```

Returns `chain` with one firing (`penguins_dont_fly` at priority 10),
`final_facts.can_fly = false`, `goal_value: false`.

## Why a separate tool

Decouples the Python orchestrator from C++ build / linking concerns. The
orchestrator just shells out via `subprocess.run(["./build/dynabolic_solver"],
input=problem_json, ...)`. When pybind11 bindings land later, this same code
path is what they'll wrap.
