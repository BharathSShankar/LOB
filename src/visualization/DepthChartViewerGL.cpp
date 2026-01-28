/**
 * @file DepthChartViewerGL.cpp
 * @brief OpenGL/ImGui Depth Chart Viewer Implementation
 *
 * This file provides a full OpenGL/ImGui implementation of the depth chart viewer.
 * It requires GLFW and Dear ImGui to be linked.
 *
 * Build Requirements:
 * - OpenGL 3.3+
 * - GLFW3
 * - Dear ImGui (included via FetchContent or submodule)
 *
 * The viewer displays:
 * - Real-time bid/ask depth walls
 * - Cumulative quantity visualization
 * - Spread and mid-price indicators
 * - Latency statistics
 */

// Note: When building with OpenGL support, define ENABLE_OPENGL_VIEWER
// and ensure GLFW and ImGui are linked.

#ifdef ENABLE_OPENGL_VIEWER

#include "visualization/DepthChartViewer.h"

// OpenGL and GLFW headers
#include <GLFW/glfw3.h>

// Dear ImGui headers
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <cmath>
#include <algorithm>
#include <iostream>

namespace lob::visualization
{

    /**
     * @brief OpenGL/ImGui Depth Chart Viewer Implementation
     */
    class DepthChartViewerGL : public IDepthChartViewer
    {
    public:
        explicit DepthChartViewerGL(const DepthChartConfig &config = {})
            : config_(config), window_(nullptr), running_(false) {}

        ~DepthChartViewerGL() override
        {
            shutdown();
        }

        bool initialize() override
        {
            // Initialize GLFW
            if (!glfwInit())
            {
                return false;
            }

            // GL 3.3 + GLSL 330
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

            // Create window
            window_ = glfwCreateWindow(
                config_.window_width,
                config_.window_height,
                config_.title.c_str(),
                nullptr, nullptr);

            if (!window_)
            {
                glfwTerminate();
                return false;
            }

            glfwMakeContextCurrent(window_);
            glfwSwapInterval(1); // Enable vsync

            // Initialize Dear ImGui
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO &io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

            // Setup Platform/Renderer backends
            ImGui_ImplGlfw_InitForOpenGL(window_, true);
            ImGui_ImplOpenGL3_Init("#version 330");

            // Setup style
            ImGui::StyleColorsDark();
            ImGuiStyle &style = ImGui::GetStyle();
            style.WindowRounding = 5.0f;
            style.FrameRounding = 3.0f;

            running_ = true;
            return true;
        }

        void shutdown() override
        {
            running_ = false;

            if (window_)
            {
                ImGui_ImplOpenGL3_Shutdown();
                ImGui_ImplGlfw_Shutdown();
                ImGui::DestroyContext();
                glfwDestroyWindow(window_);
                glfwTerminate();
                window_ = nullptr;
            }
        }

        bool is_running() const override
        {
            return running_ && window_ && !glfwWindowShouldClose(window_);
        }

        void update(const DepthChartSnapshot &snapshot) override
        {
            std::lock_guard<std::mutex> lock(mutex_);

            // Animate if configured
            if (config_.animate_changes)
            {
                animate_to_snapshot(snapshot);
            }
            else
            {
                current_snapshot_ = snapshot;
            }

            stats_.updates_received++;
            stats_.last_update_timestamp = snapshot.timestamp;
        }

        bool render_frame() override
        {
            if (!window_)
                return false;

            // Poll events FIRST to keep window responsive on macOS
            glfwPollEvents();

            // Now check if window should close
            if (!running_ || glfwWindowShouldClose(window_))
                return false;

            auto frame_start = std::chrono::high_resolution_clock::now();

            // Start ImGui frame
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            // Clear background
            glClearColor(config_.bg_color[0], config_.bg_color[1],
                         config_.bg_color[2], config_.bg_color[3]);
            glClear(GL_COLOR_BUFFER_BIT);

            // Render depth chart
            render_depth_chart();

            // Render stats panel
            render_stats_panel();

            // Render order book table
            render_order_book_table();

            // Render ImGui
            ImGui::Render();
            int display_w, display_h;
            glfwGetFramebufferSize(window_, &display_w, &display_h);
            glViewport(0, 0, display_w, display_h);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            glfwSwapBuffers(window_);

            auto frame_end = std::chrono::high_resolution_clock::now();
            auto frame_time = std::chrono::duration_cast<std::chrono::microseconds>(
                                  frame_end - frame_start)
                                  .count();

            stats_.frames_rendered++;
            stats_.avg_frame_time_ms = stats_.avg_frame_time_ms * 0.95 +
                                       (frame_time / 1000.0) * 0.05;

            return true;
        }

        ViewerStats get_stats() const override
        {
            return stats_;
        }

    private:
        void animate_to_snapshot(const DepthChartSnapshot &target)
        {
            // Lerp current values to target for smooth animation
            float t = config_.animation_speed;

            // Animate bids
            animated_bids_.resize(target.bids.size());
            for (size_t i = 0; i < target.bids.size(); ++i)
            {
                if (i < animated_bids_.size())
                {
                    animated_bids_[i].price = lerp(animated_bids_[i].price,
                                                   target.bids[i].price, t);
                    animated_bids_[i].quantity = lerp(animated_bids_[i].quantity,
                                                      target.bids[i].quantity, t);
                    animated_bids_[i].cumulative_quantity =
                        lerp(animated_bids_[i].cumulative_quantity,
                             target.bids[i].cumulative_quantity, t);
                }
                else
                {
                    animated_bids_[i] = target.bids[i];
                }
            }

            // Animate asks
            animated_asks_.resize(target.asks.size());
            for (size_t i = 0; i < target.asks.size(); ++i)
            {
                if (i < animated_asks_.size())
                {
                    animated_asks_[i].price = lerp(animated_asks_[i].price,
                                                   target.asks[i].price, t);
                    animated_asks_[i].quantity = lerp(animated_asks_[i].quantity,
                                                      target.asks[i].quantity, t);
                    animated_asks_[i].cumulative_quantity =
                        lerp(animated_asks_[i].cumulative_quantity,
                             target.asks[i].cumulative_quantity, t);
                }
                else
                {
                    animated_asks_[i] = target.asks[i];
                }
            }

            current_snapshot_ = target;
            current_snapshot_.bids = animated_bids_;
            current_snapshot_.asks = animated_asks_;
        }

        double lerp(double a, double b, float t)
        {
            return a + (b - a) * t;
        }

        void render_depth_chart()
        {
            std::lock_guard<std::mutex> lock(mutex_);

            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(780, 400), ImGuiCond_FirstUseEver);

            ImGui::Begin("Depth Chart", nullptr, ImGuiWindowFlags_NoCollapse);

            // Get available region
            ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
            ImVec2 canvas_size = ImGui::GetContentRegionAvail();

            if (canvas_size.x < 100 || canvas_size.y < 100)
            {
                ImGui::End();
                return;
            }

            ImDrawList *draw_list = ImGui::GetWindowDrawList();

            // Calculate price range
            double min_price = current_snapshot_.mid_price - 100;
            double max_price = current_snapshot_.mid_price + 100;

            if (!current_snapshot_.bids.empty())
            {
                min_price = current_snapshot_.bids.back().price;
            }
            if (!current_snapshot_.asks.empty())
            {
                max_price = current_snapshot_.asks.back().price;
            }

            double price_range = max_price - min_price;
            if (price_range <= 0)
                price_range = 1.0;

            // Find max cumulative quantity
            double max_cum_qty = 1.0;
            for (const auto &bid : current_snapshot_.bids)
            {
                max_cum_qty = std::max(max_cum_qty, bid.cumulative_quantity);
            }
            for (const auto &ask : current_snapshot_.asks)
            {
                max_cum_qty = std::max(max_cum_qty, ask.cumulative_quantity);
            }

            // Draw bid wall (left side, green)
            auto bid_color = ImColor(config_.bid_color[0], config_.bid_color[1],
                                     config_.bid_color[2], config_.bid_color[3]);

            float mid_x = canvas_pos.x + canvas_size.x / 2;

            for (size_t i = 0; i < current_snapshot_.bids.size(); ++i)
            {
                const auto &bid = current_snapshot_.bids[i];
                float y = canvas_pos.y + canvas_size.y *
                                             (1.0f - static_cast<float>((bid.price - min_price) / price_range));
                float width = static_cast<float>(bid.cumulative_quantity / max_cum_qty) *
                              (canvas_size.x / 2 - 10);

                draw_list->AddRectFilled(
                    ImVec2(mid_x - width, y - 2),
                    ImVec2(mid_x, y + 2),
                    bid_color);
            }

            // Draw ask wall (right side, red)
            auto ask_color = ImColor(config_.ask_color[0], config_.ask_color[1],
                                     config_.ask_color[2], config_.ask_color[3]);

            for (size_t i = 0; i < current_snapshot_.asks.size(); ++i)
            {
                const auto &ask = current_snapshot_.asks[i];
                float y = canvas_pos.y + canvas_size.y *
                                             (1.0f - static_cast<float>((ask.price - min_price) / price_range));
                float width = static_cast<float>(ask.cumulative_quantity / max_cum_qty) *
                              (canvas_size.x / 2 - 10);

                draw_list->AddRectFilled(
                    ImVec2(mid_x, y - 2),
                    ImVec2(mid_x + width, y + 2),
                    ask_color);
            }

            // Draw mid-price line
            if (current_snapshot_.mid_price > 0)
            {
                float mid_y = canvas_pos.y + canvas_size.y *
                                                 (1.0f - static_cast<float>((current_snapshot_.mid_price - min_price) / price_range));
                draw_list->AddLine(
                    ImVec2(canvas_pos.x, mid_y),
                    ImVec2(canvas_pos.x + canvas_size.x, mid_y),
                    ImColor(1.0f, 1.0f, 1.0f, 0.5f), 2.0f);

                // Price label
                char label[64];
                snprintf(label, sizeof(label), "Mid: %.2f", current_snapshot_.mid_price);
                draw_list->AddText(ImVec2(mid_x - 30, mid_y - 20),
                                   ImColor(1.0f, 1.0f, 1.0f), label);
            }

            // Draw spread indicator
            char spread_label[128];
            snprintf(spread_label, sizeof(spread_label),
                     "Spread: %.4f | Best Bid: %.2f | Best Ask: %.2f",
                     current_snapshot_.spread,
                     current_snapshot_.best_bid,
                     current_snapshot_.best_ask);
            draw_list->AddText(ImVec2(canvas_pos.x + 10, canvas_pos.y + 10),
                               ImColor(0.8f, 0.8f, 0.8f), spread_label);

            ImGui::End();
        }

        void render_stats_panel()
        {
            ImGui::SetNextWindowPos(ImVec2(800, 10), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(380, 200), ImGuiCond_FirstUseEver);

            ImGui::Begin("Statistics", nullptr);

            ImGui::Text("Performance Metrics");
            ImGui::Separator();

            ImGui::Text("Frames Rendered: %llu", stats_.frames_rendered);
            ImGui::Text("Updates Received: %llu", stats_.updates_received);
            ImGui::Text("Avg Frame Time: %.2f ms", stats_.avg_frame_time_ms);
            ImGui::Text("FPS: %.1f", 1000.0 / std::max(0.001, stats_.avg_frame_time_ms));

            ImGui::Separator();
            ImGui::Text("Order Book");
            ImGui::Text("Bid Levels: %zu", current_snapshot_.bids.size());
            ImGui::Text("Ask Levels: %zu", current_snapshot_.asks.size());

            ImGui::End();
        }

        void render_order_book_table()
        {
            ImGui::SetNextWindowPos(ImVec2(800, 220), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(380, 370), ImGuiCond_FirstUseEver);

            ImGui::Begin("Order Book", nullptr);

            if (ImGui::BeginTable("OrderBookTable", 4,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY))
            {
                ImGui::TableSetupColumn("Bid Qty");
                ImGui::TableSetupColumn("Bid Price");
                ImGui::TableSetupColumn("Ask Price");
                ImGui::TableSetupColumn("Ask Qty");
                ImGui::TableHeadersRow();

                size_t max_rows = std::max(current_snapshot_.bids.size(),
                                           current_snapshot_.asks.size());
                max_rows = std::min(max_rows, static_cast<size_t>(20));

                for (size_t i = 0; i < max_rows; ++i)
                {
                    ImGui::TableNextRow();

                    // Bid quantity
                    ImGui::TableNextColumn();
                    if (i < current_snapshot_.bids.size())
                    {
                        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.3f, 1.0f),
                                           "%.4f", current_snapshot_.bids[i].quantity);
                    }

                    // Bid price
                    ImGui::TableNextColumn();
                    if (i < current_snapshot_.bids.size())
                    {
                        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.3f, 1.0f),
                                           "%.2f", current_snapshot_.bids[i].price);
                    }

                    // Ask price
                    ImGui::TableNextColumn();
                    if (i < current_snapshot_.asks.size())
                    {
                        ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f),
                                           "%.2f", current_snapshot_.asks[i].price);
                    }

                    // Ask quantity
                    ImGui::TableNextColumn();
                    if (i < current_snapshot_.asks.size())
                    {
                        ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f),
                                           "%.4f", current_snapshot_.asks[i].quantity);
                    }
                }

                ImGui::EndTable();
            }

            ImGui::End();
        }

        DepthChartConfig config_;
        GLFWwindow *window_;
        std::atomic<bool> running_;
        mutable std::mutex mutex_;

        DepthChartSnapshot current_snapshot_;
        std::vector<DepthPoint> animated_bids_;
        std::vector<DepthPoint> animated_asks_;

        ViewerStats stats_;
    };

    // Factory function
    std::unique_ptr<IDepthChartViewer> create_opengl_viewer(
        const DepthChartConfig &config)
    {
        return std::make_unique<DepthChartViewerGL>(config);
    }

} // namespace lob::visualization

#endif // ENABLE_OPENGL_VIEWER

// Stub implementation when OpenGL is not available
#ifndef ENABLE_OPENGL_VIEWER

#include "visualization/DepthChartViewer.h"

namespace lob::visualization
{
    // When OpenGL is not enabled, provide a factory that returns the terminal viewer
    std::unique_ptr<IDepthChartViewer> create_opengl_viewer(
        const DepthChartConfig &config)
    {
        // Fallback to terminal viewer
        return std::make_unique<TerminalDepthViewer>(config);
    }
} // namespace lob::visualization

#endif // !ENABLE_OPENGL_VIEWER
