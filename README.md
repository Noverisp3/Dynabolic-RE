# Dynabolic-RE Documentation

## Overview

Dynabolic-RE is a **matrix-free reasoning engine** using graph-based reasoning with only the C++ standard library. It eliminates all matrix operations (MatMul) by using native graph structures designed for reasoning tasks.

**Key Achievement**: 100% matrix-free reasoning using only `<vector>`, `<map>`, `<thread>`, `<memory>` - no external dependencies.

## Why Pure C++ Without Matrix Libraries?

Modern AI frameworks (PyTorch, TensorFlow, NumPy) are **100% optimized for matrix multiplication**. Building graph-based reasoning on them is like using a sledgehammer for surgery - you're fighting the tool every step.

**Our Solution**: Pure C++ standard library provides:
- **Explicit Control**: Every operation visible and controllable
- **Native Graph Semantics**: Data structures designed for graphs, not matrices
- **Deep Memory Access**: Full control over memory layout
- **Zero Dependencies**: Works anywhere with C++17 compiler
- **True Performance**: Optimized for actual problem domain

## Architecture

### Core Components

**Graph Foundation**
- `GraphNode`: Base class with 7 types (Concept, Fact, Rule, Query, Inference, Memory, Control)
- `GraphLink`: 7 relationship types (Causal, Implies, Supports, Contradicts, Sequential, Hierarchical, Associative)
- Thread-safe operations with mutex protection

**Chain-of-Links Reasoning**
- Path tracing with cycle detection
- Signal propagation with link-type-specific rules
- Causal chain analysis for explainable reasoning

**Logic Processing**
- Pure logical operations: AND, OR, NOT, IMPLIES
- **Non-Monotonic Reasoning (TMS)**: Full Truth Maintenance System with dependency tracking.
- **Recursive Retraction**: Automatic removal of derived conclusions when supporting facts are retracted.
- Rule evaluation against fact databases
- Forward chaining deduction
- Contradiction detection

**Probabilistic & Temporal Reasoning**
- **Bayesian Processor**: Probabilistic inference with entropy, posterior calculation, and mutual information.
- **Confidence-Based Firing**: Rules only fire when antecedent confidence exceeds a configurable threshold.
- **Temporal Trends**: Time-stamped fact history with trend detection (INCREASING, DECREASING, STABLE).
- **Process Understanding**: Ability to reason about trends (e.g., "Reactor-7 temperature is rising") instead of just static values.

**Multi-threaded Engine**
- Producer-consumer task queue pattern
- Configurable worker threads
- 5 task types: Activate, Propagate, Evaluate, Update, Cleanup
- Atomic performance metrics

**Specialized Reasoners**
- Forward Chaining: Fact-to-conclusion deduction
- Backward Chaining: Goal-to-fact reasoning
- Analogical Reasoning: Similarity-based knowledge transfer

### Memory & Performance

**Memory Efficiency**
- Sparse graph representation: O(V+E) vs O(V²) for matrices
- Smart pointer management (std::shared_ptr, std::unique_ptr)
- No large weight matrices
- Property compression with string-keyed metadata

**Performance**
- Node operations: O(1) lookup with std::map
- Path finding: O(V+E) with visited set
- Rule evaluation: O(n) where n = antecedents
- Multi-threaded scaling: Linear with worker count
- Benchmarks: ~1.25M nodes/sec, ~666K links/sec

## Project Structure

```
Dynabolic-RE/
├── include/                    # Public API headers
│   ├── graph_node.hpp          # Core graph structures
│   ├── reasoning_engine.hpp    # Multi-threaded engine
│   └── json_parser.hpp         # JSON persistence
│   └── object_pool.hpp         # Memory pool
├── src/                        # Implementation files
│   ├── graph_node.cpp
│   ├── reasoning_engine.cpp
│   └── json_parser.cpp
├── examples/                   # Example programs
│   ├── demo.cpp                # Comprehensive C++ demo
│   └── tweety_penguin.py       # End-to-end LLM-symbolic hybrid demo
├── tools/                      # Standalone CLIs
│   ├── dynabolic_solver.cpp    # JSON-in/JSON-out forward-chaining solver
│   └── README.md               # Solver schema + usage
├── dynabolic_re/               # Python LLM-orchestrator (stdlib only)
│   ├── provider.py             # Ollama / OpenAI / Anthropic / Mock
│   ├── extractor.py            # NL -> problem JSON
│   ├── verbalizer.py           # chain -> NL answer
│   ├── pipeline.py             # extract -> solve -> verbalise
│   └── cli.py                  # `python -m dynabolic_re "..."`
├── tests/                      # Unit tests
│   ├── test_graph.cpp          # Graph structure and logic tests
│   ├── test_tms.hpp            # TMS, Bayesian, and Temporal tests
│   ├── test_solver.sh          # Smoke tests for dynabolic_solver
│   ├── test_dynabolic_re.py    # Orchestrator tests (use MockProvider)
│   └── CMakeLists.txt
├── CMakeLists.txt              # CMake build (cross-platform)
├── Makefile                    # Make build (Linux/Mac)
├── build.bat                   # Windows build script
└── README.md                   # This file
```

## Build Instructions

### Linux/Mac
```bash
# Using Make
make
./build/dynabolic_demo

# Using CMake
mkdir build && cd build
cmake ..
make
./dynabolic_demo
```

### Windows
```cmd
build.bat
```

## Usage Examples

### Basic Graph Operations
```cpp
auto mammal = std::make_shared<ConceptNode>("mammal");
mammal->setProperty("warm_blooded", "true");

auto dog = std::make_shared<ConceptNode>("dog");
auto link = std::make_shared<GraphLink>("link1", mammal, dog, 
                                        LinkType::HIERARCHICAL, 0.8);
```

### Logical & Probabilistic Reasoning
```cpp
LogicProcessor logic;
BayesianProcessor bayesian;
logic.setBayesianProcessor(&bayesian);
logic.setFiringThreshold(0.7); // Only fire rules if confidence > 0.7

// Temporal facts
logic.addTemporalFact("temp_high", true, 0.4, 1000); // t=1000, conf=0.4
logic.addTemporalFact("temp_high", true, 0.9, 2000); // t=2000, conf=0.9

// Trend detection
auto trend = logic.calculateTrend("temp_high", 5000); // INCREASING
```

### Chain Reasoning
```cpp
ReasoningEngine engine;
auto path = engine.performChainReasoning("rain", "accident");
```

### Multi-threaded Processing
```cpp
ReasoningEngine engine(4); // 4 workers
engine.start();
engine.activateNodeAsync("node_id", context);
engine.waitForCompletion();
```

## LLM-symbolic hybrid (`dynabolic_re`)

A Python orchestrator that wraps an LLM around the C++ solver:

```
NL question -> [LLM extractor] -> facts/rules/goal JSON
             -> [dynabolic_solver] -> derivation chain + final facts
             -> [LLM verbaliser] -> NL answer with reasoning
```

The LLM does **only** extraction and verbalisation. All multi-step
reasoning runs in the C++ engine, so every conclusion is grounded in a
verifiable chain of rule firings. No external Python dependencies.

Quickstart against a local Ollama:

```bash
# 1. Build the solver
make

# 2. Start an Ollama server with a model
ollama serve &
ollama pull llama3.1:8b

# 3. Run the Tweety demo
python3 examples/tweety_penguin.py

# 4. Or ask anything else
python3 -m dynabolic_re \
  "All humans are mammals. Mary is human. Is Mary a mammal?"
```

Or against OpenAI / Anthropic:

```bash
DYNABOLIC_RE_PROVIDER=openai    OPENAI_API_KEY=...    python3 -m dynabolic_re "..."
DYNABOLIC_RE_PROVIDER=anthropic ANTHROPIC_API_KEY=... python3 -m dynabolic_re "..."
```

Or offline against a canned mock (no LLM at all — useful for CI / smoke tests):

```bash
DYNABOLIC_RE_PROVIDER=mock python3 examples/tweety_penguin.py
```

Run the unit tests:

```bash
make                              # build solver first
python3 -m unittest tests/test_dynabolic_re.py
```

## Applications

- **Chain-of-thought reasoning**: Explicit reasoning paths
- **Mathematical problem solving**: Logical deduction
- **Knowledge graph reasoning**: Native graph operations
- **Explainable AI**: Transparent reasoning chains
- **Causal inference**: Built-in causal relationships
- **Legal/medical reasoning**: Rule-based decision making

## Requirements

- C++17 compatible compiler (g++ 7+, clang 5+, MSVC 2017+)
- pthread library (usually included with compiler)
- CMake 3.10+ (optional, for CMake builds)

## License

Research prototype. Free to use and modify for educational and research purposes.