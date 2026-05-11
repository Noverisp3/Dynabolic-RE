#include "reasoning_engine.hpp"
#include "json_parser.hpp"
#include <algorithm>
#include <chrono>
#include <set>
#include <stack>
#include <cmath>
#include <queue>
#include <sstream>

namespace dynabolic {

// ChainOfLinks Implementation
void ChainOfLinks::addLink(std::shared_ptr<GraphLink> link) {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    links_.push_back(link);
}

void ChainOfLinks::removeLink(const std::string& link_id) {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    links_.erase(
        std::remove_if(links_.begin(), links_.end(),
            [&link_id](const std::shared_ptr<GraphLink>& link) {
                return link->getId() == link_id;
            }),
        links_.end()
    );
}

std::vector<std::shared_ptr<GraphNode>> ChainOfLinks::tracePath(
    std::shared_ptr<GraphNode> start,
    std::shared_ptr<GraphNode> end,
    int max_depth) {

    std::vector<std::shared_ptr<GraphNode>> path;
    // Use uint32_t unordered_set for O(1) visited checks
    std::unordered_set<uint32_t> visited;
    std::stack<std::pair<std::shared_ptr<GraphNode>, int>> stack;

    stack.push({start, 0});
    visited.insert(start->getNumericId());
    path.push_back(start);

    while (!stack.empty()) {
        auto [current, depth] = stack.top();
        stack.pop();

        if (current == end) {
            return path;
        }

        if (depth >= max_depth) continue;

        for (auto& link : current->getOutgoingLinks()) {
            auto neighbor = link->getTarget();
            if (visited.find(neighbor->getNumericId()) == visited.end()) {
                visited.insert(neighbor->getNumericId());
                path.push_back(neighbor);
                stack.push({neighbor, depth + 1});
            }
        }
    }

    return {}; // No path found
}

double ChainOfLinks::calculatePathStrength(const std::vector<std::shared_ptr<GraphNode>>& path) {
    if (path.size() < 2) return 0.0;

    double total_strength = 1.0;
    for (size_t i = 0; i < path.size() - 1; i++) {
        bool found_link = false;
        for (auto& link : path[i]->getOutgoingLinks()) {
            if (link->getTarget() == path[i + 1]) {
                total_strength *= link->getWeight();
                found_link = true;
                break;
            }
        }
        if (!found_link) return 0.0;
    }

    return total_strength / path.size();
}

void ChainOfLinks::propagateAlongChain(std::shared_ptr<GraphNode> start_node, double initial_activation) {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    start_node->setActivation(initial_activation);

    std::queue<std::shared_ptr<GraphNode>> queue;
    queue.push(start_node);

    while (!queue.empty()) {
        auto current = queue.front();
        queue.pop();

        current->propagate();

        for (auto& link : current->getOutgoingLinks()) {
            auto target = link->getTarget();
            if (target->getActivation() > 0.1) {
                queue.push(target);
            }
        }
    }
}

std::vector<std::shared_ptr<GraphLink>> ChainOfLinks::getCausalChain(std::shared_ptr<GraphNode> node) {
    std::vector<std::shared_ptr<GraphLink>> causal_links;

    std::lock_guard<std::mutex> lock(chain_mutex_);
    for (auto& link : links_) {
        if (link->getSource() == node && link->getType() == LinkType::CAUSAL) {
            causal_links.push_back(link);
        }
    }

    return causal_links;
}

std::vector<std::shared_ptr<GraphLink>> ChainOfLinks::getImplicationChain(std::shared_ptr<GraphNode> node) {
    std::vector<std::shared_ptr<GraphLink>> implication_links;

    std::lock_guard<std::mutex> lock(chain_mutex_);
    for (auto& link : links_) {
        if (link->getSource() == node && link->getType() == LinkType::IMPLIES) {
            implication_links.push_back(link);
        }
    }

    return implication_links;
}

void ChainOfLinks::optimizeChainWeights() {
    std::lock_guard<std::mutex> lock(chain_mutex_);

    // Simple weight optimization based on activation history
    for (auto& link : links_) {
        std::string link_key = link->getSource()->getName() + "->" + link->getTarget()->getName();
        auto it = activation_history_.find(link_key);
        if (it != activation_history_.end()) {
            double optimal_weight = std::min(1.0, it->second * 1.1);
            link->setWeight(optimal_weight);
        }
    }
}

std::string ChainOfLinks::serializeChain() const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    std::ostringstream oss;
    oss << "chain:";
    for (auto& link : links_) {
        oss << link->serialize() << ";";
    }
    return oss.str();
}

// LogicProcessor Implementation
void LogicProcessor::addFact(const std::string& fact, bool value, bool is_explicit) {
    std::lock_guard<std::mutex> lock(logic_mutex_);
    facts_[fact] = value;
    if (is_explicit) {
        explicit_facts_.insert(fact);
    }
}

void LogicProcessor::removeFact(const std::string& fact) {
    std::lock_guard<std::mutex> lock(logic_mutex_);
    facts_.erase(fact);
    explicit_facts_.erase(fact);
    justifications_.erase(fact);
    
    // After removing an explicit fact or a justification, we might need to trigger retraction
    // This is handled by retractFact or by the next deduceFacts cycle.
}

void LogicProcessor::retractFact(const std::string& fact) {
    std::lock_guard<std::mutex> lock(logic_mutex_);
    
    if (facts_.find(fact) == facts_.end()) return;

    // 1. Mark the fact as non-explicit (it's being retracted)
    explicit_facts_.erase(fact);
    
    // 2. Clear its justifications (it's being retracted)
    justifications_.erase(fact);

    // 3. Recursive retraction: find all derived facts that depend on this one
    bool changed = true;
    while (changed) {
        changed = false;
        
        std::vector<std::string> to_retract;
        for (auto& pair : justifications_) {
            const std::string& derived_fact = pair.first;
            
            // If it's already scheduled for retraction or is an explicit fact, skip
            if (facts_.find(derived_fact) == facts_.end() || explicit_facts_.count(derived_fact) > 0) {
                continue;
            }

            // Check if all rules supporting this derived fact are now invalid
            std::unordered_set<std::string>& rule_ids = pair.second;
            std::vector<std::string> invalid_rules;
            
            for (const std::string& rule_id : rule_ids) {
                // Find the rule to check its antecedents
                for (auto& rule : rules_) {
                    if (rule->getId() == rule_id) {
                        for (const auto& ant : rule->getAntecedents()) {
                            if (ant == fact || facts_.find(ant) == facts_.end()) {
                                invalid_rules.push_back(rule_id);
                                break;
                            }
                        }
                        break;
                    }
                }
            }
            
            // Remove invalid justifications
            for (const auto& rid : invalid_rules) {
                rule_ids.erase(rid);
            }
            
            // If no justifications left, retract the derived fact
            if (rule_ids.empty()) {
                to_retract.push_back(derived_fact);
            }
        }
        
        for (const auto& f : to_retract) {
            facts_.erase(f);
            justifications_.erase(f);
            changed = true;
        }
    }
    
    // Finally remove the target fact
    facts_.erase(fact);
}

bool LogicProcessor::hasFact(const std::string& fact) const {
    std::lock_guard<std::mutex> lock(logic_mutex_);
    return facts_.find(fact) != facts_.end();
}

bool LogicProcessor::getFact(const std::string& fact) const {
    std::lock_guard<std::mutex> lock(logic_mutex_);
    auto it = facts_.find(fact);
    return (it != facts_.end()) ? it->second : false;
}

void LogicProcessor::addRule(std::shared_ptr<RuleNode> rule) {
    std::lock_guard<std::mutex> lock(logic_mutex_);
    rules_.push_back(rule);
}

void LogicProcessor::removeRule(const std::string& rule_id) {
    std::lock_guard<std::mutex> lock(logic_mutex_);
    rules_.erase(
        std::remove_if(rules_.begin(), rules_.end(),
            [&rule_id](const std::shared_ptr<RuleNode>& rule) {
                return rule->getId() == rule_id;
            }),
        rules_.end()
    );
}

std::vector<std::string> LogicProcessor::deduceFacts() {
    std::lock_guard<std::mutex> lock(logic_mutex_);
    std::vector<std::string> new_facts;
    std::vector<std::string> retracted_facts;
    bool changed = true;

    while (changed) {
        changed = false;

        // 1. Retract facts that are no longer supported
        auto it = facts_.begin();
        while (it != facts_.end()) {
            const std::string& fact_name = it->first;
            if (explicit_facts_.count(fact_name) == 0 && justifications_[fact_name].empty()) {
                retracted_facts.push_back(fact_name);
                it = facts_.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }

        // 2. Try to derive new facts or refresh justifications
        for (auto& rule : rules_) {
            std::unordered_map<std::string, bool> fact_map;
            for (const auto& f : facts_) {
                fact_map[f.first] = f.second;
            }

            std::string consequent = rule->getProperty("consequent");
            
            // Bayesian rule firing: Rule only fires if confidence of antecedents exceeds threshold.
            // If BayesianProcessor is available, we use it to check fact acceptance.
            bool rule_satisfied = true;
            
            if (bayesian_processor_) {
                for (const auto& ant : rule->getAntecedents()) {
                    if (!bayesian_processor_->acceptFact(ant, firing_threshold_)) {
                        rule_satisfied = false;
                        break;
                    }
                }
            } else {
                // Fallback to legacy binary facts
                std::unordered_map<std::string, bool> rule_fact_map;
                for (const auto& f : facts_) {
                    rule_fact_map[f.first] = f.second;
                }
                rule_satisfied = rule->evaluate(rule_fact_map);
            }

            if (rule_satisfied) {
                // Rule is satisfied. Add justification.
                if (justifications_[consequent].insert(rule->getId()).second) {
                    changed = true;
                }
                
                if (facts_.find(consequent) == facts_.end()) {
                    facts_[consequent] = true;
                    new_facts.push_back(consequent);
                    
                    // Propagate confidence to BayesianProcessor if possible
                    if (bayesian_processor_) {
                        // Simplified: set derived fact prior to max of antecedent priors
                        double max_conf = 0.0;
                        for (const auto& ant : rule->getAntecedents()) {
                            max_conf = std::max(max_conf, bayesian_processor_->getPrior(ant));
                        }
                        bayesian_processor_->setPrior(consequent, max_conf);
                    }
                    changed = true;
                }
            } else {
                // Rule not satisfied. Remove justification if it existed.
                if (justifications_[consequent].erase(rule->getId()) > 0) {
                    changed = true;
                }
            }
        }
    }

    return new_facts;
}

bool LogicProcessor::evaluateRule(std::shared_ptr<RuleNode> rule) {
    std::lock_guard<std::mutex> lock(logic_mutex_);
    std::unordered_map<std::string, bool> fact_map;
    for (const auto& fact : facts_) {
        fact_map[fact.first] = fact.second;
    }
    return rule->evaluate(fact_map);
}

std::vector<std::shared_ptr<RuleNode>> LogicProcessor::getApplicableRules() const {
    std::vector<std::shared_ptr<RuleNode>> applicable;
    std::lock_guard<std::mutex> lock(logic_mutex_);

    std::unordered_map<std::string, bool> fact_map;
    for (const auto& fact : facts_) {
        fact_map[fact.first] = fact.second;
    }

    for (auto& rule : rules_) {
        if (rule->evaluate(fact_map)) {
            applicable.push_back(rule);
        }
    }

    return applicable;
}

bool LogicProcessor::AND(const std::vector<std::string>& facts) const {
    std::lock_guard<std::mutex> lock(logic_mutex_);
    for (const auto& fact : facts) {
        auto it = facts_.find(fact);
        if (it == facts_.end() || !it->second) {
            return false;
        }
    }
    return true;
}

bool LogicProcessor::OR(const std::vector<std::string>& facts) const {
    std::lock_guard<std::mutex> lock(logic_mutex_);
    for (const auto& fact : facts) {
        auto it = facts_.find(fact);
        if (it != facts_.end() && it->second) {
            return true;
        }
    }
    return false;
}

bool LogicProcessor::NOT(const std::string& fact) const {
    std::lock_guard<std::mutex> lock(logic_mutex_);
    auto it = facts_.find(fact);
    return (it == facts_.end()) || !it->second;
}

bool LogicProcessor::IMPLIES(const std::string& antecedent, const std::string& consequent) const {
    return NOT(antecedent) || getFact(consequent);
}

std::vector<std::pair<std::string, std::string>> LogicProcessor::findContradictions() const {
    std::vector<std::pair<std::string, std::string>> contradictions;
    std::lock_guard<std::mutex> lock(logic_mutex_);

    for (auto it1 = facts_.begin(); it1 != facts_.end(); ++it1) {
        for (auto it2 = std::next(it1); it2 != facts_.end(); ++it2) {
            // Check for direct contradictions (same fact with opposite values)
            if (it1->first == it2->first && it1->second != it2->second) {
                contradictions.push_back({it1->first, it2->first});
            }
        }
    }

    return contradictions;
}

void LogicProcessor::resolveContradiction(const std::string& fact1, const std::string& fact2, bool keep_fact1) {
    std::lock_guard<std::mutex> lock(logic_mutex_);
    if (keep_fact1) {
        facts_.erase(fact2);
    } else {
        facts_.erase(fact1);
    }
}

// BayesianProcessor Implementation
BayesianProcessor::BayesianProcessor() {}

void BayesianProcessor::setPrior(const std::string& fact, double probability) {
    std::lock_guard<std::mutex> lock(bayesian_mutex_);
    priors_[fact] = std::max(0.0, std::min(1.0, probability));
}

double BayesianProcessor::getPrior(const std::string& fact) const {
    std::lock_guard<std::mutex> lock(bayesian_mutex_);
    auto it = priors_.find(fact);
    return (it != priors_.end()) ? it->second : 0.5; // Default prior = 0.5 (uncertainty)
}

void BayesianProcessor::setConditional(const std::string& effect, const std::string& cause, double probability) {
    std::lock_guard<std::mutex> lock(bayesian_mutex_);
    conditionals_[effect][cause] = std::max(0.0, std::min(1.0, probability));
}

double BayesianProcessor::getConditional(const std::string& effect, const std::string& cause) const {
    std::lock_guard<std::mutex> lock(bayesian_mutex_);
    auto it = conditionals_.find(effect);
    if (it != conditionals_.end()) {
        auto jt = it->second.find(cause);
        if (jt != it->second.end()) {
            return jt->second;
        }
    }
    return 0.5; // Default conditional probability
}

double BayesianProcessor::inferPosterior(const std::string& cause, const std::string& effect) {
    // Bayes' theorem: P(cause | effect) = P(effect | cause) * P(cause) / P(effect)
    double prior_cause = getPrior(cause);
    double likelihood = getConditional(effect, cause);

    // Calculate marginal probability P(effect) = sum over all possible causes
    double marginal_effect = 0.0;
    for (const auto& pair : priors_) {
        double prior_alt = pair.second;
        double conditional_alt = getConditional(effect, pair.first);
        marginal_effect += conditional_alt * prior_alt;
    }

    if (marginal_effect < 1e-10) {
        marginal_effect = 1e-10; // Prevent division by zero
    }

    double posterior = (likelihood * prior_cause) / marginal_effect;
    return std::max(0.0, std::min(1.0, posterior));
}

double BayesianProcessor::combineEvidence(const std::string& hypothesis,
                                           const std::vector<std::string>& evidence_list) {
    // Naive Bayes approach: P(hypothesis | evidence) proportional to P(hypothesis) * product(P(evidence | hypothesis))
    double posterior = getPrior(hypothesis);

    for (const auto& evidence : evidence_list) {
        double likelihood = getConditional(evidence, hypothesis);
        // Use log-space to prevent underflow
        if (posterior > 0 && likelihood > 0) {
            posterior *= likelihood;
        }
    }

    // Normalize (simplified - assuming binary hypothesis)
    double prior_not_h = 1.0 - getPrior(hypothesis);
    double product_not_h = prior_not_h;
    for (const auto& evidence : evidence_list) {
        double likelihood_not_h = 1.0 - getConditional(evidence, hypothesis);
        if (product_not_h > 0 && likelihood_not_h > 0) {
            product_not_h *= likelihood_not_h;
        }
    }

    double normalizer = posterior + product_not_h;
    if (normalizer < 1e-10) {
        return posterior;
    }

    return std::max(0.0, std::min(1.0, posterior / normalizer));
}

double BayesianProcessor::resolveConflict(const std::string& fact_a, const std::string& fact_b,
                                          const std::vector<std::string>& supporting_evidence) {
    // Calculate posterior probabilities for both conflicting facts
    double prob_a = combineEvidence(fact_a, supporting_evidence);
    double prob_b = combineEvidence(fact_b, supporting_evidence);

    // Calculate confidence-weighted resolution
    double total_prob = prob_a + prob_b;
    if (total_prob < 1e-10) {
        return 0.5; // Equal uncertainty
    }

    // Return normalized probability for fact_a
    return prob_a / total_prob;
}

std::unordered_map<std::string, double> BayesianProcessor::propagateProbabilities(
    const std::string& start_fact,
    const std::unordered_map<uint32_t, std::shared_ptr<GraphNode>>& nodes) {

    std::unordered_map<std::string, double> propagated_probs;
    propagated_probs[start_fact] = getPrior(start_fact);

    std::queue<std::string> queue;
    queue.push(start_fact);
    std::unordered_set<std::string> visited;
    visited.insert(start_fact);

    while (!queue.empty()) {
        std::string current = queue.front();
        queue.pop();

        // Find node by name
        std::shared_ptr<GraphNode> current_node = nullptr;
        for (const auto& pair : nodes) {
            if (pair.second->getName() == current) {
                current_node = pair.second;
                break;
            }
        }

        if (!current_node) continue;

        auto current_prob = propagated_probs[current];

        // Propagate to neighbors
        for (const auto& link : current_node->getOutgoingLinks()) {
            auto target = link->getTarget();
            std::string target_id = target->getName();

            if (visited.find(target_id) != visited.end()) continue;

            double conditional_prob = getConditional(target_id, current);
            double propagated_prob = current_prob * conditional_prob * link->getWeight();

            propagated_probs[target_id] = propagated_prob;
            visited.insert(target_id);
            queue.push(target_id);
        }
    }

    return propagated_probs;
}

double BayesianProcessor::calculateEntropy(const std::string& fact) const {
    double p = getPrior(fact);
    if (p <= 0.0 || p >= 1.0) {
        return 0.0;
    }
    // Shannon entropy: -p*log(p) - (1-p)*log(1-p)
    return -(p * std::log(p) + (1.0 - p) * std::log(1.0 - p));
}

double BayesianProcessor::calculateMutualInformation(const std::string& fact_a,
                                                        const std::string& fact_b) const {
    double p_a = getPrior(fact_a);
    double p_b = getPrior(fact_b);
    double p_ab = getConditional(fact_b, fact_a) * p_a; // Joint probability approximation

    if (p_ab <= 0.0 || p_a <= 0.0 || p_b <= 0.0) {
        return 0.0;
    }

    // I(A;B) = P(A,B) * log(P(A,B) / (P(A) * P(B)))
    double ratio = p_ab / (p_a * p_b);
    if (ratio <= 0.0) {
        return 0.0;
    }

    return p_ab * std::log(ratio);
}

bool BayesianProcessor::acceptFact(const std::string& fact, double threshold) const {
    return getPrior(fact) >= threshold;
}

std::string BayesianProcessor::serializeProbabilities() const {
    std::lock_guard<std::mutex> lock(bayesian_mutex_);
    std::ostringstream oss;

    // Serialize priors
    oss << "priors:";
    for (const auto& pair : priors_) {
        oss << pair.first << "=" << pair.second << ",";
    }

    // Serialize conditionals
    oss << "|conditionals:";
    for (const auto& outer : conditionals_) {
        for (const auto& inner : outer.second) {
            oss << outer.first << "|" << inner.first << "=" << inner.second << ",";
        }
    }

    return oss.str();
}

void BayesianProcessor::deserializeProbabilities(const std::string& data) {
    std::lock_guard<std::mutex> lock(bayesian_mutex_);
    priors_.clear();
    conditionals_.clear();

    std::istringstream iss(data);
    std::string section;

    // Parse priors section
    if (std::getline(iss, section, '|')) {
        if (section.substr(0, 7) == "priors:") {
            std::string priors_data = section.substr(7);
            std::istringstream priors_stream(priors_data);
            std::string pair;

            while (std::getline(priors_stream, pair, ',')) {
                size_t pos = pair.find('=');
                if (pos != std::string::npos) {
                    std::string fact = pair.substr(0, pos);
                    double prob = std::stod(pair.substr(pos + 1));
                    priors_[fact] = prob;
                }
            }
        }
    }

    // Parse conditionals section
    if (std::getline(iss, section, '|')) {
        if (section.substr(0, 12) == "conditionals:") {
            std::string cond_data = section.substr(12);
            std::istringstream cond_stream(cond_data);
            std::string pair;

            while (std::getline(cond_stream, pair, ',')) {
                size_t pipe_pos = pair.find('|');
                size_t eq_pos = pair.find('=');
                if (pipe_pos != std::string::npos && eq_pos != std::string::npos) {
                    std::string effect = pair.substr(0, pipe_pos);
                    std::string cause = pair.substr(pipe_pos + 1, eq_pos - pipe_pos - 1);
                    double prob = std::stod(pair.substr(eq_pos + 1));
                    conditionals_[effect][cause] = prob;
                }
            }
        }
    }
}

// ReasoningEngine Implementation
ReasoningEngine::ReasoningEngine(int num_workers)
    : running_(false), tasks_processed_(0), average_processing_time_(0.0),
      activation_threshold_(0.5), max_reasoning_depth_(10), max_workers_(num_workers) {

    chain_processor_ = std::make_unique<ChainOfLinks>();
    logic_processor_ = std::make_unique<LogicProcessor>();
    bayesian_processor_ = std::make_unique<BayesianProcessor>();
    
    // Link Bayesian to Logic
    logic_processor_->setBayesianProcessor(bayesian_processor_.get());
}

ReasoningEngine::~ReasoningEngine() {
    stop();
}

void ReasoningEngine::addNode(std::shared_ptr<GraphNode> node) {
    if (!node) return;
    uint32_t nid = node->getNumericId();
    nodes_[nid] = node;
    name_to_id_[node->getName()] = nid;
    // Register so any GraphLink using NodeId ctor can resolve this node.
    nodePool().registerWithId(nid, node);
}

void ReasoningEngine::removeNode(const std::string& node_id) {
    auto it = name_to_id_.find(node_id);
    if (it != name_to_id_.end()) {
        uint32_t nid = it->second;
        // Remove associated links
        auto node = nodes_[nid];
        if (node) {
            for (auto& link : node->getOutgoingLinks()) {
                removeLink(link->getId());
            }
            for (auto& link : node->getIncomingLinks()) {
                removeLink(link->getId());
            }
        }
        nodes_.erase(nid);
        name_to_id_.erase(it);
    }
}

std::shared_ptr<GraphNode> ReasoningEngine::getNode(const std::string& node_id) {
    auto it = name_to_id_.find(node_id);
    if (it != name_to_id_.end()) {
        auto jt = nodes_.find(it->second);
        if (jt != nodes_.end()) {
            return jt->second;
        }
    }
    return nullptr;
}

std::shared_ptr<GraphNode> ReasoningEngine::getNodeByNumericId(uint32_t numeric_id) {
    auto it = nodes_.find(numeric_id);
    return (it != nodes_.end()) ? it->second : nullptr;
}

void ReasoningEngine::addLink(std::shared_ptr<GraphLink> link) {
    if (!link) return;
    uint32_t lid = link->getNumericId();
    links_[lid] = link;
    chain_processor_->addLink(link);
    linkPool().registerWithId(lid, link);

    // Update node link references
    if (auto src = link->getSource()) src->addOutgoingLink(lid);
    if (auto tgt = link->getTarget()) tgt->addIncomingLink(lid);
}

void ReasoningEngine::removeLink(const std::string& link_id) {
    // Find link by name
    for (auto it = links_.begin(); it != links_.end(); ++it) {
        if (it->second->getId() == link_id) {
            chain_processor_->removeLink(link_id);
            links_.erase(it);
            break;
        }
    }
}

std::shared_ptr<GraphLink> ReasoningEngine::getLink(const std::string& link_id) {
    for (auto& pair : links_) {
        if (pair.second->getId() == link_id) {
            return pair.second;
        }
    }
    return nullptr;
}

void ReasoningEngine::start() {
    if (running_) return;

    running_ = true;
    for (int i = 0; i < max_workers_; i++) {
        worker_threads_.emplace_back(&ReasoningEngine::workerLoop, this);
    }
}

void ReasoningEngine::stop() {
    if (!running_) return;

    running_ = false;
    queue_cv_.notify_all();

    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    worker_threads_.clear();
}

void ReasoningEngine::workerLoop() {
    while (running_) {
        ReasoningTask task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !task_queue_.empty() || !running_;
            });

            if (!running_) break;

            if (!task_queue_.empty()) {
                task = task_queue_.front();
                task_queue_.pop();
            }
        }

        if (task.node) {
            auto start = std::chrono::high_resolution_clock::now();
            processTask(task);
            auto end = std::chrono::high_resolution_clock::now();
            double duration = std::chrono::duration<double>(end - start).count();
            updateMetrics(duration);
        }
    }
}

void ReasoningEngine::processTask(const ReasoningTask& task) {
    switch (task.type) {
        case ReasoningTask::ACTIVATE_NODE:
            activateNodeInternal(task.node, task.context);
            break;
        case ReasoningTask::PROPAGATE_SIGNAL:
            propagateSignal(task.node);
            break;
        case ReasoningTask::EVALUATE_RULE:
            if (auto rule = std::dynamic_pointer_cast<RuleNode>(task.node)) {
                // Rule evaluation logic
            }
            break;
        case ReasoningTask::UPDATE_CONFIDENCE:
            // Update confidence logic
            break;
        case ReasoningTask::CLEANUP_GRAPH:
            cleanupInactiveNodes();
            break;
    }

    if (task.callback) {
        task.callback();
    }

    tasks_processed_++;
}

void ReasoningEngine::updateMetrics(double processing_time) {
    long count = tasks_processed_.load();
    double current_avg = average_processing_time_.load();
    double new_avg = (current_avg * count + processing_time) / (count + 1);
    average_processing_time_.store(new_avg);
}

void ReasoningEngine::submitTask(const ReasoningTask& task) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.push(task);
    }
    queue_cv_.notify_one();
}

void ReasoningEngine::activateNodeAsync(const std::string& node_id,
                                        const std::unordered_map<std::string, double>& context) {
    auto node = getNode(node_id);
    if (node) {
        ReasoningTask task(ReasoningTask::ACTIVATE_NODE, node);
        task.context = context;
        submitTask(task);
    }
}

void ReasoningEngine::propagateAsync(const std::string& node_id) {
    auto node = getNode(node_id);
    if (node) {
        submitTask(ReasoningTask(ReasoningTask::PROPAGATE_SIGNAL, node));
    }
}

void ReasoningEngine::activateNode(const std::string& node_id,
                                   const std::unordered_map<std::string, double>& context) {
    auto node = getNode(node_id);
    if (node) {
        activateNodeInternal(node, context);
    }
}

void ReasoningEngine::activateNodeInternal(std::shared_ptr<GraphNode> node,
                                             const std::unordered_map<std::string, double>& context) {
    node->activate(context);
    node->propagate();
}

void ReasoningEngine::propagate(const std::string& node_id) {
    auto node = getNode(node_id);
    if (node) {
        propagateSignal(node);
    }
}

void ReasoningEngine::propagateSignal(std::shared_ptr<GraphNode> node) {
    node->propagate();

    // Recursively propagate to active neighbors
    for (auto& neighbor : node->getActiveNeighbors()) {
        if (neighbor->getActivation() > activation_threshold_) {
            propagateSignal(neighbor);
        }
    }
}

void ReasoningEngine::performFullReasoningCycle() {
    // 1. Activate relevant nodes
    for (auto& pair : nodes_) {
        if (pair.second->getActivation() > activation_threshold_) {
            pair.second->propagate();
        }
    }

    // 2. Evaluate all rules
    evaluateAllRules();

    // 3. Update confidences
    updateConfidences();

    // 4. Cleanup
    cleanupInactiveNodes();
}

void ReasoningEngine::evaluateAllRules() {
    for (auto& pair : nodes_) {
        if (pair.second->getType() == NodeType::RULE) {
            auto rule = std::dynamic_pointer_cast<RuleNode>(pair.second);
            if (rule && logic_processor_->evaluateRule(rule)) {
                rule->setActivation(1.0);
            }
        }
    }
}

void ReasoningEngine::updateConfidences() {
    for (auto& pair : nodes_) {
        if (pair.second->getType() == NodeType::INFERENCE) {
            auto inference = std::dynamic_pointer_cast<InferenceNode>(pair.second);
            if (inference) {
                inference->setConfidence(inference->calculateConfidence());
            }
        }
    }
}

void ReasoningEngine::cleanupInactiveNodes() {
    for (auto& pair : nodes_) {
        if (pair.second->getActivation() < 0.01) {
            pair.second->setActivation(0.0);
        }
    }
}

std::vector<std::shared_ptr<GraphNode>> ReasoningEngine::performChainReasoning(
    const std::string& start_node_id,
    const std::string& query_node_id,
    int max_depth) {

    auto start_node = getNode(start_node_id);
    auto query_node = getNode(query_node_id);

    if (!start_node || !query_node) return {};

    return chain_processor_->tracePath(start_node, query_node, max_depth);
}

std::vector<std::string> ReasoningEngine::performLogicalDeduction() {
    return logic_processor_->deduceFacts();
}

bool ReasoningEngine::evaluateLogicalQuery(const std::string& query) {
    return logic_processor_->hasFact(query) && logic_processor_->getFact(query);
}

void ReasoningEngine::setActivationThreshold(double threshold) {
    activation_threshold_ = threshold;
}

void ReasoningEngine::setMaxReasoningDepth(int depth) {
    max_reasoning_depth_ = depth;
}

void ReasoningEngine::setMaxWorkers(int workers) {
    max_workers_ = workers;
}

size_t ReasoningEngine::getNodeCount() const {
    return nodes_.size();
}

size_t ReasoningEngine::getLinkCount() const {
    return links_.size();
}

std::vector<std::shared_ptr<GraphNode>> ReasoningEngine::findActiveNodes(double threshold) {
    std::vector<std::shared_ptr<GraphNode>> active_nodes;
    for (auto& pair : nodes_) {
        if (pair.second->getActivation() >= threshold) {
            active_nodes.push_back(pair.second);
        }
    }
    return active_nodes;
}

void ReasoningEngine::waitForCompletion() {
    while (!task_queue_.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void ReasoningEngine::saveToFile(const std::string& filename) {
    auto json = std::make_shared<JsonParser::JsonValue>();
    std::map<std::string, std::shared_ptr<JsonParser::JsonValue>> root_obj;

    // Serialize nodes
    auto nodes_array = std::make_shared<JsonParser::JsonValue>();
    std::vector<std::shared_ptr<JsonParser::JsonValue>> nodes_data;
    for (auto& pair : nodes_) {
        auto node_json = std::make_shared<JsonParser::JsonValue>(pair.second->serialize());
        nodes_data.push_back(node_json);
    }
    nodes_array->setArray(nodes_data);
    root_obj["nodes"] = nodes_array;

    // Serialize links
    auto links_array = std::make_shared<JsonParser::JsonValue>();
    std::vector<std::shared_ptr<JsonParser::JsonValue>> links_data;
    for (auto& pair : links_) {
        auto link_json = std::make_shared<JsonParser::JsonValue>(pair.second->serialize());
        links_data.push_back(link_json);
    }
    links_array->setArray(links_data);
    root_obj["links"] = links_array;

    auto root_value = std::make_shared<JsonParser::JsonValue>();
    root_value->setObject(root_obj);

    JsonParser::saveToFile(root_value, filename);
}

void ReasoningEngine::loadFromFile(const std::string& filename) {
    auto json = JsonParser::parseFile(filename);
    auto root = json->asObject();

    // Load nodes
    auto nodes_array = root.at("nodes")->asArray();
    for (auto& node_json : nodes_array) {
        auto node = std::make_shared<GraphNode>("temp", NodeType::CONCEPT);
        node->deserialize(node_json->asString());
        addNode(node);
    }

    // Load links
    auto links_array = root.at("links")->asArray();
    for ([[maybe_unused]]auto& link_json : links_array) {
        // Link deserialization would need node references
        // This is simplified for the PoC
    }
}

// Specialized Reasoners Implementation
ForwardChainingReasoner::ForwardChainingReasoner(std::shared_ptr<ReasoningEngine> engine)
    : engine_(engine) {}

std::vector<std::string> ForwardChainingReasoner::reasonFromFacts(
    const std::unordered_map<std::string, bool>& initial_facts) {

    for (const auto& fact : initial_facts) {
        engine_->logic_processor_->addFact(fact.first, fact.second);
    }

    return engine_->performLogicalDeduction();
}

BackwardChainingReasoner::BackwardChainingReasoner(std::shared_ptr<ReasoningEngine> engine)
    : engine_(engine) {}

std::vector<std::string> BackwardChainingReasoner::reasonToGoal(const std::string& goal) {
    std::vector<std::string> reasoning_path;

    // Simplified backward chaining
    if (engine_->evaluateLogicalQuery(goal)) {
        reasoning_path.push_back(goal);
    }

    return reasoning_path;
}

bool BackwardChainingReasoner::canProve(const std::string& goal) {
    return !reasonToGoal(goal).empty();
}

AnalogicalReasoner::AnalogicalReasoner(std::shared_ptr<ReasoningEngine> engine)
    : engine_(engine) {}

std::vector<std::shared_ptr<GraphNode>> AnalogicalReasoner::findAnalogies(
    std::shared_ptr<GraphNode> source_node) {

    std::vector<std::shared_ptr<GraphNode>> analogies;
    const auto& nodes = engine_->getNodes();

    for (auto& pair : nodes) {
        if (pair.second != source_node &&
            pair.second->getType() == source_node->getType()) {
            // Check for similar properties
            int similar_props = 0;
            const auto& source_props = source_node->getProperties();
            for (const auto& prop : source_props) {
                if (pair.second->hasProperty(prop.first) &&
                    pair.second->getProperty(prop.first) == prop.second) {
                    similar_props++;
                }
            }

            if (similar_props > 0) {
                analogies.push_back(pair.second);
            }
        }
    }

    return analogies;
}

void AnalogicalReasoner::applyAnalogy(std::shared_ptr<GraphNode> source,
                                     std::shared_ptr<GraphNode> target) {
    // Copy properties from source to target
    const auto& source_props = source->getProperties();
    for (const auto& prop : source_props) {
        target->setProperty(prop.first, prop.second);
    }
}

} // namespace dynabolic