#include "core/MatchingEngine.h"
#include "core/Order.h"
#include "core/OrderBook.h"
#include "memory/ObjectPool.h"
#include "concurrency/OrderEntryGateway.h"
#include "market_data/MarketDataPublisher.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <iomanip>
#include <sstream>
#include <memory>

using namespace lob;

// =============================================================================
// Terminal Visualization
// =============================================================================

/**
 * @brief Print a visual order book depth chart in the terminal
 */
void print_depth_chart(const core::OrderBook &book, size_t levels = 10)
{
    std::vector<core::OrderBook::DepthLevel> bids, asks;
    book.get_market_depth(bids, asks, levels);

    // Find max quantity for scaling
    uint64_t max_qty = 1;
    for (const auto &bid : bids)
        max_qty = std::max(max_qty, bid.quantity);
    for (const auto &ask : asks)
        max_qty = std::max(max_qty, ask.quantity);

    const int bar_width = 20;

    std::cout << "\n╔════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                        ORDER BOOK DEPTH                            ║\n";
    std::cout << "╠══════════════════════════════╦═══════════════════════════════════════╣\n";
    std::cout << "║          BIDS (BUY)          ║          ASKS (SELL)                 ║\n";
    std::cout << "╠══════════════════════════════╬═══════════════════════════════════════╣\n";

    for (size_t i = 0; i < levels; ++i)
    {
        // Bid side (reversed - show from best to worst)
        size_t bid_idx = i < bids.size() ? i : SIZE_MAX;
        std::string bid_bar, bid_info;

        if (bid_idx != SIZE_MAX)
        {
            int filled = static_cast<int>((bids[bid_idx].quantity * bar_width) / max_qty);
            bid_bar = std::string(bar_width - filled, ' ') + std::string(filled, '#');

            std::ostringstream oss;
            oss << std::setw(8) << bids[bid_idx].price << " | "
                << std::setw(6) << bids[bid_idx].quantity;
            bid_info = oss.str();
        }
        else
        {
            bid_bar = std::string(bar_width, ' ');
            bid_info = "         |       ";
        }

        // Ask side
        size_t ask_idx = i < asks.size() ? i : SIZE_MAX;
        std::string ask_bar, ask_info;

        if (ask_idx != SIZE_MAX)
        {
            int filled = static_cast<int>((asks[ask_idx].quantity * bar_width) / max_qty);
            ask_bar = std::string(filled, '#') + std::string(bar_width - filled, ' ');

            std::ostringstream oss;
            oss << std::setw(6) << asks[ask_idx].quantity << " | "
                << std::setw(8) << asks[ask_idx].price;
            ask_info = oss.str();
        }
        else
        {
            ask_bar = std::string(bar_width, ' ');
            ask_info = "       |         ";
        }

        std::cout << "║ " << bid_bar << " " << bid_info
                  << " ║ " << ask_info << " " << ask_bar << " ║\n";
    }

    std::cout << "╚══════════════════════════════╩═══════════════════════════════════════╝\n";

    // Print spread information
    auto best_bid = book.get_best_bid();
    auto best_ask = book.get_best_ask();
    auto spread = book.get_spread();

    if (best_bid && best_ask)
    {
        std::cout << "  Best Bid: " << *best_bid
                  << " | Best Ask: " << *best_ask
                  << " | Spread: " << (spread ? *spread : 0)
                  << " (" << std::fixed << std::setprecision(2)
                  << (100.0 * (*spread) / *best_bid) << "%)\n";
    }
    std::cout << std::endl;
}

/**
 * @brief Print trade information
 */
void print_trade(const core::Trade &trade)
{
    std::cout << "  🔔 TRADE: " << trade.quantity << " @ " << trade.price
              << " (Buy: " << trade.buy_order_id << ", Sell: " << trade.sell_order_id << ")\n";
}

/**
 * @brief Print statistics summary
 */
void print_statistics(const core::MatchingEngine::Statistics &stats,
                      const concurrency::OrderEntryGateway::Statistics &gateway_stats,
                      double elapsed_seconds)
{
    std::cout << "\n╔════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                      PERFORMANCE STATISTICS                        ║\n";
    std::cout << "╠══════════════════════════════════════════════════════════════════════╣\n";

    double throughput = stats.total_orders_processed / elapsed_seconds;
    double trade_rate = stats.total_trades_executed / elapsed_seconds;

    std::cout << "║  Orders Processed: " << std::setw(12) << stats.total_orders_processed
              << "    Trades Executed: " << std::setw(10) << stats.total_trades_executed << "  ║\n";
    std::cout << "║  Total Volume:     " << std::setw(12) << stats.total_volume
              << "    Cancellations:   " << std::setw(10) << stats.total_orders_cancelled << "  ║\n";
    std::cout << "║  Orders Rejected:  " << std::setw(12) << stats.total_orders_rejected
              << "    Orders Dropped:  " << std::setw(10) << gateway_stats.total_orders_dropped << "  ║\n";
    std::cout << "╠══════════════════════════════════════════════════════════════════════╣\n";
    std::cout << "║  Throughput:       " << std::setw(12) << std::fixed << std::setprecision(0)
              << throughput << " orders/sec"
              << "                            ║\n";
    std::cout << "║  Trade Rate:       " << std::setw(12) << std::fixed << std::setprecision(0)
              << trade_rate << " trades/sec"
              << "                            ║\n";
    std::cout << "║  Elapsed Time:     " << std::setw(12) << std::fixed << std::setprecision(2)
              << elapsed_seconds << " seconds"
              << "                               ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════════╝\n";
}

// =============================================================================
// Main Application
// =============================================================================

int main()
{
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║     HIGH-PERFORMANCE LIMIT ORDER BOOK MATCHING ENGINE              ║\n";
    std::cout << "║     Timeline: 6 Weeks | Target: >1M orders/sec                     ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════════╝\n";
    std::cout << std::endl;

    // =========================================================================
    // Week 1-2: Initialize Matching Engine
    // =========================================================================
    std::cout << "[Week 1-2] Initializing Matching Engine...\n";
    core::MatchingEngine engine;
    engine.initialize();
    std::cout << "  ✓ Matching Engine initialized\n";

    // =========================================================================
    // Week 3-4: Object Pool Integration
    // =========================================================================
    std::cout << "\n[Week 3-4] Object Pool Integration...\n";
    auto &order_pool = engine.get_order_pool();
    auto pool_stats = engine.get_pool_statistics();
    std::cout << "  ✓ Object Pool integrated with capacity: " << pool_stats.capacity << "\n";

    // =========================================================================
    // Week 5: Lock-Free Concurrency
    // =========================================================================
    std::cout << "\n[Week 5] Initializing Lock-Free Ring Buffer & Gateway...\n";

    // Create a separate pool for the gateway (100K orders) - heap allocated to avoid stack overflow
    auto gateway_pool = std::make_unique<memory::ObjectPool<core::Order, 100000>>();
    concurrency::OrderEntryGateway gateway(*gateway_pool);

    // Configure order generation
    concurrency::OrderEntryGateway::Config config;
    config.base_price = 10000; // $100.00
    config.price_range = 100;  // +/- $0.50
    config.min_quantity = 10;
    config.max_quantity = 500;
    config.buy_sell_ratio = 0.5;      // 50% buy, 50% sell
    config.limit_market_ratio = 0.95; // 95% limit orders
    config.orders_per_batch = 1000;
    config.batch_delay_us = 1000; // 1ms between batches
    config.auto_generate = false; // Manual control for demo
    gateway.set_config(config);

    std::cout << "  ✓ Order Entry Gateway initialized\n";
    std::cout << "  ✓ Ring Buffer capacity: 65536 orders\n";

    // =========================================================================
    // Week 6: Market Data Publisher & Visualization
    // =========================================================================
    std::cout << "\n[Week 6] Initializing Market Data Publisher...\n";
    market_data::MarketDataPublisher publisher;

    // Subscribe to trades for logging
    std::atomic<uint64_t> trade_count{0};
    publisher.subscribe_trades([&trade_count](const auto &trade)
                               { trade_count.fetch_add(1, std::memory_order_relaxed); });

    std::cout << "  ✓ Market Data Publisher initialized\n";

    // =========================================================================
    // Demo 1: Simple Order Matching
    // =========================================================================
    std::cout << "\n╔════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                    DEMO 1: SIMPLE ORDER MATCHING                   ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════════╝\n";

    // Create initial buy orders to build the book
    std::cout << "\nBuilding initial order book...\n";

    for (int i = 0; i < 5; ++i)
    {
        auto *buy = order_pool.acquire();
        if (buy)
        {
            *buy = core::Order(
                100 + i, // order_id
                std::chrono::steady_clock::now().time_since_epoch().count(),
                10000 - i * 10, // prices: 10000, 9990, 9980, ...
                100 * (i + 1),  // quantities: 100, 200, 300, ...
                core::Side::BUY,
                core::OrderType::LIMIT);

            engine.process_order(buy);
            std::cout << "  Added BUY:  " << buy->quantity << " @ " << buy->price << "\n";
        }
    }

    for (int i = 0; i < 5; ++i)
    {
        auto *sell = order_pool.acquire();
        if (sell)
        {
            *sell = core::Order(
                200 + i, // order_id
                std::chrono::steady_clock::now().time_since_epoch().count(),
                10010 + i * 10, // prices: 10010, 10020, 10030, ...
                100 * (i + 1),  // quantities: 100, 200, 300, ...
                core::Side::SELL,
                core::OrderType::LIMIT);

            engine.process_order(sell);
            std::cout << "  Added SELL: " << sell->quantity << " @ " << sell->price << "\n";
        }
    }

    // Display order book
    print_depth_chart(*engine.get_order_book("DEFAULT"), 5);

    // Execute a crossing order
    std::cout << "Sending crossing SELL order: 150 @ 9990 (will match with BUY @ 10000)\n";
    auto *crossing = order_pool.acquire();
    if (crossing)
    {
        *crossing = core::Order(
            300,
            std::chrono::steady_clock::now().time_since_epoch().count(),
            9990, // Will match with best bid at 10000
            150,
            core::Side::SELL,
            core::OrderType::LIMIT);

        auto trades = engine.process_order(crossing);
        for (const auto &trade : trades)
        {
            print_trade(trade);
            publisher.publish_trade(trade);
        }
    }

    // Display updated order book
    print_depth_chart(*engine.get_order_book("DEFAULT"), 5);

    // =========================================================================
    // Demo 2: Multi-Threaded Producer/Consumer
    // =========================================================================
    std::cout << "\n╔════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║              DEMO 2: MULTI-THREADED PRODUCER/CONSUMER              ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════════╝\n";

    // Reset engine for clean test
    engine.initialize();

    std::atomic<bool> consumer_running{true};
    std::atomic<uint64_t> orders_consumed{0};

    // Consumer thread (Matching Engine)
    std::thread consumer_thread([&]()
                                {
        while (consumer_running.load(std::memory_order_acquire))
        {
            core::Order* order = nullptr;
            if (gateway.pop_order(order))
            {
                if (order)
                {
                    auto trades = engine.process_order(order);
                    orders_consumed.fetch_add(1, std::memory_order_relaxed);
                    
                    // Publish trades
                    for (const auto& trade : trades)
                    {
                        publisher.publish_trade(trade);
                    }
                    
                    // Release order back to gateway pool
                    gateway_pool->release(order);
                }
            }
            else
            {
                // No order available, yield
                std::this_thread::yield();
            }
        }
        
        // Drain remaining orders
        core::Order* order = nullptr;
        while (gateway.pop_order(order))
        {
            if (order)
            {
                engine.process_order(order);
                orders_consumed.fetch_add(1, std::memory_order_relaxed);
                gateway_pool->release(order);
            }
        } });

    std::cout << "\nGenerating random orders...\n";
    std::cout << "  Producer Thread: Gateway generating orders\n";
    std::cout << "  Consumer Thread: Matching Engine processing orders\n\n";

    auto start_time = std::chrono::high_resolution_clock::now();

    // Generate orders in batches
    constexpr size_t TOTAL_ORDERS = 100000;
    constexpr size_t BATCH_SIZE = 10000;

    for (size_t batch = 0; batch < TOTAL_ORDERS / BATCH_SIZE; ++batch)
    {
        size_t generated = gateway.generate_orders(BATCH_SIZE);
        std::cout << "  Batch " << (batch + 1) << ": Generated " << generated << " orders, "
                  << "Consumed: " << orders_consumed.load() << ", "
                  << "Buffer: " << gateway.buffer_size() << "\n";

        // Small delay to let consumer catch up
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Wait for consumer to finish
    std::cout << "\nWaiting for consumer to finish...\n";
    while (!gateway.is_buffer_empty())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Stop consumer thread
    consumer_running.store(false, std::memory_order_release);
    consumer_thread.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    double elapsed_seconds = std::chrono::duration<double>(end_time - start_time).count();

    // Display final order book
    print_depth_chart(*engine.get_order_book("DEFAULT"), 10);

    // Display statistics
    auto engine_stats = engine.get_statistics();
    auto gateway_stats = gateway.get_statistics();
    print_statistics(engine_stats, gateway_stats, elapsed_seconds);

    // =========================================================================
    // Demo 3: High-Frequency Burst Test
    // =========================================================================
    std::cout << "\n╔════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                 DEMO 3: HIGH-FREQUENCY BURST TEST                  ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════════╝\n";

    // Create a fresh pool for burst test - heap allocated to avoid stack overflow
    auto burst_pool = std::make_unique<memory::ObjectPool<core::Order, 100000>>();

    std::cout << "\nMeasuring single-threaded order processing speed...\n";

    auto burst_start = std::chrono::high_resolution_clock::now();

    constexpr size_t BURST_ORDERS = 100000;
    for (size_t i = 0; i < BURST_ORDERS; ++i)
    {
        auto *order = burst_pool->acquire();
        if (order)
        {
            *order = core::Order(
                10000 + i,
                i * 100,
                10000 + (i % 100) - 50,
                10 + (i % 100),
                (i % 2 == 0) ? core::Side::BUY : core::Side::SELL,
                core::OrderType::LIMIT);

            engine.process_order(order);
            burst_pool->release(order);
        }
    }

    auto burst_end = std::chrono::high_resolution_clock::now();
    double burst_seconds = std::chrono::duration<double>(burst_end - burst_start).count();
    double burst_throughput = BURST_ORDERS / burst_seconds;

    std::cout << "\n  Orders: " << BURST_ORDERS << "\n";
    std::cout << "  Time: " << std::fixed << std::setprecision(4) << burst_seconds << " seconds\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << burst_throughput << " orders/second\n";

    if (burst_throughput > 1000000)
    {
        std::cout << "\n  ✓ TARGET ACHIEVED: >" << std::fixed << std::setprecision(2)
                  << (burst_throughput / 1000000.0) << "M orders/second!\n";
    }
    else
    {
        std::cout << "\n  ⚠ Target: 1M orders/second. Achieved: "
                  << std::fixed << std::setprecision(0) << burst_throughput << "\n";
    }

    // =========================================================================
    // Final Summary
    // =========================================================================
    std::cout << "\n╔════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                      IMPLEMENTATION COMPLETE                       ║\n";
    std::cout << "╠══════════════════════════════════════════════════════════════════════╣\n";
    std::cout << "║  ✓ Week 1-2: Matching Engine with Price-Time Priority              ║\n";
    std::cout << "║  ✓ Week 3-4: Object Pool for Zero-Allocation Hot Path              ║\n";
    std::cout << "║  ✓ Week 5:   Lock-Free Ring Buffer & Multi-Threading               ║\n";
    std::cout << "║  ✓ Week 6:   Market Data Publisher & Visualization                 ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════════╝\n";

    std::cout << "\nRun './build/lob_benchmark' for detailed performance benchmarks.\n";
    std::cout << "See ROADMAP.md for complete implementation details.\n\n";

    return 0;
}
