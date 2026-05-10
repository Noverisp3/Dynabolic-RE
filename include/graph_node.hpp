#ifndef GRAPH_NODE_HPP
#define GRAPH_NODE_HPP

#include <vector>
#include <map>
#include <string>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>

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

// Graph Node - Core building block
class GraphNode {
private:
    std::string id_;
    NodeType type_;
    std::map<std::string, std::string> properties_;
    std::vector<std::shared_ptr<GraphLink>> outgoing_links_;
    std::vector<std::shared_ptr<GraphLink>> incoming_links_;
    mutable std::mutex node_mutex_;
    
    // Activation value (0.0 to 1.0)
    double activation_;
    
    // Confidence value (0.0 to 1.0)
    double confidence_;
    
public:
    GraphNode(const std::string& id, NodeType type);
    virtual ~GraphNode() = default;
    
    // Core properties
    const std::string& getId() const { return id_; }
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
    
    // Activation and confidence
    void setActivation(double activation);
    double getActivation() const { return activation_; }
    void setConfidence(double confidence);
    double getConfidence() const { return confidence_; }
    
    // Reasoning operations
    virtual void activate(const std::map<std::string, double>& context);
    virtual void propagate();
    virtual std::vector<std::shared_ptr<GraphNode>> getActiveNeighbors() const;
    
    // Thread safety
    void lock() const { node_mutex_.lock(); }
    void unlock() const { node_mutex_.unlock(); }
    
    // Allow derived classes to access properties
    const std::map<std::string, std::string>& getProperties() const { return properties_; }
    
    // Serialization
    virtual std::string serialize() const;
    virtual void deserialize(const std::string& data);
};

// Graph Link - Connection between nodes
class GraphLink {
private:
    std::string id_;
    std::shared_ptr<GraphNode> source_;
    std::shared_ptr<GraphNode> target_;
    LinkType type_;
    double weight_;
    std::map<std::string, std::string> properties_;
    mutable std::mutex link_mutex_;
    
public:
    GraphLink(const std::string& id, 
              std::shared_ptr<GraphNode> source,
              std::shared_ptr<GraphNode> target,
              LinkType type, double weight = 1.0);
    
    // Core properties
    const std::string& getId() const { return id_; }
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
    
    // Thread safety
    void lock() const { link_mutex_.lock(); }
    void unlock() const { link_mutex_.unlock(); }
    
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
    
    void activate(const std::map<std::string, double>& context) override;
};

class RuleNode : public GraphNode {
private:
    std::vector<std::string> antecedents_;
    std::string consequent_;
    
public:
    RuleNode(const std::string& id) : GraphNode(id, NodeType::RULE) {}
    
    void setAntecedents(const std::vector<std::string>& antecedents);
    void setConsequent(const std::string& consequent);
    
    bool evaluate(const std::map<std::string, bool>& facts) const;
    void activate(const std::map<std::string, double>& context) override;
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
    void activate(const std::map<std::string, double>& context) override;
};

} // namespace dynabolic

#endif // GRAPH_NODE_HPP