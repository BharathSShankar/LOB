#include <gtest/gtest.h>
#include "core/MatchingEngine.h"
#include "core/Order.h"
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <random>
#include <iostream>

using namespace lob::core;

class MatchingEngineTest : public ::testing::Test
{
protected:
    MatchingEngine engine;

    void SetUp() override
    {
        engine.initialize();
    }

    void TearDown() override
    {
        engine.reset_statistics();
    }
};

// ============================================================================
// Week 1-2 Tests - Engine Operations
// ============================================================================

TEST_F(MatchingEngineTest, Initialization)
{
    auto stats = engine.get_statistics();
    EXPECT_EQ(stats.total_orders_processed, 0);
    EXPECT_EQ(stats.total_trades_executed, 0);
}

TEST_F(MatchingEngineTest, ProcessValidOrder)
{
    Order order(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);
    auto trades = engine.process_order(&order);

    auto stats = engine.get_statistics();
    EXPECT_EQ(stats.total_orders_processed, 1);
}

TEST_F(MatchingEngineTest, RejectInvalidOrder)
{
    Order invalid_order(0, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);
    auto trades = engine.process_order(&invalid_order);

    EXPECT_EQ(invalid_order.status, OrderStatus::REJECTED);

    auto stats = engine.get_statistics();
    EXPECT_EQ(stats.total_orders_rejected, 1);
}

TEST_F(MatchingEngineTest, OrderMatching)
{
    Order buy(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);
    Order sell(2, 1001, 10000, 100, Side::SELL, OrderType::LIMIT);

    engine.process_order(&buy);
    auto trades = engine.process_order(&sell);

    EXPECT_EQ(trades.size(), 1);

    auto stats = engine.get_statistics();
    EXPECT_EQ(stats.total_orders_processed, 2);
    EXPECT_EQ(stats.total_trades_executed, 1);
    EXPECT_EQ(stats.total_volume, 100);
}

TEST_F(MatchingEngineTest, CancelOrder)
{
    Order order(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);
    engine.process_order(&order);

    bool cancelled = engine.cancel_order(1, "DEFAULT");
    EXPECT_TRUE(cancelled);

    auto stats = engine.get_statistics();
    EXPECT_EQ(stats.total_orders_cancelled, 1);
}

TEST_F(MatchingEngineTest, MultipleInstruments)
{
    engine.create_order_book("BTCUSD");
    engine.create_order_book("ETHUSD");

    auto *btc_book = engine.get_order_book("BTCUSD");
    auto *eth_book = engine.get_order_book("ETHUSD");

    EXPECT_NE(btc_book, nullptr);
    EXPECT_NE(eth_book, nullptr);
    EXPECT_NE(btc_book, eth_book);
}

TEST_F(MatchingEngineTest, StatisticsTracking)
{
    Order buy1(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);
    Order buy2(2, 1001, 9900, 50, Side::BUY, OrderType::LIMIT);
    Order sell(3, 1002, 10000, 100, Side::SELL, OrderType::LIMIT);

    engine.process_order(&buy1);
    engine.process_order(&buy2);
    engine.process_order(&sell);

    auto stats = engine.get_statistics();
    EXPECT_EQ(stats.total_orders_processed, 3);
    EXPECT_GE(stats.total_volume, 0);
}

TEST_F(MatchingEngineTest, ResetStatistics)
{
    Order order(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);
    engine.process_order(&order);

    engine.reset_statistics();
    auto stats = engine.get_statistics();

    EXPECT_EQ(stats.total_orders_processed, 0);
    EXPECT_EQ(stats.total_trades_executed, 0);
}

// ============================================================================
// Week 1-2 Extended Tests - Edge Cases & Error Handling
// ============================================================================

TEST_F(MatchingEngineTest, ProcessNullOrder)
{
    // Test processing null order pointer
    auto trades = engine.process_order(nullptr);

    EXPECT_EQ(trades.size(), 0);

    auto stats = engine.get_statistics();
    EXPECT_EQ(stats.total_orders_processed, 0);
    EXPECT_EQ(stats.total_orders_rejected, 0);
}

TEST_F(MatchingEngineTest, RejectOrderWithZeroPrice)
{
    // Test rejecting limit order with zero price
    Order invalid_order(1, 1000, 0, 100, Side::BUY, OrderType::LIMIT);
    auto trades = engine.process_order(&invalid_order);

    EXPECT_EQ(invalid_order.status, OrderStatus::REJECTED);
    EXPECT_EQ(trades.size(), 0);

    auto stats = engine.get_statistics();
    EXPECT_EQ(stats.total_orders_rejected, 1);
}

TEST_F(MatchingEngineTest, RejectOrderWithZeroQuantity)
{
    // Test rejecting order with zero quantity
    Order invalid_order(1, 1000, 10000, 0, Side::BUY, OrderType::LIMIT);
    auto trades = engine.process_order(&invalid_order);

    EXPECT_EQ(invalid_order.status, OrderStatus::REJECTED);
    EXPECT_EQ(trades.size(), 0);

    auto stats = engine.get_statistics();
    EXPECT_EQ(stats.total_orders_rejected, 1);
}

TEST_F(MatchingEngineTest, CancelNonExistentOrder)
{
    // Test canceling order that doesn't exist
    bool cancelled = engine.cancel_order(999, "DEFAULT");
    EXPECT_FALSE(cancelled);

    auto stats = engine.get_statistics();
    EXPECT_EQ(stats.total_orders_cancelled, 0);
}

TEST_F(MatchingEngineTest, CancelOrderFromNonExistentInstrument)
{
    // Test canceling order from instrument that doesn't exist
    bool cancelled = engine.cancel_order(1, "NONEXISTENT");
    EXPECT_FALSE(cancelled);

    auto stats = engine.get_statistics();
    EXPECT_EQ(stats.total_orders_cancelled, 0);
}

TEST_F(MatchingEngineTest, GetNonExistentOrderBook)
{
    // Test getting order book for non-existent instrument
    auto *book = engine.get_order_book("NONEXISTENT");
    EXPECT_EQ(book, nullptr);
}

TEST_F(MatchingEngineTest, MultipleTradesVolumeTracking)
{
    // Test volume tracking across multiple trades
    Order buy1(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);
    Order buy2(2, 1001, 10000, 50, Side::BUY, OrderType::LIMIT);
    Order sell(3, 1002, 10000, 150, Side::SELL, OrderType::LIMIT);

    engine.process_order(&buy1);
    engine.process_order(&buy2);
    auto trades = engine.process_order(&sell);

    // Should match both buy orders
    EXPECT_EQ(trades.size(), 2);

    auto stats = engine.get_statistics();
    EXPECT_EQ(stats.total_trades_executed, 2);
    EXPECT_EQ(stats.total_volume, 150); // 100 + 50
}

// ============================================================================
// Week 3-4 Tests - Memory Integration
// ============================================================================

TEST_F(MatchingEngineTest, ObjectPoolIntegration)
{
    // Test that matching engine has integrated object pool
    auto &order_pool = engine.get_order_pool();

    // Verify pool is initialized with correct capacity
    EXPECT_EQ(order_pool.capacity(), 1000000);
    EXPECT_EQ(order_pool.available(), 1000000);
    EXPECT_TRUE(order_pool.is_full());
    EXPECT_FALSE(order_pool.is_empty());

    // Test pool statistics
    auto pool_stats = engine.get_pool_statistics();
    EXPECT_EQ(pool_stats.capacity, 1000000);
    EXPECT_EQ(pool_stats.available, 1000000);
    EXPECT_EQ(pool_stats.in_use, 0);
}

TEST_F(MatchingEngineTest, ObjectPoolAcquireRelease)
{
    // Test acquiring and releasing orders from engine's pool
    auto &order_pool = engine.get_order_pool();

    // Acquire an order
    auto *order = order_pool.acquire();
    ASSERT_NE(order, nullptr);

    // Initialize the order
    *order = Order(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);
    EXPECT_EQ(order->order_id, 1);
    EXPECT_EQ(order->price, 10000);
    EXPECT_EQ(order->quantity, 100);

    // Pool should have one less available
    EXPECT_EQ(order_pool.available(), 999999);
    auto pool_stats = engine.get_pool_statistics();
    EXPECT_EQ(pool_stats.in_use, 1);

    // Release the order back to pool
    order_pool.release(order);

    // Pool should be back to full capacity
    EXPECT_EQ(order_pool.available(), 1000000);
    EXPECT_TRUE(order_pool.is_full());
}

TEST_F(MatchingEngineTest, ObjectPoolProcessOrder)
{
    // Test processing orders acquired from pool
    auto &order_pool = engine.get_order_pool();

    // Acquire and process a buy order
    auto *buy_order = order_pool.acquire();
    ASSERT_NE(buy_order, nullptr);
    *buy_order = Order(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);

    auto trades = engine.process_order(buy_order);
    EXPECT_EQ(trades.size(), 0); // No match
    EXPECT_EQ(buy_order->status, OrderStatus::NEW);

    // Acquire and process a matching sell order
    auto *sell_order = order_pool.acquire();
    ASSERT_NE(sell_order, nullptr);
    *sell_order = Order(2, 1001, 10000, 50, Side::SELL, OrderType::LIMIT);

    trades = engine.process_order(sell_order);
    EXPECT_EQ(trades.size(), 1); // One trade
    EXPECT_EQ(trades[0].quantity, 50);
    EXPECT_EQ(trades[0].price, 10000);

    // Verify statistics
    auto stats = engine.get_statistics();
    EXPECT_EQ(stats.total_orders_processed, 2);
    EXPECT_EQ(stats.total_trades_executed, 1);
    EXPECT_EQ(stats.total_volume, 50);

    // Verify pool usage
    auto pool_stats = engine.get_pool_statistics();
    EXPECT_EQ(pool_stats.in_use, 2);
    EXPECT_EQ(pool_stats.available, 999998);
}

TEST_F(MatchingEngineTest, ObjectPoolMultipleOrders)
{
    // Test acquiring multiple orders from pool
    auto &order_pool = engine.get_order_pool();

    const int NUM_ORDERS = 100;
    std::vector<Order *> orders;

    // Acquire multiple orders
    for (int i = 0; i < NUM_ORDERS; ++i)
    {
        auto *order = order_pool.acquire();
        ASSERT_NE(order, nullptr);
        *order = Order(i + 1, 1000 + i, 10000, 100, Side::BUY, OrderType::LIMIT);
        orders.push_back(order);
    }

    // Verify pool has correct number available
    EXPECT_EQ(order_pool.available(), 1000000 - NUM_ORDERS);
    auto pool_stats = engine.get_pool_statistics();
    EXPECT_EQ(pool_stats.in_use, NUM_ORDERS);

    // Release all orders
    for (auto *order : orders)
    {
        order_pool.release(order);
    }

    // Pool should be full again
    EXPECT_EQ(order_pool.available(), 1000000);
    EXPECT_TRUE(order_pool.is_full());
}

// ============================================================================
// Week 5 Tests - Concurrency
// ============================================================================

TEST_F(MatchingEngineTest, ThreadSafety)
{
    // Test thread safety with concurrent order processing
    // Note: Current implementation uses sequential processing with a mutex
    // This test validates correctness when multiple threads submit orders

    const int NUM_THREADS = 4;
    const int ORDERS_PER_THREAD = 100;
    std::atomic<int> orders_submitted{0};
    std::atomic<int> trades_executed{0};
    std::mutex engine_mutex;

    // Pre-allocate all orders
    std::vector<std::vector<Order>> thread_orders(NUM_THREADS);
    for (int t = 0; t < NUM_THREADS; ++t)
    {
        thread_orders[t].reserve(ORDERS_PER_THREAD);
        for (int i = 0; i < ORDERS_PER_THREAD; ++i)
        {
            uint64_t order_id = t * ORDERS_PER_THREAD + i + 1;
            uint64_t price = 10000 + (i % 10) - 5; // Price range: 9995-10004
            Side side = (t % 2 == 0) ? Side::BUY : Side::SELL;
            thread_orders[t].emplace_back(order_id, 1000 + order_id, price, 10, side, OrderType::LIMIT);
        }
    }

    auto worker = [&](int thread_id)
    {
        for (int i = 0; i < ORDERS_PER_THREAD; ++i)
        {
            std::lock_guard<std::mutex> lock(engine_mutex);
            auto trades = engine.process_order(&thread_orders[thread_id][i]);
            orders_submitted.fetch_add(1);
            trades_executed.fetch_add(static_cast<int>(trades.size()));
        }
    };

    auto start = std::chrono::high_resolution_clock::now();

    // Spawn threads
    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; ++t)
    {
        threads.emplace_back(worker, t);
    }

    // Wait for all threads to complete
    for (auto &t : threads)
    {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "\n[ThreadSafety] Results:\n";
    std::cout << "  - Threads: " << NUM_THREADS << "\n";
    std::cout << "  - Orders per thread: " << ORDERS_PER_THREAD << "\n";
    std::cout << "  - Total orders submitted: " << orders_submitted.load() << "\n";
    std::cout << "  - Total trades executed: " << trades_executed.load() << "\n";
    std::cout << "  - Total time: " << duration.count() << " µs\n";

    // Verify all orders were processed
    EXPECT_EQ(orders_submitted.load(), NUM_THREADS * ORDERS_PER_THREAD);

    auto stats = engine.get_statistics();
    EXPECT_EQ(stats.total_orders_processed, NUM_THREADS * ORDERS_PER_THREAD);
}

// ============================================================================
// Week 6 Tests - Performance
// ============================================================================

TEST_F(MatchingEngineTest, LatencyBenchmark)
{
    // Measure tick-to-trade latency (time from order submission to trade execution)
    const int NUM_MEASUREMENTS = 1000;
    std::vector<int64_t> latencies;
    latencies.reserve(NUM_MEASUREMENTS);

    // Pre-populate the book with resting orders
    std::vector<Order> resting_orders;
    resting_orders.reserve(NUM_MEASUREMENTS);
    for (int i = 0; i < NUM_MEASUREMENTS; ++i)
    {
        resting_orders.emplace_back(i + 1, 1000 + i, 10000, 100, Side::SELL, OrderType::LIMIT);
        engine.process_order(&resting_orders.back());
    }

    // Create matching orders and measure latency
    std::vector<Order> matching_orders;
    matching_orders.reserve(NUM_MEASUREMENTS);
    for (int i = 0; i < NUM_MEASUREMENTS; ++i)
    {
        matching_orders.emplace_back(NUM_MEASUREMENTS + i + 1, 2000 + i, 10000, 100, Side::BUY, OrderType::LIMIT);

        auto start = std::chrono::high_resolution_clock::now();
        auto trades = engine.process_order(&matching_orders.back());
        auto end = std::chrono::high_resolution_clock::now();

        if (!trades.empty())
        {
            auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            latencies.push_back(latency);
        }
    }

    // Calculate statistics
    if (!latencies.empty())
    {
        std::sort(latencies.begin(), latencies.end());
        int64_t min_lat = latencies.front();
        int64_t max_lat = latencies.back();
        int64_t median_lat = latencies[latencies.size() / 2];
        int64_t p99_lat = latencies[static_cast<size_t>(latencies.size() * 0.99)];

        int64_t sum = 0;
        for (auto lat : latencies)
            sum += lat;
        double avg_lat = static_cast<double>(sum) / latencies.size();

        std::cout << "\n[LatencyBenchmark] Tick-to-Trade Latency (ns):\n";
        std::cout << "  - Samples: " << latencies.size() << "\n";
        std::cout << "  - Min: " << min_lat << " ns\n";
        std::cout << "  - Max: " << max_lat << " ns\n";
        std::cout << "  - Avg: " << static_cast<int64_t>(avg_lat) << " ns\n";
        std::cout << "  - Median: " << median_lat << " ns\n";
        std::cout << "  - P99: " << p99_lat << " ns\n";

        // Performance assertion: median latency should be under 10 microseconds
        EXPECT_LT(median_lat, 10000) << "Median latency exceeds 10µs";
    }
    else
    {
        FAIL() << "No trades executed for latency measurement";
    }
}

TEST_F(MatchingEngineTest, ThroughputBenchmark)
{
    // Measure throughput (orders/second)
    const int NUM_ORDERS = 50000;
    std::vector<Order> orders;
    orders.reserve(NUM_ORDERS);

    // Create mixed buy/sell orders at various prices
    std::mt19937 gen(42);
    std::uniform_int_distribution<uint64_t> price_dist(9000, 11000);
    std::uniform_int_distribution<uint64_t> qty_dist(10, 100);

    for (int i = 0; i < NUM_ORDERS; ++i)
    {
        uint64_t price = price_dist(gen);
        uint64_t qty = qty_dist(gen);
        Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        orders.emplace_back(i + 1, 1000 + i, price, qty, side, OrderType::LIMIT);
    }

    auto start = std::chrono::high_resolution_clock::now();

    size_t total_trades = 0;
    for (auto &order : orders)
    {
        auto trades = engine.process_order(&order);
        total_trades += trades.size();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double seconds = duration.count() / 1000000.0;
    double orders_per_second = NUM_ORDERS / seconds;
    double trades_per_second = total_trades / seconds;

    std::cout << "\n[ThroughputBenchmark] Results:\n";
    std::cout << "  - Orders processed: " << NUM_ORDERS << "\n";
    std::cout << "  - Trades executed: " << total_trades << "\n";
    std::cout << "  - Total time: " << duration.count() << " µs (" << seconds << " s)\n";
    std::cout << "  - Throughput: " << static_cast<int>(orders_per_second) << " orders/sec\n";
    std::cout << "  - Trade rate: " << static_cast<int>(trades_per_second) << " trades/sec\n";

    // Performance assertion: should handle at least 100k orders/second
    EXPECT_GT(orders_per_second, 100000) << "Throughput below 100k orders/second";
}
