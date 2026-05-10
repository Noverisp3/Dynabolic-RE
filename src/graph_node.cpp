#include "graph_node.hpp"
#include <sstream>
#include <algorithm>
#include <cmath>

namespace dynabolic {

// Initialize static atomic counter
std::atomic<uint32_t> IdGenerator::next_id_(1);

// GraphNode Implementation
GraphNode::GraphNode(const std::string& name, NodeType type)
    : numeric_id_(IdGenerator::generate()),
      name_(name),
      type_(type),
      activation_(0.0),
      confidence_(0.5) {}

void GraphNode::setProperty(const std::string& key, const std::string& value) {
    std::unique_lock<std::shared_mutex> lock(node_mutex_);
    properties_[key] = value;
}

std::string GraphNode::getProperty(const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(node_mutex_);
    auto it = properties_.find(key);
    return (it != properties_.end()) ? it->second : "";
}

bool GraphNode::hasProperty(const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(node_mutex_);
    return properties_.find(key) != properties_.end();
}

void GraphNode::addOutgoingLink(std::shared_ptr<GraphLink> link) {
    std::unique_lock<std::shared_mutex> lock(node_mutex_);
    outgoing_links_.push_back(link);
}

void GraphNode::addIncomingLink(std::shared_ptr<GraphLink> link) {
    std::unique_lock<std::shared_mutex> lock(node_mutex_);
    incoming_links_.push_back(link);
}

const std::vector<std::shared_ptr<GraphLink>>& GraphNode::getOutgoingLinks() const {
    return outgoing_links_;
}

const std::vector<std::shared_ptr<GraphLink>>& GraphNode::getIncomingLinks() const {
    return incoming_links_;
}

void GraphNode::setActivation(double activation) {
    activation_.store(std::max(0.0, std::min(1.0, activation)),
                      std::memory_order_relaxed);
}

void GraphNode::setConfidence(double confidence) {
    confidence_.store(std::max(0.0, std::min(1.0, confidence)),
                      std::memory_order_relaxed);
}

void GraphNode::activate(const std::unordered_map<std::string, double>& context) {
    // Default activation based on context matching
    double total_activation = 0.0;
    int matching_properties = 0;

    for (const auto& prop : properties_) {
        auto it = context.find(prop.first);
        if (it != context.end()) {
            total_activation += it->second;
            matching_properties++;
        }
    }

    if (matching_properties > 0) {
        setActivation(total_activation / matching_properties);
    }
}

void GraphNode::propagate() {
    if (getActivation() < 0.1) return; // Threshold for propagation

    for (auto& link : outgoing_links_) {
        double signal = link->propagateSignal(getActivation());
        auto target = link->getTarget();
        target->setActivation(target->getActivation() + signal * 0.1);
    }
}

std::vector<std::shared_ptr<GraphNode>> GraphNode::getActiveNeighbors() const {
    std::vector<std::shared_ptr<GraphNode>> active_neighbors;

    for (auto& link : outgoing_links_) {
        if (link->getTarget()->getActivation() > 0.1) {
            active_neighbors.push_back(link->getTarget());
        }
    }

    for (auto& link : incoming_links_) {
        if (link->getSource()->getActivation() > 0.1) {
            active_neighbors.push_back(link->getSource());
        }
    }

    return active_neighbors;
}

std::string GraphNode::serialize() const {
    std::shared_lock<std::shared_mutex> lock(node_mutex_);
    std::ostringstream oss;
    oss << "nid:" << numeric_id_ << "|name:" << name_
        << "|type:" << static_cast<int>(type_)
        << "|activation:" << activation_.load(std::memory_order_relaxed)
        << "|confidence:" << confidence_.load(std::memory_order_relaxed);

    for (const auto& prop : properties_) {
        oss << "|" << prop.first << ":" << prop.second;
    }

    return oss.str();
}

void GraphNode::deserialize(const std::string& data) {
    std::unique_lock<std::shared_mutex> lock(node_mutex_);
    std::istringstream iss(data);
    std::string token;

    while (std::getline(iss, token, '|')) {
        size_t pos = token.find(':');
        if (pos != std::string::npos) {
            std::string key = token.substr(0, pos);
            std::string value = token.substr(pos + 1);

            if (key == "nid") {
                numeric_id_ = std::stoul(value);
            } else if (key == "name") {
                name_ = value;
            } else if (key == "type") {
                type_ = static_cast<NodeType>(std::stoi(value));
            } else if (key == "activation") {
                activation_.store(std::stod(value), std::memory_order_relaxed);
            } else if (key == "confidence") {
                confidence_.store(std::stod(value), std::memory_order_relaxed);
            } else {
                properties_[key] = value;
            }
        }
    }
}

// GraphLink Implementation
GraphLink::GraphLink(const std::string& name,
                     std::shared_ptr<GraphNode> source,
                     std::shared_ptr<GraphNode> target,
                     LinkType type, double weight)
    : numeric_id_(IdGenerator::generate()),
      name_(name),
      source_(source),
      target_(target),
      type_(type),
      weight_(weight) {}

void GraphLink::setProperty(const std::string& key, const std::string& value) {
    std::unique_lock<std::shared_mutex> lock(link_mutex_);
    properties_[key] = value;
}

std::string GraphLink::getProperty(const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(link_mutex_);
    auto it = properties_.find(key);
    return (it != properties_.end()) ? it->second : "";
}

double GraphLink::propagateSignal(double input_signal) const {
    // Different propagation rules based on link type
    switch (type_) {
        case LinkType::CAUSAL:
            return input_signal * weight_ * 0.8;
        case LinkType::IMPLIES:
            return input_signal * weight_ * 0.9;
        case LinkType::SUPPORTS:
            return input_signal * weight_ * 0.6;
        case LinkType::CONTRADICTS:
            return -input_signal * weight_ * 0.7;
        case LinkType::SEQUENTIAL:
            return input_signal * weight_ * 0.5;
        case LinkType::HIERARCHICAL:
            return input_signal * weight_ * 0.4;
        case LinkType::ASSOCIATIVE:
            return input_signal * weight_ * 0.3;
        default:
            return input_signal * weight_ * 0.5;
    }
}

std::string GraphLink::serialize() const {
    std::shared_lock<std::shared_mutex> lock(link_mutex_);
    std::ostringstream oss;
    oss << "lid:" << numeric_id_ << "|name:" << name_
        << "|source:" << source_->getNumericId()
        << "|target:" << target_->getNumericId()
        << "|type:" << static_cast<int>(type_)
        << "|weight:" << weight_;

    for (const auto& prop : properties_) {
        oss << "|" << prop.first << ":" << prop.second;
    }

    return oss.str();
}

void GraphLink::deserialize(const std::string& data) {
    std::unique_lock<std::shared_mutex> lock(link_mutex_);
    std::istringstream iss(data);
    std::string token;

    while (std::getline(iss, token, '|')) {
        size_t pos = token.find(':');
        if (pos != std::string::npos) {
            std::string key = token.substr(0, pos);
            std::string value = token.substr(pos + 1);

            if (key == "lid") {
                numeric_id_ = std::stoul(value);
            } else if (key == "name") {
                name_ = value;
            } else if (key == "type") {
                type_ = static_cast<LinkType>(std::stoi(value));
            } else if (key == "weight") {
                weight_ = std::stod(value);
            } else {
                properties_[key] = value;
            }
        }
    }
}

// ConceptNode Implementation
void ConceptNode::addAttribute(const std::string& key, const std::string& value) {
    setProperty("attr_" + key, value);
}

std::vector<std::string> ConceptNode::getAttributes() const {
    std::vector<std::string> attributes;
    const auto& props = getProperties();

    for (const auto& prop : props) {
        if (prop.first.substr(0, 5) == "attr_") {
            attributes.push_back(prop.first.substr(5) + ":" + prop.second);
        }
    }

    return attributes;
}

void ConceptNode::activate(const std::unordered_map<std::string, double>& context) {
    GraphNode::activate(context);

    // Additional concept-specific activation
    std::string category = getProperty("category");
    if (!category.empty()) {
        auto it = context.find("category_" + category);
        if (it != context.end()) {
            setActivation(getActivation() + it->second * 0.2);
        }
    }
}

// RuleNode Implementation
void RuleNode::setAntecedents(const std::vector<std::string>& antecedents) {
    antecedents_ = antecedents;
    setProperty("antecedent_count", std::to_string(antecedents.size()));
}

void RuleNode::setConsequent(const std::string& consequent) {
    consequent_ = consequent;
    setProperty("consequent", consequent);
}

bool RuleNode::evaluate(const std::unordered_map<std::string, bool>& facts) const {
    // Check if all antecedents are true
    for (const auto& antecedent : antecedents_) {
        auto it = facts.find(antecedent);
        if (it == facts.end() || !it->second) {
            return false;
        }
    }
    return true;
}

void RuleNode::activate(const std::unordered_map<std::string, double>& context) {
    GraphNode::activate(context);

    // Rule-specific activation based on antecedent truth values
    double rule_activation = 0.0;
    for (const auto& antecedent : antecedents_) {
        auto it = context.find(antecedent);
        if (it != context.end()) {
            rule_activation += it->second;
        }
    }

    if (!antecedents_.empty()) {
        rule_activation /= antecedents_.size();
        setActivation(std::max(getActivation(), rule_activation));
    }
}

// InferenceNode Implementation
void InferenceNode::setReasoningPath(const std::string& path) {
    reasoning_path_ = path;
    setProperty("reasoning_path", path);
}

void InferenceNode::addEvidence(std::shared_ptr<GraphNode> evidence) {
    evidence_nodes_.push_back(evidence);
    setProperty("evidence_count", std::to_string(evidence_nodes_.size()));
}

double InferenceNode::calculateConfidence() const {
    if (evidence_nodes_.empty()) return 0.5;

    double total_confidence = 0.0;
    for (const auto& evidence : evidence_nodes_) {
        total_confidence += evidence->getConfidence();
    }

    return total_confidence / evidence_nodes_.size();
}

void InferenceNode::activate(const std::unordered_map<std::string, double>& context) {
    GraphNode::activate(context);

    // Update confidence based on evidence
    double evidence_confidence = calculateConfidence();
    setConfidence(evidence_confidence);

    // Adjust activation based on confidence
    setActivation(getActivation() * evidence_confidence);
}

} // namespace dynabolic