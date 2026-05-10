#ifndef GRAPH_NODE_HPP
#define GRAPH_NODE_HPP

#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <cstdint>

namespace dynabolic {

// Forward declarations
class GraphLink;
class ReasoningEngine;

enum class NodeType {
    CONCEPT,      // Abstract concept node
    FACT,         // Factual information
    RULE,         // Logical rule
    QUERY,        // Question/Query node
    INFERENCE,    // Derived conclusion
    MEMORY,       // Memory storage node
    CONTROL       // Control flow node
};

enum class LinkType {
    CAUSAL,       // A causes B
    IMPLIES,      // A implies B
    SUPPORTS,     // A supports B
    CONTRADICTS,  // A contradicts B
    SEQUENTIAL,   // A precedes B
    HIERARCHICAL, // A is parent of B
    ASSOCIATIVE   // A associated with B
};

// ID Generator - monotonically increasing uint32_t IDs
class IdGenerator {
private:
    static std::atomic<uint32_t> next_id_;
public:
    static uint32_t generate() { return next_id_.fetch_add(1); }
    static void reset(uint32_t start = 0) { next_id_.store(start); }
};

// Graph Node - Core building block
class GraphNode {
private:
    uint32_t numeric_id_;            // Fast uint32_t ID for O(1) lookup
    std::string name_;                // Human-readable string name
    NodeType type_;
    std::unordered_map<std::string, std::string> properties_;
    std::vector<std::shared_ptr<GraphLink>> outgoing_links_;
    std::vector<std::shared_ptr<GraphLink>> incoming_links_;
    // Reader/writer lock: many concurrent readers, exclusive writers.
    // Cuts contention vs std::mutex when reads dominate (property/link access).
    mutable std::shared_mutex node_mutex_;

    // Activation value (0.0 to 1.0). std::atomic<double> avoids locking on the hot
    // get/set path, which is hammered during signal propagation.
    std::atomic<double> activation_;

    // Confidence value (0.0 to 1.0). Same rationale as activation_.
    std::atomic<double> confidence_;

public:
    GraphNode(const std::string& name, NodeType type);
    virtual ~GraphNode() = default;

    // Fast numeric ID for internal lookups
    uint32_t getNumericId() const { return numeric_id_; }

    // Human-readable name
    const std::string& getName() const { return name_; }

    // Backwards compatibility: name acts as string ID
    const std::string& getId() const { return name_; }

    NodeType getType() const { return type_; }

    // Property management
    void setProperty(const std::string& key, const std::string& value);
    std::string getProperty(const std::string& key) const;
    bool hasProperty(const std::string& key) const;

    // Link management
    void addOutgoingLink(std::shared_ptr<GraphLink> link);
    void addIncomingLink(std::shared_ptr<GraphLink> link);
    const std::vector<std::shared_ptr<GraphLink>>& getOutgoingLinks() const;
    const std::vector<std::shared_ptr<GraphLink>>& getIncomingLinks() const;

    // Activation and confidence (lock-free atomic access)
    void setActivation(double activation);
    double getActivation() const { return activation_.load(std::memory_order_relaxed); }
    void setConfidence(double confidence);
    double getConfidence() const { return confidence_.load(std::memory_order_relaxed); }

    // Reasoning operations
    virtual void activate(const std::unordered_map<std::string, double>& context);
    virtual void propagate();
    virtual std::vector<std::shared_ptr<GraphNode>> getActiveNeighbors() const;

    // Thread safety. lock()/unlock() take the writer (exclusive) lock for backwards
    // compatibility; lock_shared()/unlock_shared() take the reader (shared) lock.
    void lock() const { node_mutex_.lock(); }
    void unlock() const { node_mutex_.unlock(); }
    void lock_shared() const { node_mutex_.lock_shared(); }
    void unlock_shared() const { node_mutex_.unlock_shared(); }

    // Allow derived classes to access properties
    const std::unordered_map<std::string, std::string>& getProperties() const { return properties_; }

    // Serialization
    virtual std::string serialize() const;
    virtual void deserialize(const std::string& data);
};

// Graph Link - Connection between nodes
class GraphLink {
private:
    uint32_t numeric_id_;            // Fast uint32_t ID
    std::string name_;                // Human-readable name
    std::shared_ptr<GraphNode> source_;
    std::shared_ptr<GraphNode> target_;
    LinkType type_;
    double weight_;
    std::unordered_map<std::string, std::string> properties_;
    // Reader/writer lock so many threads can read link properties simultaneously.
    mutable std::shared_mutex link_mutex_;

public:
    GraphLink(const std::string& name,
              std::shared_ptr<GraphNode> source,
              std::shared_ptr<GraphNode> target,
              LinkType type, double weight = 1.0);

    // Fast numeric ID
    uint32_t getNumericId() const { return numeric_id_; }

    // Backwards compatibility
    const std::string& getId() const { return name_; }
    const std::string& getName() const { return name_; }

    std::shared_ptr<GraphNode> getSource() const { return source_; }
    std::shared_ptr<GraphNode> getTarget() const { return target_; }
    LinkType getType() const { return type_; }
    double getWeight() const { return weight_; }
    void setWeight(double weight) { weight_ = weight; }

    // Property management
    void setProperty(const std::string& key, const std::string& value);
    std::string getProperty(const std::string& key) const;

    // Signal propagation
    double propagateSignal(double input_signal) const;

    // Thread safety. lock()/unlock() take the writer (exclusive) lock;
    // lock_shared()/unlock_shared() take the reader (shared) lock.
    void lock() const { link_mutex_.lock(); }
    void unlock() const { link_mutex_.unlock(); }
    void lock_shared() const { link_mutex_.lock_shared(); }
    void unlock_shared() const { link_mutex_.unlock_shared(); }

    // Serialization
    std::string serialize() const;
    void deserialize(const std::string& data);
};

// Specialized node types
class ConceptNode : public GraphNode {
public:
    ConceptNode(const std::string& id) : GraphNode(id, NodeType::CONCEPT) {}

    void addAttribute(const std::string& key, const std::string& value);
    std::vector<std::string> getAttributes() const;

    void activate(const std::unordered_map<std::string, double>& context) override;
};

class RuleNode : public GraphNode {
private:
    std::vector<std::string> antecedents_;
    std::string consequent_;

public:
    RuleNode(const std::string& id) : GraphNode(id, NodeType::RULE) {}

    void setAntecedents(const std::vector<std::string>& antecedents);
    void setConsequent(const std::string& consequent);

    bool evaluate(const std::unordered_map<std::string, bool>& facts) const;
    void activate(const std::unordered_map<std::string, double>& context) override;
};

class InferenceNode : public GraphNode {
private:
    std::string reasoning_path_;
    std::vector<std::shared_ptr<GraphNode>> evidence_nodes_;

public:
    InferenceNode(const std::string& id) : GraphNode(id, NodeType::INFERENCE) {}

    void setReasoningPath(const std::string& path);
    void addEvidence(std::shared_ptr<GraphNode> evidence);

    double calculateConfidence() const;
    void activate(const std::unordered_map<std::string, double>& context) override;
};

} // namespace dynabolic

#endif // GRAPH_NODE_HPP