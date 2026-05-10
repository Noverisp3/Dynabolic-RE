# Dynabolic-LM
A Chain Graph Reasoning Language Model - Pure C++ Matrix-Free AI Architecture

## Overview

Dynabolic-LM is a revolutionary AI architecture that completely eliminates matrix operations (MatMul) by using graph-based reasoning with only the C++ standard library.

## Quick Start

### Prerequisites
- C++17 compatible compiler (g++ 7+, clang 5+, MSVC 2017+)
- CMake 3.10+ (optional, for CMake build)
- pthread library (usually included with compiler)

### Build Instructions

#### Using Make (Linux/Mac)
```bash
make
./build/dynabolic_demo
```

#### Using CMake (Cross-platform)
```bash
mkdir build && cd build
cmake ..
make
./dynabolic_demo
```

#### Using Visual Studio (Windows)
```cmd
build.bat
```

## Project Structure

```
Dynabolic-LM/
├── include/              # Header files
│   ├── graph_node.hpp
│   ├── reasoning_engine.hpp
│   └── json_parser.hpp
├── src/                  # Source files
│   ├── graph_node.cpp
│   ├── reasoning_engine.cpp
│   └── json_parser.cpp
├── examples/             # Example programs
│   └── demo.cpp
├── tests/                # Unit tests
│   ├── test_graph.cpp
│   └── CMakeLists.txt
├── docs/                 # Documentation
│   ├── README.md         # Detailed documentation
│   ├── ARCHITECTURE.md   # Technical architecture
│   └── IMPLEMENTATION_SUMMARY.md
├── build/                # Build output (created during build)
├── CMakeLists.txt        # CMake build configuration
├── Makefile             # Make build configuration
└── build.bat            # Windows build script
```

## Documentation

- **[README.md](docs/README.md)** - Detailed user documentation and API reference
- **[ARCHITECTURE.md](docs/ARCHITECTURE.md)** - Technical architecture and design decisions
- **[IMPLEMENTATION_SUMMARY.md](docs/IMPLEMENTATION_SUMMARY.md)** - Implementation overview and achievements

## Key Features

**100% Matrix-Free**: No MatMul operations anywhere in the architecture  
**Pure Standard Library**: Only `<vector>`, `<map>`, `<thread>`, `<memory>`  
**Graph-Based Reasoning**: Chain-of-links, path tracing, signal propagation  
**Logic Processing**: AND, OR, NOT, IMPLIES, rule evaluation  
**Multi-threaded**: Worker pool with task queue, atomic metrics  
**Memory Efficient**: O(V+E) vs O(V²) for matrix approaches  
**Thread Safe**: Fine-grained locking, deadlock prevention  

## Running Examples

### Main Demo
```bash
make run
# or
./build/dynabolic_demo
```

### Run Tests
```bash
make test
# or with CMake
cd build && ctest --output-on-failure
```

## Build Targets

### Make
- `make` - Build everything
- `make run` - Build and run main demo
- `make clean` - Remove build artifacts
- `make test` - Run tests
- `make docs` - Show documentation
- `make help` - Show all available targets

### CMake
- `cmake ..` - Configure project
- `make` - Build project
- `make test` - Run tests
- `make doc_doxygen` - Generate API documentation (if Doxygen available)

## Performance

The demo includes benchmarks showing:
- **Node creation**: ~1.25M nodes/second
- **Link creation**: ~666K links/second
- **Async activation**: ~25K operations/second with 4 threads
- **Graph size**: Tested to 1000+ nodes, 2000+ links
- **Memory**: O(V+E) complexity for sparse graphs

## Design Philosophy

This architecture follows the principle that **graph-based reasoning should not be forced into matrix-optimized frameworks**. By using pure C++ with only standard library components:

1. **No Hidden Optimizations**: Every operation is explicit and controllable
2. **True Graph Semantics**: Native graph operations, not matrix approximations
3. **Deep Memory Access**: Full control over memory layout and access patterns
4. **Portable**: No external dependencies, works anywhere with a C++17 compiler
5. **Efficient**: Optimized for graph operations, not matrix operations

## Contributing

This is a research prototype. Contributions welcome for:
- Performance optimizations
- Additional reasoning strategies
- Extended node and link types
- Improved serialization formats
- Cross-platform testing

## License

This is a research prototype. Feel free to use and modify for educational and research purposes.

## Citation

If you use this work in your research, please cite:

```
Dynabolic-LM: A Pure C++ Matrix-Free Graph-Based AI Architecture
https://github.com/Noverisp3/Dynabolic-LM
```

## Acknowledgments

This architecture demonstrates that sophisticated AI reasoning can be built without matrix operations, using fundamental computer science concepts applied appropriately to the problem domain.