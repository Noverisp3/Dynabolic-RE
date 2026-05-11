#include "../include/graph_node.hpp"
#include "test_tms.hpp"
#include <iostream>
#include <cassert>

using namespace dynabolic;

void test_graph_node_creation() {
    std::cout << "Testing GraphNode creation..." << std::endl;
    
    auto node = std::make_shared<ConceptNode>("test_node");
    assert(node->getId() == "test_node");
    assert(node->getType() == NodeType::CONCEPT);
    
    node->setProperty("key", "value");
    assert(node->getProperty("key") == "value");
    assert(node->hasProperty("key") == true);
    
    std::cout << "✓ GraphNode creation test passed" << std::endl;
}

void test_graph_link_creation() {
    std::cout << "Testing GraphLink creation..." << std::endl;
    
    auto node1 = std::make_shared<ConceptNode>("node1");
    auto node2 = std::make_shared<ConceptNode>("node2");
    
    auto link = std::make_shared<GraphLink>("link1", node1, node2, 
                                              LinkType::CAUSAL, 0.8);
    assert(link->getId() == "link1");
    assert(link->getType() == LinkType::CAUSAL);
    assert(link->getWeight() == 0.8);
    assert(link->getSource() == node1);
    assert(link->getTarget() == node2);
    
    std::cout << "✓ GraphLink creation test passed" << std::endl;
}

void test_activation_propagation() {
    std::cout << "Testing activation propagation..." << std::endl;
    
    auto node1 = std::make_shared<ConceptNode>("node1");
    auto node2 = std::make_shared<ConceptNode>("node2");
    
    auto link = std::make_shared<GraphLink>("link1", node1, node2, 
                                              LinkType::CAUSAL, 0.8);
    
    node1->addOutgoingLink(link);
    node2->addIncomingLink(link);
    
    node1->setActivation(1.0);
    node1->propagate();
    
    assert(node2->getActivation() > 0.0);
    
    std::cout << "✓ Activation propagation test passed" << std::endl;
}

void test_serialization() {
    std::cout << "Testing serialization..." << std::endl;
    
    auto node = std::make_shared<ConceptNode>("test_node");
    node->setProperty("key1", "value1");
    node->setProperty("key2", "value2");
    node->setActivation(0.5);
    node->setConfidence(0.8);
    
    std::string serialized = node->serialize();
    assert(!serialized.empty());
    
    auto new_node = std::make_shared<ConceptNode>("new_node");
    new_node->deserialize(serialized);
    
    assert(new_node->getId() == "test_node");
    assert(new_node->getProperty("key1") == "value1");
    assert(new_node->getProperty("key2") == "value2");
    
    std::cout << "✓ Serialization test passed" << std::endl;
}

int main() {
    std::cout << "Running Dynabolic-RE Tests" << std::endl;
    std::cout << "=============================" << std::endl;
    
    try {
        test_graph_node_creation();
        test_graph_link_creation();
        test_activation_propagation();
        test_serialization();
        testTMS();
        
        std::cout << "\n=============================" << std::endl;
        std::cout << "All tests passed successfully!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}