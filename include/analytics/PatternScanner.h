#pragma once

#include "analytics/OHLCVAggregator.h"
#include <string>
#include <vector>

namespace lob::analytics
{

    // ============================================================================
    // PatternType – all supported TA patterns
    // ============================================================================

    /**
     * @brief Enumeration of technical analysis patterns
     */
    enum class PatternType : uint8_t
    {
        // ── Trend Patterns ────────────────────────────────────────────────────
        UPTREND = 0,
        DOWNTREND,
        SIDEWAYS,

        // ── Reversal Patterns ─────────────────────────────────────────────────
        HEAD_AND_SHOULDERS,
        INVERSE_HEAD_AND_SHOULDERS,
        DOUBLE_TOP,
        DOUBLE_BOTTOM,

        // ── Continuation Patterns ─────────────────────────────────────────────
        BULL_FLAG,
        BEAR_FLAG,
        ASCENDING_TRIANGLE,
        DESCENDING_TRIANGLE,
        SYMMETRICAL_TRIANGLE,

        // ── Single/Multi-Candle Patterns ──────────────────────────────────────
        DOJI,
        HAMMER,
        SHOOTING_STAR,
        ENGULFING_BULLISH,
        ENGULFING_BEARISH,
        MORNING_STAR,
        EVENING_STAR,

        // ── Gap & Crash Patterns ──────────────────────────────────────────────
        GAP_UP,
        GAP_DOWN,
        FLASH_CRASH
    };

    /**
     * @brief Convert PatternType to a human-readable string
     */
    std::string to_string(PatternType type);

    // ============================================================================
    // PatternMatch – result of a pattern detection
    // ============================================================================

    /**
     * @brief A single detected pattern within a candle series
     */
    struct PatternMatch
    {
        PatternType pattern;     ///< Pattern type
        uint32_t start_index;    ///< First candle index of the pattern (in input vector)
        uint32_t end_index;      ///< Last candle index (inclusive)
        double confidence;       ///< Detection confidence [0.0 – 1.0]
        std::string description; ///< Human-readable description
        bool is_bullish;         ///< true = bullish signal, false = bearish / neutral
    };

    // ============================================================================
    // PatternScanner
    // ============================================================================

    /**
     * @brief Technical Analysis Pattern Scanner
     *
     * Detects a comprehensive set of chart patterns from OHLCV candlestick data.
     *
     * Supported groups:
     *  - Trend: uptrend, downtrend, sideways
     *  - Reversal: head & shoulders, double top/bottom
     *  - Continuation: bull/bear flag
     *  - Candlestick: doji, hammer, shooting star, engulfing, morning/evening star
     *  - Gap & Crash: gap up/down, flash crash
     *
     * Usage:
     * @code
     *   PatternScanner scanner;
     *   auto candles = aggregator.get_candles(200);
     *   auto patterns = scanner.scan(candles);
     *   for (auto& p : patterns)
     *       std::cout << to_string(p.pattern) << " conf=" << p.confidence << '\n';
     * @endcode
     */
    class PatternScanner
    {
    public:
        // ── Configuration ─────────────────────────────────────────────────────

        /**
         * @brief Tunable parameters for pattern detection
         */
        struct Config
        {
            uint32_t min_trend_candles = 5; ///< Minimum consecutive candles to confirm trend
            double trend_threshold = 0.001; ///< Minimum per-candle move (0.1%) for trend
            double sideways_range = 0.005;  ///< Maximum total range (0.5%) for sideways

            double doji_threshold = 0.10;      ///< Max body/range ratio for a doji
            double wick_body_ratio = 2.0;      ///< Min wick/body ratio for hammer/star
            double engulfing_min_body = 0.001; ///< Minimum body size to qualify as engulfing

            double hs_shoulder_tol = 0.05;   ///< Allowed height difference between H&S shoulders (5%)
            double double_top_tol = 0.03;    ///< Allowed height difference between double-top peaks (3%)
            uint32_t double_top_min_gap = 2; ///< Minimum candles between peaks

            double flag_pole_gain = 0.03; ///< Minimum flagpole move (3%)
            double flag_range_max = 0.03; ///< Maximum consolidation range (3%)
            uint32_t flag_pole_len = 4;   ///< Number of candles in flagpole
            uint32_t flag_max_len = 5;    ///< Maximum candles in flag

            double flash_crash_drop = 0.10; ///< Minimum drop (10%) for flash crash
            uint32_t flash_crash_max_c = 3; ///< Maximum candles over which crash occurs

            double gap_threshold = 0.002; ///< Minimum gap size (0.2% of price)
        };

        /** Construct with default detection thresholds */
        PatternScanner() noexcept;

        /** Construct with custom detection thresholds */
        explicit PatternScanner(const Config &config) noexcept;

        // ── Main Entry Point ──────────────────────────────────────────────────

        /**
         * @brief Scan an OHLCV series for all supported patterns
         * @param candles Candles from oldest (index 0) to newest
         * @return All detected patterns, may be empty
         */
        std::vector<PatternMatch> scan(const std::vector<Candle> &candles);

        // ── Trend Detectors ───────────────────────────────────────────────────

        /** Detect UPTREND: N consecutive higher closes */
        bool detect_uptrend(const std::vector<Candle> &candles,
                            uint32_t start = 0) const;

        /** Detect DOWNTREND: N consecutive lower closes */
        bool detect_downtrend(const std::vector<Candle> &candles,
                              uint32_t start = 0) const;

        /** Detect SIDEWAYS: total close range within threshold */
        bool detect_sideways(const std::vector<Candle> &candles,
                             uint32_t start = 0) const;

        // ── Reversal Detectors ────────────────────────────────────────────────

        /** Head & Shoulders: 3 peaks, middle highest, shoulders similar */
        bool detect_head_and_shoulders(const std::vector<Candle> &candles,
                                       uint32_t &out_start, uint32_t &out_end) const;

        /** Inverse H&S: 3 troughs, middle lowest, shoulders similar */
        bool detect_inverse_head_and_shoulders(const std::vector<Candle> &candles,
                                               uint32_t &out_start, uint32_t &out_end) const;

        /** Double Top: two peaks at similar price with a valley in between */
        bool detect_double_top(const std::vector<Candle> &candles,
                               uint32_t &out_start, uint32_t &out_end) const;

        /** Double Bottom: two troughs at similar price with a peak in between */
        bool detect_double_bottom(const std::vector<Candle> &candles,
                                  uint32_t &out_start, uint32_t &out_end) const;

        // ── Continuation Detectors ────────────────────────────────────────────

        /** Bull Flag: strong up-move followed by tight sideways consolidation */
        bool detect_bull_flag(const std::vector<Candle> &candles,
                              uint32_t &out_start, uint32_t &out_end) const;

        /** Bear Flag: strong down-move followed by tight sideways consolidation */
        bool detect_bear_flag(const std::vector<Candle> &candles,
                              uint32_t &out_start, uint32_t &out_end) const;

        // ── Candlestick Detectors ─────────────────────────────────────────────

        /** Doji: body < doji_threshold * range */
        bool detect_doji(const Candle &c) const;

        /** Hammer: small body, long lower wick, tiny upper wick */
        bool detect_hammer(const Candle &c) const;

        /** Shooting Star: small body, long upper wick, tiny lower wick */
        bool detect_shooting_star(const Candle &c) const;

        /** Bullish Engulfing: bearish candle followed by larger bullish candle */
        bool detect_bullish_engulfing(const Candle &prev, const Candle &curr) const;

        /** Bearish Engulfing: bullish candle followed by larger bearish candle */
        bool detect_bearish_engulfing(const Candle &prev, const Candle &curr) const;

        /** Morning Star: bearish, small body, then strong bullish */
        bool detect_morning_star(const Candle &c1, const Candle &c2, const Candle &c3) const;

        /** Evening Star: bullish, small body, then strong bearish */
        bool detect_evening_star(const Candle &c1, const Candle &c2, const Candle &c3) const;

        // ── Gap & Crash Detectors ─────────────────────────────────────────────

        /** Gap Up: current open > previous high by threshold */
        bool detect_gap_up(const Candle &prev, const Candle &curr) const;

        /** Gap Down: current open < previous low by threshold */
        bool detect_gap_down(const Candle &prev, const Candle &curr) const;

        /** Flash Crash: price drops flash_crash_drop% within flash_crash_max_c candles */
        bool detect_flash_crash(const std::vector<Candle> &candles,
                                uint32_t &out_start, uint32_t &out_end) const;

        // ── Utilities ─────────────────────────────────────────────────────────

        /** Find indices of local price maxima (peaks) in the close series */
        std::vector<uint32_t> find_local_maxima(const std::vector<Candle> &candles,
                                                uint32_t window = 2) const;

        /** Find indices of local price minima (troughs) in the close series */
        std::vector<uint32_t> find_local_minima(const std::vector<Candle> &candles,
                                                uint32_t window = 2) const;

        /** Calculate Simple Moving Average of close prices */
        std::vector<double> calculate_sma(const std::vector<Candle> &candles,
                                          uint32_t period) const;

    private:
        Config config_;
    };

} // namespace lob::analytics
