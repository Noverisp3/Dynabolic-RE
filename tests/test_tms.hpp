#ifndef TMS_TEST_HPP
#define TMS_TEST_HPP

#include "reasoning_engine.hpp"
#include <cassert>
#include <iostream>
#include <vector>

namespace dynabolic {

void testTMS() {
    LogicProcessor lp;

    // 1. Setup rules
    // Rule 1: A -> B
    auto rule1 = std::make_shared<RuleNode>("rule1");
    rule1->setAntecedents({"A"});
    rule1->setConsequent("B");
    rule1->setProperty("consequent", "B");
    lp.addRule(rule1);

    // Rule 2: B -> C
    auto rule2 = std::make_shared<RuleNode>("rule2");
    rule2->setAntecedents({"B"});
    rule2->setConsequent("C");
    rule2->setProperty("consequent", "C");
    lp.addRule(rule2);

    std::cout << "Running TMS Tests..." << std::endl;

    // 2. Add explicit fact A
    lp.addFact("A", true, true);
    lp.deduceFacts();

    assert(lp.getFact("A") == true);
    assert(lp.getFact("B") == true);
    assert(lp.getFact("C") == true);
    std::cout << "Step 1: Forward deduction OK (A -> B -> C)" << std::endl;

    // 3. Retract A
    lp.retractFact("A");
    
    // In a real TMS, retractFact should be enough. 
    // Our implementation of retractFact is recursive.
    
    assert(lp.hasFact("A") == false);
    assert(lp.hasFact("B") == false);
    assert(lp.hasFact("C") == false);
    std::cout << "Step 2: Recursive retraction OK (Retract A -> Retract B -> Retract C)" << std::endl;

    // 4. Multiple justifications
    // Rule 3: D -> C
    auto rule3 = std::make_shared<RuleNode>("rule3");
    rule3->setAntecedents({"D"});
    rule3->setConsequent("C");
    rule3->setProperty("consequent", "C");
    lp.addRule(rule3);

    lp.addFact("A", true, true);
    lp.addFact("D", true, true);
    lp.deduceFacts();

    assert(lp.getFact("C") == true);
    
    // Retract A, C should still be there because of D
    lp.retractFact("A");
    assert(lp.hasFact("B") == false);
    assert(lp.hasFact("C") == true);
    std::cout << "Step 3: Multiple justification OK (Retract A, C still supported by D)" << std::endl;

    // Retract D, C should finally disappear
    lp.retractFact("D");
    assert(lp.hasFact("C") == false);
    std::cout << "Step 4: Final retraction OK" << std::endl;

    std::cout << "All TMS tests passed!" << std::endl;
}

} // namespace dynabolic

#endif
