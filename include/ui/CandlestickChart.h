#pragma once

/**
 * @file CandlestickChart.h
 * @brief ImGui/OpenGL candlestick chart with SMA overlay and pattern markers
 *
 * Renders a candlestick OHLCV chart using ImGui's ImDrawList API.
 * Supports:
 *  - Green/red OHLCV candles with wicks
 *  - Volume bars at the bottom (20% height)
 *  - SMA-50 (yellow) and SMA-200 (blue) line overlays
 *  - Pattern match boxes with labels
 *  - Scroll/zoom via mouse (scroll = horizontal pan, ctrl+scroll = zoom)
 *
 * Week 10: Day 34-36 implementation
 */

#include "analytics/OHLCVAggregator.h"
#include "analytics/PatternScanner.h"
#include <vector>
#include <string>
#include <deque>

namespace lob::ui
{

    /**
     * @brief Interactive candlestick chart rendered via ImGui DrawList
     *
     * In each frame, call render() after ImGui::Begin() / before ImGui::End().
     * The chart uses the full available content region for the parent window.
     *
     * Usage:
     * @code
     *   CandlestickChart chart;
     *   chart.set_show_volume(true);
     *   chart.set_show_sma(true);
     *
     *   // In render loop:
     *   ImGui::Begin("Chart");
     *   chart.render(candles, patterns);
     *   ImGui::End();
     * @endcode
     */
    class CandlestickChart
    {
    public:
        CandlestickChart();

        // ── Display options ───────────────────────────────────────────────────
        void set_show_volume(bool v) { show_volume_ = v; }
        void set_show_sma(bool v) { show_sma_ = v; }
        void set_show_patterns(bool v) { show_patterns_ = v; }
        void set_show_grid(bool v) { show_grid_ = v; }

        bool get_show_volume() const { return show_volume_; }
        bool get_show_sma() const { return show_sma_; }
        bool get_show_patterns() const { return show_patterns_; }

        // ── Viewport ──────────────────────────────────────────────────────────
        /** Maximum candles visible at default zoom */
        void set_visible_candles(int n) { visible_candles_ = n; }

        // ── Main render call ──────────────────────────────────────────────────
        /**
         * @brief Render the chart into the current ImGui window content area
         * @param candles OHLCV candle vector (oldest first)
         * @param patterns Detected pattern matches (indices into candles)
         */
        void render(const std::vector<analytics::Candle> &candles,
                    const std::vector<analytics::PatternMatch> &patterns);

    private:
        // Display flags
        bool show_volume_{true};
        bool show_sma_{true};
        bool show_patterns_{true};
        bool show_grid_{true};

        // View state
        int visible_candles_{80}; ///< How many candles fit at current zoom
        int scroll_offset_{0};    ///< First candle index to display
        float candle_width_{0.0f};

        // Computed layout rectangles (filled each frame)
        float chart_x_{0}, chart_y_{0}, chart_w_{0}, chart_h_{0};
        float price_h_{0}, vol_h_{0};

        // Price range for current view
        double view_lo_{0}, view_hi_{0};
        double max_vol_{1};

        // ── Helpers ───────────────────────────────────────────────────────────
        void compute_layout(float avail_w, float avail_h);
        void compute_price_range(const std::vector<analytics::Candle> &candles,
                                 int first, int last);

        float price_to_y(double price) const;
        float index_to_x(int local_idx) const;
        float vol_to_h(double vol) const;

        void draw_grid(void *draw_list, int first, int last,
                       const std::vector<analytics::Candle> &candles) const;
        void draw_candles(void *draw_list, int first, int last,
                          const std::vector<analytics::Candle> &candles) const;
        void draw_volume(void *draw_list, int first, int last,
                         const std::vector<analytics::Candle> &candles) const;
        void draw_sma(void *draw_list, int first, int last,
                      const std::vector<analytics::Candle> &candles) const;
        void draw_patterns(void *draw_list, int first,
                           const std::vector<analytics::PatternMatch> &patterns) const;

        /** Calculate SMA for candle closes, returns NaN where insufficient data */
        std::vector<double> calc_sma(const std::vector<analytics::Candle> &candles,
                                     int period) const;
    };

} // namespace lob::ui
