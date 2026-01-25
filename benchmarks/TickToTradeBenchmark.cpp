/**
 * @file TickToTradeBenchmark.cpp
 * @brief Comprehensive Tick-to-Trade Latency Benchmark Harness
 *
 * This benchmark measures the end-to-end latency from order entry to trade
 * execution, providing detailed statistics including:
 * - Latency histograms
 * - Percentiles (p50, p95, p99, p99.9, p99.99)
 * - Comparison between different scenarios
 *
 * Data Sources for Crypto L2/L3 Data:
 * ===================================
 * 1. Coinbase Exchange API:
 *    - Public WebSocket feed: wss://ws-feed.exchange.coinbase.com
 *    - Level 2 snapshot: GET https://api.exchange.coinbase.com/products/{product_id}/book?level=2
 *    - Level 3 full order book: GET https://api.exchange.coinbase.com/products/{product_id}/book?level=3
 *
 * 2. Binance:
 *    - WebSocket: wss://stream.binance.com:9443/ws/{symbol}@depth
 *    - REST API: GET https://api.binance.com/api/v3/depth?symbol={symbol}&limit=5000
 *
 * 3. Historical Data Files:
 *    - Kaiko: https://www.kaiko.com (premium historical L2/L3 data)
 *    - Tardis.dev: https://tardis.dev (order book snapshots, trades)
 *    - Coinbase Pro Sandbox: For testing with realistic data
 *
 * 4. Free Historical Data:
 *    - CryptoDataDownload: https://www.cryptodatadownload.com
 *    - Bitcoin Charts: https://bitcoincharts.com/charts
 *    - Kaggle datasets: https://www.kaggle.com/datasets
 */

#include <benchmark/benchmark.h>
#include "core/MatchingEngine.h"
#include "core/Order.h"
#include "memory/ObjectPool.h"
#include "concurrency/OrderEntryGateway.h"
#include "benchmarks/LatencyStats.h"
#include <random>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iostream>
#include <atomic>
#include <thread>

using namespace lob;
using namespace std::chrono;

namespace
{
    // Global latency statistics for post-benchmark analysis
    benchmarks::LatencyStats g_latency_stats;

    // High-resolution clock type for consistent measurements
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;

} // anonymous namespace

// =============================================================================
// BENCHMARK: Tick-to-Trade Latency (Single Order)
// =============================================================================

/**
 * @brief Measures latency from order creation to trade execution
 *
 * This is the core benchmark measuring the critical path:
 * OrderIn -> Validation -> Matching -> TradeOut
 */
static void BM_TickToTrade_SingleOrder(benchmark::State &state)
{
    core::MatchingEngine engine;
    engine.initialize();

    auto &pool = engine.get_order_pool();
    benchmarks::LatencyStats stats;

    // Pre-populate with resting orders
    for (int i = 0; i < 1000; ++i)
    {
        auto *order = pool.acquire();
        if (order)
        {
            *order = core::Order(i, i * 100, 9500 + (i % 100), 100,
                                 core::Side::BUY, core::OrderType::LIMIT);
            engine.process_order(order);
        }
    }

    uint64_t order_id = 10000;

    for (auto _ : state)
    {
        order_id++;
        auto *order = pool.acquire();
        if (!order)
            continue;

        // Price that matches existing orders (aggressive sell)
        *order = core::Order(order_id, 0, 9550, 10,
                             core::Side::SELL, core::OrderType::LIMIT);

        // Measure tick-to-trade
        auto start = Clock::now();
        auto trades = engine.process_order(order);
        auto end = Clock::now();

        auto latency_ns = duration_cast<nanoseconds>(end - start).count();
        stats.record(static_cast<uint64_t>(latency_ns));

        state.SetIterationTime(static_cast<double>(latency_ns) / 1e9);
        benchmark::DoNotOptimize(trades);
    }

    state.SetItemsProcessed(state.iterations());

    // Report custom counters
    state.counters["p50_us"] = static_cast<double>(stats.p50()) / 1000.0;
    state.counters["p99_us"] = static_cast<double>(stats.p99()) / 1000.0;
    state.counters["p999_us"] = static_cast<double>(stats.p999()) / 1000.0;
    state.counters["min_us"] = static_cast<double>(stats.min()) / 1000.0;
    state.counters["max_us"] = static_cast<double>(stats.max()) / 1000.0;

    // Copy to global stats for final report
    g_latency_stats = stats;
}
BENCHMARK(BM_TickToTrade_SingleOrder)
    ->UseManualTime()
    ->Iterations(100000)
    ->Unit(benchmark::kNanosecond);

// =============================================================================
// BENCHMARK: Tick-to-Trade with Book Depth Variation
// =============================================================================

/**
 * @brief Measures latency impact of order book depth
 *
 * Tests with varying numbers of resting orders to understand
 * how book depth affects matching performance.
 */
static void BM_TickToTrade_BookDepth(benchmark::State &state)
{
    core::MatchingEngine engine;
    engine.initialize();

    auto &pool = engine.get_order_pool();
    const size_t book_depth = static_cast<size_t>(state.range(0));

    // Pre-populate with specified depth
    for (size_t i = 0; i < book_depth; ++i)
    {
        auto *order = pool.acquire();
        if (order)
        {
            uint64_t price = 9500 + (i % 200);
            *order = core::Order(i, i * 100, price, 100,
                                 core::Side::BUY, core::OrderType::LIMIT);
            engine.process_order(order);
        }
    }

    benchmarks::LatencyStats stats;
    uint64_t order_id = 100000;

    for (auto _ : state)
    {
        order_id++;
        auto *order = pool.acquire();
        if (!order)
            continue;

        *order = core::Order(order_id, 0, 9600, 10,
                             core::Side::SELL, core::OrderType::LIMIT);

        auto start = Clock::now();
        auto trades = engine.process_order(order);
        auto end = Clock::now();

        auto latency_ns = duration_cast<nanoseconds>(end - start).count();
        stats.record(static_cast<uint64_t>(latency_ns));

        state.SetIterationTime(static_cast<double>(latency_ns) / 1e9);
        benchmark::DoNotOptimize(trades);
    }

    state.SetItemsProcessed(state.iterations());
    state.counters["depth"] = static_cast<double>(book_depth);
    state.counters["p99_us"] = static_cast<double>(stats.p99()) / 1000.0;
}
BENCHMARK(BM_TickToTrade_BookDepth)
    ->UseManualTime()
    ->Arg(100)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(50000)
    ->Unit(benchmark::kNanosecond);

// =============================================================================
// BENCHMARK: Tick-to-Trade with Match Quantity Variation
// =============================================================================

/**
 * @brief Measures latency impact of match quantity
 *
 * An order that matches against many resting orders takes longer
 * than one that matches against just one.
 */
static void BM_TickToTrade_MultiMatch(benchmark::State &state)
{
    core::MatchingEngine engine;
    engine.initialize();

    auto &pool = engine.get_order_pool();
    const size_t orders_to_match = static_cast<size_t>(state.range(0));

    // Pre-populate with small orders at same price (will all match)
    for (size_t i = 0; i < orders_to_match * 2; ++i)
    {
        auto *order = pool.acquire();
        if (order)
        {
            *order = core::Order(i, i * 100, 10000, 10,
                                 core::Side::BUY, core::OrderType::LIMIT);
            engine.process_order(order);
        }
    }

    benchmarks::LatencyStats stats;
    uint64_t order_id = 100000;

    for (auto _ : state)
    {
        // Refill orders that were matched
        for (size_t i = 0; i < orders_to_match; ++i)
        {
            auto *order = pool.acquire();
            if (order)
            {
                *order = core::Order(order_id++, 0, 10000, 10,
                                     core::Side::BUY, core::OrderType::LIMIT);
                engine.process_order(order);
            }
        }

        // Now measure aggressive sell that matches multiple orders
        auto *sell_order = pool.acquire();
        if (!sell_order)
            continue;

        uint64_t qty = static_cast<uint64_t>(orders_to_match * 10);
        *sell_order = core::Order(order_id++, 0, 10000, qty,
                                  core::Side::SELL, core::OrderType::LIMIT);

        auto start = Clock::now();
        auto trades = engine.process_order(sell_order);
        auto end = Clock::now();

        auto latency_ns = duration_cast<nanoseconds>(end - start).count();
        stats.record(static_cast<uint64_t>(latency_ns));

        state.SetIterationTime(static_cast<double>(latency_ns) / 1e9);
        benchmark::DoNotOptimize(trades);
    }

    state.SetItemsProcessed(state.iterations());
    state.counters["matches"] = static_cast<double>(orders_to_match);
    state.counters["p99_us"] = static_cast<double>(stats.p99()) / 1000.0;
}
BENCHMARK(BM_TickToTrade_MultiMatch)
    ->UseManualTime()
    ->Arg(1)
    ->Arg(5)
    ->Arg(10)
    ->Arg(50)
    ->Arg(100)
    ->Unit(benchmark::kNanosecond);

// =============================================================================
// BENCHMARK: End-to-End with Ring Buffer (Producer-Consumer)
// =============================================================================

/**
 * @brief Measures latency including inter-thread communication
 *
 * This benchmark includes the OrderEntryGateway (producer) pushing
 * orders to the ring buffer and the matching engine (consumer)
 * processing them.
 */
static void BM_TickToTrade_EndToEnd(benchmark::State &state)
{
    core::MatchingEngine engine;
    engine.initialize();

    auto &pool = engine.get_order_pool();

    // Create gateway (standalone mode, we'll submit orders manually)
    concurrency::OrderEntryGateway gateway;

    // Pre-populate order book
    for (int i = 0; i < 1000; ++i)
    {
        auto *order = pool.acquire();
        if (order)
        {
            *order = core::Order(i, i * 100, 9500 + (i % 100), 100,
                                 core::Side::BUY, core::OrderType::LIMIT);
            engine.process_order(order);
        }
    }

    benchmarks::LatencyStats stats;
    uint64_t order_id = 10000;

    for (auto _ : state)
    {
        order_id++;
        auto *order = pool.acquire();
        if (!order)
            continue;

        *order = core::Order(order_id, 0, 9550, 10,
                             core::Side::SELL, core::OrderType::LIMIT);

        // Record timestamp BEFORE entering ring buffer
        auto start = Clock::now();

        // Submit through gateway
        gateway.submit_order(order);

        // Pop and process (simulating consumer)
        core::Order *popped = nullptr;
        while (!gateway.pop_order(popped))
        {
            // Busy wait (realistic for low-latency systems)
        }

        auto trades = engine.process_order(popped);

        auto end = Clock::now();

        auto latency_ns = duration_cast<nanoseconds>(end - start).count();
        stats.record(static_cast<uint64_t>(latency_ns));

        state.SetIterationTime(static_cast<double>(latency_ns) / 1e9);
        benchmark::DoNotOptimize(trades);
    }

    state.SetItemsProcessed(state.iterations());
    state.counters["p50_us"] = static_cast<double>(stats.p50()) / 1000.0;
    state.counters["p99_us"] = static_cast<double>(stats.p99()) / 1000.0;
    state.counters["p999_us"] = static_cast<double>(stats.p999()) / 1000.0;
}
BENCHMARK(BM_TickToTrade_EndToEnd)
    ->UseManualTime()
    ->Iterations(50000)
    ->Unit(benchmark::kNanosecond);

// =============================================================================
// BENCHMARK: Market Order vs Limit Order Latency
// =============================================================================

static void BM_TickToTrade_MarketOrder(benchmark::State &state)
{
    core::MatchingEngine engine;
    engine.initialize();

    auto &pool = engine.get_order_pool();

    benchmarks::LatencyStats stats;
    uint64_t order_id = 10000;

    for (auto _ : state)
    {
        // Ensure there's liquidity
        for (int i = 0; i < 10; ++i)
        {
            auto *buy = pool.acquire();
            if (buy)
            {
                *buy = core::Order(order_id++, 0, 10000, 100,
                                   core::Side::BUY, core::OrderType::LIMIT);
                engine.process_order(buy);
            }
        }

        auto *order = pool.acquire();
        if (!order)
            continue;

        *order = core::Order(order_id++, 0, 0, 100,
                             core::Side::SELL, core::OrderType::MARKET);

        auto start = Clock::now();
        auto trades = engine.process_order(order);
        auto end = Clock::now();

        auto latency_ns = duration_cast<nanoseconds>(end - start).count();
        stats.record(static_cast<uint64_t>(latency_ns));

        state.SetIterationTime(static_cast<double>(latency_ns) / 1e9);
        benchmark::DoNotOptimize(trades);
    }

    state.SetItemsProcessed(state.iterations());
    state.counters["type"] = 0; // 0 = market
    state.counters["p99_us"] = static_cast<double>(stats.p99()) / 1000.0;
}
BENCHMARK(BM_TickToTrade_MarketOrder)
    ->UseManualTime()
    ->Unit(benchmark::kNanosecond);

static void BM_TickToTrade_LimitOrder(benchmark::State &state)
{
    core::MatchingEngine engine;
    engine.initialize();

    auto &pool = engine.get_order_pool();

    benchmarks::LatencyStats stats;
    uint64_t order_id = 10000;

    for (auto _ : state)
    {
        // Ensure there's liquidity
        for (int i = 0; i < 10; ++i)
        {
            auto *buy = pool.acquire();
            if (buy)
            {
                *buy = core::Order(order_id++, 0, 10000, 100,
                                   core::Side::BUY, core::OrderType::LIMIT);
                engine.process_order(buy);
            }
        }

        auto *order = pool.acquire();
        if (!order)
            continue;

        *order = core::Order(order_id++, 0, 10000, 100,
                             core::Side::SELL, core::OrderType::LIMIT);

        auto start = Clock::now();
        auto trades = engine.process_order(order);
        auto end = Clock::now();

        auto latency_ns = duration_cast<nanoseconds>(end - start).count();
        stats.record(static_cast<uint64_t>(latency_ns));

        state.SetIterationTime(static_cast<double>(latency_ns) / 1e9);
        benchmark::DoNotOptimize(trades);
    }

    state.SetItemsProcessed(state.iterations());
    state.counters["type"] = 1; // 1 = limit
    state.counters["p99_us"] = static_cast<double>(stats.p99()) / 1000.0;
}
BENCHMARK(BM_TickToTrade_LimitOrder)
    ->UseManualTime()
    ->Unit(benchmark::kNanosecond);

// =============================================================================
// BENCHMARK: Sustained Throughput with Latency Tracking
// =============================================================================

static void BM_SustainedThroughput(benchmark::State &state)
{
    core::MatchingEngine engine;
    engine.initialize();

    auto &pool = engine.get_order_pool();
    std::mt19937 rng(42);
    std::uniform_int_distribution<uint64_t> price_dist(9000, 11000);
    std::uniform_int_distribution<uint64_t> qty_dist(1, 100);
    std::uniform_int_distribution<int> side_dist(0, 1);

    benchmarks::LatencyStats stats;
    uint64_t order_id = 0;
    uint64_t total_trades = 0;

    for (auto _ : state)
    {
        auto *order = pool.acquire();
        if (!order)
            continue;

        order_id++;
        *order = core::Order(
            order_id,
            0,
            price_dist(rng),
            qty_dist(rng),
            side_dist(rng) == 0 ? core::Side::BUY : core::Side::SELL,
            core::OrderType::LIMIT);

        auto start = Clock::now();
        auto trades = engine.process_order(order);
        auto end = Clock::now();

        auto latency_ns = duration_cast<nanoseconds>(end - start).count();
        stats.record(static_cast<uint64_t>(latency_ns));

        total_trades += trades.size();
        benchmark::DoNotOptimize(trades);
    }

    state.SetItemsProcessed(state.iterations());
    state.counters["trades"] = static_cast<double>(total_trades);
    state.counters["p50_us"] = static_cast<double>(stats.p50()) / 1000.0;
    state.counters["p99_us"] = static_cast<double>(stats.p99()) / 1000.0;
    state.counters["p999_us"] = static_cast<double>(stats.p999()) / 1000.0;
}
BENCHMARK(BM_SustainedThroughput)
    ->Iterations(1000000)
    ->Unit(benchmark::kNanosecond);

// =============================================================================
// Custom Main to Print Final Report
// =============================================================================

int main(int argc, char **argv)
{
    ::benchmark::Initialize(&argc, argv);

    if (::benchmark::ReportUnrecognizedArguments(argc, argv))
        return 1;

    ::benchmark::RunSpecifiedBenchmarks();
    ::benchmark::Shutdown();

    // Print final latency report
    if (g_latency_stats.count() > 0)
    {
        std::cout << g_latency_stats.summary();
        std::cout << g_latency_stats.ascii_histogram();
    }

    // Print data sourcing information
    std::cout << R"(

=================================================================
CRYPTO L2/L3 DATA SOURCES FOR REPLAY
=================================================================

1. COINBASE EXCHANGE (Recommended for production-quality data):
   - WebSocket Feed: wss://ws-feed.exchange.coinbase.com
   - REST API Level 2: GET https://api.exchange.coinbase.com/products/{product_id}/book?level=2
   - REST API Level 3: GET https://api.exchange.coinbase.com/products/{product_id}/book?level=3
   - Documentation: https://docs.cloud.coinbase.com/exchange/docs

2. BINANCE:
   - WebSocket Depth Stream: wss://stream.binance.com:9443/ws/{symbol}@depth
   - Diff Depth Stream: wss://stream.binance.com:9443/ws/{symbol}@depth@100ms
   - REST Snapshot: GET https://api.binance.com/api/v3/depth?symbol={symbol}&limit=5000
   - Documentation: https://binance-docs.github.io/apidocs/spot/en/

3. HISTORICAL DATA PROVIDERS:
   - Tardis.dev: https://tardis.dev (L2/L3 historical data, free tier available)
   - Kaiko: https://www.kaiko.com (premium institutional-grade data)
   - CryptoDataDownload: https://www.cryptodatadownload.com (free OHLCV data)

4. SAMPLE DATA FORMAT (Coinbase L2):
   {
     "type": "l2update",
     "product_id": "BTC-USD",
     "time": "2024-01-15T10:30:00.000Z",
     "changes": [
       ["buy", "42150.50", "1.5"],
       ["sell", "42151.00", "0.8"]
     ]
   }

5. SAMPLE DATA FORMAT (Binance Depth):
   {
     "lastUpdateId": 160,
     "bids": [["0.0024", "10"]],
     "asks": [["0.0026", "100"]]
   }

=================================================================
)";

    return 0;
}
