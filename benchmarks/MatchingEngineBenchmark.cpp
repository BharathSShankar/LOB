#include <benchmark/benchmark.h>
#include "core/MatchingEngine.h"
#include "core/Order.h"
#include "memory/ObjectPool.h"
#include <random>

using namespace lob;

// ============================================================================
// Week 6 - Performance Benchmarks
// ============================================================================

// Benchmark single order processing
static void BM_ProcessSingleOrder(benchmark::State &state)
{
    core::MatchingEngine engine;
    engine.initialize();

    memory::ObjectPool<core::Order, 1000> pool;

    for (auto _ : state)
    {
        auto *order = pool.acquire();
        *order = core::Order(1, 1000, 10000, 100, core::Side::BUY, core::OrderType::LIMIT);

        auto trades = engine.process_order(order);
        benchmark::DoNotOptimize(trades);

        pool.release(order);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ProcessSingleOrder);

// Benchmark order matching
static void BM_OrderMatching(benchmark::State &state)
{
    core::MatchingEngine engine;
    engine.initialize();

    memory::ObjectPool<core::Order, 10000> pool;

    for (auto _ : state)
    {
        state.PauseTiming();

        // Setup: Add buy order
        auto *buy = pool.acquire();
        *buy = core::Order(1, 1000, 10000, 100, core::Side::BUY, core::OrderType::LIMIT);
        engine.process_order(buy);

        // Measure: Match with sell order
        auto *sell = pool.acquire();
        *sell = core::Order(2, 1001, 10000, 100, core::Side::SELL, core::OrderType::LIMIT);

        state.ResumeTiming();
        auto trades = engine.process_order(sell);
        benchmark::DoNotOptimize(trades);

        pool.release(buy);
        pool.release(sell);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_OrderMatching);

// Benchmark throughput with many orders
static void BM_HighThroughput(benchmark::State &state)
{
    core::MatchingEngine engine;
    engine.initialize();

    memory::ObjectPool<core::Order, 100000> pool;
    std::mt19937 rng(42);
    std::uniform_int_distribution<uint64_t> price_dist(9000, 11000);
    std::uniform_int_distribution<uint64_t> qty_dist(1, 1000);
    std::uniform_int_distribution<int> side_dist(0, 1);

    size_t order_id = 0;

    for (auto _ : state)
    {
        auto *order = pool.acquire();
        if (!order)
            continue;

        order_id++;
        *order = core::Order(
            order_id,
            order_id * 100,
            price_dist(rng),
            qty_dist(rng),
            side_dist(rng) == 0 ? core::Side::BUY : core::Side::SELL,
            core::OrderType::LIMIT);

        auto trades = engine.process_order(order);
        benchmark::DoNotOptimize(trades);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HighThroughput)->Iterations(1000000);

// Benchmark object pool allocation
static void BM_ObjectPoolAllocation(benchmark::State &state)
{
    memory::ObjectPool<core::Order, 100000> pool;

    for (auto _ : state)
    {
        auto *order = pool.acquire();
        benchmark::DoNotOptimize(order);
        pool.release(order);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ObjectPoolAllocation);

// Benchmark heap allocation (for comparison)
static void BM_HeapAllocation(benchmark::State &state)
{
    for (auto _ : state)
    {
        auto *order = new core::Order();
        benchmark::DoNotOptimize(order);
        delete order;
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HeapAllocation);

// Benchmark tick-to-trade latency
static void BM_TickToTrade(benchmark::State &state)
{
    // Measure end-to-end latency from order entry to trade execution
    core::MatchingEngine engine;
    engine.initialize();

    // Use engine's internal pool to avoid stack overflow
    auto &pool = engine.get_order_pool();

    // Pre-populate book with resting orders
    for (int i = 0; i < 100; ++i)
    {
        auto *order = pool.acquire();
        if (order)
        {
            *order = core::Order(i, i * 100, 9500 + i, 100,
                                 core::Side::BUY, core::OrderType::LIMIT);
            engine.process_order(order);
        }
    }

    uint64_t order_id = 1000;

    for (auto _ : state)
    {
        order_id++;
        auto *order = pool.acquire();
        if (!order)
            continue;

        *order = core::Order(order_id, order_id * 100, 9550, 10,
                             core::Side::SELL, core::OrderType::LIMIT);

        auto start = std::chrono::high_resolution_clock::now();
        auto trades = engine.process_order(order);
        auto end = std::chrono::high_resolution_clock::now();

        auto latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                              end - start)
                              .count();

        state.SetIterationTime(latency_ns / 1e9);
        benchmark::DoNotOptimize(trades);

        pool.release(order);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_TickToTrade)->UseManualTime();

BENCHMARK_MAIN();
