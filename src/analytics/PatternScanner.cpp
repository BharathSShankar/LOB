#include "analytics/PatternScanner.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace lob::analytics
{

    // ============================================================================
    // to_string helper
    // ============================================================================

    std::string to_string(PatternType type)
    {
        switch (type)
        {
        case PatternType::UPTREND:
            return "Uptrend";
        case PatternType::DOWNTREND:
            return "Downtrend";
        case PatternType::SIDEWAYS:
            return "Sideways";
        case PatternType::HEAD_AND_SHOULDERS:
            return "Head and Shoulders";
        case PatternType::INVERSE_HEAD_AND_SHOULDERS:
            return "Inverse Head and Shoulders";
        case PatternType::DOUBLE_TOP:
            return "Double Top";
        case PatternType::DOUBLE_BOTTOM:
            return "Double Bottom";
        case PatternType::BULL_FLAG:
            return "Bull Flag";
        case PatternType::BEAR_FLAG:
            return "Bear Flag";
        case PatternType::ASCENDING_TRIANGLE:
            return "Ascending Triangle";
        case PatternType::DESCENDING_TRIANGLE:
            return "Descending Triangle";
        case PatternType::SYMMETRICAL_TRIANGLE:
            return "Symmetrical Triangle";
        case PatternType::DOJI:
            return "Doji";
        case PatternType::HAMMER:
            return "Hammer";
        case PatternType::SHOOTING_STAR:
            return "Shooting Star";
        case PatternType::ENGULFING_BULLISH:
            return "Bullish Engulfing";
        case PatternType::ENGULFING_BEARISH:
            return "Bearish Engulfing";
        case PatternType::MORNING_STAR:
            return "Morning Star";
        case PatternType::EVENING_STAR:
            return "Evening Star";
        case PatternType::GAP_UP:
            return "Gap Up";
        case PatternType::GAP_DOWN:
            return "Gap Down";
        case PatternType::FLASH_CRASH:
            return "Flash Crash";
        default:
            return "Unknown";
        }
    }

    // ============================================================================
    // Constructors
    // ============================================================================

    PatternScanner::PatternScanner() noexcept
        : config_(Config{})
    {
    }

    PatternScanner::PatternScanner(const Config &config) noexcept
        : config_(config)
    {
    }

    // ============================================================================
    // scan() – master detector
    // ============================================================================

    std::vector<PatternMatch> PatternScanner::scan(const std::vector<Candle> &candles)
    {
        std::vector<PatternMatch> results;
        if (candles.empty())
            return results;

        const uint32_t n = static_cast<uint32_t>(candles.size());

        // ── Whole-series trend ──────────────────────────────────────────────────
        if (detect_uptrend(candles))
            results.push_back({PatternType::UPTREND, 0, n - 1, 0.80,
                               "Uptrend detected", true});
        else if (detect_downtrend(candles))
            results.push_back({PatternType::DOWNTREND, 0, n - 1, 0.80,
                               "Downtrend detected", false});
        else if (detect_sideways(candles))
            results.push_back({PatternType::SIDEWAYS, 0, n - 1, 0.75,
                               "Sideways consolidation", false});

        // ── Per-candle patterns ─────────────────────────────────────────────────
        for (uint32_t i = 0; i < n; ++i)
        {
            if (detect_doji(candles[i]))
                results.push_back({PatternType::DOJI, i, i, 0.85,
                                   "Doji (indecision)", false});
            if (detect_hammer(candles[i]))
                results.push_back({PatternType::HAMMER, i, i, 0.80,
                                   "Hammer (bullish reversal)", true});
            if (detect_shooting_star(candles[i]))
                results.push_back({PatternType::SHOOTING_STAR, i, i, 0.80,
                                   "Shooting Star (bearish reversal)", false});
        }

        // ── Two-candle patterns ─────────────────────────────────────────────────
        for (uint32_t i = 1; i < n; ++i)
        {
            const Candle &prev = candles[i - 1];
            const Candle &curr = candles[i];

            if (detect_bullish_engulfing(prev, curr))
                results.push_back({PatternType::ENGULFING_BULLISH, i - 1, i, 0.85,
                                   "Bullish Engulfing", true});
            if (detect_bearish_engulfing(prev, curr))
                results.push_back({PatternType::ENGULFING_BEARISH, i - 1, i, 0.85,
                                   "Bearish Engulfing", false});
            if (detect_gap_up(prev, curr))
                results.push_back({PatternType::GAP_UP, i - 1, i, 0.90,
                                   "Gap Up", true});
            if (detect_gap_down(prev, curr))
                results.push_back({PatternType::GAP_DOWN, i - 1, i, 0.90,
                                   "Gap Down", false});
        }

        // ── Three-candle patterns ───────────────────────────────────────────────
        for (uint32_t i = 2; i < n; ++i)
        {
            if (detect_morning_star(candles[i - 2], candles[i - 1], candles[i]))
                results.push_back({PatternType::MORNING_STAR, i - 2, i, 0.82,
                                   "Morning Star (bullish reversal)", true});
            if (detect_evening_star(candles[i - 2], candles[i - 1], candles[i]))
                results.push_back({PatternType::EVENING_STAR, i - 2, i, 0.82,
                                   "Evening Star (bearish reversal)", false});
        }

        // ── Multi-candle patterns ───────────────────────────────────────────────
        uint32_t os = 0, oe = 0;

        if (detect_head_and_shoulders(candles, os, oe))
            results.push_back({PatternType::HEAD_AND_SHOULDERS, os, oe, 0.75,
                               "Head and Shoulders (bearish reversal)", false});
        if (detect_inverse_head_and_shoulders(candles, os, oe))
            results.push_back({PatternType::INVERSE_HEAD_AND_SHOULDERS, os, oe, 0.75,
                               "Inverse H&S (bullish reversal)", true});
        if (detect_double_top(candles, os, oe))
            results.push_back({PatternType::DOUBLE_TOP, os, oe, 0.80,
                               "Double Top (bearish reversal)", false});
        if (detect_double_bottom(candles, os, oe))
            results.push_back({PatternType::DOUBLE_BOTTOM, os, oe, 0.80,
                               "Double Bottom (bullish reversal)", true});
        if (detect_bull_flag(candles, os, oe))
            results.push_back({PatternType::BULL_FLAG, os, oe, 0.75,
                               "Bull Flag (bullish continuation)", true});
        if (detect_bear_flag(candles, os, oe))
            results.push_back({PatternType::BEAR_FLAG, os, oe, 0.75,
                               "Bear Flag (bearish continuation)", false});
        if (detect_flash_crash(candles, os, oe))
            results.push_back({PatternType::FLASH_CRASH, os, oe, 0.95,
                               "Flash Crash detected!", false});

        return results;
    }

    // ============================================================================
    // Trend detectors
    // ============================================================================

    bool PatternScanner::detect_uptrend(const std::vector<Candle> &candles,
                                        uint32_t start) const
    {
        if (candles.size() - start < config_.min_trend_candles)
            return false;

        uint32_t consecutive = 0;
        for (size_t i = start + 1; i < candles.size(); ++i)
        {
            double prev = candles[i - 1].close;
            if (prev <= 0.0)
            {
                consecutive = 0;
                continue;
            }

            double chg = (candles[i].close - prev) / prev;
            if (chg > config_.trend_threshold)
                ++consecutive;
            else
                consecutive = 0;

            if (consecutive >= config_.min_trend_candles - 1)
                return true;
        }
        return false;
    }

    bool PatternScanner::detect_downtrend(const std::vector<Candle> &candles,
                                          uint32_t start) const
    {
        if (candles.size() - start < config_.min_trend_candles)
            return false;

        uint32_t consecutive = 0;
        for (size_t i = start + 1; i < candles.size(); ++i)
        {
            double prev = candles[i - 1].close;
            if (prev <= 0.0)
            {
                consecutive = 0;
                continue;
            }

            double chg = (candles[i].close - prev) / prev;
            if (chg < -config_.trend_threshold)
                ++consecutive;
            else
                consecutive = 0;

            if (consecutive >= config_.min_trend_candles - 1)
                return true;
        }
        return false;
    }

    bool PatternScanner::detect_sideways(const std::vector<Candle> &candles,
                                         uint32_t start) const
    {
        if (candles.size() - start < config_.min_trend_candles)
            return false;

        double lo = candles[start].close;
        double hi = candles[start].close;

        for (size_t i = start + 1; i < candles.size(); ++i)
        {
            if (candles[i].close < lo)
                lo = candles[i].close;
            if (candles[i].close > hi)
                hi = candles[i].close;
        }

        if (lo <= 0.0)
            return false;
        return ((hi - lo) / lo) <= config_.sideways_range;
    }

    // ============================================================================
    // Reversal detectors
    // ============================================================================

    bool PatternScanner::detect_head_and_shoulders(const std::vector<Candle> &candles,
                                                   uint32_t &out_start,
                                                   uint32_t &out_end) const
    {
        if (candles.size() < 7)
            return false;
        auto peaks = find_local_maxima(candles, 2);
        if (peaks.size() < 3)
            return false;

        for (size_t i = 0; i + 2 < peaks.size(); ++i)
        {
            uint32_t li = peaks[i], hi = peaks[i + 1], ri = peaks[i + 2];
            double lH = candles[li].high;
            double hH = candles[hi].high;
            double rH = candles[ri].high;
            if (lH <= 0.0 || rH <= 0.0)
                continue;

            bool head_higher = (hH > lH * 1.02) && (hH > rH * 1.02);
            bool shoulders_sim = std::abs(lH - rH) / lH < config_.hs_shoulder_tol;

            if (head_higher && shoulders_sim)
            {
                out_start = li;
                out_end = ri;
                return true;
            }
        }
        return false;
    }

    bool PatternScanner::detect_inverse_head_and_shoulders(const std::vector<Candle> &candles,
                                                           uint32_t &out_start,
                                                           uint32_t &out_end) const
    {
        if (candles.size() < 7)
            return false;
        auto troughs = find_local_minima(candles, 2);
        if (troughs.size() < 3)
            return false;

        for (size_t i = 0; i + 2 < troughs.size(); ++i)
        {
            uint32_t li = troughs[i], hi = troughs[i + 1], ri = troughs[i + 2];
            double lL = candles[li].low;
            double hL = candles[hi].low;
            double rL = candles[ri].low;
            if (lL <= 0.0 || rL <= 0.0)
                continue;

            bool head_lower = (hL < lL * 0.98) && (hL < rL * 0.98);
            bool shoulders_sim = std::abs(lL - rL) / lL < config_.hs_shoulder_tol;

            if (head_lower && shoulders_sim)
            {
                out_start = li;
                out_end = ri;
                return true;
            }
        }
        return false;
    }

    bool PatternScanner::detect_double_top(const std::vector<Candle> &candles,
                                           uint32_t &out_start, uint32_t &out_end) const
    {
        if (candles.size() < 5)
            return false;
        // Use window=1 so peaks near the edges of the series are not missed
        auto peaks = find_local_maxima(candles, 1);
        if (peaks.size() < 2)
            return false;

        for (size_t i = 0; i + 1 < peaks.size(); ++i)
        {
            uint32_t p1 = peaks[i], p2 = peaks[i + 1];
            double h1 = candles[p1].high;
            double h2 = candles[p2].high;
            if (h1 <= 0.0)
                continue;

            bool sim_price = std::abs(h1 - h2) / h1 < config_.double_top_tol;
            bool gap_ok = (p2 - p1) >= config_.double_top_min_gap;

            if (sim_price && gap_ok)
            {
                out_start = p1;
                out_end = p2;
                return true;
            }
        }
        return false;
    }

    bool PatternScanner::detect_double_bottom(const std::vector<Candle> &candles,
                                              uint32_t &out_start, uint32_t &out_end) const
    {
        if (candles.size() < 5)
            return false;
        // Use window=1 so troughs near the edges of the series are not missed
        auto troughs = find_local_minima(candles, 1);
        if (troughs.size() < 2)
            return false;

        for (size_t i = 0; i + 1 < troughs.size(); ++i)
        {
            uint32_t t1 = troughs[i], t2 = troughs[i + 1];
            double l1 = candles[t1].low;
            double l2 = candles[t2].low;
            if (l1 <= 0.0)
                continue;

            bool sim_price = std::abs(l1 - l2) / l1 < config_.double_top_tol;
            bool gap_ok = (t2 - t1) >= config_.double_top_min_gap;

            if (sim_price && gap_ok)
            {
                out_start = t1;
                out_end = t2;
                return true;
            }
        }
        return false;
    }

    // ============================================================================
    // Continuation detectors
    // ============================================================================

    bool PatternScanner::detect_bull_flag(const std::vector<Candle> &candles,
                                          uint32_t &out_start, uint32_t &out_end) const
    {
        const uint32_t n = static_cast<uint32_t>(candles.size());
        if (n < config_.flag_pole_len + 2)
            return false;

        for (uint32_t ps = 0; ps + config_.flag_pole_len + 2 <= n; ++ps)
        {
            double pole_open = candles[ps].open;
            double pole_close = candles[ps + config_.flag_pole_len - 1].close;
            if (pole_open <= 0.0)
                continue;
            double gain = (pole_close - pole_open) / pole_open;
            if (gain < config_.flag_pole_gain)
                continue;

            uint32_t flag_start = ps + config_.flag_pole_len;
            uint32_t flag_end = std::min(flag_start + config_.flag_max_len, n - 1);

            double lo = candles[flag_start].low;
            double hi = candles[flag_start].high;
            for (uint32_t j = flag_start + 1; j <= flag_end; ++j)
            {
                if (candles[j].low < lo)
                    lo = candles[j].low;
                if (candles[j].high > hi)
                    hi = candles[j].high;
            }
            if (lo <= 0.0)
                continue;

            if ((hi - lo) / lo <= config_.flag_range_max)
            {
                out_start = ps;
                out_end = flag_end;
                return true;
            }
        }
        return false;
    }

    bool PatternScanner::detect_bear_flag(const std::vector<Candle> &candles,
                                          uint32_t &out_start, uint32_t &out_end) const
    {
        const uint32_t n = static_cast<uint32_t>(candles.size());
        if (n < config_.flag_pole_len + 2)
            return false;

        for (uint32_t ps = 0; ps + config_.flag_pole_len + 2 <= n; ++ps)
        {
            double pole_open = candles[ps].open;
            double pole_close = candles[ps + config_.flag_pole_len - 1].close;
            if (pole_open <= 0.0)
                continue;
            double drop = (pole_open - pole_close) / pole_open;
            if (drop < config_.flag_pole_gain)
                continue;

            uint32_t flag_start = ps + config_.flag_pole_len;
            uint32_t flag_end = std::min(flag_start + config_.flag_max_len, n - 1);

            double lo = candles[flag_start].low;
            double hi = candles[flag_start].high;
            for (uint32_t j = flag_start + 1; j <= flag_end; ++j)
            {
                if (candles[j].low < lo)
                    lo = candles[j].low;
                if (candles[j].high > hi)
                    hi = candles[j].high;
            }
            if (lo <= 0.0)
                continue;

            if ((hi - lo) / lo <= config_.flag_range_max)
            {
                out_start = ps;
                out_end = flag_end;
                return true;
            }
        }
        return false;
    }

    // ============================================================================
    // Candlestick detectors
    // ============================================================================

    bool PatternScanner::detect_doji(const Candle &c) const
    {
        double r = c.range();
        if (r <= 0.0)
            return false;
        return (c.body_size() / r) <= config_.doji_threshold;
    }

    bool PatternScanner::detect_hammer(const Candle &c) const
    {
        double r = c.range();
        if (r <= 0.0)
            return false;

        double body = c.body_size();
        double lwik = c.lower_wick();
        double uwik = c.upper_wick();

        // Use max(body, 1% of range) as reference so doji-hammers (body≈0)
        // are still classified correctly.
        double ref = std::max(body, r * 0.01);

        return (body / r < 0.35) &&
               (lwik >= config_.wick_body_ratio * ref) &&
               (uwik < r * 0.15); // upper wick < 15 % of range
    }

    bool PatternScanner::detect_shooting_star(const Candle &c) const
    {
        double r = c.range();
        if (r <= 0.0)
            return false;

        double body = c.body_size();
        double lwik = c.lower_wick();
        double uwik = c.upper_wick();

        double ref = std::max(body, r * 0.01);

        return (body / r < 0.35) &&
               (uwik >= config_.wick_body_ratio * ref) &&
               (lwik < r * 0.15); // lower wick < 15 % of range
    }

    bool PatternScanner::detect_bullish_engulfing(const Candle &prev,
                                                  const Candle &curr) const
    {
        if (!prev.is_bearish() || !curr.is_bullish())
            return false;
        if (curr.body_size() < config_.engulfing_min_body)
            return false;
        return (curr.open <= prev.close) && (curr.close >= prev.open);
    }

    bool PatternScanner::detect_bearish_engulfing(const Candle &prev,
                                                  const Candle &curr) const
    {
        if (!prev.is_bullish() || !curr.is_bearish())
            return false;
        if (curr.body_size() < config_.engulfing_min_body)
            return false;
        return (curr.open >= prev.close) && (curr.close <= prev.open);
    }

    bool PatternScanner::detect_morning_star(const Candle &c1,
                                             const Candle &c2,
                                             const Candle &c3) const
    {
        bool c1_bear = c1.is_bearish() && (c1.body_size() / std::max(c1.range(), 0.001) > 0.5);
        bool c2_small = c2.body_size() < std::max(c1.body_size(), c3.body_size()) * 0.4;
        bool c3_bull = c3.is_bullish() && (c3.body_size() / std::max(c3.range(), 0.001) > 0.5);
        bool recovery = c3.close > (c1.open + c1.close) * 0.5;
        return c1_bear && c2_small && c3_bull && recovery;
    }

    bool PatternScanner::detect_evening_star(const Candle &c1,
                                             const Candle &c2,
                                             const Candle &c3) const
    {
        bool c1_bull = c1.is_bullish() && (c1.body_size() / std::max(c1.range(), 0.001) > 0.5);
        bool c2_small = c2.body_size() < std::max(c1.body_size(), c3.body_size()) * 0.4;
        bool c3_bear = c3.is_bearish() && (c3.body_size() / std::max(c3.range(), 0.001) > 0.5);
        bool decline = c3.close < (c1.open + c1.close) * 0.5;
        return c1_bull && c2_small && c3_bear && decline;
    }

    // ============================================================================
    // Gap & crash detectors
    // ============================================================================

    bool PatternScanner::detect_gap_up(const Candle &prev, const Candle &curr) const
    {
        if (prev.high <= 0.0)
            return false;
        return curr.open > prev.high * (1.0 + config_.gap_threshold);
    }

    bool PatternScanner::detect_gap_down(const Candle &prev, const Candle &curr) const
    {
        if (prev.low <= 0.0)
            return false;
        return curr.open < prev.low * (1.0 - config_.gap_threshold);
    }

    bool PatternScanner::detect_flash_crash(const std::vector<Candle> &candles,
                                            uint32_t &out_start,
                                            uint32_t &out_end) const
    {
        const uint32_t n = static_cast<uint32_t>(candles.size());
        if (n < 2)
            return false;

        for (uint32_t i = 0; i < n; ++i)
        {
            uint32_t end = std::min(i + config_.flash_crash_max_c, n - 1);
            double hi = candles[i].high;
            double lo = candles[i].low;

            for (uint32_t j = i + 1; j <= end; ++j)
                if (candles[j].low < lo)
                    lo = candles[j].low;

            if (hi <= 0.0)
                continue;
            if ((hi - lo) / hi >= config_.flash_crash_drop)
            {
                out_start = i;
                out_end = end;
                return true;
            }
        }
        return false;
    }

    // ============================================================================
    // Helpers
    // ============================================================================

    std::vector<uint32_t> PatternScanner::find_local_maxima(
        const std::vector<Candle> &candles, uint32_t window) const
    {
        std::vector<uint32_t> peaks;
        const uint32_t n = static_cast<uint32_t>(candles.size());

        for (uint32_t i = window; i + window < n; ++i)
        {
            bool is_peak = true;
            for (uint32_t j = i - window; j <= i + window; ++j)
            {
                if (j != i && candles[j].high >= candles[i].high)
                {
                    is_peak = false;
                    break;
                }
            }
            if (is_peak)
                peaks.push_back(i);
        }
        return peaks;
    }

    std::vector<uint32_t> PatternScanner::find_local_minima(
        const std::vector<Candle> &candles, uint32_t window) const
    {
        std::vector<uint32_t> troughs;
        const uint32_t n = static_cast<uint32_t>(candles.size());

        for (uint32_t i = window; i + window < n; ++i)
        {
            bool is_trough = true;
            for (uint32_t j = i - window; j <= i + window; ++j)
            {
                if (j != i && candles[j].low <= candles[i].low)
                {
                    is_trough = false;
                    break;
                }
            }
            if (is_trough)
                troughs.push_back(i);
        }
        return troughs;
    }

    std::vector<double> PatternScanner::calculate_sma(const std::vector<Candle> &candles,
                                                      uint32_t period) const
    {
        std::vector<double> sma;
        sma.reserve(candles.size());

        double sum = 0.0;
        for (uint32_t i = 0; i < static_cast<uint32_t>(candles.size()); ++i)
        {
            sum += candles[i].close;
            if (i >= period)
                sum -= candles[i - period].close;
            double cnt = static_cast<double>(std::min(i + 1, period));
            sma.push_back(sum / cnt);
        }
        return sma;
    }

} // namespace lob::analytics
