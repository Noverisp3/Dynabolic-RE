// dynabolic_solver — JSON-in / JSON-out forward-chaining solver.
//
// Reads a problem description from stdin (single JSON object) and writes a
// result JSON to stdout. Designed to be the C++ end of a Python orchestrator
// that talks to an LLM for fact extraction and answer verbalisation. The
// solver does prioritised defeasible reasoning with negation-as-failure
// (closed-world semantics) and records every rule firing in a derivation
// chain.
//
// Input schema (V2; V1 strings still accepted for back-compat):
//   {
//     "facts": [{"name": "is_bird", "value": true}, ...],
//     "rules": [
//       {
//         "name":        "default_birds_fly",
//         "antecedents": [{"name": "is_bird", "value": true}, ...],
//         "consequent":  {"name": "can_fly", "value": true},
//         "priority":    0           // optional, default 0
//       }, ...
//     ],
//     "goal": "can_fly"          // optional; affects derived/goal_value
//   }
//
// Antecedent forms (mix freely):
//   "is_bird"                              -> {name:"is_bird", value:true}      (V1)
//   {"name":"is_bird", "value":true}                                            (V2)
//   {"name":"is_penguin", "value":false}   -> negation-as-failure: satisfied
//                                             iff is_penguin is NOT derivable
//                                             as true (closed-world).
//
// Consequent forms:
//   "can_fly"                              -> {name:"can_fly", value:true}      (V1)
//   {"name":"can_fly", "value":true}                                            (V2)
//   {"name":"can_fly", "value":false}      -> sets can_fly to false             (V2)
//
// Conflict resolution: when two rules want to set the same predicate name
// to different values, the strictly higher `priority` wins. Equal priority
// with disagreement -> neither fires for that predicate this iteration,
// recorded under `tie_skipped`.
//
// Termination: fixed-point iteration with a hard cap (kMaxIterations) to
// detect NAF-induced oscillation. If the cap is hit, we return what we have
// so far plus `oscillated: true` so the caller knows the chain is partial.
//
// Output schema:
//   {
//     "ok":          true,
//     "derived":     true,                       // only if goal given
//     "goal_value":  true,                       // only if goal in final_facts
//     "chain": [
//       {"step":1, "rule":"r1",
//        "fired_because":["is_bird=true"],
//        "concluded":"can_fly", "value":true,
//        "priority":0,
//        "overrides_previous": false}, ...
//     ],
//     "tie_skipped": [                          // omitted if empty
//       {"predicate":"can_fly", "priority":5,
//        "rules":[{"name":"r1","value":true},{"name":"r2","value":false}]}
//     ],
//     "final_facts": {"is_bird": true, "can_fly": false, ...},
//     "oscillated":  false                      // omitted if false
//   }
//
// On error: {"ok": false, "error": "..."}
// Exit code: 0 on solver success, 1 on error.

#include "graph_node.hpp"
#include "json_parser.hpp"
#include "reasoning_engine.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using dynabolic::JsonParser;
using JsonValue = JsonParser::JsonValue;
using JsonValuePtr = std::shared_ptr<JsonValue>;

constexpr int kMaxIterations = 1024;

struct Antecedent {
    std::string name;
    bool value;
};

struct Consequent {
    std::string name;
    bool value;
};

struct ParsedRule {
    std::string name;
    std::vector<Antecedent> antecedents;
    Consequent consequent;
    int priority = 0;
};

struct ParsedProblem {
    std::unordered_map<std::string, bool> facts;
    std::vector<ParsedRule> rules;
    std::string goal;
    bool has_goal = false;
};

struct InferenceStep {
    int step;
    std::string rule_name;
    std::vector<std::string> fired_because;   // "is_bird=true" / "is_penguin=false"
    std::string concluded;
    bool value;
    int priority;
    bool overrides_previous = false;
};

struct TieSkip {
    std::string predicate;
    int priority;
    std::vector<std::pair<std::string, bool>> rules;   // {rule_name, would_set_value}
};

// JSON helpers ------------------------------------------------------------

JsonValuePtr makeString(const std::string& s) {
    return std::make_shared<JsonValue>(s);
}
JsonValuePtr makeBool(bool b) {
    return std::make_shared<JsonValue>(b);
}
JsonValuePtr makeNumber(int n) {
    return std::make_shared<JsonValue>(n);
}
JsonValuePtr makeObject(const std::map<std::string, JsonValuePtr>& fields) {
    auto v = std::make_shared<JsonValue>();
    v->setObject(fields);
    return v;
}
JsonValuePtr makeArray(const std::vector<JsonValuePtr>& items) {
    auto v = std::make_shared<JsonValue>();
    v->setArray(items);
    return v;
}

const JsonValuePtr& requireField(const std::map<std::string, JsonValuePtr>& obj,
                                 const std::string& key,
                                 const std::string& context) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        throw std::runtime_error("missing field '" + key + "' in " + context);
    }
    return it->second;
}

void emitError(const std::string& message) {
    auto err = makeObject({
        {"ok",    makeBool(false)},
        {"error", makeString(message)},
    });
    std::cout << err->serialize() << std::endl;
}

// Parse an antecedent that may be a V1 string or a V2 {name, value} object.
Antecedent parseAntecedent(const JsonValuePtr& node) {
    if (!node) throw std::runtime_error("antecedent is null");
    if (node->getType() == JsonParser::STRING) {
        return Antecedent{node->asString(), true};
    }
    if (node->getType() != JsonParser::OBJECT) {
        throw std::runtime_error(
            "antecedent must be a string or {name, value} object");
    }
    const auto& obj = node->asObject();
    const auto& name = requireField(obj, "name", "antecedent");
    const auto& value = requireField(obj, "value", "antecedent");
    if (name->getType() != JsonParser::STRING) {
        throw std::runtime_error("antecedent.name must be a string");
    }
    if (value->getType() != JsonParser::BOOLEAN) {
        throw std::runtime_error("antecedent.value must be a boolean");
    }
    return Antecedent{name->asString(), value->asBool()};
}

// Parse a consequent that may be a V1 string or a V2 {name, value} object.
Consequent parseConsequent(const JsonValuePtr& node) {
    if (!node) throw std::runtime_error("consequent is null");
    if (node->getType() == JsonParser::STRING) {
        return Consequent{node->asString(), true};
    }
    if (node->getType() != JsonParser::OBJECT) {
        throw std::runtime_error(
            "consequent must be a string or {name, value} object");
    }
    const auto& obj = node->asObject();
    const auto& name = requireField(obj, "name", "consequent");
    const auto& value = requireField(obj, "value", "consequent");
    if (name->getType() != JsonParser::STRING) {
        throw std::runtime_error("consequent.name must be a string");
    }
    if (value->getType() != JsonParser::BOOLEAN) {
        throw std::runtime_error("consequent.value must be a boolean");
    }
    return Consequent{name->asString(), value->asBool()};
}

// Input parsing -----------------------------------------------------------

ParsedProblem parseProblem(const JsonValuePtr& root) {
    if (!root || root->getType() != JsonParser::OBJECT) {
        throw std::runtime_error("top-level value must be a JSON object");
    }
    const auto& top = root->asObject();
    ParsedProblem out;

    // facts
    auto facts_it = top.find("facts");
    if (facts_it != top.end()) {
        const auto& facts_value = facts_it->second;
        if (facts_value->getType() != JsonParser::ARRAY) {
            throw std::runtime_error("'facts' must be an array");
        }
        for (const auto& fact_node : facts_value->asArray()) {
            if (fact_node->getType() != JsonParser::OBJECT) {
                throw std::runtime_error("each fact must be an object");
            }
            const auto& fact_obj = fact_node->asObject();
            const auto& name = requireField(fact_obj, "name", "fact");
            const auto& value = requireField(fact_obj, "value", "fact");
            if (name->getType() != JsonParser::STRING) {
                throw std::runtime_error("fact.name must be a string");
            }
            if (value->getType() != JsonParser::BOOLEAN) {
                throw std::runtime_error("fact.value must be a boolean");
            }
            out.facts[name->asString()] = value->asBool();
        }
    }

    // rules
    auto rules_it = top.find("rules");
    if (rules_it != top.end()) {
        const auto& rules_value = rules_it->second;
        if (rules_value->getType() != JsonParser::ARRAY) {
            throw std::runtime_error("'rules' must be an array");
        }
        for (const auto& rule_node : rules_value->asArray()) {
            if (rule_node->getType() != JsonParser::OBJECT) {
                throw std::runtime_error("each rule must be an object");
            }
            const auto& rule_obj = rule_node->asObject();
            const auto& name = requireField(rule_obj, "name", "rule");
            const auto& antecedents = requireField(rule_obj, "antecedents", "rule");
            const auto& consequent = requireField(rule_obj, "consequent", "rule");
            if (name->getType() != JsonParser::STRING) {
                throw std::runtime_error("rule.name must be a string");
            }
            if (antecedents->getType() != JsonParser::ARRAY) {
                throw std::runtime_error("rule.antecedents must be an array");
            }

            ParsedRule rule;
            rule.name = name->asString();
            rule.antecedents.reserve(antecedents->asArray().size());
            for (const auto& a : antecedents->asArray()) {
                rule.antecedents.push_back(parseAntecedent(a));
            }
            rule.consequent = parseConsequent(consequent);

            auto pri_it = rule_obj.find("priority");
            if (pri_it != rule_obj.end()) {
                if (pri_it->second->getType() != JsonParser::NUMBER) {
                    throw std::runtime_error("rule.priority must be a number");
                }
                rule.priority = static_cast<int>(pri_it->second->asNumber());
            }

            out.rules.push_back(std::move(rule));
        }
    }

    // goal (optional string)
    auto goal_it = top.find("goal");
    if (goal_it != top.end()) {
        if (goal_it->second->getType() != JsonParser::STRING) {
            throw std::runtime_error("'goal' must be a string");
        }
        out.goal = goal_it->second->asString();
        out.has_goal = !out.goal.empty();
    }

    return out;
}

// Reasoning ---------------------------------------------------------------
//
// Antecedent semantics (closed-world / NAF):
//   {name, value:true}  -> satisfied iff facts[name] is present and true.
//   {name, value:false} -> satisfied iff facts[name] is absent, OR present
//                          and false. I.e. the engine has no evidence the
//                          predicate is true.
bool antecedentSatisfied(const Antecedent& a,
                         const std::unordered_map<std::string, bool>& facts) {
    auto it = facts.find(a.name);
    bool known_true = (it != facts.end() && it->second);
    return a.value ? known_true : !known_true;
}

bool allAntecedentsSatisfied(const ParsedRule& rule,
                             const std::unordered_map<std::string, bool>& facts) {
    for (const auto& a : rule.antecedents) {
        if (!antecedentSatisfied(a, facts)) return false;
    }
    return true;
}

std::string formatAntecedent(const Antecedent& a) {
    return a.name + (a.value ? "=true" : "=false");
}

struct ReasonOutcome {
    std::unordered_map<std::string, bool> final_facts;
    std::vector<InferenceStep> chain;
    std::vector<TieSkip> ties;
    bool oscillated = false;
};

// One iteration of prioritised forward chaining.
//
// Steps:
//   1. Collect every (not-yet-fired) rule whose antecedents are satisfied.
//   2. Group activations by consequent name.
//   3. Per consequent: pick the strictly-highest-priority value. If the top
//      priority tier has disagreement, record a tie-skip and don't apply
//      anything to that consequent this iteration.
//   4. If a winning activation would change the fact, apply it and record
//      it in the chain.
//   5. Mark every winning rule as fired so we don't re-record it.
//
// Returns true iff anything changed (a fact got set/overridden or a rule
// got recorded for the first time).
bool runIteration(const std::vector<ParsedRule>& rules,
                  std::unordered_map<std::string, bool>& facts,
                  std::unordered_set<std::string>& fired_rules,
                  std::unordered_set<std::string>& reported_ties,
                  std::vector<InferenceStep>& chain,
                  std::vector<TieSkip>& ties,
                  int& step_counter) {
    // Collect activations from EVERY rule with satisfied antecedents,
    // regardless of whether it's already fired in a previous iteration.
    // This is essential for priority semantics: a high-priority rule that
    // fired earlier still has to keep "voting" or a later-firing lower-
    // priority rule on the same predicate would silently override it.
    // The `fired_rules` set only governs chain recording.
    std::unordered_map<std::string, std::vector<size_t>> by_consequent;
    for (size_t i = 0; i < rules.size(); ++i) {
        const auto& rule = rules[i];
        if (!allAntecedentsSatisfied(rule, facts)) continue;
        by_consequent[rule.consequent.name].push_back(i);
    }

    bool any_change = false;

    for (auto& kv : by_consequent) {
        const std::string& cname = kv.first;
        const auto& indices = kv.second;

        // Find max priority among candidates.
        int max_p = rules[indices.front()].priority;
        for (size_t idx : indices) {
            max_p = std::max(max_p, rules[idx].priority);
        }

        // Collect rules at top tier.
        std::vector<size_t> top;
        for (size_t idx : indices) {
            if (rules[idx].priority == max_p) top.push_back(idx);
        }

        // Detect tie with disagreement at top tier.
        bool first_value = rules[top.front()].consequent.value;
        bool disagreement = false;
        for (size_t idx : top) {
            if (rules[idx].consequent.value != first_value) {
                disagreement = true;
                break;
            }
        }
        if (disagreement) {
            // Only report this tie once per (predicate, priority) pair.
            std::string tie_key = cname + "@" + std::to_string(max_p);
            if (!reported_ties.count(tie_key)) {
                TieSkip ts;
                ts.predicate = cname;
                ts.priority = max_p;
                for (size_t idx : top) {
                    ts.rules.push_back({rules[idx].name, rules[idx].consequent.value});
                }
                ties.push_back(std::move(ts));
                reported_ties.insert(tie_key);
                any_change = true;
            }
            continue;
        }

        // Top tier agrees -> winner is first one in `top`.
        const ParsedRule& winner_rule = rules[top.front()];
        bool winner_value = winner_rule.consequent.value;
        bool already_present = facts.count(cname) > 0;
        bool old_value = already_present ? facts[cname] : false;
        bool needs_change = !already_present || (old_value != winner_value);

        if (needs_change) {
            facts[cname] = winner_value;
            any_change = true;
        }

        // Record any not-yet-recorded rule at the top tier. Only the first
        // newly-recorded rule when a previously-set value gets flipped is
        // marked as overrides_previous.
        bool overrides = needs_change && already_present;
        for (size_t idx : top) {
            const ParsedRule& r = rules[idx];
            if (fired_rules.count(r.name)) continue;
            std::vector<std::string> ants;
            ants.reserve(r.antecedents.size());
            for (const auto& a : r.antecedents) ants.push_back(formatAntecedent(a));
            chain.push_back(InferenceStep{
                ++step_counter,
                r.name,
                std::move(ants),
                cname,
                winner_value,
                r.priority,
                overrides,
            });
            overrides = false;
            fired_rules.insert(r.name);
            any_change = true;
        }
    }
    return any_change;
}

ReasonOutcome runReasoner(const ParsedProblem& problem) {
    ReasonOutcome out;
    out.final_facts = problem.facts;
    std::unordered_set<std::string> fired_rules;
    std::unordered_set<std::string> reported_ties;
    int step_counter = 0;

    for (int iter = 0; iter < kMaxIterations; ++iter) {
        bool changed = runIteration(problem.rules,
                                    out.final_facts,
                                    fired_rules,
                                    reported_ties,
                                    out.chain,
                                    out.ties,
                                    step_counter);
        if (!changed) return out;
    }
    out.oscillated = true;
    return out;
}

// Output construction -----------------------------------------------------

JsonValuePtr buildResult(const ParsedProblem& problem,
                         const ReasonOutcome& outcome) {
    std::vector<JsonValuePtr> chain_array;
    chain_array.reserve(outcome.chain.size());
    for (const auto& s : outcome.chain) {
        std::vector<JsonValuePtr> ants;
        ants.reserve(s.fired_because.size());
        for (const auto& a : s.fired_because) ants.push_back(makeString(a));
        std::map<std::string, JsonValuePtr> fields = {
            {"step",          makeNumber(s.step)},
            {"rule",          makeString(s.rule_name)},
            {"fired_because", makeArray(ants)},
            {"concluded",     makeString(s.concluded)},
            {"value",         makeBool(s.value)},
            {"priority",      makeNumber(s.priority)},
        };
        if (s.overrides_previous) {
            fields["overrides_previous"] = makeBool(true);
        }
        chain_array.push_back(makeObject(fields));
    }

    std::map<std::string, JsonValuePtr> final_obj;
    for (const auto& kv : outcome.final_facts) {
        final_obj[kv.first] = makeBool(kv.second);
    }

    std::map<std::string, JsonValuePtr> result_fields = {
        {"ok",          makeBool(true)},
        {"chain",       makeArray(chain_array)},
        {"final_facts", makeObject(final_obj)},
    };

    if (!outcome.ties.empty()) {
        std::vector<JsonValuePtr> ties_array;
        ties_array.reserve(outcome.ties.size());
        for (const auto& t : outcome.ties) {
            std::vector<JsonValuePtr> rule_entries;
            rule_entries.reserve(t.rules.size());
            for (const auto& r : t.rules) {
                rule_entries.push_back(makeObject({
                    {"name",  makeString(r.first)},
                    {"value", makeBool(r.second)},
                }));
            }
            ties_array.push_back(makeObject({
                {"predicate", makeString(t.predicate)},
                {"priority",  makeNumber(t.priority)},
                {"rules",     makeArray(rule_entries)},
            }));
        }
        result_fields["tie_skipped"] = makeArray(ties_array);
    }

    if (outcome.oscillated) {
        result_fields["oscillated"] = makeBool(true);
    }

    if (problem.has_goal) {
        auto it = outcome.final_facts.find(problem.goal);
        bool derived = (it != outcome.final_facts.end());
        result_fields["derived"] = makeBool(derived);
        if (derived) {
            result_fields["goal_value"] = makeBool(it->second);
        }
    }

    return makeObject(result_fields);
}

}  // namespace

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    std::ostringstream buf;
    buf << std::cin.rdbuf();
    const std::string input = buf.str();
    if (input.empty()) {
        emitError("empty input on stdin (expected a JSON object)");
        return 1;
    }

    JsonValuePtr root;
    try {
        root = JsonValue::parse(input);
    } catch (const std::exception& e) {
        emitError(std::string("JSON parse error: ") + e.what());
        return 1;
    }

    ParsedProblem problem;
    try {
        problem = parseProblem(root);
    } catch (const std::exception& e) {
        emitError(std::string("problem schema error: ") + e.what());
        return 1;
    }

    ReasonOutcome outcome;
    try {
        outcome = runReasoner(problem);
    } catch (const std::exception& e) {
        emitError(std::string("solver error: ") + e.what());
        return 1;
    }

    auto result = buildResult(problem, outcome);
    std::cout << result->serialize() << std::endl;
    return 0;
}
