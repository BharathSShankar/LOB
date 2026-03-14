/**
 * @file AgentConfigPanel.cpp
 * @brief ImGui configuration panel implementation for agent population
 */

#include "ui/AgentConfigPanel.h"

// ImGui is only available in the visualization build
#ifdef ENABLE_OPENGL_VIEWER
#include "imgui.h"
#endif

#include <cstring>
#include <cstdio>

namespace lob::ui
{

    // ── Helpers ────────────────────────────────────────────────────────────────

    static const char *agent_type_name(int idx)
    {
        static const char *names[] = {
            "Market Maker", "Trend Follower", "Mean Reverter",
            "Noise Trader", "Whale"};
        if (idx < 0 || idx > 4)
            return "Unknown";
        return names[idx];
    }

    static agents::AgentType index_to_type(int idx)
    {
        switch (idx)
        {
        case 0:
            return agents::AgentType::MARKET_MAKER;
        case 1:
            return agents::AgentType::TREND_FOLLOWER;
        case 2:
            return agents::AgentType::MEAN_REVERTER;
        case 3:
            return agents::AgentType::NOISE_TRADER;
        case 4:
            return agents::AgentType::WHALE;
        default:
            return agents::AgentType::NOISE_TRADER;
        }
    }

    // ── Constructor ────────────────────────────────────────────────────────────

    AgentConfigPanel::AgentConfigPanel()
    {
        load_preset(0); // Default: Bull Run
    }

    // ── Preset loading ─────────────────────────────────────────────────────────

    void AgentConfigPanel::load_preset(int index)
    {
        preset_index_ = index;
        switch (index)
        {
        case 0: // Bull Run
            config_ = agents::create_bull_run_population();
            counts_[0] = 500; // MM
            counts_[1] = 400; // TF
            counts_[2] = 100; // MR
            counts_[3] = 0;   // NT
            counts_[4] = 0;   // Whale
            aggr_[1] = 0.8f;
            aggr_[2] = 0.3f;
            mm_spread_pct_ = 0.001f;
            tf_threshold_pct_ = 0.02f;
            mr_threshold_pct_ = 0.10f;
            mr_fair_value_ = 100.0;
            break;

        case 1: // Consolidation
            config_ = agents::create_consolidation_population();
            counts_[0] = 500; // MM
            counts_[1] = 0;   // TF
            counts_[2] = 500; // MR
            counts_[3] = 0;   // NT
            counts_[4] = 0;   // Whale
            aggr_[2] = 0.7f;
            mm_spread_pct_ = 0.0005f;
            mr_threshold_pct_ = 0.02f;
            mr_fair_value_ = 100.0;
            break;

        case 2: // Flash Crash
            config_ = agents::create_flash_crash_population();
            counts_[0] = 300; // MM
            counts_[1] = 100; // TF
            counts_[2] = 0;   // MR
            counts_[3] = 200; // NT
            counts_[4] = 1;   // Whale
            whale_trigger_tick_ = 5000.0f;
            whale_sell_side_ = true;
            whale_size_ = 10000.0f;
            break;

        default: // Custom – don't touch sliders
            break;
        }
    }

    void AgentConfigPanel::set_config(const agents::PopulationConfig &config)
    {
        config_ = config;
        preset_index_ = PRESET_CUSTOM;
        sync_sliders_from_config();
    }

    void AgentConfigPanel::sync_sliders_from_config()
    {
        for (int i = 0; i < 5; ++i)
        {
            auto type = index_to_type(i);
            auto it = config_.populations.find(type);
            counts_[i] = (it != config_.populations.end()) ? static_cast<int>(it->second.count) : 0;
            if (it != config_.populations.end())
            {
                aggr_[i] = static_cast<float>(it->second.base_config.aggression);
                risk_[i] = static_cast<float>(it->second.base_config.risk_tolerance);
            }
        }
    }

    void AgentConfigPanel::sync_config_from_sliders()
    {
        config_.populations.clear();
        for (int i = 0; i < 5; ++i)
        {
            if (counts_[i] <= 0)
                continue;
            auto type = index_to_type(i);
            agents::AgentConfig ac;
            ac.type = type;
            ac.aggression = static_cast<double>(aggr_[i]);
            ac.risk_tolerance = static_cast<double>(risk_[i]);
            ac.max_position = 1000;
            ac.order_size_mean = 100.0;
            ac.order_size_stddev = 20.0;

            // Type-specific params
            switch (type)
            {
            case agents::AgentType::MARKET_MAKER:
                ac.params["spread_pct"] = static_cast<double>(mm_spread_pct_);
                break;
            case agents::AgentType::TREND_FOLLOWER:
                ac.params["threshold_pct"] = static_cast<double>(tf_threshold_pct_);
                break;
            case agents::AgentType::MEAN_REVERTER:
                ac.params["fair_value"] = mr_fair_value_;
                ac.params["threshold_pct"] = static_cast<double>(mr_threshold_pct_);
                break;
            case agents::AgentType::NOISE_TRADER:
                ac.params["noise_stddev"] = static_cast<double>(nt_noise_stddev_);
                break;
            case agents::AgentType::WHALE:
                ac.params["trigger_tick"] = static_cast<double>(whale_trigger_tick_);
                ac.params["whale_side"] = whale_sell_side_ ? 1.0 : 0.0;
                ac.params["whale_size"] = static_cast<double>(whale_size_);
                ac.params["slice_size"] = whale_size_ / 10.0;
                break;
            default:
                break;
            }

            config_.populations[type] = agents::TypeConfig{
                static_cast<uint32_t>(counts_[i]), ac};
        }
    }

    // ── Main render ────────────────────────────────────────────────────────────

    void AgentConfigPanel::render()
    {
#ifdef ENABLE_OPENGL_VIEWER
        ImGui::SetNextWindowSize(ImVec2(340, 600), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("⚙ Agent Configuration"))
        {
            ImGui::End();
            return;
        }

        // ── Preset selector ──────────────────────────────────────────────────
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "POPULATION PRESET");
        ImGui::Separator();

        static const char *preset_names[] = {
            "🐂 Bull Run", "🦀 Consolidation", "💥 Flash Crash", "✏ Custom"};

        if (ImGui::BeginCombo("##preset", preset_names[preset_index_]))
        {
            for (int i = 0; i < 4; ++i)
            {
                bool selected = (preset_index_ == i);
                if (ImGui::Selectable(preset_names[i], selected))
                {
                    if (i != PRESET_CUSTOM)
                        load_preset(i);
                    else
                        preset_index_ = PRESET_CUSTOM;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::Spacing();

        // ── Per-agent-type rows ──────────────────────────────────────────────
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "POPULATION MIX");
        ImGui::Separator();

        ImGui::BeginTable("pop_table", 2,
                          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg);
        ImGui::TableSetupColumn("Agent Type", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int i = 0; i < 5; ++i)
        {
            render_agent_row(agent_type_name(i), i, index_to_type(i));
        }
        ImGui::EndTable();

        // Total count
        int total = 0;
        for (int i = 0; i < 5; ++i)
            total += counts_[i];

        ImGui::Spacing();
        ImGui::Text("Total Agents: %d / 10000", total);
        float frac = static_cast<float>(total) / 10000.0f;
        ImVec4 bar_col = (frac > 0.8f) ? ImVec4(0.9f, 0.2f, 0.2f, 1.0f)
                                       : ImVec4(0.2f, 0.8f, 0.3f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, bar_col);
        char pop_label[32];
        std::snprintf(pop_label, sizeof(pop_label), "%d / 10000", total);
        ImGui::ProgressBar(frac, ImVec2(-1, 0), pop_label);
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── Type-specific parameters ─────────────────────────────────────────
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "AGENT PARAMETERS");
        ImGui::Separator();

        if (ImGui::TreeNodeEx("Market Maker", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SliderFloat("Spread %##mm", &mm_spread_pct_, 0.0001f, 0.01f, "%.4f");
            ImGui::SliderFloat("Aggression##mm", &aggr_[0], 0.0f, 1.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Trend Follower"))
        {
            ImGui::SliderFloat("Threshold %##tf", &tf_threshold_pct_, 0.005f, 0.10f, "%.3f");
            ImGui::SliderFloat("Aggression##tf", &aggr_[1], 0.0f, 1.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Mean Reverter"))
        {
            ImGui::InputDouble("Fair Value##mr", &mr_fair_value_, 1.0, 10.0, "%.2f");
            ImGui::SliderFloat("Band %##mr", &mr_threshold_pct_, 0.005f, 0.20f, "%.3f");
            ImGui::SliderFloat("Aggression##mr", &aggr_[2], 0.0f, 1.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Noise Trader"))
        {
            ImGui::SliderFloat("Noise Stddev##nt", &nt_noise_stddev_, 0.001f, 0.05f, "%.4f");
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Whale"))
        {
            ImGui::SliderFloat("Trigger Tick##wh", &whale_trigger_tick_, 100.0f, 20000.0f, "%.0f");
            ImGui::SliderFloat("Order Size##wh", &whale_size_, 100.0f, 50000.0f, "%.0f");
            ImGui::Checkbox("Sell Side (crash)", &whale_sell_side_);
            ImGui::TreePop();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── Action buttons ───────────────────────────────────────────────────
        float btn_w = (ImGui::GetContentRegionAvail().x - 8.0f) / 2.0f;

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.5f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
        if (ImGui::Button("✅ Apply", ImVec2(btn_w, 32.0f)))
        {
            preset_index_ = PRESET_CUSTOM;
            sync_config_from_sliders();
            apply_requested_ = true;
            if (on_apply_)
                on_apply_(config_);
        }
        ImGui::PopStyleColor(2);

        ImGui::SameLine(0, 8.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("🔄 Reset", ImVec2(btn_w, 32.0f)))
        {
            reset_requested_ = true;
            if (on_reset_)
                on_reset_();
        }
        ImGui::PopStyleColor(2);

        ImGui::End();
#endif // ENABLE_OPENGL_VIEWER
    }

    // ── Per-type row ───────────────────────────────────────────────────────────

    void AgentConfigPanel::render_agent_row(const char *label, int type_idx,
                                            agents::AgentType /*type*/)
    {
#ifdef ENABLE_OPENGL_VIEWER
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        // Color-code by type
        static const ImVec4 type_colors[] = {
            {0.2f, 0.8f, 0.8f, 1.0f}, // MM - cyan
            {0.9f, 0.6f, 0.1f, 1.0f}, // TF - orange
            {0.3f, 0.7f, 0.3f, 1.0f}, // MR - green
            {0.6f, 0.6f, 0.6f, 1.0f}, // NT - grey
            {0.9f, 0.2f, 0.2f, 1.0f}, // Wh - red
        };
        ImGui::TextColored(type_colors[type_idx], "%s", label);

        ImGui::TableSetColumnIndex(1);
        char id_buf[32];
        std::snprintf(id_buf, sizeof(id_buf), "##cnt%d", type_idx);
        ImGui::SetNextItemWidth(-1.0f);

        int max_cap[] = {3000, 2000, 2000, 5000, 10};
        if (ImGui::SliderInt(id_buf, &counts_[type_idx], 0, max_cap[type_idx]))
            preset_index_ = PRESET_CUSTOM;
#endif
    }

} // namespace lob::ui
