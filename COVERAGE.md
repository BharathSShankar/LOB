# Test Coverage Guide

This document explains how to generate and view code coverage reports for the LOB project.

## Overview

The project has **83% line coverage** across all source files (excluding tests and external dependencies).

### Current Coverage Summary

| Component              | Lines Coverage | Functions Coverage | Branch Coverage |
| ---------------------- | -------------- | ------------------ | --------------- |
| **Order.cpp**          | 100.00%        | 100.00%            | 83.33%          |
| **MatchingEngine.cpp** | 87.78%         | 100.00%            | 82.14%          |
| **OrderBook.cpp**      | 77.43%         | 87.50%             | 64.91%          |
| **ObjectPool.h**       | 93.62%         | 100.00%            | 78.57%          |
| **RingBuffer.h**       | 89.06%         | 100.00%            | 75.00%          |
| **TOTAL**              | **83.49%**     | **96.15%**         | **70.11%**      |

## Quick Start

### Generate Coverage Report (One Command)

```bash
./scripts/generate_coverage.sh
```

This script will:
1. Create a separate build directory (`build_coverage`)
2. Configure CMake with coverage enabled
3. Build the project
4. Run all tests
5. Generate coverage report (HTML if tools available)

### View Coverage Report

After running the script:

**HTML Report (Best for detailed analysis):**
```bash
open build_coverage/coverage_html/index.html
```

**Terminal Report (Quick summary):**
```bash
cd build_coverage
xcrun llvm-cov report ./lob_tests -instr-profile=coverage.profdata \
    -ignore-filename-regex='.*/(tests|_deps)/.*'
```

## Manual Coverage Generation

If you prefer to generate coverage manually:

### Step 1: Configure with Coverage

```bash
mkdir -p build_coverage
cd build_coverage
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON ..
```

### Step 2: Build

```bash
cmake --build . -j$(sysctl -n hw.ncpu)
```

### Step 3: Run Tests

```bash
# Set profile file location
export LLVM_PROFILE_FILE="coverage-%p.profraw"
./lob_tests
```

### Step 4: Generate Report

**Create merged profile data:**
```bash
xcrun llvm-profdata merge -sparse *.profraw -o coverage.profdata
```

**View summary in terminal:**
```bash
xcrun llvm-cov report ./lob_tests -instr-profile=coverage.profdata \
    -ignore-filename-regex='.*/(tests|_deps)/.*'
```

**Generate HTML report:**
```bash
xcrun llvm-cov show ./lob_tests -instr-profile=coverage.profdata \
    -format=html -output-dir=coverage_html \
    -ignore-filename-regex='.*/(tests|_deps)/.*'
```

**View specific file coverage:**
```bash
xcrun llvm-cov show ./lob_tests -instr-profile=coverage.profdata \
    ../src/core/MatchingEngine.cpp
```

## Tool Requirements

### macOS (Current Setup)
- **Compiler**: Clang (comes with Xcode)
- **Coverage Tool**: `llvm-cov` (included with Xcode Command Line Tools)
- **Profile Tool**: `llvm-profdata` (included with Xcode Command Line Tools)

No additional installation required! ✅

### Linux with GCC
Install lcov or gcovr:
```bash
# Ubuntu/Debian
sudo apt-get install lcov

# Or install gcovr
pip install gcovr
```

Then run:
```bash
./scripts/generate_coverage.sh
```

### Linux with Clang
```bash
# Ensure llvm tools are available
sudo apt-get install llvm

./scripts/generate_coverage.sh
```

## Understanding Coverage Metrics

### Line Coverage
Percentage of code lines executed during tests.
- **100%**: All lines executed (Order.cpp) ✅
- **88%**: Most lines executed, excellent coverage (MatchingEngine.cpp) ✅
- **77%**: Good coverage, some edge cases remain (OrderBook.cpp)

### Function Coverage
Percentage of functions called during tests.
- Currently at **96.15%** - excellent! ✅

### Branch Coverage
Percentage of conditional branches (if/else, switch, loops) taken.
- Currently at **70.11%** - good coverage
- OrderBook.cpp branch coverage (65%) indicates some conditional paths not fully tested

## Improving Coverage

### Areas Needing Attention

1. **OrderBook.cpp (77% line coverage, 65% branch coverage)**
   - Some market order edge cases
   - Certain error handling paths
   - Complex order cancellation scenarios

2. **Branch Coverage (70% overall)**
   - Additional tests for error conditions
   - Edge case scenarios in order matching
   - Complex multi-level price matching paths

### Adding New Tests

When adding tests to improve coverage:

1. Run coverage to identify uncovered lines
2. Write tests targeting those specific lines
3. Verify coverage improved
4. Commit tests with coverage report

Example workflow:
```bash
# Generate initial coverage
./scripts/generate_coverage.sh

# Check which lines are uncovered in a file
cd build_coverage
xcrun llvm-cov show ./lob_tests -instr-profile=coverage.profdata \
    ../src/core/OrderBook.cpp | less

# Add tests to tests/core/OrderBookTest.cpp
# ... edit tests ...

# Rebuild and check coverage again
cmake --build . && LLVM_PROFILE_FILE="coverage-%p.profraw" ./lob_tests
xcrun llvm-profdata merge -sparse *.profraw -o coverage.profdata
xcrun llvm-cov report ./lob_tests -instr-profile=coverage.profdata
```

## Continuous Integration

For CI/CD pipelines, add coverage generation:

```yaml
# Example GitHub Actions
- name: Generate Coverage
  run: |
    cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON -B build_coverage
    cmake --build build_coverage
    cd build_coverage
    export LLVM_PROFILE_FILE="coverage-%p.profraw"
    ./lob_tests
    llvm-profdata merge -sparse *.profraw -o coverage.profdata
    llvm-cov report ./lob_tests -instr-profile=coverage.profdata

- name: Upload Coverage Report
  run: |
    llvm-cov export ./lob_tests -instr-profile=coverage.profdata \
      -format=lcov > coverage.lcov
  # Upload to codecov, coveralls, etc.
```

## Troubleshooting

### No .profraw files generated
- Ensure LLVM_PROFILE_FILE environment variable is set
- Check that coverage flags are enabled in CMake
- Verify tests are actually running

### "Profile data out of date" error
- Clean and rebuild: `rm -rf build_coverage && ./scripts/generate_coverage.sh`
- Ensure you're using the same compiler for build and coverage

### Coverage seems too low
- Make sure you're running ALL tests: `./lob_tests`
- Check that you're filtering out test files in the report
- Verify external dependencies (_deps/) are excluded

### HTML report not generated
- Check if llvm-cov is available: `xcrun llvm-cov --version`
- Try installing lcov: `brew install lcov` (macOS) or `apt-get install lcov` (Linux)
- Or install gcovr: `pip install gcovr`

## Best Practices

1. **Run coverage regularly** - Catch coverage regressions early
2. **Aim for 80%+ line coverage** - Good balance of thoroughness
3. **Focus on branch coverage** - More important than line coverage
4. **Don't chase 100%** - Some unreachable code is acceptable
5. **Document uncovered code** - If intentionally not tested, add comments
6. **Review coverage in PRs** - Ensure new code is tested

## Additional Resources

- [LLVM Coverage Mapping Format](https://llvm.org/docs/CoverageMappingFormat.html)
- [Clang Source-based Code Coverage](https://clang.llvm.org/docs/SourceBasedCodeCoverage.html)
- [Google Test Documentation](https://google.github.io/googletest/)

---

**Last Updated**: 2026-01-25 15:18 IST
**Project**: High-Performance Limit Order Book
**Test Framework**: Google Test
**Coverage Tool**: LLVM Coverage (Clang)
