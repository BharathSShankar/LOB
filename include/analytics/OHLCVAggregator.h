#pragma once

#include "core/OrderBook.h"
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace lob::analytics
{

    /**
     * @brief OHLCV Candlestick data for a single time period
     *
     * Represents a candlestick bar with Open, High, Low, Close prices
     * and Volume data. Used for technical analysis and pattern detection.
     */
    struct Candle
    {
        uint64_t timestamp;   ///< Start time of candle (nanoseconds, aligned to period)
        double open;          ///< Opening price
        double high;          ///< Highest price during period
        double low;           ///< Lowest price during period
        double close;         ///< Closing price
        uint64_t volume;      ///< Total traded volume (sum of trade quantities)
        uint32_t trade_count; ///< Number of individual trades

        Candle() noexcept;

        /**
         * @brief Size of the candle body (|close - open|)
         */
        double body_size() const noexcept { return close > open ? close - open : open - close; }

        /**
         * @brief Length of the upper wick (high - max(open, close))
         */
        double upper_wick() const noexcept
        {
            return high - (close > open ? close : open);
        }

        /**
         * @brief Length of the lower wick (min(open, close) - low)
         */
        double lower_wick() const noexcept
        {
            return (close < open ? close : open) - low;
        }

        /**
         * @brief Total candle range (high - low)
         */
        double range() const noexcept { return high - low; }

        /**
         * @brief True if close > open (bullish)
         */
        bool is_bullish() const noexcept { return close > open; }

        /**
         * @brief True if close < open (bearish)
         */
        bool is_bearish() const noexcept { return close < open; }

        /**
         * @brief True if body is < 10% of total range (indecision)
         */
        bool is_doji(double threshold = 0.10) const noexcept;
    };

    /**
     * @brief Aggregates trades from the matching engine into OHLCV candlestick data
     *
     * Receives individual trades and aggregates them into fixed-period
     * candlestick bars. Supports CSV export for analysis in external tools.
     *
     * Design:
     * - Prices are stored as uint64_t in the Trade struct (fixed-point integer).
     *   A price_scale divisor converts raw prices to double (e.g., 100 → cents to dollars).
     * - Candle timestamps are aligned to candle_period_ns boundaries.
     * - Stores up to MAX_CANDLES completed candles in a rolling deque.
     *
     * Thread Safety: NOT thread-safe. Call from the matching engine thread.
     *
     * Usage:
     * @code
     *   OHLCVAggregator agg(1000);  // 1-second candles
     *   auto trades = engine.process_order(order);
     *   for (auto& trade : trades)
     *       agg.add_trade(trade);
     *   auto candles = agg.get_candles(200);
     * @endcode
     */
    class OHLCVAggregator
    {
    public:
        /// Default scale: prices stored as integer * 100 (i.e., cents)
        static constexpr double DEFAULT_PRICE_SCALE = 100.0;

        /// Maximum completed candles retained in memory
        static constexpr size_t MAX_CANDLES = 10000;

        /**
         * @brief Construct OHLCVAggregator
         * @param candle_period_ms Duration of each candle period in milliseconds (default: 1000 = 1s)
         * @param price_scale  Divisor to convert raw integer price to double (default: 100.0)
         */
        explicit OHLCVAggregator(uint64_t candle_period_ms = 1000,
                                 double price_scale = DEFAULT_PRICE_SCALE) noexcept;

        /**
         * @brief Feed a completed trade into the aggregator
         * @param trade Trade data from the matching engine
         *
         * Automatically opens a new candle when the current period ends.
         */
        void add_trade(const lob::core::Trade &trade);

        /**
         * @brief Retrieve completed candles (oldest → newest)
         * @param max_count Maximum number of candles to return (returns the most recent ones)
         * @return Vector of completed candles
         */
        std::vector<Candle> get_candles(uint32_t max_count = 100) const;

        /**
         * @brief Get the candle currently being formed
         */
        Candle get_current_candle() const noexcept { return current_candle_; }

        /**
         * @brief Number of completed candles stored
         */
        size_t candle_count() const noexcept { return completed_candles_.size(); }

        /**
         * @brief Total trades processed since construction / last reset
         */
        uint64_t total_trade_count() const noexcept { return total_trades_; }

        /**
         * @brief Force-finalize the current candle (useful at end of simulation)
         */
        void flush();

        /**
         * @brief Clear all data, reset to initial state
         */
        void reset() noexcept;

        /**
         * @brief Export all completed candles to a CSV file
         * @param filename Output path
         * @return true on success, false on IO error
         *
         * CSV columns: timestamp,open,high,low,close,volume,trade_count
         */
        bool export_to_csv(const std::string &filename) const;

    private:
        void start_new_candle(uint64_t aligned_timestamp) noexcept;
        void finalize_current_candle();
        double to_price(uint64_t raw_price) const noexcept;
        uint64_t align_timestamp(uint64_t ts) const noexcept;

        uint64_t candle_period_ns_; ///< Period in nanoseconds
        double price_scale_;        ///< Divisor for integer price conversion

        Candle current_candle_;                ///< Forming candle
        std::deque<Candle> completed_candles_; ///< Rolling history of completed candles

        uint64_t total_trades_; ///< Cumulative trade count
    };

} // namespace lob::analytics
