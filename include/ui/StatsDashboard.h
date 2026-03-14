#pragma once

/**
 * @file StatsDashboard.h
 * @brief Real-time market statistics & simulation control panel
 *
 * Provides a live dashboard showing:
 *  - Price ticker (last, bid, ask, spread, % change)
 *  - Price / volume sparklines
 *  - Agent population breakdown (counts + pie-style bars)
 *  - Latest detected patterns with confidence scores
 *  - Simulation controls (play / pause / reset, tick-rate slider)
 *  - Matching engine statistics (orders, trades, volume)
 *
 * Week 10: Day 37-38 implementation
 */

#include "agents/AgentZoo.h"
#include "agents/MarketState.h"
#include "analytics/OHLCVAggregator.h"
#include "analytics/PatternScanner.h"
#include "core/MatchingEngine.h"
#include <deque>
#include <vector>
#include <functional>
#include <string>

namespace lob::ui
{

    /**
     * @brief Live market statistics and simulation control panel
     *
     * Usage:
     * @code
     *   StatsDashboard dash;
     *   dash.set_on_pause([&]() { orchestrator.stop(); });
     *   dash.set_on_resume([&]() { orchestrator.start(); });
     *   dash.set_on_reset([&]() { reset_simulation(); });
     *
     *   // Each frame:
     *   dash.update(market_state, zoo.get_stats(), engine.get_statistics(),
     *               patterns, current_tick);
     *   dash.render();
     * @endcode
     */
    class StatsDashboard
    {
    public:
        StatsDashboard();

        // ── Callbacks ─────────────────────────────────────────────────────────
        void set_on_pause(std::function<void()> cb) { on_pause_ = std::move(cb); }
        void set_on_resume(std::function<void()> cb) { on_resume_ = std::move(cb); }
        void set_on_reset(std::function<void()> cb) { on_reset_ = std::move(cb); }
        void set_on_tick_rate(std::function<void(int)> cb) { on_tick_rate_ = std::move(cb); }
        void set_on_export_csv(std::function<void()> cb) { on_export_csv_ = std::move(cb); }

        // ── State setters ──────────────────────────────────────────────────────
        void set_paused(bool p) { is_paused_ = p; }
        bool is_paused() const { return is_paused_; }

        // ── Data update ────────────────────────────────────────────────────────
        /**
         * @brief Feed latest data to the dashboard (call every simulation tick)
         */
        void update(const agents::MarketState &state,
                    const agents::PopulationStats &pop_stats,
                    const core::MatchingEngine::Statistics &eng_stats,
                    const std::vector<analytics::PatternMatch> &patterns,
                    uint64_t tick_count,
                    int tick_rate_hz);

        // ── Rendering ──────────────────────────────────────────────────────────
        /**
         * @brief Render the dashboard into a free-floating ImGui window
         */
        void render();

    private:
        // Simulation state
        bool is_paused_{false};
        int tick_rate_slider_{100};

        // Snapshot of last update
        agents::MarketState market_state_{};
        agents::PopulationStats pop_stats_{};
        core::MatchingEngine::Statistics eng_stats_{};
        std::vector<analytics::PatternMatch> patterns_;
        uint64_t tick_count_{0};

        // Price % change since session start
        double session_open_{0.0};

        // History for sparklines (float for ImGui PlotLines compatibility)
        static constexpr int HISTORY = 200;
        std::deque<float> price_hist_;
        std::deque<float> spread_hist_;
        std::deque<float> vol_hist_; // recent trade volume samples

        // Callbacks
        std::function<void()> on_pause_;
        std::function<void()> on_resume_;
        std::function<void()> on_reset_;
        std::function<void(int)> on_tick_rate_;
        std::function<void()> on_export_csv_;

        // ── Render helpers ────────────────────────────────────────────────────
        void render_price_ticker();
        void render_sparklines();
        void render_sim_controls();
        void render_population();
        void render_patterns();
        void render_engine_stats();

        // Utility
        static const char *agent_type_label(agents::AgentType t);
    };

} // namespace lob::ui
