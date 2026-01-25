#pragma once

/**
 * @file DepthChartViewer.h
 * @brief OpenGL/ImGui Depth Chart Visualization
 *
 * This header defines the interface for the depth chart viewer.
 * The actual implementation requires linking with OpenGL, GLFW, and ImGui.
 *
 * Dependencies:
 * - GLFW3: Window management and OpenGL context
 * - Dear ImGui: Immediate mode GUI
 * - OpenGL 3.3+: Rendering
 *
 * Installation (macOS):
 *   brew install glfw
 *   # ImGui is typically included as a submodule
 *
 * Installation (Ubuntu):
 *   sudo apt-get install libglfw3-dev
 */

#include "core/OrderBook.h"
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <memory>

namespace lob::visualization
{

    /**
     * @brief Depth Chart Data Point
     */
    struct DepthPoint
    {
        double price = 0.0;
        double quantity = 0.0;
        double cumulative_quantity = 0.0;
    };

    /**
     * @brief Depth Chart Configuration
     */
    struct DepthChartConfig
    {
        int window_width = 1200;
        int window_height = 800;
        std::string title = "LOB Depth Chart - Real-time";

        // Chart settings
        size_t max_levels = 50;        // Max price levels to display
        double price_precision = 2;    // Decimal places for price
        double quantity_precision = 4; // Decimal places for quantity

        // Colors (RGBA)
        float bid_color[4] = {0.2f, 0.8f, 0.3f, 0.7f}; // Green
        float ask_color[4] = {0.9f, 0.2f, 0.2f, 0.7f}; // Red
        float bg_color[4] = {0.1f, 0.1f, 0.12f, 1.0f}; // Dark gray

        // Animation
        float animation_speed = 0.15f;
        bool animate_changes = true;

        // Update rate
        int target_fps = 60;
        double update_interval_ms = 16.66; // ~60 FPS
    };

    /**
     * @brief Depth Chart Snapshot
     *
     * Thread-safe snapshot of order book depth for rendering.
     */
    struct DepthChartSnapshot
    {
        std::vector<DepthPoint> bids;
        std::vector<DepthPoint> asks;
        double best_bid = 0.0;
        double best_ask = 0.0;
        double spread = 0.0;
        double mid_price = 0.0;
        uint64_t timestamp = 0;
        uint64_t sequence = 0;
    };

    /**
     * @brief Statistics for the depth chart viewer
     */
    struct ViewerStats
    {
        uint64_t frames_rendered = 0;
        uint64_t updates_received = 0;
        double avg_frame_time_ms = 0.0;
        double avg_update_latency_us = 0.0;
        uint64_t last_update_timestamp = 0;
    };

    /**
     * @brief Abstract Depth Chart Viewer Interface
     *
     * This can be implemented using different rendering backends:
     * - OpenGL/ImGui (provided in DepthChartViewerGL.cpp)
     * - Terminal ASCII (fallback)
     * - Web (via WebSocket + HTML5 Canvas)
     */
    class IDepthChartViewer
    {
    public:
        virtual ~IDepthChartViewer() = default;

        /**
         * @brief Initialize the viewer
         * @return true if initialization successful
         */
        virtual bool initialize() = 0;

        /**
         * @brief Shutdown the viewer
         */
        virtual void shutdown() = 0;

        /**
         * @brief Check if viewer is running
         */
        virtual bool is_running() const = 0;

        /**
         * @brief Update the depth chart with new data
         * @param snapshot Current order book snapshot
         */
        virtual void update(const DepthChartSnapshot &snapshot) = 0;

        /**
         * @brief Render one frame (call each frame)
         * @return true if window should continue running
         */
        virtual bool render_frame() = 0;

        /**
         * @brief Get current statistics
         */
        virtual ViewerStats get_stats() const = 0;
    };

    /**
     * @brief Convert OrderBook to DepthChartSnapshot
     */
    inline DepthChartSnapshot create_snapshot(const core::OrderBook &book,
                                              size_t levels = 50,
                                              uint64_t price_divisor = 100)
    {
        DepthChartSnapshot snapshot;

        std::vector<core::OrderBook::DepthLevel> bids_raw, asks_raw;
        book.get_market_depth(bids_raw, asks_raw, levels);

        // Convert bids
        double cumulative = 0.0;
        for (const auto &level : bids_raw)
        {
            DepthPoint point;
            point.price = static_cast<double>(level.price) / price_divisor;
            point.quantity = static_cast<double>(level.quantity) / 100000000.0; // satoshis to BTC
            cumulative += point.quantity;
            point.cumulative_quantity = cumulative;
            snapshot.bids.push_back(point);
        }

        // Convert asks
        cumulative = 0.0;
        for (const auto &level : asks_raw)
        {
            DepthPoint point;
            point.price = static_cast<double>(level.price) / price_divisor;
            point.quantity = static_cast<double>(level.quantity) / 100000000.0;
            cumulative += point.quantity;
            point.cumulative_quantity = cumulative;
            snapshot.asks.push_back(point);
        }

        // Calculate best prices
        auto best_bid = book.get_best_bid();
        auto best_ask = book.get_best_ask();

        if (best_bid.has_value())
        {
            snapshot.best_bid = static_cast<double>(*best_bid) / price_divisor;
        }
        if (best_ask.has_value())
        {
            snapshot.best_ask = static_cast<double>(*best_ask) / price_divisor;
        }

        if (best_bid.has_value() && best_ask.has_value())
        {
            snapshot.spread = snapshot.best_ask - snapshot.best_bid;
            snapshot.mid_price = (snapshot.best_bid + snapshot.best_ask) / 2.0;
        }

        snapshot.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count();

        return snapshot;
    }

    /**
     * @brief ASCII Terminal Depth Chart Viewer (Fallback)
     *
     * Simple terminal-based depth chart that doesn't require OpenGL.
     * Useful for headless environments or quick debugging.
     */
    class TerminalDepthViewer : public IDepthChartViewer
    {
    public:
        TerminalDepthViewer(const DepthChartConfig &config = {})
            : config_(config), running_(false) {}

        bool initialize() override
        {
            running_ = true;
            return true;
        }

        void shutdown() override
        {
            running_ = false;
        }

        bool is_running() const override
        {
            return running_;
        }

        void update(const DepthChartSnapshot &snapshot) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            current_snapshot_ = snapshot;
            stats_.updates_received++;
        }

        bool render_frame() override
        {
            if (!running_)
                return false;

            std::lock_guard<std::mutex> lock(mutex_);

            // Clear screen (ANSI escape)
            std::cout << "\033[2J\033[H";

            // Draw header
            std::cout << "╔═══════════════════════════════════════════════════════════╗\n";
            std::cout << "║           LOB DEPTH CHART - TERMINAL VIEW                  ║\n";
            std::cout << "╠═══════════════════════════════════════════════════════════╣\n";

            // Draw stats
            std::cout << "║ Mid: " << std::fixed << std::setprecision(2)
                      << current_snapshot_.mid_price
                      << " | Spread: " << current_snapshot_.spread
                      << " | Updates: " << stats_.updates_received << "     ║\n";
            std::cout << "╠═══════════════════════════════════════════════════════════╣\n";

            // Draw bids and asks
            std::cout << "║      BIDS (BUY)             ║         ASKS (SELL)          ║\n";
            std::cout << "╠═════════════════════════════╬══════════════════════════════╣\n";

            size_t max_levels = std::min(config_.max_levels,
                                         std::max(current_snapshot_.bids.size(),
                                                  current_snapshot_.asks.size()));

            double max_qty = 0.0;
            for (const auto &b : current_snapshot_.bids)
                max_qty = std::max(max_qty, b.quantity);
            for (const auto &a : current_snapshot_.asks)
                max_qty = std::max(max_qty, a.quantity);

            for (size_t i = 0; i < max_levels; ++i)
            {
                std::cout << "║ ";

                // Bid side
                if (i < current_snapshot_.bids.size())
                {
                    const auto &bid = current_snapshot_.bids[i];
                    int bar_len = max_qty > 0
                                      ? static_cast<int>(bid.quantity / max_qty * 15)
                                      : 0;
                    std::cout << std::setw(10) << std::fixed << std::setprecision(2)
                              << bid.price << " |";
                    std::cout << std::string(bar_len, '#')
                              << std::string(15 - bar_len, ' ');
                }
                else
                {
                    std::cout << std::string(26, ' ');
                }

                std::cout << " | ";

                // Ask side
                if (i < current_snapshot_.asks.size())
                {
                    const auto &ask = current_snapshot_.asks[i];
                    int bar_len = max_qty > 0
                                      ? static_cast<int>(ask.quantity / max_qty * 15)
                                      : 0;
                    std::cout << std::string(bar_len, '=')
                              << std::string(15 - bar_len, ' ') << "| ";
                    std::cout << std::setw(10) << std::fixed << std::setprecision(2)
                              << ask.price;
                }
                else
                {
                    std::cout << std::string(26, ' ');
                }

                std::cout << " ║\n";
            }

            std::cout << "╚═══════════════════════════════════════════════════════════╝\n";
            std::cout << "\nPress Ctrl+C to exit.\n";

            stats_.frames_rendered++;
            return true;
        }

        ViewerStats get_stats() const override
        {
            return stats_;
        }

    private:
        DepthChartConfig config_;
        DepthChartSnapshot current_snapshot_;
        std::atomic<bool> running_;
        mutable std::mutex mutex_;
        ViewerStats stats_;
    };

    /**
     * @brief Factory function to create OpenGL viewer
     * @param config Viewer configuration
     * @return Unique pointer to the viewer (OpenGL or terminal fallback)
     */
    std::unique_ptr<IDepthChartViewer> create_opengl_viewer(const DepthChartConfig &config);

} // namespace lob::visualization
