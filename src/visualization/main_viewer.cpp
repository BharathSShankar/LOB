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
 *   --live         Connect to live exchange WebSocket
 *   --replay FILE  Replay market data from file
 *   --exchange EX  Exchange: coinbase, binance
 *   --symbol SYM   Trading symbol (default: BTC-USD)
 *   --speed N      Replay speed multiplier (default: 1.0)
 */

#include "visualization/DepthChartViewer.h"
#include "market_data/CryptoDataReplay.h"
#include "market_data/WebSocketClient.h"
#include "core/MatchingEngine.h"
#include "core/OrderBook.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <random>

// Forward declare the live WebSocket factory
#ifdef ENABLE_LIVE_WEBSOCKET
namespace lob::market_data
{
    std::unique_ptr<IWebSocketClient> create_live_websocket_client(const WebSocketConfig &config);
}
#endif

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
  --live            Connect to live exchange WebSocket feed
  --replay FILE     Replay market data from a file
  --exchange EX     Exchange: coinbase, binance (default: coinbase)
  --symbol SYM      Trading symbol (default: BTC-USD for Coinbase, BTCUSDT for Binance)
  --speed N         Replay speed multiplier (default: 1.0, 0 = as fast as possible)
  --levels N        Number of price levels to display (default: 20)
  --help            Show this help message

Examples:
  # Run with simulated random data
  ./lob_depth_viewer

  # Connect to LIVE Coinbase feed
  ./lob_depth_viewer --live --exchange coinbase --symbol BTC-USD

  # Connect to LIVE Binance feed
  ./lob_depth_viewer --live --exchange binance --symbol BTCUSDT

  # Replay from file
  ./lob_depth_viewer --replay btc_l2_data.ndjson --exchange coinbase

Data Sources:
  Coinbase: wss://ws-feed.exchange.coinbase.com (L2 order book)
  Binance:  wss://stream.binance.com:9443/ws/{symbol}@depth@100ms

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
    // Use DEFAULT book since that's where process_order() adds orders
    auto *book = engine.get_order_book("DEFAULT");

    if (!book)
    {
        std::cerr << "Error: Could not get order book 'DEFAULT'" << std::endl;
        return;
    }

    std::cout << "Order book 'DEMO' ready, starting simulation..." << std::endl;

    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<uint64_t> price_dist(9500, 10500);
    std::uniform_int_distribution<uint64_t> qty_dist(100, 10000);
    std::uniform_int_distribution<int> side_dist(0, 1);

    uint64_t order_id = 1;

    // Initial population - render frames during this to keep window responsive
    std::cout << "Populating initial order book..." << std::endl;
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

        // Render every 10 orders during init to keep window responsive
        if (i % 10 == 0)
        {
            auto snapshot = visualization::create_snapshot(*book, levels, 100, 1.0);
            viewer.update(snapshot);
            if (!viewer.render_frame())
            {
                std::cout << "Window closed during initialization" << std::endl;
                return;
            }
        }
    }

    std::cout << "Initial population complete. Running main loop..." << std::endl;
    std::cout << "Press Ctrl+C or close the window to exit." << std::endl;

    // Main rendering loop
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

        // Update viewer with current snapshot (price_divisor=100, quantity_divisor=1.0 for simulation)
        auto snapshot = visualization::create_snapshot(*book, levels, 100, 1.0);
        viewer.update(snapshot);

        // Render frame
        if (!viewer.render_frame())
        {
            break;
        }

        // Target ~60 FPS
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    std::cout << "Simulation loop ended." << std::endl;
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
            // For crypto replay, use satoshi to BTC conversion (100M)
            auto snapshot = visualization::create_snapshot(*book, levels, config.price_multiplier, 100000000.0);
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

/**
 * @brief Stream live market data from exchange WebSocket
 */
void stream_live_data(const std::string &exchange_str,
                      const std::string &symbol,
                      core::MatchingEngine &engine,
                      visualization::IDepthChartViewer &viewer,
                      size_t levels)
{
#ifdef ENABLE_LIVE_WEBSOCKET
    // Configure WebSocket
    market_data::WebSocketConfig ws_config;

    if (exchange_str == "binance")
    {
        ws_config.exchange = market_data::WebSocketConfig::Exchange::BINANCE;
        ws_config.symbol = symbol.empty() ? "BTCUSDT" : symbol;
    }
    else
    {
        ws_config.exchange = market_data::WebSocketConfig::Exchange::COINBASE;
        ws_config.symbol = symbol.empty() ? "BTC-USD" : symbol;
    }

    ws_config.subscribe_l2 = true;
    ws_config.subscribe_trades = true;
    ws_config.price_multiplier = 100;

    std::cout << "Connecting to " << exchange_str << " WebSocket..." << std::endl;
    std::cout << "Symbol: " << ws_config.symbol << std::endl;
    std::cout << "URL: " << ws_config.get_url() << std::endl;

    // Create live WebSocket client
    auto client = market_data::create_live_websocket_client(ws_config);

    auto *book = engine.get_order_book("DEFAULT");
    auto &pool = engine.get_order_pool();
    std::atomic<uint64_t> order_id{1};
    std::atomic<uint64_t> events_received{0};

    // Set up event callback
    client->on_event([&](const market_data::MarketDataEvent &event)
                     {
        events_received.fetch_add(1, std::memory_order_relaxed);
        
        if (event.type == market_data::MarketDataEvent::Type::L2_UPDATE ||
            event.type == market_data::MarketDataEvent::Type::SNAPSHOT)
        {
            auto *order = pool.acquire();
            if (order)
            {
                *order = core::Order(
                    order_id.fetch_add(1, std::memory_order_relaxed),
                    event.timestamp_ns,
                    event.price,
                    event.quantity,
                    event.side,
                    core::OrderType::LIMIT
                );
                
                if (!event.is_delete)
                {
                    engine.process_order(order);
                }
                else
                {
                    pool.release(order);
                }
            }
        } });

    client->on_error([](const std::string &error)
                     { std::cerr << "WebSocket error: " << error << std::endl; });

    // Connect
    if (!client->connect())
    {
        std::cerr << "Failed to connect to " << exchange_str << std::endl;
        return;
    }

    std::cout << "Connected! Streaming live data..." << std::endl;
    std::cout << "Press Ctrl+C to exit." << std::endl;

    // Main rendering loop
    auto last_stats_time = std::chrono::steady_clock::now();

    while (g_running && viewer.is_running() && client->is_connected())
    {
        // Update viewer with current snapshot
        if (book)
        {
            // For crypto, use satoshi conversion
            auto snapshot = visualization::create_snapshot(*book, levels, 100, 100000000.0);
            viewer.update(snapshot);
        }

        // Render frame
        if (!viewer.render_frame())
        {
            break;
        }

        // Print stats every 5 seconds
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_stats_time).count() >= 5)
        {
            auto stats = client->get_stats();
            std::cout << "Events: " << events_received.load()
                      << " | Messages: " << stats.messages_received
                      << " | Bytes: " << stats.bytes_received << std::endl;
            last_stats_time = now;
        }

        // Target ~60 FPS
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    client->disconnect();
    std::cout << "Disconnected from " << exchange_str << std::endl;

#else
    (void)exchange_str;
    (void)symbol;
    (void)engine;
    (void)viewer;
    (void)levels;

    std::cerr << "Live WebSocket support not enabled!" << std::endl;
    std::cerr << "Rebuild with: cmake -DBUILD_VISUALIZATION=ON -DENABLE_LIVE_WEBSOCKET=ON .." << std::endl;
#endif
}

int main(int argc, char *argv[])
{
    // Set up signal handling
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Parse command line arguments
    bool use_terminal = false;
    bool use_live = false;
    std::string replay_file;
    std::string exchange_str = "coinbase";
    std::string symbol; // Empty = use default for exchange
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
        else if (arg == "--live")
        {
            use_live = true;
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

    // Set default symbol based on exchange
    if (symbol.empty())
    {
        symbol = (exchange_str == "binance") ? "BTCUSDT" : "BTC-USD";
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

    // Create order book for replay mode only (simulation uses DEFAULT created by initialize())
    if (!replay_file.empty())
    {
        engine.create_order_book("REPLAY");
    }

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

    if (use_live)
    {
        std::cout << "Mode: LIVE WebSocket" << std::endl;
        std::cout << "Exchange: " << exchange_str << std::endl;
        stream_live_data(exchange_str, symbol, engine, *viewer, levels);
    }
    else if (!replay_file.empty())
    {
        std::cout << "Mode: File Replay" << std::endl;
        std::cout << "Replay file: " << replay_file << std::endl;
        std::cout << "Speed: " << speed << "x" << std::endl;
        replay_data_file(replay_file, exchange, speed, engine, *viewer, levels);
    }
    else
    {
        std::cout << "Mode: Simulation (random data)" << std::endl;
        simulate_orders(engine, *viewer, levels);
    }

    viewer->shutdown();
    std::cout << "Viewer closed." << std::endl;

    return 0;
}
