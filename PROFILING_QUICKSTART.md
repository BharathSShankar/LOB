# Profiling & Optimization Quick Start Guide

## Running Profiling

### One-Command Profiling

```bash
./scripts/run_profiling.sh
```

This will:
- Build the project with profiling enabled
- Run all benchmarks
- Generate memory and hot path reports
- Create comprehensive analysis documents
- Save all results to `profiling_results/`

---

## Understanding the Results

### 1. Master Report

**Location:** `profiling_results/PROFILING_MASTER_REPORT.md`

This is your starting point. It contains:
- Executive summary
- Benchmark results
- Memory profiling data
- Hot path analysis
- Optimization recommendations

### 2. Memory Report

**Location:** `profiling_results/profiling_memory_report.txt`

Key metrics:
- **Total Allocations** - Number of memory allocations
- **Peak Memory** - Maximum memory usage
- **Memory Leaks** - Unfreed allocations (should be 0)

### 3. Hot Path Report

**Location:** `profiling_results/profiling_hotpath_report.txt`

Shows execution time statistics:
- **Avg (ns)** - Average execution time
- **P95/P99** - 95th/99th percentile latency
- **Max** - Worst-case latency

### 4. JSON Summary

**Location:** `profiling_results/profiling_summary.json`

Machine-readable format for automated analysis and CI/CD integration.

---

## Key Performance Indicators

### Excellent Performance ✅
```
Single Order Processing: ~13 ns
Order Matching:           ~494 ns
Throughput:               77M orders/sec
Memory Leaks:             0
Object Pool Advantage:    6.2x faster than heap
```

### Watch For ⚠️
```
P99 Latency:              > 1 μs
Latency variance:         > 100x difference between avg and max
Memory leaks:             > 0
Throughput:               < 10M orders/sec
```

---

## Reading Benchmark Output

### Benchmark Table Format
```
BM_ProcessSingleOrder    13.0 ns    13.0 ns    54060316
                          ^           ^           ^
                          |           |           |
                    Wall time    CPU time    Iterations
```

### Interpreting Latency
```
nanoseconds (ns):  1 billionth of a second
microseconds (μs): 1 millionth of a second (1,000 ns)
milliseconds (ms): 1 thousandth of a second (1,000,000 ns)

Target latencies:
- Critical path:  < 100 ns
- Hot path:       < 1 μs
- Acceptable:     < 10 μs
- Slow:           > 100 μs
```

---

## Comparing Results

### Before/After Optimization

1. **Run baseline profiling:**
```bash
./scripts/run_profiling.sh
cp -r profiling_results profiling_results_baseline
```

2. **Make your optimization**

3. **Run profiling again:**
```bash
./scripts/run_profiling.sh
```

4. **Compare:**
```bash
# Compare specific metrics
diff profiling_results_baseline/profiling_summary.json \
     profiling_results/profiling_summary.json

# Compare reports
diff profiling_results_baseline/profiling_hotpath_report.txt \
     profiling_results/profiling_hotpath_report.txt
```

---

## Profiling in Your Code

### Adding Memory Profiling

```cpp
#include "profiling/MemoryProfiler.h"

void my_function() {
    PROFILE_MEMORY_SCOPE("my_function");
    
    auto* obj = pool.acquire();
    PROFILE_ALLOC(obj, sizeof(MyObject), "MyObject");
    
    // ... do work ...
    
    PROFILE_DEALLOC(obj, sizeof(MyObject), "MyObject");
    pool.release(obj);
}
```

**Note:** Only active when compiled with `-DENABLE_MEMORY_PROFILING`

### Adding Hot Path Profiling

```cpp
#include "profiling/HotPathProfiler.h"

void critical_function() {
    PROFILE_HOTPATH("critical_function");
    
    // Your code here
}
```

**Note:** Only active when compiled with `-DENABLE_HOTPATH_PROFILING`

---

## Building with Profiling

### Manual Build

```bash
mkdir -p build_profiling
cd build_profiling

cmake .. \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_BENCHMARK=ON \
    -DENABLE_MEMORY_PROFILING=ON \
    -DENABLE_HOTPATH_PROFILING=ON

ninja
```

### Running Specific Benchmarks

```bash
# Run all benchmarks
./build_profiling/lob_benchmark

# Run with specific filter
./build_profiling/lob_benchmark --benchmark_filter=OrderMatching

# Run with JSON output
./build_profiling/lob_benchmark --benchmark_out=results.json

# Run with specific iterations
./build_profiling/lob_benchmark --benchmark_min_time=5.0
```

---

## Platform-Specific Profiling

### macOS

**Instruments (if Xcode installed):**
```bash
# Time Profiler
instruments -t "Time Profiler" -D output.trace ./lob_benchmark

# Allocations
instruments -t "Allocations" -D allocations.trace ./lob_benchmark

# View results
open output.trace
```

**Heap Analysis:**
```bash
heap ./lob_matching_engine
```

### Linux

**Perf:**
```bash
# Record performance data
perf record -g ./lob_benchmark

# View report
perf report

# View statistics
perf stat ./lob_benchmark
```

**Valgrind:**
```bash
# Memory leaks
valgrind --leak-check=full ./lob_benchmark

# Cache analysis
valgrind --tool=cachegrind ./lob_benchmark

# Call graph
valgrind --tool=callgrind ./lob_benchmark
kcachegrind callgrind.out.*
```

---

## Interpreting Warnings

### High Variance Warning
```
⚠️ 'function_name' shows high variance (range: 88833 ns, avg: 172 ns)
```

**Meaning:** Execution time is unpredictable  
**Causes:** Context switches, cache misses, interrupts  
**Solution:** CPU pinning, prefetching, optimize hot data

### Bottleneck Warning
```
🐢 'function_name' averaging 150 μs (potential bottleneck)
```

**Meaning:** Function is taking too long  
**Solution:** Profile deeper, optimize algorithm, consider caching

### Hot Path Notice
```
🔥 'function_name' called 100000 times (hot path)
```

**Meaning:** Function is called very frequently  
**Action:** Ensure it's optimized, consider inlining

---

## Best Practices

### 1. Always Profile in Release Mode
```bash
# Use RelWithDebInfo for profiling (optimizations + debug symbols)
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

### 2. Run Multiple Times
```bash
# Get stable results by running multiple times
for i in {1..5}; do
    ./scripts/run_profiling.sh
    mv profiling_results profiling_run_$i
done
```

### 3. Profile Real Workloads
- Use realistic data distributions
- Test with actual order patterns
- Include multi-threaded scenarios

### 4. Measure Before Optimizing
> "Premature optimization is the root of all evil" - Donald Knuth

Always:
1. Measure current performance
2. Identify bottlenecks
3. Optimize
4. Measure again
5. Verify improvement

---

## Troubleshooting

### Profiling Script Fails

**Problem:** Permission denied  
**Solution:**
```bash
chmod +x scripts/run_profiling.sh
```

**Problem:** Ninja not found  
**Solution:**
```bash
# macOS
brew install ninja

# Linux
sudo apt-get install ninja-build
```

### Inconsistent Results

**Problem:** High variance in measurements  
**Solution:**
- Close other applications
- Run on isolated CPU core
- Disable CPU frequency scaling
- Run multiple times and average

### No Profiling Data

**Problem:** Reports show zero data  
**Solution:**
- Ensure profiling macros are enabled in code
- Check CMake configuration includes profiling flags
- Verify code paths are actually executed

---

## Integration with CI/CD

### Example GitHub Actions

```yaml
name: Performance Benchmarks

on: [push, pull_request]

jobs:
  benchmark:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      
      - name: Install Dependencies
        run: sudo apt-get install -y ninja-build
        
      - name: Run Profiling
        run: ./scripts/run_profiling.sh
        
      - name: Upload Results
        uses: actions/upload-artifact@v2
        with:
          name: profiling-results
          path: profiling_results/
          
      - name: Check Performance Regression
        run: |
          # Compare with baseline
          python3 scripts/check_perf_regression.py \
            profiling_results/profiling_summary.json \
            baseline/profiling_summary.json
```

---

## Advanced: Custom Benchmarks

### Creating Your Own Benchmark

```cpp
#include <benchmark/benchmark.h>
#include "profiling/MemoryProfiler.h"
#include "profiling/HotPathProfiler.h"

static void BM_MyOptimization(benchmark::State& state) {
    auto& profiler = profiling::MemoryProfiler::instance();
    profiler.start_session("MyOptimization");
    
    // Setup
    MyEngine engine;
    
    for (auto _ : state) {
        profiling::ScopeTimer timer("my_operation");
        
        // Code to benchmark
        auto result = engine.do_something();
        benchmark::DoNotOptimize(result);
    }
    
    profiler.end_session();
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MyOptimization)->Iterations(100000);
```

### Add to CMakeLists.txt

```cmake
add_executable(my_benchmark benchmarks/MyBenchmark.cpp)
target_link_libraries(my_benchmark PRIVATE lob_lib benchmark::benchmark)
```

---

## Quick Reference: Command Cheat Sheet

```bash
# Run full profiling suite
./scripts/run_profiling.sh

# Build with profiling only
cmake -B build_prof -DBUILD_BENCHMARK=ON -DENABLE_MEMORY_PROFILING=ON
ninja -C build_prof

# Run specific benchmark
./build_prof/lob_benchmark --benchmark_filter=ProcessSingleOrder

# View reports
less profiling_results/PROFILING_MASTER_REPORT.md
cat profiling_results/profiling_summary.json | jq

# Compare runs
diff -u run1/profiling_summary.json run2/profiling_summary.json

# Clean profiling data
rm -rf profiling_results/ build_profiling/
```

---

## Getting Help

**Documentation:**
- [`OPTIMIZATION_ANALYSIS.md`](OPTIMIZATION_ANALYSIS.md) - Detailed analysis and recommendations
- [`profiling_results/PROFILING_MASTER_REPORT.md`](profiling_results/PROFILING_MASTER_REPORT.md) - Latest profiling results
- [`ROADMAP.md`](ROADMAP.md) - Development roadmap

**Code:**
- [`include/profiling/MemoryProfiler.h`](include/profiling/MemoryProfiler.h) - Memory profiling API
- [`include/profiling/HotPathProfiler.h`](include/profiling/HotPathProfiler.h) - Hot path profiling API
- [`benchmarks/ProfilingBenchmark.cpp`](benchmarks/ProfilingBenchmark.cpp) - Example usage

---

**Happy Profiling! 🚀**
