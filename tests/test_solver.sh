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

echo
echo "Results: $pass passed, $fail failed"
[[ "$fail" -eq 0 ]]
