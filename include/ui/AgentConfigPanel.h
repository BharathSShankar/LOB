#pragma once

/**
 * @file AgentConfigPanel.h
 * @brief ImGui configuration panel for ABMS agent population
 *
 * Provides a UI control panel for:
 * - Selecting population presets (Bull Run, Consolidation, Flash Crash)
 * - Adjusting per-agent-type counts and behavioral parameters
 * - Applying configuration to the live simulation
 *
 * Week 10: Day 31-33 implementation
 */

#include "agents/AgentZoo.h"
#include <functional>
#include <string>

namespace lob::ui
{

    /**
     * @brief ImGui-based configuration panel for agent population
     *
     * Renders a Dear ImGui window with:
     *  - A preset combo box (Bull Run / Consolidation / Flash Crash / Custom)
     *  - Per-type integer sliders for agent counts
     *  - Collapsible parameter sections per agent type
     *  - An "Apply" button that fires the on_apply callback
     *  - A "Reset" button that re-initialises to the current preset
     *
     * Usage:
     * @code
     *   AgentConfigPanel panel;
     *   panel.set_config(agents::create_bull_run_population());
     *   panel.set_on_apply([&](agents::PopulationConfig cfg) {
     *       orchestrator.set_population(cfg);
     *   });
     *
     *   // Per-frame (inside ImGui render pass):
     *   panel.render();
     * @endcode
     */
    class AgentConfigPanel
    {
    public:
        AgentConfigPanel();

        // ── Callbacks ─────────────────────────────────────────────────────────

        /** Called when the user presses "Apply Configuration" */
        void set_on_apply(std::function<void(agents::PopulationConfig)> cb) { on_apply_ = std::move(cb); }

        /** Called when the user presses "Reset Simulation" */
        void set_on_reset(std::function<void()> cb) { on_reset_ = std::move(cb); }

        // ── Configuration ─────────────────────────────────────────────────────

        /** Replace the current edited configuration */
        void set_config(const agents::PopulationConfig &config);

        /** Read the currently edited configuration */
        const agents::PopulationConfig &get_config() const { return config_; }

        /** True once per frame after the user presses "Apply"; call clear_flags() after reading */
        bool apply_requested() const { return apply_requested_; }

        /** True once per frame after the user presses "Reset"; call clear_flags() after reading */
        bool reset_requested() const { return reset_requested_; }

        /** Clear one-shot flags */
        void clear_flags()
        {
            apply_requested_ = false;
            reset_requested_ = false;
        }

        // ── Rendering ─────────────────────────────────────────────────────────

        /**
         * @brief Render the configuration panel
         *
         * Must be called from within an active ImGui frame.
         */
        void render();

    private:
        agents::PopulationConfig config_;

        // Preset management
        int preset_index_{0}; ///< 0=Bull Run, 1=Consolidation, 2=Flash Crash, 3=Custom
        static constexpr int PRESET_CUSTOM = 3;

        // Editable sliders (mirror config_ for ImGui int sliders)
        int counts_[5]{500, 400, 100, 0, 0}; ///< [MM, TF, MR, NT, Whale]
        float aggr_[5]{0.5f, 0.8f, 0.3f, 0.5f, 0.9f};
        float risk_[5]{0.5f, 0.6f, 0.7f, 0.5f, 1.0f};

        // Per-type specific sliders
        float mm_spread_pct_{0.001f};
        float tf_threshold_pct_{0.02f};
        float mr_threshold_pct_{0.10f};
        double mr_fair_value_{100.0};
        float nt_noise_stddev_{0.005f};
        float whale_trigger_tick_{5000.0f};
        bool whale_sell_side_{true};
        float whale_size_{10000.0f};

        bool apply_requested_{false};
        bool reset_requested_{false};

        std::function<void(agents::PopulationConfig)> on_apply_;
        std::function<void()> on_reset_;

        // ── Private helpers ───────────────────────────────────────────────────
        void load_preset(int index);
        void sync_sliders_from_config();
        void sync_config_from_sliders();
        void render_agent_row(const char *label, int type_idx, agents::AgentType type);
    };

} // namespace lob::ui
