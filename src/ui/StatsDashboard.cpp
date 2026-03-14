/**
 * @file StatsDashboard.cpp
 * @brief Real-time market statistics and simulation control panel implementation
 */

#include "ui/StatsDashboard.h"

#ifdef ENABLE_OPENGL_VIEWER
#include "imgui.h"
#endif

#include <cstdio>
#include <cmath>
#include <algorithm>
#include <array>

namespace lob::ui
{

    // ────────────────────────────────────────────────────────────────────────────
    StatsDashboard::StatsDashboard() = default;

    // ────────────────────────────────────────────────────────────────────────────
    // update()
    // ────────────────────────────────────────────────────────────────────────────

    void StatsDashboard::update(const agents::MarketState &state,
                                const agents::PopulationStats &pop_stats,
                                const core::MatchingEngine::Statistics &eng_stats,
                                const std::vector<analytics::PatternMatch> &patterns,
                                uint64_t tick_count,
                                int tick_rate_hz)
    {
        market_state_ = state;
        pop_stats_ = pop_stats;
        eng_stats_ = eng_stats;
        patterns_ = patterns;
        tick_count_ = tick_count;
        tick_rate_slider_ = tick_rate_hz;

        if (session_open_ == 0.0 && state.last_price > 0.0)
            session_open_ = state.last_price;

        // Push histories
        auto push = [](std::deque<float> &dq, float v, int max_len)
        {
            dq.push_back(v);
            while (static_cast<int>(dq.size()) > max_len)
                dq.pop_front();
        };

        push(price_hist_, static_cast<float>(state.last_price), HISTORY);
        push(spread_hist_, static_cast<float>(state.spread), HISTORY);
        push(vol_hist_, static_cast<float>(state.volume_24h), HISTORY);
    }

    // ────────────────────────────────────────────────────────────────────────────
    // render()
    // ────────────────────────────────────────────────────────────────────────────

    void StatsDashboard::render()
    {
#ifdef ENABLE_OPENGL_VIEWER
        ImGui::SetNextWindowSize(ImVec2(320, 700), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 330, 10),
                                ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("📊 Market Statistics"))
        {
            ImGui::End();
            return;
        }

        render_sim_controls();
        ImGui::Separator();
        render_price_ticker();
        ImGui::Separator();
        render_sparklines();
        ImGui::Separator();
        render_population();
        ImGui::Separator();
        render_patterns();
        ImGui::Separator();
        render_engine_stats();

        ImGui::End();
#endif
    }

    // ────────────────────────────────────────────────────────────────────────────
    // Simulation Controls
    // ────────────────────────────────────────────────────────────────────────────

    void StatsDashboard::render_sim_controls()
    {
#ifdef ENABLE_OPENGL_VIEWER
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "SIMULATION CONTROL");

        float btn_w = (ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f;

        if (is_paused_)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.5f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
            if (ImGui::Button("▶ Resume", ImVec2(btn_w, 28.0f)))
            {
                is_paused_ = false;
                if (on_resume_)
                    on_resume_();
            }
            ImGui::PopStyleColor(2);
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.4f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.6f, 0.2f, 1.0f));
            if (ImGui::Button("⏸ Pause", ImVec2(btn_w, 28.0f)))
            {
                is_paused_ = true;
                if (on_pause_)
                    on_pause_();
            }
            ImGui::PopStyleColor(2);
        }

        ImGui::SameLine(0, 8.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("⏹ Reset", ImVec2(btn_w, 28.0f)))
        {
            is_paused_ = false;
            session_open_ = 0.0;
            price_hist_.clear();
            spread_hist_.clear();
            vol_hist_.clear();
            if (on_reset_)
                on_reset_();
        }
        ImGui::PopStyleColor(2);

        // Tick rate slider
        if (ImGui::SliderInt("Tick Rate (Hz)", &tick_rate_slider_, 10, 500))
        {
            if (on_tick_rate_)
                on_tick_rate_(tick_rate_slider_);
        }

        // Export CSV button
        if (ImGui::SmallButton("💾 Export OHLCV CSV"))
        {
            if (on_export_csv_)
                on_export_csv_();
        }

        char tick_buf[64];
        std::snprintf(tick_buf, sizeof(tick_buf),
                      "Tick: %llu  (%.1f s)",
                      static_cast<unsigned long long>(tick_count_),
                      static_cast<double>(tick_count_) / std::max(tick_rate_slider_, 1));
        ImGui::TextDisabled("%s", tick_buf);
#endif
    }

    // ────────────────────────────────────────────────────────────────────────────
    // Price Ticker
    // ────────────────────────────────────────────────────────────────────────────

    void StatsDashboard::render_price_ticker()
    {
#ifdef ENABLE_OPENGL_VIEWER
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "PRICE");

        const double last = market_state_.last_price;
        const double bid = market_state_.best_bid;
        const double ask = market_state_.best_ask;
        const double spread = market_state_.spread;
        const double chg_pct = (session_open_ > 0.0)
                                   ? (last - session_open_) / session_open_ * 100.0
                                   : 0.0;

        // Large price display
        ImVec4 chg_col = (chg_pct >= 0.0)
                             ? ImVec4(0.2f, 0.9f, 0.3f, 1.0f)
                             : ImVec4(0.9f, 0.2f, 0.2f, 1.0f);

        ImGui::SetWindowFontScale(1.4f);
        ImGui::TextColored(chg_col, "%.4f", last);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SameLine();
        char chg_buf[32];
        std::snprintf(chg_buf, sizeof(chg_buf), "%+.2f%%", chg_pct);
        ImGui::TextColored(chg_col, "%s", chg_buf);

        ImGui::Columns(2, "price_cols", false);
        ImGui::Text("Bid:");
        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "%.4f", bid);
        ImGui::NextColumn();
        ImGui::Text("Ask:");
        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "%.4f", ask);
        ImGui::NextColumn();
        ImGui::Text("Spread:");
        ImGui::NextColumn();
        ImGui::Text("%.4f (%.3f%%)", spread,
                    last > 0 ? spread / last * 100.0 : 0.0);
        ImGui::NextColumn();
        ImGui::Text("SMA-50:");
        ImGui::NextColumn();
        ImGui::Text("%.4f", market_state_.price_sma_50);
        ImGui::NextColumn();
        ImGui::Text("SMA-200:");
        ImGui::NextColumn();
        ImGui::Text("%.4f", market_state_.price_sma_200);
        ImGui::NextColumn();
        ImGui::Text("Volatility:");
        ImGui::NextColumn();
        ImGui::Text("%.4f", market_state_.volatility);
        ImGui::NextColumn();
        ImGui::Columns(1);
#endif
    }

    // ────────────────────────────────────────────────────────────────────────────
    // Sparklines
    // ────────────────────────────────────────────────────────────────────────────

    void StatsDashboard::render_sparklines()
    {
#ifdef ENABLE_OPENGL_VIEWER
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "HISTORY");

        // Convert deque → raw float array for ImGui::PlotLines
        auto to_vec = [](const std::deque<float> &dq) -> std::vector<float>
        {
            return std::vector<float>(dq.begin(), dq.end());
        };

        if (!price_hist_.empty())
        {
            auto pv = to_vec(price_hist_);
            float lo = *std::min_element(pv.begin(), pv.end());
            float hi = *std::max_element(pv.begin(), pv.end());
            char ov[32];
            std::snprintf(ov, sizeof(ov), "%.4f", pv.back());
            ImGui::Text("Price");
            ImGui::PlotLines("##ph", pv.data(), static_cast<int>(pv.size()),
                             0, ov, lo * 0.995f, hi * 1.005f,
                             ImVec2(-1, 60));
        }

        if (!spread_hist_.empty())
        {
            auto sv = to_vec(spread_hist_);
            ImGui::Text("Spread");
            ImGui::PlotLines("##sh", sv.data(), static_cast<int>(sv.size()),
                             0, nullptr, 0, FLT_MAX,
                             ImVec2(-1, 40));
        }

        if (!vol_hist_.empty())
        {
            auto vv = to_vec(vol_hist_);
            ImGui::Text("Volume");
            ImGui::PlotHistogram("##vh", vv.data(), static_cast<int>(vv.size()),
                                 0, nullptr, 0, FLT_MAX,
                                 ImVec2(-1, 40));
        }
#endif
    }

    // ────────────────────────────────────────────────────────────────────────────
    // Population breakdown
    // ────────────────────────────────────────────────────────────────────────────

    const char *StatsDashboard::agent_type_label(agents::AgentType t)
    {
        switch (t)
        {
        case agents::AgentType::MARKET_MAKER:
            return "Market Maker";
        case agents::AgentType::TREND_FOLLOWER:
            return "Trend Follower";
        case agents::AgentType::MEAN_REVERTER:
            return "Mean Reverter";
        case agents::AgentType::NOISE_TRADER:
            return "Noise Trader";
        case agents::AgentType::WHALE:
            return "Whale";
        default:
            return "Unknown";
        }
    }

    void StatsDashboard::render_population()
    {
#ifdef ENABLE_OPENGL_VIEWER
        uint32_t total = pop_stats_.total_active;
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f),
                           "AGENTS  (%u active)", total);

        // Colour per type
        static const std::pair<agents::AgentType, ImVec4> entries[] = {
            {agents::AgentType::MARKET_MAKER, {0.2f, 0.8f, 0.8f, 1.0f}},
            {agents::AgentType::TREND_FOLLOWER, {0.9f, 0.6f, 0.1f, 1.0f}},
            {agents::AgentType::MEAN_REVERTER, {0.3f, 0.8f, 0.3f, 1.0f}},
            {agents::AgentType::NOISE_TRADER, {0.6f, 0.6f, 0.6f, 1.0f}},
            {agents::AgentType::WHALE, {0.9f, 0.2f, 0.2f, 1.0f}},
        };

        float avail_w = ImGui::GetContentRegionAvail().x;

        for (const auto &[type, col] : entries)
        {
            auto it = pop_stats_.counts_by_type.find(type);
            uint32_t cnt = (it != pop_stats_.counts_by_type.end()) ? it->second : 0;
            float frac = (total > 0) ? static_cast<float>(cnt) / total : 0.0f;

            ImGui::TextColored(col, "%-16s", agent_type_label(type));
            ImGui::SameLine(avail_w * 0.52f);
            ImGui::Text("%u", cnt);
            ImGui::SameLine(avail_w * 0.68f);

            char lbl[16];
            std::snprintf(lbl, sizeof(lbl), "%.1f%%", frac * 100.0f);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
            ImGui::ProgressBar(frac, ImVec2(-1, 10), "");
            ImGui::PopStyleColor();
        }
#endif
    }

    // ────────────────────────────────────────────────────────────────────────────
    // Detected patterns
    // ────────────────────────────────────────────────────────────────────────────

    void StatsDashboard::render_patterns()
    {
#ifdef ENABLE_OPENGL_VIEWER
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f),
                           "PATTERNS  (%d detected)", static_cast<int>(patterns_.size()));

        if (patterns_.empty())
        {
            ImGui::TextDisabled("  No patterns detected yet");
            return;
        }

        // Show at most 8 most-recent patterns
        int show = std::min(static_cast<int>(patterns_.size()), 8);
        int start = static_cast<int>(patterns_.size()) - show;

        for (int i = start; i < static_cast<int>(patterns_.size()); ++i)
        {
            const auto &pm = patterns_[i];
            ImVec4 col = pm.is_bullish
                             ? ImVec4(0.2f, 0.9f, 0.3f, 1.0f)
                             : ImVec4(0.9f, 0.2f, 0.2f, 1.0f);

            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s  (%.0f%%)",
                          analytics::to_string(pm.pattern).c_str(),
                          pm.confidence * 100.0);
            ImGui::BulletText("");
            ImGui::SameLine();
            ImGui::TextColored(col, "%s", buf);

            if (ImGui::IsItemHovered() && !pm.description.empty())
                ImGui::SetTooltip("%s\n[%u – %u]",
                                  pm.description.c_str(),
                                  pm.start_index, pm.end_index);
        }
#endif
    }

    // ────────────────────────────────────────────────────────────────────────────
    // Engine statistics
    // ────────────────────────────────────────────────────────────────────────────

    void StatsDashboard::render_engine_stats()
    {
#ifdef ENABLE_OPENGL_VIEWER
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "ENGINE STATS");

        ImGui::Columns(2, "eng_cols", false);
        ImGui::Text("Orders:");
        ImGui::NextColumn();
        ImGui::Text("%llu", static_cast<unsigned long long>(eng_stats_.total_orders_processed));
        ImGui::NextColumn();
        ImGui::Text("Trades:");
        ImGui::NextColumn();
        ImGui::Text("%llu", static_cast<unsigned long long>(eng_stats_.total_trades_executed));
        ImGui::NextColumn();
        ImGui::Text("Volume:");
        ImGui::NextColumn();
        ImGui::Text("%llu", static_cast<unsigned long long>(eng_stats_.total_volume));
        ImGui::NextColumn();
        ImGui::Text("Rejected:");
        ImGui::NextColumn();
        ImGui::Text("%llu", static_cast<unsigned long long>(eng_stats_.total_orders_rejected));
        ImGui::NextColumn();
        ImGui::Columns(1);

        // Fill rate
        uint64_t total_ord = eng_stats_.total_orders_processed;
        uint64_t filled = eng_stats_.total_trades_executed;
        float fill_rate = (total_ord > 0)
                              ? static_cast<float>(filled) / total_ord
                              : 0.0f;
        ImGui::Text("Fill Rate:");
        ImGui::SameLine();
        char fr_lbl[16];
        std::snprintf(fr_lbl, sizeof(fr_lbl), "%.1f%%", fill_rate * 100.0f);
        ImGui::ProgressBar(fill_rate, ImVec2(-1, 10), fr_lbl);
#endif
    }

} // namespace lob::ui
