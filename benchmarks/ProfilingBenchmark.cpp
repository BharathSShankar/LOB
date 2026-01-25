#include <benchmark/benchmark.h>
#include "core/MatchingEngine.h"
#include "core/Order.h"
#include "memory/ObjectPool.h"
#include "profiling/MemoryProfiler.h"
#include "profiling/HotPathProfiler.h"
#include <random>
#include <fstream>
#include <iostream>

using namespace lob;

/**
 * Comprehensive profiling benchmark with memory and hot path tracking
 */

// Benchmark with full profiling enabled
static void BM_ProfilingOrderProcessing(benchmark::State &state)
{
    auto &mem_profiler = profiling::MemoryProfiler::instance();
    auto &path_profiler = profiling::HotPathProfiler::instance();

    mem_profiler.start_session("OrderProcessing");
    mem_profiler.set_event_recording(true);
    path_profiler.set_sample_collection(true);

    core::MatchingEngine engine;
    engine.initialize();

    memory::ObjectPool<core::Order, 10000> pool;

    for (auto _ : state)
    {
        profiling::ScopeTimer timer("process_order");

        auto *order = pool.acquire();
        mem_profiler.record_allocation(order, sizeof(core::Order), "Order");

        *order = core::Order(1, 1000, 10000, 100, core::Side::BUY, core::OrderType::LIMIT);

        auto trades = engine.process_order(order);
        benchmark::DoNotOptimize(trades);

        mem_profiler.record_deallocation(order, sizeof(core::Order), "Order");
        pool.release(order);
    }

    mem_profiler.end_session();

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ProfilingOrderProcessing)->Iterations(100000);

// Benchmark order matching with profiling
static void BM_ProfilingOrderMatching(benchmark::State &state)
{
    auto &mem_profiler = profiling::MemoryProfiler::instance();
    auto &path_profiler = profiling::HotPathProfiler::instance();

    mem_profiler.start_session("OrderMatching");
    path_profiler.reset();

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

        {
            profiling::ScopeTimer timer("match_order");
            auto trades = engine.process_order(sell);
            benchmark::DoNotOptimize(trades);
        }

        pool.release(buy);
        pool.release(sell);
    }

    mem_profiler.end_session();

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ProfilingOrderMatching)->Iterations(50000);

// High throughput test with profiling
static void BM_ProfilingHighThroughput(benchmark::State &state)
{
    auto &mem_profiler = profiling::MemoryProfiler::instance();
    auto &path_profiler = profiling::HotPathProfiler::instance();

    mem_profiler.start_session("HighThroughput");
    path_profiler.reset();

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
        profiling::ScopeTimer timer("high_throughput_order");

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

    mem_profiler.end_session();

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ProfilingHighThroughput)->Iterations(100000);

// Benchmark to measure allocation overhead
static void BM_ProfilingAllocationOverhead(benchmark::State &state)
{
    auto &mem_profiler = profiling::MemoryProfiler::instance();

    mem_profiler.start_session("AllocationOverhead");
    mem_profiler.set_event_recording(false); // Low overhead mode

    memory::ObjectPool<core::Order, 100000> pool;

    for (auto _ : state)
    {
        auto *order = pool.acquire();
        mem_profiler.record_allocation(order, sizeof(core::Order), "PoolAlloc");
        benchmark::DoNotOptimize(order);
        mem_profiler.record_deallocation(order, sizeof(core::Order), "PoolAlloc");
        pool.release(order);
    }

    mem_profiler.end_session();

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ProfilingAllocationOverhead)->Iterations(1000000);

// Custom main to generate reports
int main(int argc, char **argv)
{
    // Run benchmarks
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv))
        return 1;

    benchmark::RunSpecifiedBenchmarks();

    // Generate profiling reports
    std::cout << "\n\n";
    std::cout << "===============================================\n";
    std::cout << "          PROFILING REPORTS                    \n";
    std::cout << "===============================================\n";

    auto &mem_profiler = profiling::MemoryProfiler::instance();
    auto &path_profiler = profiling::HotPathProfiler::instance();

    std::string mem_report = mem_profiler.generate_report();
    std::string path_report = path_profiler.generate_report();

    std::cout << mem_report;
    std::cout << path_report;

    // Write reports to files
    std::ofstream mem_file("profiling_memory_report.txt");
    if (mem_file.is_open())
    {
        mem_file << mem_report;
        mem_file.close();
        std::cout << "\n📊 Memory report saved to: profiling_memory_report.txt\n";
    }

    std::ofstream path_file("profiling_hotpath_report.txt");
    if (path_file.is_open())
    {
        path_file << path_report;
        path_file.close();
        std::cout << "📊 Hot path report saved to: profiling_hotpath_report.txt\n";
    }

    // Generate summary JSON for automated analysis
    std::ofstream json_file("profiling_summary.json");
    if (json_file.is_open())
    {
        json_file << "{\n";
        json_file << "  \"memory\": {\n";
        auto stats = mem_profiler.get_stats();
        json_file << "    \"allocations\": " << stats.alloc_count << ",\n";
        json_file << "    \"deallocations\": " << stats.dealloc_count << ",\n";
        json_file << "    \"total_allocated_bytes\": " << stats.total_bytes_allocated << ",\n";
        json_file << "    \"peak_memory_bytes\": " << stats.peak_memory << ",\n";
        json_file << "    \"current_memory_bytes\": " << stats.current_memory << "\n";
        json_file << "  },\n";
        json_file << "  \"hot_paths\": {\n";
        auto path_stats = path_profiler.get_all_stats();
        bool first = true;
        for (const auto &[name, stats] : path_stats)
        {
            if (!first)
                json_file << ",\n";
            first = false;
            json_file << "    \"" << name << "\": {\n";
            json_file << "      \"call_count\": " << stats.call_count << ",\n";
            json_file << "      \"avg_ns\": " << (stats.call_count > 0 ? stats.total_time_ns / stats.call_count : 0) << ",\n";
            json_file << "      \"min_ns\": " << (stats.min_time_ns == UINT64_MAX ? 0 : stats.min_time_ns) << ",\n";
            json_file << "      \"max_ns\": " << stats.max_time_ns << "\n";
            json_file << "    }";
        }
        json_file << "\n  }\n";
        json_file << "}\n";
        json_file.close();
        std::cout << "📊 JSON summary saved to: profiling_summary.json\n";
    }

    std::cout << "\n===============================================\n\n";

    benchmark::Shutdown();
    return 0;
}
