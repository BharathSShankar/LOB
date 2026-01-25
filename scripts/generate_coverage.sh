#!/bin/bash

# Script to generate test coverage report
# Usage: ./scripts/generate_coverage.sh

set -e

echo "========================================="
echo "Generating Test Coverage Report"
echo "========================================="

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Create build directory for coverage
COVERAGE_DIR="build_coverage"
echo -e "${BLUE}Step 1: Creating coverage build directory...${NC}"
mkdir -p "$COVERAGE_DIR"
cd "$COVERAGE_DIR"

# Configure with coverage enabled
echo -e "${BLUE}Step 2: Configuring CMake with coverage enabled...${NC}"
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON ..

# Build the project
echo -e "${BLUE}Step 3: Building project...${NC}"
cmake --build . -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Detect compiler
COMPILER=$(grep "CMAKE_CXX_COMPILER_ID" CMakeCache.txt | cut -d= -f2)
echo "Detected compiler: $COMPILER"

# Run tests to generate coverage data
echo -e "${BLUE}Step 4: Running tests to generate coverage data...${NC}"

if [[ "$COMPILER" == *"Clang"* ]]; then
    # For Clang, set LLVM_PROFILE_FILE environment variable
    export LLVM_PROFILE_FILE="coverage-%p.profraw"
    ctest --output-on-failure
elif [[ "$COMPILER" == *"GNU"* ]]; then
    ctest --output-on-failure
else
    ctest --output-on-failure
fi

# Generate coverage report
echo -e "${BLUE}Step 5: Generating coverage report...${NC}"

# Detect macOS and set up LLVM tools
LLVM_PROFDATA="llvm-profdata"
LLVM_COV="llvm-cov"

if [[ "$OSTYPE" == "darwin"* ]]; then
    # On macOS, use xcrun to access LLVM tools
    if xcrun llvm-profdata --version &> /dev/null; then
        LLVM_PROFDATA="xcrun llvm-profdata"
        LLVM_COV="xcrun llvm-cov"
        echo "Detected macOS: Using xcrun for LLVM tools"
    fi
fi

if [[ "$COMPILER" == *"Clang"* ]] && $LLVM_PROFDATA --version &> /dev/null && $LLVM_COV --version &> /dev/null; then
    echo "Using llvm-cov for Clang coverage..."
    
    # Merge profraw files
    $LLVM_PROFDATA merge -sparse coverage-*.profraw -o coverage.profdata
    
    # Generate HTML report
    $LLVM_COV show ./lob_tests -instr-profile=coverage.profdata \
        -format=html -output-dir=coverage_html \
        -ignore-filename-regex='.*/(tests|_deps)/.*'
    
    # Generate summary
    echo ""
    $LLVM_COV report ./lob_tests -instr-profile=coverage.profdata \
        -ignore-filename-regex='.*/(tests|_deps)/.*'
    
    echo -e "${GREEN}=========================================${NC}"
    echo -e "${GREEN}Coverage report generated successfully!${NC}"
    echo -e "${GREEN}=========================================${NC}"
    echo ""
    echo -e "View the report by opening: ${YELLOW}$(pwd)/coverage_html/index.html${NC}"
    
elif command -v lcov &> /dev/null; then
    echo "Using lcov to generate HTML coverage report..."
    
    # Capture coverage data
    lcov --capture --directory . --output-file coverage.info
    
    # Remove external library coverage (googletest, etc.)
    lcov --remove coverage.info \
        '/usr/*' \
        '*/build_coverage/_deps/*' \
        '*/tests/*' \
        --output-file coverage_filtered.info
    
    # Generate HTML report
    genhtml coverage_filtered.info --output-directory coverage_html
    
    echo -e "${GREEN}=========================================${NC}"
    echo -e "${GREEN}Coverage report generated successfully!${NC}"
    echo -e "${GREEN}=========================================${NC}"
    echo ""
    echo -e "View the report by opening: ${YELLOW}$COVERAGE_DIR/coverage_html/index.html${NC}"
    echo ""
    echo "Summary:"
    lcov --summary coverage_filtered.info
    
elif command -v gcovr &> /dev/null; then
    echo "Using gcovr to generate coverage report..."
    
    # Generate HTML report
    gcovr --html-details coverage_html/index.html \
          --exclude-unreachable-branches \
          --exclude-throw-branches \
          --exclude '.*/tests/.*' \
          --exclude '.*/build_coverage/_deps/.*' \
          --print-summary
    
    echo -e "${GREEN}=========================================${NC}"
    echo -e "${GREEN}Coverage report generated successfully!${NC}"
    echo -e "${GREEN}=========================================${NC}"
    echo ""
    echo -e "View the report by opening: ${YELLOW}$COVERAGE_DIR/coverage_html/index.html${NC}"
    
else
    echo "Generating basic coverage info with gcov..."
    
    # Find all .gcno files and run gcov
    find . -name "*.gcno" -exec gcov {} \;
    
    echo -e "${YELLOW}=========================================${NC}"
    echo -e "${YELLOW}Basic coverage data generated.${NC}"
    echo -e "${YELLOW}=========================================${NC}"
    echo ""
    echo "Coverage files (*.gcov) generated in current directory."
    echo ""
    echo -e "${YELLOW}For better visualization, install lcov, gcovr, or llvm-cov:${NC}"
    echo "  - Ubuntu/Debian: sudo apt-get install lcov"
    echo "  - macOS (GCC): brew install lcov"
    echo "  - macOS (Clang): llvm-cov should be available with Xcode"
    echo "  - Python: pip install gcovr"
    echo ""
    echo "Note: If using Clang on macOS, llvm-cov is recommended."
    echo "      Try: xcrun llvm-cov --version"
fi

cd ..

echo ""
echo -e "${GREEN}Done!${NC}"
echo ""
echo -e "${BLUE}To view coverage in terminal:${NC}"
if [[ "$COMPILER" == *"Clang"* ]]; then
    if [[ "$OSTYPE" == "darwin"* ]]; then
        echo "  xcrun llvm-cov report build_coverage/lob_tests -instr-profile=build_coverage/coverage.profdata"
    else
        echo "  llvm-cov report build_coverage/lob_tests -instr-profile=build_coverage/coverage.profdata"
    fi
else
    echo "  lcov --summary build_coverage/coverage_filtered.info"
fi
