#!/bin/bash

# Comprehensive Profiling Script for LOB Matching Engine
# This script runs various profiling tools and generates reports

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build_profiling"
RESULTS_DIR="${PROJECT_ROOT}/profiling_results"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Detect OS
OS_TYPE="$(uname -s)"

echo -e "${BLUE}=============================================${NC}"
echo -e "${BLUE}  LOB Profiling & Optimization Suite${NC}"
echo -e "${BLUE}=============================================${NC}"
echo ""
echo "Operating System: ${OS_TYPE}"
echo "Project Root: ${PROJECT_ROOT}"
echo ""

# Create results directory
mkdir -p "${RESULTS_DIR}"
echo -e "${GREEN}✓${NC} Created results directory: ${RESULTS_DIR}"

# Function to run a profiling step
run_step() {
    local step_name=$1
    local step_desc=$2
    echo ""
    echo -e "${YELLOW}▶${NC} ${step_name}: ${step_desc}"
}

# Function to check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Step 1: Build with profiling enabled
run_step "STEP 1" "Building project with profiling support"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

cmake .. \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_BENCHMARK=ON \
    -DENABLE_MEMORY_PROFILING=ON \
    -DENABLE_HOTPATH_PROFILING=ON \
    -GNinja

ninja
echo -e "${GREEN}✓${NC} Build complete"

# Step 2: Run custom profiling benchmarks
run_step "STEP 2" "Running custom profiling benchmarks"
cd "${BUILD_DIR}"
./lob_profiling_benchmark \
    --benchmark_out="${RESULTS_DIR}/benchmark_results.json" \
    --benchmark_out_format=json \
    --benchmark_format=console | tee "${RESULTS_DIR}/benchmark_console_output.txt"

echo -e "${GREEN}✓${NC} Custom benchmarks complete"

# Step 3: Run standard benchmarks
run_step "STEP 3" "Running standard benchmarks"
./lob_benchmark \
    --benchmark_out="${RESULTS_DIR}/standard_benchmark_results.json" \
    --benchmark_out_format=json \
    --benchmark_format=console | tee "${RESULTS_DIR}/standard_benchmark_output.txt"

echo -e "${GREEN}✓${NC} Standard benchmarks complete"

# Step 4: Platform-specific profiling
if [ "${OS_TYPE}" = "Darwin" ]; then
    # macOS - Use Instruments
    run_step "STEP 4" "Running macOS Instruments profiling"
    
    if command_exists instruments; then
        echo "Running Time Profiler..."
        instruments -t "Time Profiler" -D "${RESULTS_DIR}/time_profile.trace" -l 10000 "${BUILD_DIR}/lob_profiling_benchmark" 2>&1 | tee "${RESULTS_DIR}/instruments_time.log" || true
        
        echo "Running Allocations..."
        instruments -t "Allocations" -D "${RESULTS_DIR}/allocations.trace" -l 10000 "${BUILD_DIR}/lob_profiling_benchmark" 2>&1 | tee "${RESULTS_DIR}/instruments_alloc.log" || true
        
        echo "Running Leaks..."
        instruments -t "Leaks" -D "${RESULTS_DIR}/leaks.trace" -l 10000 "${BUILD_DIR}/lob_profiling_benchmark" 2>&1 | tee "${RESULTS_DIR}/instruments_leaks.log" || true
        
        echo -e "${GREEN}✓${NC} Instruments profiling complete"
        echo -e "  View traces with: open ${RESULTS_DIR}/*.trace"
    else
        echo -e "${YELLOW}⚠${NC} Instruments not found (requires Xcode Command Line Tools)"
    fi
    
    # Heap profiling with heap command
    echo ""
    echo "Running heap analysis..."
    if command_exists heap; then
        heap "${BUILD_DIR}/lob_profiling_benchmark" > "${RESULTS_DIR}/heap_analysis.txt" 2>&1 || true
        echo -e "${GREEN}✓${NC} Heap analysis complete"
    fi
    
elif [ "${OS_TYPE}" = "Linux" ]; then
    # Linux - Use perf, valgrind
    run_step "STEP 4" "Running Linux profiling tools"
    
    # Perf profiling
    if command_exists perf; then
        echo "Running perf stat..."
        perf stat -d "${BUILD_DIR}/lob_profiling_benchmark" 2>&1 | tee "${RESULTS_DIR}/perf_stat.txt" || true
        
        echo "Running perf record..."
        perf record -g -o "${RESULTS_DIR}/perf.data" "${BUILD_DIR}/lob_profiling_benchmark" 2>&1 | tee "${RESULTS_DIR}/perf_record.log" || true
        
        echo "Generating perf report..."
        perf report -i "${RESULTS_DIR}/perf.data" > "${RESULTS_DIR}/perf_report.txt" 2>&1 || true
        
        echo -e "${GREEN}✓${NC} Perf profiling complete"
    else
        echo -e "${YELLOW}⚠${NC} perf not found. Install with: sudo apt-get install linux-tools-common"
    fi
    
    # Valgrind profiling
    if command_exists valgrind; then
        echo "Running Valgrind memcheck..."
        valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose \
            --log-file="${RESULTS_DIR}/valgrind_memcheck.txt" \
            "${BUILD_DIR}/lob_profiling_benchmark" 2>&1 | tee "${RESULTS_DIR}/valgrind_console.txt" || true
        
        echo "Running Valgrind cachegrind..."
        valgrind --tool=cachegrind --cachegrind-out-file="${RESULTS_DIR}/cachegrind.out" \
            "${BUILD_DIR}/lob_profiling_benchmark" 2>&1 | tee "${RESULTS_DIR}/cachegrind_console.txt" || true
        
        echo "Running Valgrind callgrind..."
        valgrind --tool=callgrind --callgrind-out-file="${RESULTS_DIR}/callgrind.out" \
            "${BUILD_DIR}/lob_profiling_benchmark" 2>&1 | tee "${RESULTS_DIR}/callgrind_console.txt" || true
        
        echo -e "${GREEN}✓${NC} Valgrind profiling complete"
    else
        echo -e "${YELLOW}⚠${NC} Valgrind not found. Install with: sudo apt-get install valgrind"
    fi
fi

# Step 5: Heap profiling with gperftools (if available)
run_step "STEP 5" "Running heap profiling with gperftools"
if [ -f "/usr/local/lib/libprofiler.so" ] || [ -f "/usr/lib/libprofiler.so" ]; then
    echo "Running with heap profiler..."
    HEAPPROFILE="${RESULTS_DIR}/heap.prof" \
    LD_PRELOAD=/usr/local/lib/libprofiler.so \
    "${BUILD_DIR}/lob_profiling_benchmark" 2>&1 | tee "${RESULTS_DIR}/gperftools_heap.log" || true
    
    if command_exists pprof; then
        pprof --text "${BUILD_DIR}/lob_profiling_benchmark" "${RESULTS_DIR}"/heap.prof.* > "${RESULTS_DIR}/pprof_heap_report.txt" 2>&1 || true
        echo -e "${GREEN}✓${NC} gperftools profiling complete"
    fi
else
    echo -e "${YELLOW}⚠${NC} gperftools not found. Install with: sudo apt-get install google-perftools"
fi

# Step 6: Copy profiling reports generated by benchmarks
run_step "STEP 6" "Collecting generated reports"
cd "${BUILD_DIR}"
if [ -f "profiling_memory_report.txt" ]; then
    cp profiling_memory_report.txt "${RESULTS_DIR}/"
    echo -e "${GREEN}✓${NC} Copied memory profiling report"
fi
if [ -f "profiling_hotpath_report.txt" ]; then
    cp profiling_hotpath_report.txt "${RESULTS_DIR}/"
    echo -e "${GREEN}✓${NC} Copied hot path profiling report"
fi
if [ -f "profiling_summary.json" ]; then
    cp profiling_summary.json "${RESULTS_DIR}/"
    echo -e "${GREEN}✓${NC} Copied profiling summary JSON"
fi

# Step 7: Generate master report
run_step "STEP 7" "Generating master profiling report"
cd "${PROJECT_ROOT}"

REPORT_FILE="${RESULTS_DIR}/PROFILING_MASTER_REPORT.md"

cat > "${REPORT_FILE}" << 'EOF'
# LOB Matching Engine - Profiling Master Report

**Generated:** $(date)
**System:** $(uname -a)
**Build Type:** RelWithDebInfo with Profiling

---

## Executive Summary

This report contains comprehensive profiling data for the Limit Order Book (LOB) Matching Engine,
including:

- Custom memory and hot path profiling
- Benchmark performance metrics
- System-level profiling (Instruments/perf/Valgrind)
- Heap analysis
- Optimization recommendations

---

## 1. Custom Profiling Results

### Memory Profiling
EOF

if [ -f "${RESULTS_DIR}/profiling_memory_report.txt" ]; then
    echo '```' >> "${REPORT_FILE}"
    cat "${RESULTS_DIR}/profiling_memory_report.txt" >> "${REPORT_FILE}"
    echo '```' >> "${REPORT_FILE}"
fi

cat >> "${REPORT_FILE}" << 'EOF'

### Hot Path Profiling
EOF

if [ -f "${RESULTS_DIR}/profiling_hotpath_report.txt" ]; then
    echo '```' >> "${REPORT_FILE}"
    cat "${RESULTS_DIR}/profiling_hotpath_report.txt" >> "${REPORT_FILE}"
    echo '```' >> "${REPORT_FILE}"
fi

cat >> "${REPORT_FILE}" << 'EOF'

---

## 2. Benchmark Results

### Standard Benchmarks
EOF

if [ -f "${RESULTS_DIR}/standard_benchmark_output.txt" ]; then
    echo '```' >> "${REPORT_FILE}"
    head -n 50 "${RESULTS_DIR}/standard_benchmark_output.txt" >> "${REPORT_FILE}"
    echo '```' >> "${REPORT_FILE}"
fi

cat >> "${REPORT_FILE}" << 'EOF'

### Profiling Benchmarks
EOF

if [ -f "${RESULTS_DIR}/benchmark_console_output.txt" ]; then
    echo '```' >> "${REPORT_FILE}"
    head -n 50 "${RESULTS_DIR}/benchmark_console_output.txt" >> "${REPORT_FILE}"
    echo '```' >> "${REPORT_FILE}"
fi

cat >> "${REPORT_FILE}" << 'EOF'

---

## 3. System Profiling Results

### Platform-Specific Profiling
EOF

if [ "${OS_TYPE}" = "Darwin" ]; then
    echo "**macOS Instruments Profiling**" >> "${REPORT_FILE}"
    echo "" >> "${REPORT_FILE}"
    echo "Trace files generated (open with Instruments.app):" >> "${REPORT_FILE}"
    echo "- \`time_profile.trace\` - Time Profiler data" >> "${REPORT_FILE}"
    echo "- \`allocations.trace\` - Memory allocations" >> "${REPORT_FILE}"
    echo "- \`leaks.trace\` - Memory leak detection" >> "${REPORT_FILE}"
elif [ "${OS_TYPE}" = "Linux" ]; then
    echo "**Linux perf Results**" >> "${REPORT_FILE}"
    echo "" >> "${REPORT_FILE}"
    if [ -f "${RESULTS_DIR}/perf_stat.txt" ]; then
        echo '```' >> "${REPORT_FILE}"
        cat "${RESULTS_DIR}/perf_stat.txt" >> "${REPORT_FILE}"
        echo '```' >> "${REPORT_FILE}"
    fi
fi

cat >> "${REPORT_FILE}" << 'EOF'

---

## 4. Optimization Recommendations

Based on the profiling data, here are the recommended optimizations:

### High Priority
1. **Memory Allocations**: Review memory profiling report for allocation hot spots
2. **Hot Paths**: Optimize functions with highest execution time (see hot path report)
3. **Cache Performance**: Analyze cachegrind output for cache misses

### Medium Priority
1. **Object Pool Tuning**: Adjust pool sizes based on actual usage patterns
2. **Lock Contention**: Identify and reduce mutex contention points
3. **Algorithm Selection**: Consider alternative data structures for hot operations

### Low Priority
1. **Compiler Optimizations**: Experiment with -O3 vs -O2 flags
2. **Inlining**: Profile-guided optimization for function inlining
3. **Memory Layout**: Optimize data structure padding and alignment

---

## 5. Files Generated

The following profiling data files have been generated:

EOF

cd "${RESULTS_DIR}"
ls -lh | tail -n +2 | awk '{print "- `" $9 "` (" $5 ")"}' >> "${REPORT_FILE}"

cat >> "${REPORT_FILE}" << 'EOF'

---

## 6. Next Steps

1. **Review Hot Paths**: Focus on functions appearing in hot path report
2. **Analyze Memory Usage**: Check for unnecessary allocations
3. **Benchmark Comparison**: Run benchmarks before/after optimizations
4. **Iterative Optimization**: Make changes, re-profile, validate improvements

---

## Appendix: Viewing Results

### macOS
```bash
# View Instruments traces
open profiling_results/*.trace

# View text reports
less profiling_results/PROFILING_MASTER_REPORT.md
```

### Linux
```bash
# View perf reports
perf report -i profiling_results/perf.data

# View Valgrind output
less profiling_results/valgrind_memcheck.txt

# Visualize callgrind data
kcachegrind profiling_results/callgrind.out
```

---

**End of Report**
EOF

echo -e "${GREEN}✓${NC} Master report generated: ${REPORT_FILE}"

# Summary
echo ""
echo -e "${BLUE}=============================================${NC}"
echo -e "${BLUE}  Profiling Complete!${NC}"
echo -e "${BLUE}=============================================${NC}"
echo ""
echo "Results saved to: ${RESULTS_DIR}"
echo ""
echo "Quick access:"
echo "  Master Report: ${REPORT_FILE}"
echo "  Memory Report: ${RESULTS_DIR}/profiling_memory_report.txt"
echo "  Hot Path Report: ${RESULTS_DIR}/profiling_hotpath_report.txt"
echo "  JSON Summary: ${RESULTS_DIR}/profiling_summary.json"
echo ""
echo -e "${GREEN}Next steps:${NC}"
echo "  1. Review the master report: less ${REPORT_FILE}"
echo "  2. Analyze hot paths and memory usage"
echo "  3. Apply optimizations"
echo "  4. Re-run profiling to validate improvements"
echo ""
