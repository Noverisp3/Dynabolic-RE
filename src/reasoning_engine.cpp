#include "reasoning_engine.hpp"
#include "json_parser.hpp"
#include <algorithm>
#include <chrono>
#include <set>
#include <stack>

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
    std::set<std::string> visited;
    std::stack<std::pair<std::shared_ptr<GraphNode>, int>> stack;
    
    stack.push({start, 0});
    visited.insert(start->getId());
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
            if (visited.find(neighbor->getId()) == visited.end()) {
                visited.insert(neighbor->getId());
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
        std::string link_key = link->getSource()->getId() + "->" + link->getTarget()->getId();
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
void LogicProcessor::addFact(const std::string& fact, bool value) {
    std::lock_guard<std::mutex> lock(logic_mutex_);
    facts_[fact] = value;
}

void LogicProcessor::removeFact(const std::string& fact) {
    std::lock_guard<std::mutex> lock(logic_mutex_);
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
    std::vector<std::string> new_facts;
    bool changed = true;
    
    while (changed) {
        changed = false;
        for (auto& rule : rules_) {
            std::map<std::string, bool> fact_map;
            for (const auto& fact : facts_) {
                fact_map[fact.first] = fact.second;
            }
            
            if (rule->evaluate(fact_map) && !hasFact(rule->getProperty("consequent"))) {
                addFact(rule->getProperty("consequent"), true);
                new_facts.push_back(rule->getProperty("consequent"));
                changed = true;
            }
        }
    }
    
    return new_facts;
}

bool LogicProcessor::evaluateRule(std::shared_ptr<RuleNode> rule) {
    std::lock_guard<std::mutex> lock(logic_mutex_);
    std::map<std::string, bool> fact_map;
    for (const auto& fact : facts_) {
        fact_map[fact.first] = fact.second;
    }
    return rule->evaluate(fact_map);
}

std::vector<std::shared_ptr<RuleNode>> LogicProcessor::getApplicableRules() const {
    std::vector<std::shared_ptr<RuleNode>> applicable;
    std::lock_guard<std::mutex> lock(logic_mutex_);
    
    std::map<std::string, bool> fact_map;
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

// ReasoningEngine Implementation
ReasoningEngine::ReasoningEngine(int num_workers) 
    : running_(false), tasks_processed_(0), average_processing_time_(0.0),
      activation_threshold_(0.5), max_reasoning_depth_(10), max_workers_(num_workers) {
    
    chain_processor_ = std::make_unique<ChainOfLinks>();
    logic_processor_ = std::make_unique<LogicProcessor>();
}

ReasoningEngine::~ReasoningEngine() {
    stop();
}

void ReasoningEngine::addNode(std::shared_ptr<GraphNode> node) {
    nodes_[node->getId()] = node;
}

void ReasoningEngine::removeNode(const std::string& node_id) {
    auto it = nodes_.find(node_id);
    if (it != nodes_.end()) {
        // Remove associated links
        for (auto& link : it->second->getOutgoingLinks()) {
            removeLink(link->getId());
        }
        for (auto& link : it->second->getIncomingLinks()) {
            removeLink(link->getId());
        }
        nodes_.erase(it);
    }
}

std::shared_ptr<GraphNode> ReasoningEngine::getNode(const std::string& node_id) {
    auto it = nodes_.find(node_id);
    return (it != nodes_.end()) ? it->second : nullptr;
}

void ReasoningEngine::addLink(std::shared_ptr<GraphLink> link) {
    links_[link->getId()] = link;
    chain_processor_->addLink(link);
    
    // Update node link references
    link->getSource()->addOutgoingLink(link);
    link->getTarget()->addIncomingLink(link);
}

void ReasoningEngine::removeLink(const std::string& link_id) {
    auto it = links_.find(link_id);
    if (it != links_.end()) {
        chain_processor_->removeLink(link_id);
        links_.erase(it);
    }
}

std::shared_ptr<GraphLink> ReasoningEngine::getLink(const std::string& link_id) {
    auto it = links_.find(link_id);
    return (it != links_.end()) ? it->second : nullptr;
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
                                        const std::map<std::string, double>& context) {
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
                                   const std::map<std::string, double>& context) {
    auto node = getNode(node_id);
    if (node) {
        activateNodeInternal(node, context);
    }
}

void ReasoningEngine::activateNodeInternal(std::shared_ptr<GraphNode> node, 
                                           const std::map<std::string, double>& context) {
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
    for (auto& link_json : links_array) {
        // Link deserialization would need node references
        // This is simplified for the PoC
    }
}

// Specialized Reasoners Implementation
ForwardChainingReasoner::ForwardChainingReasoner(std::shared_ptr<ReasoningEngine> engine)
    : engine_(engine) {}

std::vector<std::string> ForwardChainingReasoner::reasonFromFacts(
    const std::map<std::string, bool>& initial_facts) {
    
    for (const auto& fact : initial_facts) {
        engine_->getLogicProcessor()->addFact(fact.first, fact.second);
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