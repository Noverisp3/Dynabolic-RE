#!/usr/bin/env bash
#
# Smoke tests for build/dynabolic_solver. Runs the solver against a few
# canned problems and verifies the output JSON contains the expected fields.
# Uses only POSIX tools + grep so it works without jq.
#
# Run from repo root:
#   bash tests/test_solver.sh
#
# Exit code 0 on pass, 1 on first failure.

set -euo pipefail

SOLVER="${SOLVER:-./build/dynabolic_solver}"

if [[ ! -x "$SOLVER" ]]; then
    echo "FAIL: $SOLVER not found or not executable. Run 'make' first." >&2
    exit 1
fi

pass=0
fail=0

run_case() {
    local name="$1"
    local input="$2"
    local expect_pattern="$3"

    # The solver exits 1 on input/parse errors and we still want to inspect
    # stdout in those cases, so disable -e for the capture.
    local out
    set +e
    out="$(printf '%s' "$input" | "$SOLVER")"
    set -e

    if grep -qE "$expect_pattern" <<<"$out"; then
        printf '  [OK]   %s\n' "$name"
        pass=$((pass + 1))
    else
        printf '  [FAIL] %s\n  expected pattern: %s\n  got: %s\n' \
            "$name" "$expect_pattern" "$out" >&2
        fail=$((fail + 1))
    fi
}

echo "Testing $SOLVER..."

run_case "single-step rule fires" \
'{"facts": [{"name": "is_bird", "value": true}, {"name": "has_wings", "value": true}],
  "rules": [{"name": "r1", "antecedents": ["is_bird", "has_wings"], "consequent": "can_fly"}],
  "goal": "can_fly"}' \
'"derived":true.*"goal_value":true.*"ok":true'

run_case "three-hop chain fires in order" \
'{"facts": [{"name": "is_mammal", "value": true}, {"name": "lives_in_water", "value": true}],
  "rules": [
    {"name": "r1", "antecedents": ["is_mammal", "lives_in_water"], "consequent": "aquatic_mammal"},
    {"name": "r2", "antecedents": ["aquatic_mammal"], "consequent": "dolphin_or_whale"},
    {"name": "r3", "antecedents": ["dolphin_or_whale"], "consequent": "surfaces_for_air"}
  ],
  "goal": "surfaces_for_air"}' \
'"step":3.*"derived":true'

run_case "rule does not fire when antecedent missing" \
'{"facts": [{"name": "x", "value": true}],
  "rules": [{"name": "r1", "antecedents": ["x", "y"], "consequent": "z"}],
  "goal": "z"}' \
'"chain":\[\].*"derived":false'

run_case "no goal field omits derived/goal_value" \
'{"facts": [{"name": "a", "value": true}],
  "rules": [{"name": "r1", "antecedents": ["a"], "consequent": "b"}]}' \
'"chain":\[.*"concluded":"b".*\].*"final_facts":\{.*"a":true.*"b":true.*\}.*"ok":true'

run_case "malformed JSON yields error" \
'{ this is not json }' \
'"error":"JSON parse error.*"ok":false'

run_case "empty stdin yields error" \
'' \
'"error":"empty input.*"ok":false'

# ---------------------------------------------------------------------------
# V2 schema: negation-as-failure + rule priorities + value=false consequents.
# ---------------------------------------------------------------------------

# Canonical Tweety: birds fly UNLESS penguin. Both rules have satisfied
# antecedents; the higher-priority "penguins_dont_fly" wins on can_fly.
run_case "v2: classical Tweety — priority resolves conflict" \
'{"facts": [{"name": "is_bird", "value": true}, {"name": "is_penguin", "value": true}],
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
  "goal": "can_fly"}' \
'"derived":true.*"final_facts":\{"can_fly":false.*"goal_value":false'

# Default fires when the exception doesn't apply.
run_case "v2: default fires when exception absent" \
'{"facts": [{"name": "is_bird", "value": true}],
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
  "goal": "can_fly"}' \
'"derived":true.*"final_facts":\{"can_fly":true.*"goal_value":true'

# NAF antecedent: "birds fly if NOT known to be penguin". Without is_penguin
# in facts, NAF on is_penguin succeeds and the rule fires.
run_case "v2: NAF antecedent fires when target not asserted" \
'{"facts": [{"name": "is_bird", "value": true}],
  "rules": [{"name": "naf_default",
             "antecedents": [{"name": "is_bird",    "value": true},
                             {"name": "is_penguin", "value": false}],
             "consequent":  {"name": "can_fly", "value": true}}],
  "goal": "can_fly"}' \
'"derived":true.*"goal_value":true'

# Same NAF rule, but is_penguin=true now → NAF antecedent fails → rule
# doesn't fire → goal not derived.
run_case "v2: NAF antecedent blocks when target asserted" \
'{"facts": [{"name": "is_bird", "value": true}, {"name": "is_penguin", "value": true}],
  "rules": [{"name": "naf_default",
             "antecedents": [{"name": "is_bird",    "value": true},
                             {"name": "is_penguin", "value": false}],
             "consequent":  {"name": "can_fly", "value": true}}],
  "goal": "can_fly"}' \
'"chain":\[\].*"derived":false'

# Two rules at equal priority disagree on the same predicate → tie_skipped
# recorded, fact NOT in final_facts.
run_case "v2: equal-priority disagreement is tie-skipped" \
'{"facts": [{"name": "p", "value": true}, {"name": "q", "value": true}],
  "rules": [
    {"name": "r_true",
     "antecedents": [{"name": "p", "value": true}],
     "consequent":  {"name": "x", "value": true},
     "priority":    5},
    {"name": "r_false",
     "antecedents": [{"name": "q", "value": true}],
     "consequent":  {"name": "x", "value": false},
     "priority":    5}
  ],
  "goal": "x"}' \
'"derived":false.*"tie_skipped":\[\{"predicate":"x","priority":5'

# Higher-priority rule overrides earlier lower-priority firing in the
# SAME chain — the chain records both, the second carries overrides_previous.
# This happens when the higher rule's antecedent gets derived by an
# intermediate step.
run_case "v2: higher priority overrides earlier firing" \
'{"facts": [{"name": "a", "value": true}],
  "rules": [
    {"name": "low",
     "antecedents": [{"name": "a", "value": true}],
     "consequent":  {"name": "x", "value": true},
     "priority":    0},
    {"name": "derive_b",
     "antecedents": [{"name": "a", "value": true}],
     "consequent":  {"name": "b", "value": true},
     "priority":    0},
    {"name": "high",
     "antecedents": [{"name": "b", "value": true}],
     "consequent":  {"name": "x", "value": false},
     "priority":    10}
  ],
  "goal": "x"}' \
'"overrides_previous":true.*"final_facts":\{[^}]*"x":false'

# value:false in consequent works for a single rule (no conflict).
run_case "v2: rule can set value=false directly" \
'{"facts": [{"name": "is_penguin", "value": true}],
  "rules": [{"name": "r1",
             "antecedents": [{"name": "is_penguin", "value": true}],
             "consequent":  {"name": "can_fly", "value": false}}],
  "goal": "can_fly"}' \
'"derived":true.*"final_facts":\{"can_fly":false.*"goal_value":false'

echo
echo "Results: $pass passed, $fail failed"
[[ "$fail" -eq 0 ]]
