#ifndef REASONING_ENGINE_HPP
#define REASONING_ENGINE_HPP

#include "graph_node.hpp"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <atomic>
#include <cstdint>

namespace dynabolic {

// Reasoning task for parallel processing
struct ReasoningTask {
    enum TaskType {
        ACTIVATE_NODE,
        PROPAGATE_SIGNAL,
        EVALUATE_RULE,
        UPDATE_CONFIDENCE,
        CLEANUP_GRAPH
    };

    TaskType type;
    std::shared_ptr<GraphNode> node;
    std::unordered_map<std::string, double> context;
    std::function<void()> callback;

    ReasoningTask() : type(ACTIVATE_NODE), node(nullptr) {}
    ReasoningTask(TaskType t, std::shared_ptr<GraphNode> n = nullptr)
        : type(t), node(n) {}
};

// Chain of Links - Core reasoning mechanism
class ChainOfLinks {
private:
    std::vector<std::shared_ptr<GraphLink>> links_;
    std::unordered_map<std::string, double> activation_history_;
    mutable std::mutex chain_mutex_;

public:
    void addLink(std::shared_ptr<GraphLink> link);
    void removeLink(const std::string& link_id);

    std::vector<std::shared_ptr<GraphNode>> tracePath(
        std::shared_ptr<GraphNode> start,
        std::shared_ptr<GraphNode> end,
        int max_depth = 10);

    double calculatePathStrength(const std::vector<std::shared_ptr<GraphNode>>& path);
    void propagateAlongChain(std::shared_ptr<GraphNode> start_node, double initial_activation);

    std::vector<std::shared_ptr<GraphLink>> getCausalChain(
        std::shared_ptr<GraphNode> node);
    std::vector<std::shared_ptr<GraphLink>> getImplicationChain(
        std::shared_ptr<GraphNode> node);

    void optimizeChainWeights();
    std::string serializeChain() const;
};

// Bayesian Processor - Probabilistic reasoning with conditional probability
class BayesianProcessor;

struct TemporalFact {
    bool value;
    double confidence;
    uint64_t timestamp; // Epoch in milliseconds or logical ticks

    TemporalFact(bool v = false, double c = 0.5, uint64_t t = 0)
        : value(v), confidence(c), timestamp(t) {}
};

// Logic Processor - Pure logical reasoning without matrices
class LogicProcessor {
private:
    std::unordered_map<std::string, bool> facts_;
    // Temporal history for trend analysis
    std::unordered_map<std::string, std::vector<TemporalFact>> temporal_history_;
    
    std::vector<std::shared_ptr<RuleNode>> rules_;
    
    // TMS: Justification table. Maps a fact to the IDs of rules that support it.
    // A fact is retracted if it has no more supporting justifications.
    std::unordered_map<std::string, std::unordered_set<std::string>> justifications_;
    
    // Tracks which facts were explicitly added (not deduced).
    // Explicit facts don't need justifications but can be removed.
    std::unordered_set<std::string> explicit_facts_;

    // Bayesian Integration: Reference to BayesianProcessor for confidence-based firing
    BayesianProcessor* bayesian_processor_ = nullptr;
    double firing_threshold_ = 0.5;

    mutable std::mutex logic_mutex_;

public:
    LogicProcessor() : bayesian_processor_(nullptr), firing_threshold_(0.5) {}
    ~LogicProcessor() = default;

    void setBayesianProcessor(BayesianProcessor* bp) { 
        std::lock_guard<std::mutex> lock(logic_mutex_);
        bayesian_processor_ = bp; 
    }
    void setFiringThreshold(double threshold) {
        std::lock_guard<std::mutex> lock(logic_mutex_);
        firing_threshold_ = threshold;
    }
    void addFact(const std::string& fact, bool value, bool is_explicit = true);
    void addTemporalFact(const std::string& fact, bool value, double confidence, uint64_t timestamp);
    
    enum class Trend { INCREASING, DECREASING, STABLE, UNKNOWN };
    Trend calculateTrend(const std::string& fact, uint64_t window_ms) const;

    void removeFact(const std::string& fact);
    void retractFact(const std::string& fact); // Recursive retraction
    bool hasFact(const std::string& fact) const;
    bool getFact(const std::string& fact) const;

    void addRule(std::shared_ptr<RuleNode> rule);
    void removeRule(const std::string& rule_id);

    std::vector<std::string> deduceFacts();
    bool evaluateRule(std::shared_ptr<RuleNode> rule);
    std::vector<std::shared_ptr<RuleNode>> getApplicableRules() const;

    // Logical operations
    bool AND(const std::vector<std::string>& facts) const;
    bool OR(const std::vector<std::string>& facts) const;
    bool NOT(const std::string& fact) const;
    bool IMPLIES(const std::string& antecedent, const std::string& consequent) const;

    // Contradiction detection
    std::vector<std::pair<std::string, std::string>> findContradictions() const;
    void resolveContradiction(const std::string& fact1, const std::string& fact2, bool keep_fact1);
};

// Bayesian Processor - Probabilistic reasoning with conditional probability
class BayesianProcessor {
private:
    // Prior probabilities: P(fact)
    std::unordered_map<std::string, double> priors_;
    // Conditional probabilities: P(fact_b | fact_a)
    std::unordered_map<std::string, std::unordered_map<std::string, double>> conditionals_;
    // Joint probability tables for evidence combination
    std::unordered_map<std::string, std::unordered_map<std::string, double>> joint_probabilities_;
    mutable std::mutex bayesian_mutex_;

public:
    BayesianProcessor();

    // Prior probability management
    void setPrior(const std::string& fact, double probability);
    double getPrior(const std::string& fact) const;

    // Conditional probability: P(effect | cause)
    void setConditional(const std::string& effect, const std::string& cause, double probability);
    double getConditional(const std::string& effect, const std::string& cause) const;

    // Bayesian inference: P(cause | effect) = P(effect | cause) * P(cause) / P(effect)
    double inferPosterior(const std::string& cause, const std::string& effect);

    // Evidence combination using Bayes' theorem
    double combineEvidence(const std::string& hypothesis,
                          const std::vector<std::string>& evidence_list);

    // Conflict resolution with probabilities
    double resolveConflict(const std::string& fact_a, const std::string& fact_b,
                          const std::vector<std::string>& supporting_evidence);

    // Probability propagation through graph
    std::unordered_map<std::string, double> propagateProbabilities(
        const std::string& start_fact,
        const std::unordered_map<uint32_t, std::shared_ptr<GraphNode>>& nodes);

    // Uncertainty quantification
    double calculateEntropy(const std::string& fact) const;
    double calculateMutualInformation(const std::string& fact_a,
                                       const std::string& fact_b) const;

    // Threshold-based fact acceptance
    bool acceptFact(const std::string& fact, double threshold = 0.5) const;

    // Serialization
    std::string serializeProbabilities() const;
    void deserializeProbabilities(const std::string& data);
};

// Forward declarations for specialized reasoners
class ForwardChainingReasoner;
class BackwardChainingReasoner;
class AnalogicalReasoner;

// Multi-threaded Reasoning Engine
class ReasoningEngine {
    // Specialized reasoners need access to internal state to drive reasoning
    friend class ForwardChainingReasoner;
    friend class BackwardChainingReasoner;
    friend class AnalogicalReasoner;
    
private:
    // Fast O(1) storage using numeric IDs
    std::unordered_map<uint32_t, std::shared_ptr<GraphNode>> nodes_;
    std::unordered_map<uint32_t, std::shared_ptr<GraphLink>> links_;

    // String name to numeric ID mapping for external lookups
    std::unordered_map<std::string, uint32_t> name_to_id_;

    std::unique_ptr<ChainOfLinks> chain_processor_;
    std::unique_ptr<LogicProcessor> logic_processor_;
    // Probability Processor for Bayesian integration
    std::unique_ptr<BayesianProcessor> bayesian_processor_;

    // Threading components
    std::vector<std::thread> worker_threads_;
    std::queue<ReasoningTask> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<bool> running_;

    // Performance metrics
    std::atomic<long> tasks_processed_;
    std::atomic<double> average_processing_time_;

    // Reasoning parameters
    double activation_threshold_;
    int max_reasoning_depth_;
    int max_workers_;

public:
    ReasoningEngine(int num_workers = 4);
    ~ReasoningEngine();

    // Graph management
    void addNode(std::shared_ptr<GraphNode> node);
    void removeNode(const std::string& node_id);
    std::shared_ptr<GraphNode> getNode(const std::string& node_id);
    std::shared_ptr<GraphNode> getNodeByNumericId(uint32_t numeric_id);

    void addLink(std::shared_ptr<GraphLink> link);
    void removeLink(const std::string& link_id);
    std::shared_ptr<GraphLink> getLink(const std::string& link_id);

    // Chain reasoning
    std::vector<std::shared_ptr<GraphNode>> performChainReasoning(
        const std::string& start_node_id,
        const std::string& query_node_id,
        int max_depth = 10);

    // Logical reasoning
    std::vector<std::string> performLogicalDeduction();
    bool evaluateLogicalQuery(const std::string& query);

    // Async reasoning tasks
    void submitTask(const ReasoningTask& task);
    void activateNodeAsync(const std::string& node_id,
                          const std::unordered_map<std::string, double>& context);
    void propagateAsync(const std::string& node_id);

    // Synchronous reasoning
    void activateNode(const std::string& node_id,
                     const std::unordered_map<std::string, double>& context);
    void propagate(const std::string& node_id);
    void performFullReasoningCycle();

    // Configuration
    void setActivationThreshold(double threshold);
    void setMaxReasoningDepth(int depth);
    void setMaxWorkers(int workers);

    // Statistics and monitoring
    long getTasksProcessed() const { return tasks_processed_.load(); }
    double getAverageProcessingTime() const { return average_processing_time_.load(); }
    size_t getNodeCount() const;
    size_t getLinkCount() const;

    // Graph analysis
    std::vector<std::shared_ptr<GraphNode>> findActiveNodes(double threshold = 0.5);
    std::vector<std::shared_ptr<GraphNode>> findStronglyConnectedComponents();
    std::vector<std::shared_ptr<GraphLink>> findCriticalPaths();

    // Persistence
    void saveToFile(const std::string& filename);
    void loadFromFile(const std::string& filename);

    // Control
    void start();
    void stop();
    void waitForCompletion();

    // Accessor methods for specialized reasoners
    LogicProcessor* getLogicProcessor() { return logic_processor_.get(); }
    BayesianProcessor* getBayesianProcessor() { return bayesian_processor_.get(); }
    const std::unordered_map<uint32_t, std::shared_ptr<GraphNode>>& getNodes() const { return nodes_; }
    const std::unordered_map<std::string, uint32_t>& getNameMap() const { return name_to_id_; }

private:
    void workerLoop();
    void processTask(const ReasoningTask& task);
    void updateMetrics(double processing_time);

    // Internal reasoning helpers
    void activateNodeInternal(std::shared_ptr<GraphNode> node,
                             const std::unordered_map<std::string, double>& context);
    void propagateSignal(std::shared_ptr<GraphNode> node);
    void evaluateAllRules();
    void updateConfidences();
    void cleanupInactiveNodes();
};

// Specialized reasoning strategies
class ForwardChainingReasoner {
private:
    std::shared_ptr<ReasoningEngine> engine_;

public:
    ForwardChainingReasoner(std::shared_ptr<ReasoningEngine> engine);
    std::vector<std::string> reasonFromFacts(const std::unordered_map<std::string, bool>& initial_facts);
};

class BackwardChainingReasoner {
private:
    std::shared_ptr<ReasoningEngine> engine_;

public:
    BackwardChainingReasoner(std::shared_ptr<ReasoningEngine> engine);
    std::vector<std::string> reasonToGoal(const std::string& goal);
    bool canProve(const std::string& goal);
};

class AnalogicalReasoner {
private:
    std::shared_ptr<ReasoningEngine> engine_;

public:
    AnalogicalReasoner(std::shared_ptr<ReasoningEngine> engine);
    std::vector<std::shared_ptr<GraphNode>> findAnalogies(
        std::shared_ptr<GraphNode> source_node);
    void applyAnalogy(std::shared_ptr<GraphNode> source,
                     std::shared_ptr<GraphNode> target);
};

} // namespace dynabolic

#endif // REASONING_ENGINE_HPP