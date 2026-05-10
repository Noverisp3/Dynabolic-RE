// dynabolic_solver — JSON-in / JSON-out forward-chaining solver.
//
// Reads a problem description from stdin (single JSON object) and writes a
// result JSON to stdout. Designed to be the C++ end of a Python orchestrator
// that talks to an LLM for fact extraction and answer verbalisation. The
// solver itself does no reasoning the engine doesn't already do — it just
// drives LogicProcessor + RuleNode and records which rule fired with which
// antecedents to produce a human/LLM-readable derivation chain.
//
// Input schema:
//   {
//     "facts":  [{"name": "is_bird", "value": true}, ...],
//     "rules":  [{"name": "r1",
//                 "antecedents": ["is_bird", "has_wings"],
//                 "consequent": "can_fly",
//                 "weight": 1.0}, ...],
//     "goal":   "can_fly"          // optional; affects "derived"/"goal_value"
//   }
//
// Output schema:
//   {
//     "ok":          true,
//     "derived":     true,                  // present only if goal given
//     "goal_value":  true,                  // present only if goal derived
//     "chain": [
//       {"step": 1, "rule": "r1",
//        "fired_because": ["is_bird", "has_wings"],
//        "concluded": "can_fly", "value": true}, ...
//     ],
//     "final_facts": {"is_bird": true, ..., "can_fly": true}
//   }
//
// On error:
//   {"ok": false, "error": "..."}
//
// V1 limitations (callers should sanitise extracted output to fit):
//   - Antecedents are positive only (no negation).
//   - Rules conclude with value=true only.
//   - Strings must be plain (no embedded quotes / backslashes); the bundled
//     JsonParser does not escape on serialise.
//
// Exit code: 0 on solver success (regardless of "derived"), 1 on error.

#include "graph_node.hpp"
#include "json_parser.hpp"
#include "reasoning_engine.hpp"

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

struct ParsedProblem {
    std::unordered_map<std::string, bool> facts;
    std::vector<std::shared_ptr<dynabolic::RuleNode>> rules;
    // Antecedent names live here (RuleNode keeps them private and only exposes
    // a count) so the chain tracker can name them in the output.
    std::unordered_map<std::string, std::vector<std::string>> antecedents_by_rule;
    std::string goal;             // empty if none requested
    bool has_goal = false;
};

struct InferenceStep {
    int step;
    std::string rule_name;
    std::vector<std::string> fired_because;
    std::string concluded;
    bool value;
};

// Small helpers -----------------------------------------------------------

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

// Throw on type mismatch with a useful message.
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
        {"ok", makeBool(false)},
        {"error", makeString(message)},
    });
    std::cout << err->serialize() << std::endl;
}

// Input parsing -----------------------------------------------------------

ParsedProblem parseProblem(const JsonValuePtr& root) {
    if (!root || root->getType() != JsonParser::OBJECT) {
        throw std::runtime_error("top-level value must be a JSON object");
    }
    const auto& top = root->asObject();
    ParsedProblem out;

    // facts: array of {name, value}
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

    // rules: array of {name, antecedents[], consequent, weight?}
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
            if (consequent->getType() != JsonParser::STRING) {
                throw std::runtime_error("rule.consequent must be a string");
            }

            std::vector<std::string> antecedent_names;
            antecedent_names.reserve(antecedents->asArray().size());
            for (const auto& a : antecedents->asArray()) {
                if (a->getType() != JsonParser::STRING) {
                    throw std::runtime_error("rule.antecedents entries must be strings");
                }
                antecedent_names.push_back(a->asString());
            }

            auto rule = std::make_shared<dynabolic::RuleNode>(name->asString());
            rule->setAntecedents(antecedent_names);
            rule->setConsequent(consequent->asString());

            auto weight_it = rule_obj.find("weight");
            if (weight_it != rule_obj.end() &&
                weight_it->second->getType() == JsonParser::NUMBER) {
                rule->setActivation(weight_it->second->asNumber());
            }

            out.antecedents_by_rule[rule->getId()] = std::move(antecedent_names);
            out.rules.push_back(rule);
        }
    }

    // goal: optional string
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

// Forward chaining with chain tracking ------------------------------------
//
// LogicProcessor::deduceFacts() does the same fixed-point loop but throws away
// the (rule_fired, antecedents, consequent) triple. We need that triple, so we
// run the loop here against a local fact map and a list of rules. Mirrors the
// existing deduction semantics: each rule fires at most once (its consequent
// becomes a fact and is then skipped on subsequent passes), positive
// antecedents only, value is always true.
std::vector<InferenceStep> forwardChain(
        std::unordered_map<std::string, bool>& facts,
        const std::vector<std::shared_ptr<dynabolic::RuleNode>>& rules,
        const std::unordered_map<std::string, std::vector<std::string>>& antecedents_by_rule) {
    std::vector<InferenceStep> chain;
    std::unordered_set<std::string> fired_rules;
    int step_counter = 0;

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& rule : rules) {
            const std::string& rule_name = rule->getId();
            if (fired_rules.count(rule_name)) continue;
            if (!rule->evaluate(facts)) continue;

            const std::string consequent = rule->getProperty("consequent");
            if (consequent.empty()) continue;

            std::vector<std::string> ants;
            auto a_it = antecedents_by_rule.find(rule_name);
            if (a_it != antecedents_by_rule.end()) ants = a_it->second;

            facts[consequent] = true;
            fired_rules.insert(rule_name);
            chain.push_back(InferenceStep{
                ++step_counter, rule_name, std::move(ants), consequent, true});
            changed = true;
        }
    }
    return chain;
}

// Output construction -----------------------------------------------------

JsonValuePtr buildResult(const ParsedProblem& problem,
                        const std::unordered_map<std::string, bool>& final_facts,
                        const std::vector<InferenceStep>& chain) {
    std::vector<JsonValuePtr> chain_array;
    chain_array.reserve(chain.size());
    for (const auto& s : chain) {
        std::vector<JsonValuePtr> ants;
        ants.reserve(s.fired_because.size());
        for (const auto& a : s.fired_because) ants.push_back(makeString(a));
        chain_array.push_back(makeObject({
            {"step",          makeNumber(s.step)},
            {"rule",          makeString(s.rule_name)},
            {"fired_because", makeArray(ants)},
            {"concluded",     makeString(s.concluded)},
            {"value",         makeBool(s.value)},
        }));
    }

    std::map<std::string, JsonValuePtr> final_obj;
    for (const auto& kv : final_facts) {
        final_obj[kv.first] = makeBool(kv.second);
    }

    std::map<std::string, JsonValuePtr> result_fields = {
        {"ok",          makeBool(true)},
        {"chain",       makeArray(chain_array)},
        {"final_facts", makeObject(final_obj)},
    };

    if (problem.has_goal) {
        auto it = final_facts.find(problem.goal);
        bool derived = (it != final_facts.end());
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

    // Slurp stdin.
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

    std::unordered_map<std::string, bool> facts = problem.facts;
    std::vector<InferenceStep> chain;
    try {
        chain = forwardChain(facts, problem.rules, problem.antecedents_by_rule);
    } catch (const std::exception& e) {
        emitError(std::string("solver error: ") + e.what());
        return 1;
    }

    auto result = buildResult(problem, facts, chain);
    std::cout << result->serialize() << std::endl;
    return 0;
}
