# Week 5: Concurrency in the Limit Order Book

## Overview

Week 5 focuses on implementing low-latency concurrent components for the LOB system. This document explains the key concurrency concepts, their implementations, and how they achieve ultra-low latency communication between threads.

> **Target Platform**: Apple Silicon (M1/M2/M3) ARM64 architecture
> This implementation is optimized for ARM64 with 128-byte cache line alignment and Apple-specific compiler optimizations.

---

## Table of Contents

1. [Lock-Free SPSC Ring Buffer](#1-lock-free-spsc-ring-buffer)
2. [Memory Ordering and Atomics](#2-memory-ordering-and-atomics)
3. [The Disruptor Pattern](#3-the-disruptor-pattern)
4. [Order Entry Gateway](#4-order-entry-gateway)
5. [Cache Line Optimization](#5-cache-line-optimization)
6. [Latency Testing and Benchmarking](#6-latency-testing-and-benchmarking)
7. [ARM64 (Apple Silicon) Optimizations](#7-arm64-apple-silicon-optimizations)

---

## 1. Lock-Free SPSC Ring Buffer

### What is it?

A **Single-Producer Single-Consumer (SPSC) Ring Buffer** is a fixed-size circular queue optimized for communication between exactly two threads:
- **Producer**: Writes data (e.g., Order Entry Gateway receiving orders from network)
- **Consumer**: Reads data (e.g., Matching Engine processing orders)

### Why Lock-Free?

Traditional mutex-based queues have several problems for low-latency systems:

| Problem                | Impact                                                |
| ---------------------- | ----------------------------------------------------- |
| **Context Switching**  | OS may put thread to sleep (10-100µs penalty)         |
| **Priority Inversion** | Low-priority thread holding lock blocks high-priority |
| **Cache Pollution**    | Mutex data structures evict hot data from cache       |
| **Contention**         | Multiple threads waiting on same lock                 |

Lock-free structures eliminate these issues by using **atomic operations** instead of locks.

### Implementation Details

```cpp
// From include/concurrency/RingBuffer.h

template <typename T, size_t Size>
class RingBuffer {
    static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");
    
    // Cache-line aligned indices to prevent false sharing
    alignas(64) std::atomic<size_t> write_index_;
    alignas(64) std::atomic<size_t> read_index_;
    
    std::array<T, Size> buffer_;
    
    // Cached indices to reduce atomic loads
    size_t cached_read_index_;
    size_t cached_write_index_;
};
```

### Key Design Decisions

1. **Power-of-2 Size**: Enables fast modulo via bitwise AND
   ```cpp
   // Instead of: index % Size (expensive division)
   next_index = (index + 1) & (Size - 1)  // Fast bitwise AND
   ```

2. **Cached Indices**: Reduces expensive atomic reads
   ```cpp
   // Check cached value first (local memory)
   if (next_write == cached_read_index_) {
       // Only refresh from atomic if needed
       cached_read_index_ = read_index_.load(memory_order_acquire);
   }
   ```

3. **Separate Indices**: Producer only writes `write_index_`, consumer only writes `read_index_`

---

## 2. Memory Ordering and Atomics

### The Problem: CPU Reordering

Modern CPUs reorder instructions for performance:

```cpp
// Code written:
buffer_[write_pos] = item;  // (1)
write_index_ = next_write;   // (2)

// CPU might execute as:
write_index_ = next_write;   // (2) - FIRST!
buffer_[write_pos] = item;   // (1) - Consumer reads garbage!
```

### Memory Order Semantics

C++ provides memory ordering guarantees:

| Memory Order | Guarantee                         | Use Case                 |
| ------------ | --------------------------------- | ------------------------ |
| `relaxed`    | No ordering, just atomicity       | Counters, statistics     |
| `acquire`    | Reads before this happen before   | Consumer reading data    |
| `release`    | Writes before this complete first | Producer publishing data |
| `seq_cst`    | Total ordering (slowest)          | Default, rarely needed   |

### Correct Producer-Consumer Pattern

```cpp
// PRODUCER (writing data)
bool push(const T& item) {
    size_t write_pos = write_index_.load(memory_order_relaxed);
    size_t next_write = (write_pos + 1) & (Size - 1);
    
    // Check if full
    if (next_write == cached_read_index_) {
        cached_read_index_ = read_index_.load(memory_order_acquire);
        if (next_write == cached_read_index_) return false;
    }
    
    // Write data BEFORE publishing index
    buffer_[write_pos] = item;
    
    // RELEASE: Ensures buffer write completes before index update
    write_index_.store(next_write, memory_order_release);
    return true;
}

// CONSUMER (reading data)
bool pop(T& item) {
    size_t read_pos = read_index_.load(memory_order_relaxed);
    
    // Check if empty
    if (read_pos == cached_write_index_) {
        // ACQUIRE: Sees all writes made before producer's release
        cached_write_index_ = write_index_.load(memory_order_acquire);
        if (read_pos == cached_write_index_) return false;
    }
    
    // Read data AFTER acquiring index
    item = buffer_[read_pos];
    
    // RELEASE: Ensures data is read before advancing index
    size_t next_read = (read_pos + 1) & (Size - 1);
    read_index_.store(next_read, memory_order_release);
    return true;
}
```

### Acquire-Release Pairing

```
Producer Thread                     Consumer Thread
================                    ================
write buffer_[i] = data
    |
    v
store(write_index_, release) -----> load(write_index_, acquire)
                                         |
                                         v
                                    read item = buffer_[i]
                                    (sees producer's write)
```

---

## 3. The Disruptor Pattern

### Origin

The **Disruptor** is a high-performance inter-thread messaging library developed by LMAX Exchange (a major FX trading platform). Key ideas:

1. **Pre-allocated ring buffer** - No GC, no allocation on hot path
2. **Mechanical sympathy** - Designed for CPU cache architecture
3. **Sequence barriers** - Coordinate multiple consumers without locks

### How Our Ring Buffer Uses Disruptor Concepts

```cpp
class RingBuffer {
    // Pre-allocated fixed-size array (no allocations)
    std::array<T, Size> buffer_;
    
    // Sequences instead of head/tail pointers
    std::atomic<size_t> write_index_;  // "produced up to"
    std::atomic<size_t> read_index_;   // "consumed up to"
    
    // Cached sequences to reduce contention
    size_t cached_read_index_;
    size_t cached_write_index_;
};
```

### Sequence Numbers vs. Pointers

Traditional queue:
```
head_ptr -> [item] [item] [item] <- tail_ptr
```

Disruptor style:
```
buffer:    [0] [1] [2] [3] [4] [5] [6] [7]  (Size=8)
write_seq: 11 (means slots 0-11 are written = indices 0,1,2,3)
read_seq:  9  (means slots 0-9 are read = indices 0,1)
```

Advantage: Sequence numbers are always increasing - no wrap-around bugs!

---

## 4. Order Entry Gateway

### Purpose

The [`OrderEntryGateway`](../include/concurrency/OrderEntryGateway.h) simulates network packet parsing and order submission. It:

1. Runs in a separate **I/O thread**
2. Receives/generates orders
3. Pushes them to the ring buffer
4. Matching engine thread consumes and processes

### Architecture

```
┌─────────────────┐         ┌──────────────┐         ┌──────────────────┐
│   Network I/O   │   →     │  Ring Buffer │    →    │ Matching Engine  │
│  (Gateway)      │  push   │  (Lock-free) │   pop   │   (Consumer)     │
│                 │         │              │         │                  │
│ - TCP receive   │         │ [64K slots]  │         │ - Price matching │
│ - Parse orders  │         │              │         │ - Trade exec     │
│ - Thread 1      │         │              │         │ - Thread 2       │
└─────────────────┘         └──────────────┘         └──────────────────┘
```

### Code Walkthrough

```cpp
// Gateway submitting an order
bool OrderEntryGateway::submit_order(Order* order) {
    // Push to ring buffer (lock-free)
    bool success = ring_buffer_.push(order);
    
    if (success) {
        stats_.total_orders_submitted++;
    } else {
        stats_.total_orders_dropped++;
        stats_.buffer_full_count++;
    }
    return success;
}

// Matching engine consuming orders
void matching_engine_loop() {
    Order* order = nullptr;
    while (running) {
        if (gateway.pop_order(order)) {
            // Process order
            auto trades = engine.process_order(order);
            publish_trades(trades);
        } else {
            // Buffer empty - busy wait or yield
            _mm_pause();  // CPU hint for spin-wait
        }
    }
}
```

### Order Generation for Testing

```cpp
Order* OrderEntryGateway::generate_single_order() {
    Order* order = order_pool_->acquire();  // From object pool
    
    // Random order parameters
    Side side = (random() < 0.5) ? BUY : SELL;
    uint64_t price = base_price + random_offset;
    uint64_t qty = random_quantity;
    
    *order = Order(order_id++, timestamp, price, qty, side, LIMIT);
    return order;
}
```

---

## 5. Cache Line Optimization

### What is a Cache Line?

CPU caches work in fixed-size blocks called **cache lines**:
- **x86-64 (Intel/AMD)**: 64 bytes
- **ARM64 (Apple Silicon M1/M2/M3)**: 128 bytes

```
┌──────────────────────────────────────────────────────────────────┐
│              Cache Line (64 bytes on x86, 128 on ARM)            │
├────────┬────────┬────────┬────────┬────────┬────────┬────────┬───┤
│ 8 bytes│ 8 bytes│ 8 bytes│ 8 bytes│ 8 bytes│ 8 bytes│ 8 bytes│...│
└────────┴────────┴────────┴────────┴────────┴────────┴────────┴───┘
```

### False Sharing Problem

When two threads modify different variables on the **same cache line**, they invalidate each other's cache:

```cpp
// BAD: write_index_ and read_index_ on same cache line
struct BadRingBuffer {
    std::atomic<size_t> write_index_;  // Thread 1 writes
    std::atomic<size_t> read_index_;   // Thread 2 writes
    // Both on same cache line = constant invalidation!
};
```

### The Solution: Cache Line Padding

```cpp
// GOOD: Separate cache lines with architecture-aware sizing
// ARM64 (M1/M2/M3) uses larger 128-byte cache lines
#ifdef ARM_CACHE_LINE_SIZE
    static constexpr size_t CACHE_LINE_SIZE = 128;  // Apple Silicon
#else
    static constexpr size_t CACHE_LINE_SIZE = 64;   // x86-64
#endif

// Producer's index - own cache line
alignas(CACHE_LINE_SIZE) std::atomic<size_t> write_index_;

// Consumer's index - different cache line
alignas(CACHE_LINE_SIZE) std::atomic<size_t> read_index_;
```

### Performance Impact

Without padding:
- Each write invalidates both indices
- Cache line ping-pongs between cores
- ~100-200 cycles per operation

With padding:
- Each index stays in its core's cache
- ~10-20 cycles per operation
- **10x improvement!**

---

## 6. Latency Testing and Benchmarking

### Tick-to-Trade Latency

**Tick-to-Trade** measures end-to-end latency:
```
Order arrives → Validation → Matching → Trade executed
```

### Latency Metrics

| Metric     | Description              | Target (HFT) |
| ---------- | ------------------------ | ------------ |
| **p50**    | Median (50th percentile) | < 1 µs       |
| **p99**    | 99th percentile          | < 5 µs       |
| **p99.9**  | 99.9th percentile (tail) | < 10 µs      |
| **p99.99** | Extreme tail             | < 50 µs      |

### Why Percentiles Matter

Mean/average hides outliers:
```
Latencies: 1µs, 1µs, 1µs, 1µs, 1µs, 1µs, 1µs, 1µs, 1µs, 100µs
Mean: 10.9 µs  (looks okay)
p99:  100 µs   (1% of trades are SLOW!)
```

### Histogram Visualization

```
Latency Distribution:
   1-2 µs  |################################| (320)
   2-3 µs  |########################| (240)
   3-4 µs  |############| (120)
   4-5 µs  |######| (60)
   5-10 µs |###| (30)
  10-50 µs |#| (10)
```

### Benchmark Implementation

```cpp
// From benchmarks/TickToTradeBenchmark.cpp

static void BM_TickToTrade_SingleOrder(benchmark::State& state) {
    MatchingEngine engine;
    engine.initialize();
    LatencyStats stats;
    
    for (auto _ : state) {
        auto* order = create_order();
        
        auto start = high_resolution_clock::now();
        auto trades = engine.process_order(order);
        auto end = high_resolution_clock::now();
        
        stats.record(duration_cast<nanoseconds>(end - start).count());
    }
    
    // Report percentiles
    state.counters["p50_us"] = stats.p50() / 1000.0;
    state.counters["p99_us"] = stats.p99() / 1000.0;
    state.counters["p999_us"] = stats.p999() / 1000.0;
}
```

---

## Summary: What We Learned in Week 5

1. **Lock-free programming** eliminates mutex overhead for inter-thread communication

2. **Memory ordering** (acquire/release) ensures correct visibility of data between threads

3. **The Disruptor pattern** provides proven techniques for ultra-low latency messaging

4. **Cache line padding** prevents false sharing between threads

5. **Latency percentiles** (p99, p99.9) reveal tail latency issues hidden by averages

6. **Ring buffers** provide bounded, pre-allocated communication channels

---

## 7. ARM64 (Apple Silicon) Optimizations

This LOB implementation is optimized for Apple Silicon (M1/M2/M3) ARM64 architecture.

### Key Differences from x86-64

| Feature           | x86-64 (Intel/AMD) | ARM64 (Apple Silicon)               |
| ----------------- | ------------------ | ----------------------------------- |
| Cache Line Size   | 64 bytes           | 128 bytes                           |
| Memory Model      | TSO (strong)       | Weakly ordered (needs barriers)     |
| Atomic Ops        | Lock prefix        | LL/SC (Load-Link/Store-Conditional) |
| Branch Prediction | Moderate           | Excellent                           |
| Compiler Flag     | `-march=native`    | `-mcpu=native`                      |

### Build Configuration

```cmake
# CMake automatically detects ARM64 and sets:
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -mcpu=native -DNDEBUG")
    add_compile_definitions(ARM_CACHE_LINE_SIZE=128)
endif()
```

### Memory Ordering on ARM

ARM has a **weakly ordered** memory model, meaning:
- Reads and writes can be reordered more aggressively than x86
- Memory barriers are essential (our acquire/release semantics handle this)
- Compiler barriers prevent instruction reordering

```cpp
// ARM-specific memory barrier (if needed for raw operations)
#ifdef __aarch64__
    __asm__ __volatile__("dmb ish" ::: "memory");  // Data Memory Barrier
#endif
```

### Performance Characteristics on M1/M2/M3

| Operation            | Typical Latency |
| -------------------- | --------------- |
| L1 Cache Hit         | ~3 cycles       |
| L2 Cache Hit         | ~12 cycles      |
| Atomic Add           | ~10-15 cycles   |
| Ring Buffer Push/Pop | ~20-30 ns       |
| Full Tick-to-Trade   | ~200-500 ns     |

### Building on macOS (Apple Silicon)

```bash
# Ensure native ARM64 build
mkdir build && cd build
cmake .. -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DBUILD_BENCHMARK=ON

ninja

# Verify architecture
file lob_benchmark
# Output: lob_benchmark: Mach-O 64-bit executable arm64
```

---

## Further Reading

- [LMAX Disruptor Paper](https://lmax-exchange.github.io/disruptor/files/Disruptor-1.0.pdf)
- [C++ Memory Model](https://en.cppreference.com/w/cpp/atomic/memory_order)
- [Mechanical Sympathy Blog](https://mechanical-sympathy.blogspot.com/)
- [What Every Programmer Should Know About Memory](https://people.freebsd.org/~lstewart/articles/cpumemory.pdf)
- [ARM Architecture Reference Manual](https://developer.arm.com/documentation)
- [Apple Silicon Performance Guide](https://developer.apple.com/documentation/apple-silicon)

---

## Code References

| Component   | File                                                                                    | Description             |
| ----------- | --------------------------------------------------------------------------------------- | ----------------------- |
| Ring Buffer | [`include/concurrency/RingBuffer.h`](../include/concurrency/RingBuffer.h)               | Lock-free SPSC queue    |
| Gateway     | [`include/concurrency/OrderEntryGateway.h`](../include/concurrency/OrderEntryGateway.h) | Order entry simulation  |
| Benchmarks  | [`benchmarks/TickToTradeBenchmark.cpp`](../benchmarks/TickToTradeBenchmark.cpp)         | Latency measurements    |
| Stats       | [`include/benchmarks/LatencyStats.h`](../include/benchmarks/LatencyStats.h)             | Histogram & percentiles |
