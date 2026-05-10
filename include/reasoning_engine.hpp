#ifndef REASONING_ENGINE_HPP
#define REASONING_ENGINE_HPP

#include "graph_node.hpp"
#include <vector>
#include <map>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <atomic>

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
    std::map<std::string, double> context;
    std::function<void()> callback;
    
    ReasoningTask() : type(ACTIVATE_NODE), node(nullptr) {}
    ReasoningTask(TaskType t, std::shared_ptr<GraphNode> n = nullptr) 
        : type(t), node(n) {}
};

// Chain of Links - Core reasoning mechanism
class ChainOfLinks {
private:
    std::vector<std::shared_ptr<GraphLink>> links_;
    std::map<std::string, double> activation_history_;
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

// Logic Processor - Pure logical reasoning without matrices
class LogicProcessor {
private:
    std::map<std::string, bool> facts_;
    std::vector<std::shared_ptr<RuleNode>> rules_;
    mutable std::mutex logic_mutex_;
    
public:
    void addFact(const std::string& fact, bool value);
    void removeFact(const std::string& fact);
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

// Multi-threaded Reasoning Engine
class ReasoningEngine {
private:
    std::map<std::string, std::shared_ptr<GraphNode>> nodes_;
    std::map<std::string, std::shared_ptr<GraphLink>> links_;
    std::unique_ptr<ChainOfLinks> chain_processor_;
    std::unique_ptr<LogicProcessor> logic_processor_;
    
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
                          const std::map<std::string, double>& context);
    void propagateAsync(const std::string& node_id);
    
    // Synchronous reasoning
    void activateNode(const std::string& node_id, 
                     const std::map<std::string, double>& context);
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
    const std::map<std::string, std::shared_ptr<GraphNode>>& getNodes() const { return nodes_; }
    
private:
    void workerLoop();
    void processTask(const ReasoningTask& task);
    void updateMetrics(double processing_time);
    
    // Internal reasoning helpers
    void activateNodeInternal(std::shared_ptr<GraphNode> node, 
                             const std::map<std::string, double>& context);
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
    std::vector<std::string> reasonFromFacts(const std::map<std::string, bool>& initial_facts);
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

// Friend declarations for specialized reasoners
class ForwardChainingReasoner;
class BackwardChainingReasoner;
class AnalogicalReasoner;

} // namespace dynabolic

#endif // REASONING_ENGINE_HPP