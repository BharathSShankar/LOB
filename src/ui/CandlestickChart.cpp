/**
 * @file CandlestickChart.cpp
 * @brief ImGui/DrawList candlestick chart implementation
 */

#include "ui/CandlestickChart.h"

#ifdef ENABLE_OPENGL_VIEWER
#include "imgui.h"
#endif

#include <cmath>
#include <algorithm>
#include <limits>
#include <cstdio>

namespace lob::ui
{

    // ────────────────────────────────────────────────────────────────────────────
    // Colour constants (ImU32 ABGR format)
    // ────────────────────────────────────────────────────────────────────────────
#ifdef ENABLE_OPENGL_VIEWER
    static constexpr ImU32 COL_BULL = IM_COL32(38, 198, 97, 255);   // green
    static constexpr ImU32 COL_BEAR = IM_COL32(229, 57, 53, 255);   // red
    static constexpr ImU32 COL_WICK = IM_COL32(160, 160, 160, 220); // grey
    static constexpr ImU32 COL_BORDER = IM_COL32(60, 60, 70, 255);  // dark grey bg
    static constexpr ImU32 COL_GRID = IM_COL32(60, 60, 70, 180);    // grid lines
    static constexpr ImU32 COL_GRID_LABEL = IM_COL32(150, 150, 150, 200);
    static constexpr ImU32 COL_SMA50 = IM_COL32(255, 220, 50, 220);  // yellow
    static constexpr ImU32 COL_SMA200 = IM_COL32(80, 160, 255, 220); // blue
    static constexpr ImU32 COL_VOL_BULL = IM_COL32(38, 198, 97, 100);
    static constexpr ImU32 COL_VOL_BEAR = IM_COL32(229, 57, 53, 100);
    static constexpr ImU32 COL_PATTERN_BOX = IM_COL32(255, 180, 60, 100);
    static constexpr ImU32 COL_PATTERN_TXT = IM_COL32(255, 220, 120, 255);
    static constexpr ImU32 COL_CROSSHAIR = IM_COL32(200, 200, 200, 100);
#endif

    static constexpr float VOL_FRAC = 0.20f; // volume pane = 20% of total height

    // ────────────────────────────────────────────────────────────────────────────
    CandlestickChart::CandlestickChart() = default;

    // ────────────────────────────────────────────────────────────────────────────
    // Coordinate helpers
    // ────────────────────────────────────────────────────────────────────────────

    float CandlestickChart::price_to_y(double price) const
    {
        if (view_hi_ <= view_lo_)
            return chart_y_;
        double frac = (view_hi_ - price) / (view_hi_ - view_lo_);
        return chart_y_ + static_cast<float>(frac) * price_h_;
    }

    float CandlestickChart::index_to_x(int local_idx) const
    {
        // Centre of candle body
        return chart_x_ + (static_cast<float>(local_idx) + 0.5f) * candle_width_;
    }

    float CandlestickChart::vol_to_h(double vol) const
    {
        if (max_vol_ <= 0)
            return 0.0f;
        return static_cast<float>((vol / max_vol_) * vol_h_);
    }

    // ────────────────────────────────────────────────────────────────────────────
    // Layout computation
    // ────────────────────────────────────────────────────────────────────────────

    void CandlestickChart::compute_layout(float avail_w, float avail_h)
    {
        // Reserve a small right margin for price labels
        static constexpr float PRICE_LABEL_W = 60.0f;
        chart_w_ = avail_w - PRICE_LABEL_W;
        if (chart_w_ < 50.0f)
            chart_w_ = 50.0f;

        if (show_volume_)
        {
            vol_h_ = avail_h * VOL_FRAC;
            price_h_ = avail_h - vol_h_ - 4.0f; // 4px gap
        }
        else
        {
            vol_h_ = 0.0f;
            price_h_ = avail_h;
        }
        chart_h_ = avail_h;
    }

    void CandlestickChart::compute_price_range(
        const std::vector<analytics::Candle> &candles, int first, int last)
    {
        view_lo_ = std::numeric_limits<double>::max();
        view_hi_ = std::numeric_limits<double>::lowest();
        max_vol_ = 1.0;

        for (int i = first; i < last && i < static_cast<int>(candles.size()); ++i)
        {
            const auto &c = candles[i];
            view_lo_ = std::min(view_lo_, c.low);
            view_hi_ = std::max(view_hi_, c.high);
            max_vol_ = std::max(max_vol_, static_cast<double>(c.volume));
        }

        // Add 5% padding
        double range = view_hi_ - view_lo_;
        if (range < 1e-9)
            range = 1.0;
        view_lo_ -= range * 0.05;
        view_hi_ += range * 0.05;
    }

    // ────────────────────────────────────────────────────────────────────────────
    // SMA calculation
    // ────────────────────────────────────────────────────────────────────────────

    std::vector<double> CandlestickChart::calc_sma(
        const std::vector<analytics::Candle> &candles, int period) const
    {
        std::vector<double> sma(candles.size(), std::numeric_limits<double>::quiet_NaN());
        double sum = 0.0;
        for (int i = 0; i < static_cast<int>(candles.size()); ++i)
        {
            sum += candles[i].close;
            if (i >= period)
                sum -= candles[i - period].close;
            if (i >= period - 1)
                sma[i] = sum / period;
        }
        return sma;
    }

    // ────────────────────────────────────────────────────────────────────────────
    // Draw helper: grid
    // ────────────────────────────────────────────────────────────────────────────

    void CandlestickChart::draw_grid(void *vdl, int /*first*/, int /*last*/,
                                     const std::vector<analytics::Candle> & /*candles*/) const
    {
#ifdef ENABLE_OPENGL_VIEWER
        auto *dl = static_cast<ImDrawList *>(vdl);
        // Horizontal price lines (5 levels)
        for (int i = 0; i <= 5; ++i)
        {
            double price = view_lo_ + (view_hi_ - view_lo_) * i / 5.0;
            float y = price_to_y(price);
            dl->AddLine(ImVec2(chart_x_, y),
                        ImVec2(chart_x_ + chart_w_, y),
                        COL_GRID, 1.0f);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.2f", price);
            dl->AddText(ImVec2(chart_x_ + chart_w_ + 3, y - 7), COL_GRID_LABEL, buf);
        }
        // Border
        dl->AddRect(ImVec2(chart_x_, chart_y_),
                    ImVec2(chart_x_ + chart_w_, chart_y_ + price_h_),
                    COL_BORDER);
        if (show_volume_ && vol_h_ > 0)
        {
            float vy = chart_y_ + price_h_ + 4.0f;
            dl->AddRect(ImVec2(chart_x_, vy),
                        ImVec2(chart_x_ + chart_w_, vy + vol_h_),
                        COL_BORDER);
        }
#endif
    }

    // ────────────────────────────────────────────────────────────────────────────
    // Draw candle bodies + wicks
    // ────────────────────────────────────────────────────────────────────────────

    void CandlestickChart::draw_candles(void *vdl, int first, int last,
                                        const std::vector<analytics::Candle> &candles) const
    {
#ifdef ENABLE_OPENGL_VIEWER
        auto *dl = static_cast<ImDrawList *>(vdl);
        float half = (candle_width_ * 0.4f);
        if (half < 0.5f)
            half = 0.5f;

        for (int gi = first; gi < last && gi < static_cast<int>(candles.size()); ++gi)
        {
            const auto &c = candles[gi];
            int li = gi - first; // local index
            float cx = index_to_x(li);

            float open_y = price_to_y(c.open);
            float close_y = price_to_y(c.close);
            float high_y = price_to_y(c.high);
            float low_y = price_to_y(c.low);

            bool bull = c.is_bullish();
            ImU32 col = bull ? COL_BULL : COL_BEAR;

            // Wick
            dl->AddLine(ImVec2(cx, high_y), ImVec2(cx, low_y), COL_WICK, 1.0f);

            // Body
            float top = std::min(open_y, close_y);
            float bottom = std::max(open_y, close_y);
            float body_h = bottom - top;
            if (body_h < 1.0f)
                body_h = 1.0f;

            if (candle_width_ >= 3.0f)
            {
                dl->AddRectFilled(ImVec2(cx - half, top),
                                  ImVec2(cx + half, top + body_h), col);
                dl->AddRect(ImVec2(cx - half, top),
                            ImVec2(cx + half, top + body_h), col, 0.0f, 0, 1.0f);
            }
            else
            {
                // Very zoomed out – just draw a coloured line
                dl->AddLine(ImVec2(cx, top), ImVec2(cx, top + body_h), col, 1.0f);
            }
        }
#endif
    }

    // ────────────────────────────────────────────────────────────────────────────
    // Draw volume bars
    // ────────────────────────────────────────────────────────────────────────────

    void CandlestickChart::draw_volume(void *vdl, int first, int last,
                                       const std::vector<analytics::Candle> &candles) const
    {
#ifdef ENABLE_OPENGL_VIEWER
        if (!show_volume_ || vol_h_ <= 0)
            return;
        auto *dl = static_cast<ImDrawList *>(vdl);

        float vy_base = chart_y_ + price_h_ + 4.0f + vol_h_; // bottom of vol pane
        float half = (candle_width_ * 0.4f);
        if (half < 0.5f)
            half = 0.5f;

        for (int gi = first; gi < last && gi < static_cast<int>(candles.size()); ++gi)
        {
            const auto &c = candles[gi];
            int li = gi - first;
            float cx = index_to_x(li);
            float bar_h = vol_to_h(static_cast<double>(c.volume));
            if (bar_h < 1.0f)
                bar_h = 1.0f;

            ImU32 col = c.is_bullish() ? COL_VOL_BULL : COL_VOL_BEAR;
            dl->AddRectFilled(ImVec2(cx - half, vy_base - bar_h),
                              ImVec2(cx + half, vy_base), col);
        }
#endif
    }

    // ────────────────────────────────────────────────────────────────────────────
    // Draw SMA overlays
    // ────────────────────────────────────────────────────────────────────────────

    void CandlestickChart::draw_sma(void *vdl, int first, int last,
                                    const std::vector<analytics::Candle> &candles) const
    {
#ifdef ENABLE_OPENGL_VIEWER
        if (!show_sma_)
            return;
        auto *dl = static_cast<ImDrawList *>(vdl);

        auto sma50 = calc_sma(candles, 50);
        auto sma200 = calc_sma(candles, 200);

        auto draw_line = [&](const std::vector<double> &sma, ImU32 col)
        {
            float prev_x = 0.0f, prev_y = 0.0f;
            bool has_prev = false;
            for (int gi = first; gi < last && gi < static_cast<int>(candles.size()); ++gi)
            {
                double v = sma[gi];
                if (std::isnan(v))
                {
                    has_prev = false;
                    continue;
                }
                int li = gi - first;
                float cx = index_to_x(li);
                float cy = price_to_y(v);
                if (has_prev)
                    dl->AddLine(ImVec2(prev_x, prev_y), ImVec2(cx, cy), col, 1.5f);
                prev_x = cx;
                prev_y = cy;
                has_prev = true;
            }
        };

        draw_line(sma50, COL_SMA50);
        draw_line(sma200, COL_SMA200);

        // Legend
        float lx = chart_x_ + 6.0f;
        float ly = chart_y_ + 6.0f;
        dl->AddLine(ImVec2(lx, ly + 4), ImVec2(lx + 18, ly + 4), COL_SMA50, 2.0f);
        dl->AddText(ImVec2(lx + 22, ly), COL_SMA50, "SMA-50");
        dl->AddLine(ImVec2(lx, ly + 20), ImVec2(lx + 18, ly + 20), COL_SMA200, 2.0f);
        dl->AddText(ImVec2(lx + 22, ly + 16), COL_SMA200, "SMA-200");
#endif
    }

    // ────────────────────────────────────────────────────────────────────────────
    // Draw pattern markers
    // ────────────────────────────────────────────────────────────────────────────

    void CandlestickChart::draw_patterns(void *vdl, int view_first,
                                         const std::vector<analytics::PatternMatch> &patterns) const
    {
#ifdef ENABLE_OPENGL_VIEWER
        if (!show_patterns_)
            return;
        auto *dl = static_cast<ImDrawList *>(vdl);

        float label_y = chart_y_ + price_h_ - 18.0f;

        for (const auto &pm : patterns)
        {
            int li_start = static_cast<int>(pm.start_index) - view_first;
            int li_end = static_cast<int>(pm.end_index) - view_first;
            if (li_end < 0 || li_start >= visible_candles_)
                continue;
            li_start = std::max(li_start, 0);
            li_end = std::min(li_end, visible_candles_ - 1);

            float x1 = index_to_x(li_start) - candle_width_ * 0.5f;
            float x2 = index_to_x(li_end) + candle_width_ * 0.5f;

            // Box
            dl->AddRectFilled(ImVec2(x1, chart_y_),
                              ImVec2(x2, chart_y_ + price_h_),
                              IM_COL32(255, 180, 60, 20));
            dl->AddRect(ImVec2(x1, chart_y_),
                        ImVec2(x2, chart_y_ + price_h_),
                        COL_PATTERN_BOX, 0.0f, 0, 1.5f);

            // Label
            char buf[80];
            std::snprintf(buf, sizeof(buf), "%s %.0f%%",
                          analytics::to_string(pm.pattern).c_str(),
                          pm.confidence * 100.0);
            dl->AddText(ImVec2(x1 + 3.0f, label_y), COL_PATTERN_TXT, buf);
            label_y -= 16.0f; // stack labels upward
            if (label_y < chart_y_)
                label_y = chart_y_ + price_h_ - 18.0f;
        }
#endif
    }

    // ────────────────────────────────────────────────────────────────────────────
    // Main render
    // ────────────────────────────────────────────────────────────────────────────

    void CandlestickChart::render(const std::vector<analytics::Candle> &candles,
                                  const std::vector<analytics::PatternMatch> &patterns)
    {
#ifdef ENABLE_OPENGL_VIEWER
        if (candles.empty())
        {
            ImGui::TextDisabled("No candle data yet – simulation is running...");
            return;
        }

        // Toolbar
        ImGui::Checkbox("Volume", &show_volume_);
        ImGui::SameLine(0, 12);
        ImGui::Checkbox("SMA", &show_sma_);
        ImGui::SameLine(0, 12);
        ImGui::Checkbox("Patterns", &show_patterns_);
        ImGui::SameLine(0, 12);
        ImGui::Checkbox("Grid", &show_grid_);
        ImGui::SameLine(0, 12);

        if (ImGui::SmallButton("Reset View"))
        {
            scroll_offset_ = 0;
            visible_candles_ = 80;
        }
        ImGui::SameLine(0, 12);
        ImGui::Text("Candles: %d", static_cast<int>(candles.size()));

        ImGui::Separator();

        // Scrolling / zoom via mouse
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x < 10.0f || avail.y < 10.0f)
            return;

        // Use an invisible button as hit-box for mouse interaction
        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##canvas", avail,
                               ImGuiButtonFlags_MouseButtonLeft |
                                   ImGuiButtonFlags_MouseButtonRight);

        if (ImGui::IsItemHovered())
        {
            float wheel = ImGui::GetIO().MouseWheel;
            if (ImGui::GetIO().KeyCtrl)
            {
                // Ctrl+scroll = zoom
                visible_candles_ -= static_cast<int>(wheel * 5);
                visible_candles_ = std::clamp(visible_candles_, 5, 500);
            }
            else if (std::fabs(wheel) > 0.0f)
            {
                // Scroll = pan
                scroll_offset_ -= static_cast<int>(wheel * 3);
            }
        }

        // Clamp scroll
        int n = static_cast<int>(candles.size());
        int visible = std::min(visible_candles_, n);
        int max_off = std::max(0, n - visible);
        scroll_offset_ = std::clamp(scroll_offset_, 0, max_off);

        int first = max_off - scroll_offset_; // show newest by default
        int last = first + visible;

        // Layout
        chart_x_ = canvas_pos.x;
        chart_y_ = canvas_pos.y;
        compute_layout(avail.x, avail.y);
        candle_width_ = chart_w_ / static_cast<float>(visible == 0 ? 1 : visible);
        compute_price_range(candles, first, last);

        auto *dl = ImGui::GetWindowDrawList();

        // Background
        dl->AddRectFilled(canvas_pos,
                          ImVec2(canvas_pos.x + avail.x, canvas_pos.y + avail.y),
                          IM_COL32(15, 15, 20, 255));

        if (show_grid_)
            draw_grid(dl, first, last, candles);

        draw_candles(dl, first, last, candles);

        if (show_volume_)
            draw_volume(dl, first, last, candles);

        if (show_sma_)
            draw_sma(dl, first, last, candles);

        if (show_patterns_)
            draw_patterns(dl, first, patterns);

        // Crosshair on hover
        if (ImGui::IsItemHovered())
        {
            ImVec2 mp = ImGui::GetMousePos();
            dl->AddLine(ImVec2(chart_x_, mp.y),
                        ImVec2(chart_x_ + chart_w_, mp.y),
                        COL_CROSSHAIR, 1.0f);
            dl->AddLine(ImVec2(mp.x, chart_y_),
                        ImVec2(mp.x, chart_y_ + price_h_),
                        COL_CROSSHAIR, 1.0f);

            // Price tooltip
            double hover_price = view_hi_ -
                                 (view_hi_ - view_lo_) * (mp.y - chart_y_) / price_h_;
            char tip[64];
            std::snprintf(tip, sizeof(tip), "%.4f", hover_price);
            ImGui::SetTooltip("%s", tip);
        }
#else
        (void)candles;
        (void)patterns;
#endif
    }

} // namespace lob::ui
