# Visualization & Benchmarks Guide

## Overview

This document covers the visualization tools and benchmark harness for the Limit Order Book (LOB) system, with specific support for **Apple Silicon (M1/M2/M3)** ARM64 architecture.

---

## Table of Contents

1. [System Requirements](#1-system-requirements)
2. [Building the Benchmarks](#2-building-the-benchmarks)
3. [Running Tick-to-Trade Benchmarks](#3-running-tick-to-trade-benchmarks)
4. [Understanding Latency Metrics](#4-understanding-latency-metrics)
5. [Depth Chart Visualization](#5-depth-chart-visualization)
6. [Crypto Data Replay](#6-crypto-data-replay)
7. [ARM64 (Apple Silicon) Optimizations](#7-arm64-apple-silicon-optimizations)

---

## 1. System Requirements

### macOS (Apple Silicon M1/M2/M3)

```bash
# Install dependencies
brew install cmake ninja glfw

# Optional: For OpenGL visualization
xcode-select --install  # Xcode Command Line Tools
```

### macOS (Intel x86-64)

```bash
brew install cmake ninja glfw
```

### Linux (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install cmake ninja-build libglfw3-dev libgl1-mesa-dev
```

---

## 2. Building the Benchmarks

### Standard Build (with Benchmarks)

```bash
# Create build directory
mkdir -p build && cd build

# Configure with benchmarks enabled
cmake .. -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_BENCHMARK=ON

# Build
ninja

# The following executables will be created:
# - lob_benchmark              (basic benchmarks)
# - lob_tick_to_trade_benchmark (latency analysis)
# - lob_profiling_benchmark     (profiling benchmarks)
```

### Build with Visualization

```bash
cmake .. -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_BENCHMARK=ON \
    -DBUILD_VISUALIZATION=ON

ninja
```

### Apple Silicon Specific Build

The build system automatically detects ARM64 and applies optimizations:

```bash
cmake .. -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DBUILD_BENCHMARK=ON

ninja
```

CMake will output:
```
-- Detected ARM64 architecture (Apple Silicon)
```

---

## 3. Running Tick-to-Trade Benchmarks

### Basic Execution

```bash
./lob_tick_to_trade_benchmark
```

### With Specific Filters

```bash
# Run only Single Order benchmark
./lob_tick_to_trade_benchmark --benchmark_filter=BM_TickToTrade_SingleOrder

# Run all book depth variations
./lob_tick_to_trade_benchmark --benchmark_filter=BM_TickToTrade_BookDepth

# Run end-to-end latency test
./lob_tick_to_trade_benchmark --benchmark_filter=BM_TickToTrade_EndToEnd
```

### Available Benchmarks

| Benchmark                    | Description                    |
| ---------------------------- | ------------------------------ |
| `BM_TickToTrade_SingleOrder` | Single order to trade latency  |
| `BM_TickToTrade_BookDepth`   | Latency vs order book depth    |
| `BM_TickToTrade_MultiMatch`  | Multiple order matching        |
| `BM_TickToTrade_EndToEnd`    | Full pipeline with ring buffer |
| `BM_TickToTrade_MarketOrder` | Market order execution         |
| `BM_TickToTrade_LimitOrder`  | Limit order execution          |
| `BM_SustainedThroughput`     | High-throughput testing        |

### Output Format

```
---------------------------------------------------------------------------
Benchmark                                  Time         CPU   Iterations
---------------------------------------------------------------------------
BM_TickToTrade_SingleOrder              245 ns       245 ns      2860000
  p50_us=0.18 p99_us=0.35 p999_us=0.82 min_us=0.12 max_us=15.2
```

### Export Results

```bash
# JSON output for analysis
./lob_tick_to_trade_benchmark --benchmark_format=json > latency_results.json

# CSV for spreadsheets
./lob_tick_to_trade_benchmark --benchmark_format=csv > latency_results.csv
```

---

## 4. Understanding Latency Metrics

### What is Tick-to-Trade Latency?

```
┌─────────┐    ┌────────────┐    ┌──────────┐    ┌─────────┐
│ Order   │ → │ Validation │ → │ Matching │ →  │ Trade   │
│ Arrives │    │            │    │ Engine   │    │ Executed│
└─────────┘    └────────────┘    └──────────┘    └─────────┘
     │                                                │
     └────────────── Tick-to-Trade Latency ──────────┘
```

### Percentile Explanation

| Metric     | Meaning            | Trading Significance      |
| ---------- | ------------------ | ------------------------- |
| **p50**    | Median latency     | "Typical" user experience |
| **p95**    | 95th percentile    | Performance under load    |
| **p99**    | 99th percentile    | Tail latency (SLA target) |
| **p99.9**  | 99.9th percentile  | Extreme tail (critical)   |
| **p99.99** | 99.99th percentile | Worst case scenario       |

### Why Percentiles Matter

Mean/average hides outliers:

```
Latencies (µs): 0.2, 0.2, 0.2, 0.2, 0.3, 0.3, 0.3, 0.3, 50.0

Mean:  5.8 µs  ← Misleading!
p50:   0.25 µs ← True typical latency
p99:   50 µs   ← One bad trade!
```

For HFT/trading systems, **p99 and p99.9 are critical** because:
- One slow trade can mean millions in lost opportunity
- SLAs are typically defined on p99

### Target Latencies (Industry Reference)

| System Type           | p50 Target | p99 Target |
| --------------------- | ---------- | ---------- |
| Ultra-Low Latency HFT | < 500 ns   | < 2 µs     |
| Market Maker          | < 1 µs     | < 10 µs    |
| Retail Trading        | < 10 µs    | < 100 µs   |
| This LOB Project      | < 1 µs     | < 5 µs     |

---

## 5. Depth Chart Visualization

### Terminal Mode (No Dependencies)

```bash
./lob_depth_viewer --terminal
```

Output:
```
╔════════════════════════════════════════════════╗
║        LOB DEPTH CHART - TERMINAL VIEW         ║
╠════════════════════════════════════════════════╣
║ Mid: 10000.00 | Spread: 1.00 | Updates: 1542   ║
╠════════════════════════════════════════════════╣
║      BIDS (BUY)              ASKS (SELL)       ║
╠════════════════════════════════════════════════╣
║   9999.00 |###########    |===========| 10001.00 ║
║   9998.00 |########       |========   | 10002.00 ║
║   9997.00 |######         |======     | 10003.00 ║
╚════════════════════════════════════════════════╝
```

### OpenGL/ImGui Mode (Graphical)

```bash
# Requires GLFW installed
./lob_depth_viewer
```

Features:
- Real-time depth chart with bid/ask walls
- Animated transitions
- Statistics panel
- Full order book table view

### Command Line Options

```bash
./lob_depth_viewer --help

Options:
  --terminal        ASCII terminal mode (no OpenGL)
  --replay FILE     Replay market data from file
  --exchange EX     coinbase, binance, csv
  --symbol SYM      Trading symbol (default: BTC-USD)
  --speed N         Replay speed (1.0 = real-time)
  --levels N        Price levels to display (default: 20)
```

---

## 6. Crypto Data Replay

### Data Sources

| Source         | URL                       | Data Type           | Cost                |
| -------------- | ------------------------- | ------------------- | ------------------- |
| **Coinbase**   | api.exchange.coinbase.com | L2/L3               | Free                |
| **Binance**    | api.binance.com           | L2                  | Free                |
| **Tardis.dev** | tardis.dev                | Historical L2/L3    | Free tier available |
| **Kaiko**      | kaiko.com                 | Institutional L2/L3 | Paid                |

### Downloading Coinbase Data

```bash
# WebSocket capture (requires wscat)
npm install -g wscat

# Subscribe to BTC-USD L2 updates
wscat -c wss://ws-feed.exchange.coinbase.com

# Send subscription message:
# {"type":"subscribe","channels":[{"name":"level2","product_ids":["BTC-USD"]}]}

# Redirect to file:
wscat -c wss://ws-feed.exchange.coinbase.com | tee btc_l2_data.ndjson
```

### Sample Data Format

**Coinbase L2 (NDJSON)**:
```json
{"type":"l2update","product_id":"BTC-USD","time":"2024-01-15T10:30:00.001Z","changes":[["buy","42150.00","1.8"]]}
```

**Binance Depth**:
```json
{"lastUpdateId":160,"bids":[["42150.00","1.50"]],"asks":[["42151.00","0.90"]]}
```

**Generic CSV**:
```csv
timestamp,side,price,quantity
1705312200000,buy,42150.50,1.5
1705312200001,sell,42151.00,0.8
```

### Replay Commands

```bash
# Coinbase data
./lob_depth_viewer --replay data/sample_coinbase_l2.ndjson --exchange coinbase

# Binance data
./lob_depth_viewer --replay data/sample_binance_depth.json --exchange binance

# CSV data
./lob_depth_viewer --replay data/trades.csv --exchange csv

# Fast replay (10x speed)
./lob_depth_viewer --replay data/btc_l2.ndjson --exchange coinbase --speed 10

# As-fast-as-possible (stress test)
./lob_depth_viewer --replay data/btc_l2.ndjson --exchange coinbase --speed 0
```

---

## 7. ARM64 (Apple Silicon) Optimizations

### Cache Line Size

Apple Silicon uses **128-byte cache lines** (vs 64 bytes on x86-64):

```cpp
// Automatically set by CMake on ARM64
#ifdef ARM_CACHE_LINE_SIZE
    static constexpr size_t CACHE_LINE_SIZE = 128;
#else
    static constexpr size_t CACHE_LINE_SIZE = 64;
#endif
```

This affects:
- Ring buffer padding
- False sharing prevention
- Memory alignment

### Compiler Flags

```cmake
# ARM64 (M1/M2/M3)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -mcpu=native -DNDEBUG")

# x86-64
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
```

### Performance Expectations

Apple M1/M2/M3 typically shows:
- 10-20% better single-core latency vs Intel
- More consistent latency (smaller p99/p50 ratio)
- Excellent memory bandwidth

### Verifying ARM64 Build

```bash
# Check binary architecture
file ./lob_tick_to_trade_benchmark
# Output: lob_tick_to_trade_benchmark: Mach-O 64-bit executable arm64

# Check CPU features being used
otool -l ./lob_tick_to_trade_benchmark | grep -A2 ARM
```

---

## Quick Start Commands

```bash
# 1. Clone and build
git clone <repo>
cd LOB
mkdir build && cd build
cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARK=ON
ninja

# 2. Run benchmarks
./lob_tick_to_trade_benchmark

# 3. Run terminal viewer with simulated data
./lob_depth_viewer --terminal

# 4. Replay sample crypto data
./lob_depth_viewer --replay ../data/sample_coinbase_l2.ndjson --exchange coinbase --terminal
```

---

## Troubleshooting

### "GLFW not found"

```bash
# macOS
brew install glfw

# Ubuntu
sudo apt-get install libglfw3-dev
```

### "OpenGL context creation failed"

Try terminal mode:
```bash
./lob_depth_viewer --terminal
```

### Benchmark shows inconsistent results

```bash
# Disable CPU frequency scaling (Linux)
sudo cpupower frequency-set --governor performance

# macOS: Close other applications, disable App Nap
defaults write NSGlobalDomain NSAppSleepDisabled -bool YES
```

---

## Files Reference

| File                                                                                      | Purpose                 |
| ----------------------------------------------------------------------------------------- | ----------------------- |
| [`benchmarks/TickToTradeBenchmark.cpp`](../benchmarks/TickToTradeBenchmark.cpp)           | Latency benchmarks      |
| [`include/benchmarks/LatencyStats.h`](../include/benchmarks/LatencyStats.h)               | Statistics & histograms |
| [`include/visualization/DepthChartViewer.h`](../include/visualization/DepthChartViewer.h) | Viewer interface        |
| [`src/visualization/DepthChartViewerGL.cpp`](../src/visualization/DepthChartViewerGL.cpp) | OpenGL implementation   |
| [`include/market_data/CryptoDataReplay.h`](../include/market_data/CryptoDataReplay.h)     | Data parsers            |
| [`data/sample_coinbase_l2.ndjson`](../data/sample_coinbase_l2.ndjson)                     | Sample Coinbase data    |
