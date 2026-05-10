# Makefile for Dynabolic-LM - Pure C++ Graph-Based AI Architecture
# Organized project structure with separate include, src, and build directories

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread -I./include
LDFLAGS = -pthread

# Directories
SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build
EXAMPLES_DIR = examples
DOCS_DIR = docs

# Source files
SOURCES = $(SRC_DIR)/graph_node.cpp $(SRC_DIR)/reasoning_engine.cpp $(SRC_DIR)/json_parser.cpp
OBJECTS = $(BUILD_DIR)/graph_node.o $(BUILD_DIR)/reasoning_engine.o $(BUILD_DIR)/json_parser.o
EXECUTABLE = $(BUILD_DIR)/dynabolic_demo

# Tools (solver CLIs)
TOOLS_DIR = tools
TOOL_SOURCES = $(wildcard $(TOOLS_DIR)/*.cpp)
TOOLS = $(patsubst $(TOOLS_DIR)/%.cpp,$(BUILD_DIR)/%,$(TOOL_SOURCES))

# Example programs
EXAMPLE_SOURCES = $(wildcard $(EXAMPLES_DIR)/*.cpp)
EXAMPLES = $(patsubst $(EXAMPLES_DIR)/%.cpp,$(BUILD_DIR)/%,$(EXAMPLE_SOURCES))

# Default target
all: directories $(EXECUTABLE) examples tools

# Create build directories
directories:
	@mkdir -p $(BUILD_DIR)

# Build the main demo executable
$(EXECUTABLE): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) $(EXAMPLES_DIR)/demo.cpp -o $(EXECUTABLE) $(LDFLAGS)
	@echo "Build complete: $(EXECUTABLE)"

# Compile source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Build example programs
examples: $(EXAMPLES)

$(BUILD_DIR)/%: $(EXAMPLES_DIR)/%.cpp $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) $< -o $@ $(LDFLAGS)

# Build CLI tools (e.g. dynabolic_solver)
tools: $(TOOLS)

$(BUILD_DIR)/%: $(TOOLS_DIR)/%.cpp $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) $< -o $@ $(LDFLAGS)

# Build and run the main demo
run: $(EXECUTABLE)
	./$(EXECUTABLE)

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)/*.o $(BUILD_DIR)/*_demo
	@echo "Clean complete"

# Deep clean (remove entire build directory)
distclean: clean
	rm -rf $(BUILD_DIR)
	@echo "Deep clean complete"

# Build and run
test: clean all run

# Install (optional - copies to /usr/local/bin)
install: $(EXECUTABLE)
	install -m 755 $(EXECUTABLE) /usr/local/bin/dynabolic_demo
	@echo "Installed to /usr/local/bin/dynabolic_demo"

# Uninstall
uninstall:
	rm -f /usr/local/bin/dynabolic_demo
	@echo "Uninstalled from /usr/local/bin/dynabolic_demo"

# Generate documentation
docs:
	@echo "Documentation available in $(DOCS_DIR)/"
	@ls -la $(DOCS_DIR)/

# Show project structure
tree:
	@echo "Project Structure:"
	@tree -L 2 --charset ascii 2>/dev/null || find . -maxdepth 2 -type d | head -10

# Help target
help:
	@echo "Dynabolic-LM Makefile"
	@echo "===================="
	@echo "Available targets:"
	@echo "  all         - Build everything (default)"
	@echo "  directories - Create build directories"
	@echo "  run         - Build and run the main demo"
	@echo "  examples    - Build example programs"
	@echo "  clean       - Remove build artifacts"
	@echo "  distclean   - Remove entire build directory"
	@echo "  test        - Clean, build, and run"
	@echo "  install     - Install to /usr/local/bin"
	@echo "  uninstall   - Remove from /usr/local/bin"
	@echo "  docs        - Show documentation"
	@echo "  tree        - Show project structure"
	@echo "  help        - Show this help message"
	@echo ""
	@echo "Project Structure:"
	@echo "  include/    - Header files"
	@echo "  src/        - Source files"
	@echo "  examples/   - Example programs"
	@echo "  build/      - Build output"
	@echo "  docs/       - Documentation"

.PHONY: all directories run clean distclean test install uninstall docs examples tools tree help