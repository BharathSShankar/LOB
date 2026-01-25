/**
 * @file main_viewer.cpp
 * @brief Main entry point for the Depth Chart Viewer
 *
 * This application displays a real-time depth chart visualization
 * of the limit order book, showing bid/ask walls updating in real-time.
 *
 * Usage:
 *   ./lob_depth_viewer [options]
 *
 * Options:
 *   --terminal     Use terminal-based viewer (no OpenGL required)
 *   --replay FILE  Replay market data from file
 *   --exchange EX  Exchange format (coinbase, binance, csv)
 *   --symbol SYM   Trading symbol (default: BTC-USD)
 *   --speed N      Replay speed multiplier (default: 1.0)
 */

#include "visualization/DepthChartViewer.h"
#include "market_data/CryptoDataReplay.h"
#include "core/MatchingEngine.h"
#include "core/OrderBook.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <random>

using namespace lob;

// Global flag for graceful shutdown
std::atomic<bool> g_running{true};

void signal_handler(int)
{
    g_running = false;
}

void print_usage()
{
    std::cout << R"(
LOB Depth Chart Viewer
======================

Usage: lob_depth_viewer [options]

Options:
  --terminal        Use terminal-based viewer (ASCII, no OpenGL required)
  --replay FILE     Replay market data from a file
  --exchange EX     Exchange data format: coinbase, binance, csv
  --symbol SYM      Trading symbol (default: BTC-USD)
  --speed N         Replay speed multiplier (default: 1.0, 0 = as fast as possible)
  --levels N        Number of price levels to display (default: 20)
  --help            Show this help message

Examples:
  # Run with simulated data (terminal mode)
  ./lob_depth_viewer --terminal

  # Replay Coinbase L2 data file
  ./lob_depth_viewer --replay btc_l2_data.ndjson --exchange coinbase

  # Replay at 10x speed
  ./lob_depth_viewer --replay data.csv --exchange csv --speed 10

Data Sources:
  Coinbase: wss://ws-feed.exchange.coinbase.com
  Binance:  wss://stream.binance.com:9443/ws/btcusdt@depth
  Tardis:   https://tardis.dev (historical L2/L3 data)

)";
}

/**
 * @brief Simulate order book activity for demo purposes
 */
void simulate_orders(core::MatchingEngine &engine,
                     visualization::IDepthChartViewer &viewer,
                     size_t levels)
{
    auto &pool = engine.get_order_pool();
    auto *book = engine.get_order_book("DEMO");

    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<uint64_t> price_dist(9500, 10500);
    std::uniform_int_distribution<uint64_t> qty_dist(100, 10000);
    std::uniform_int_distribution<int> side_dist(0, 1);

    uint64_t order_id = 1;

    // Initial population
    for (int i = 0; i < 100; ++i)
    {
        auto *order = pool.acquire();
        if (order)
        {
            core::Side side = (i < 50) ? core::Side::BUY : core::Side::SELL;
            uint64_t base_price = (side == core::Side::BUY) ? 9800 : 10200;
            uint64_t price = base_price + (i % 50) * 5;

            *order = core::Order(order_id++, 0, price, qty_dist(rng),
                                 side, core::OrderType::LIMIT);
            engine.process_order(order);
        }
    }

    while (g_running && viewer.is_running())
    {
        // Generate random orders
        for (int i = 0; i < 5; ++i)
        {
            auto *order = pool.acquire();
            if (order)
            {
                core::Side side = side_dist(rng) == 0 ? core::Side::BUY : core::Side::SELL;
                uint64_t price = price_dist(rng);

                *order = core::Order(order_id++, 0, price, qty_dist(rng),
                                     side, core::OrderType::LIMIT);
                engine.process_order(order);
            }
        }

        // Update viewer with current snapshot
        if (book)
        {
            auto snapshot = visualization::create_snapshot(*book, levels, 1);
            viewer.update(snapshot);
        }

        // Render frame
        viewer.render_frame();

        // Target ~60 FPS
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

/**
 * @brief Replay market data file
 */
void replay_data_file(const std::string &file_path,
                      market_data::DataSourceConfig::Exchange exchange,
                      double speed,
                      core::MatchingEngine &engine,
                      visualization::IDepthChartViewer &viewer,
                      size_t levels)
{
    market_data::DataSourceConfig config;
    config.exchange = exchange;
    config.speed_multiplier = speed;
    config.price_multiplier = 100;

    market_data::CryptoDataReplay replay(config);

    std::cout << "Loading data file: " << file_path << std::endl;
    size_t events = replay.load_file(file_path);
    std::cout << "Loaded " << events << " events" << std::endl;

    if (events == 0)
    {
        std::cerr << "Error: No events loaded from file" << std::endl;
        return;
    }

    auto *book = engine.get_order_book("REPLAY");
    auto &pool = engine.get_order_pool();
    uint64_t order_id = 1;

    // Process each event and update visualization
    for (const auto &event : replay.events())
    {
        if (!g_running || !viewer.is_running())
            break;

        // Convert market data event to order
        auto *order = pool.acquire();
        if (order)
        {
            *order = core::Order(order_id++, event.timestamp_ns,
                                 event.price, event.quantity,
                                 event.side, core::OrderType::LIMIT);

            // For L2 updates, handle deletions
            if (event.is_delete)
            {
                // In a real implementation, we'd track and cancel the order
                pool.release(order);
            }
            else
            {
                engine.process_order(order);
            }
        }

        // Update viewer
        if (book)
        {
            auto snapshot = visualization::create_snapshot(*book, levels, config.price_multiplier);
            viewer.update(snapshot);
        }

        viewer.render_frame();

        // Rate limiting based on speed multiplier
        if (speed > 0)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(
                static_cast<uint64_t>(1000 / speed)));
        }
    }

    std::cout << "Replay complete." << std::endl;
}

int main(int argc, char *argv[])
{
    // Set up signal handling
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Parse command line arguments
    bool use_terminal = false;
    std::string replay_file;
    std::string exchange_str = "coinbase";
    std::string symbol = "BTC-USD";
    double speed = 1.0;
    size_t levels = 20;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h")
        {
            print_usage();
            return 0;
        }
        else if (arg == "--terminal")
        {
            use_terminal = true;
        }
        else if (arg == "--replay" && i + 1 < argc)
        {
            replay_file = argv[++i];
        }
        else if (arg == "--exchange" && i + 1 < argc)
        {
            exchange_str = argv[++i];
        }
        else if (arg == "--symbol" && i + 1 < argc)
        {
            symbol = argv[++i];
        }
        else if (arg == "--speed" && i + 1 < argc)
        {
            speed = std::stod(argv[++i]);
        }
        else if (arg == "--levels" && i + 1 < argc)
        {
            levels = std::stoull(argv[++i]);
        }
    }

    // Parse exchange type
    market_data::DataSourceConfig::Exchange exchange;
    if (exchange_str == "coinbase")
    {
        exchange = market_data::DataSourceConfig::Exchange::COINBASE;
    }
    else if (exchange_str == "binance")
    {
        exchange = market_data::DataSourceConfig::Exchange::BINANCE;
    }
    else if (exchange_str == "csv")
    {
        exchange = market_data::DataSourceConfig::Exchange::GENERIC_CSV;
    }
    else
    {
        exchange = market_data::DataSourceConfig::Exchange::COINBASE;
    }

    // Create matching engine
    core::MatchingEngine engine;
    engine.initialize();

    // Create order book
    std::string book_symbol = replay_file.empty() ? "DEMO" : "REPLAY";
    engine.create_order_book(book_symbol);

    // Create viewer
    visualization::DepthChartConfig config;
    config.max_levels = levels;
    config.title = "LOB Depth Chart - " + symbol;

    std::unique_ptr<visualization::IDepthChartViewer> viewer;

    if (use_terminal)
    {
        viewer = std::make_unique<visualization::TerminalDepthViewer>(config);
    }
    else
    {
        // Try OpenGL viewer, fall back to terminal
#ifdef ENABLE_OPENGL_VIEWER
        viewer = visualization::create_opengl_viewer(config);
#else
        std::cout << "OpenGL viewer not available, using terminal mode" << std::endl;
        viewer = std::make_unique<visualization::TerminalDepthViewer>(config);
#endif
    }

    if (!viewer->initialize())
    {
        std::cerr << "Failed to initialize viewer" << std::endl;
        return 1;
    }

    std::cout << "Starting depth chart viewer..." << std::endl;
    std::cout << "Symbol: " << symbol << std::endl;
    std::cout << "Levels: " << levels << std::endl;

    if (!replay_file.empty())
    {
        std::cout << "Replay file: " << replay_file << std::endl;
        std::cout << "Speed: " << speed << "x" << std::endl;
        replay_data_file(replay_file, exchange, speed, engine, *viewer, levels);
    }
    else
    {
        std::cout << "Running with simulated data..." << std::endl;
        simulate_orders(engine, *viewer, levels);
    }

    viewer->shutdown();
    std::cout << "Viewer closed." << std::endl;

    return 0;
}
