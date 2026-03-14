#pragma once

#include "Agent.h"
#include <deque>
#include <random>

namespace lob
{
    namespace agents
    {

        /**
         * @brief Mean Reverter agent - bets on price returning to fair value
         *
         * Mean reverters are contrarian traders who believe prices oscillate around
         * a fair value and capitalize on deviations. They buy when prices are low
         * and sell when prices are high relative to their estimate of fair value.
         *
         * Characteristics:
         * - Define fair value (configurable or based on SMA)
         * - Sell when price > upper band (overvalued)
         * - Buy when price < lower band (undervalued)
         * - Limit orders slightly inside market for better execution
         * - Position-aware to prevent excessive inventory
         * - Optional Bollinger Bands and RSI implementations
         *
         * Configuration Parameters:
         * - "fair_value": Fixed fair value price (default: 100.0)
         * - "threshold_pct": Percentage deviation threshold (default: 0.05 = 5%)
         * - "use_sma_fair_value": Use SMA as fair value instead of fixed (default: 0.0 = false)
         * - "bollinger_period": Period for Bollinger Bands (default: 20)
         * - "bollinger_std_dev": Number of standard deviations for bands (default: 2.0)
         * - "use_rsi": Use RSI instead of fair value (default: 0.0 = false)
         * - "rsi_period": Period for RSI calculation (default: 14)
         * - "rsi_overbought": RSI overbought threshold (default: 70)
         * - "rsi_oversold": RSI oversold threshold (default: 30)
         */
        class MeanReverter : public Agent
        {
        public:
            MeanReverter();
            ~MeanReverter() override = default;

            // Agent interface implementation
            void tick(const MarketState &state) override;
            core::Order *decide(const MarketState &state) override;
            void initialize(uint64_t agent_id, const AgentConfig &config) override;
            void reset() override;

        private:
            /**
             * @brief Calculate current fair value
             * @param state Current market state
             * @return Estimated fair value
             */
            double calculate_fair_value(const MarketState &state) const;

            /**
             * @brief Calculate upper band (sell threshold)
             * @param fair_value Current fair value
             * @param state Current market state
             * @return Upper band price
             */
            double calculate_upper_band(double fair_value, const MarketState &state) const;

            /**
             * @brief Calculate lower band (buy threshold)
             * @param fair_value Current fair value
             * @param state Current market state
             * @return Lower band price
             */
            double calculate_lower_band(double fair_value, const MarketState &state) const;

            /**
             * @brief Calculate Bollinger Bands
             * @param sma Simple moving average
             * @param std_dev Standard deviation
             * @param upper Output: upper band
             * @param lower Output: lower band
             */
            void calculate_bollinger_bands(double sma, double std_dev, double &upper, double &lower) const;

            /**
             * @brief Calculate RSI (Relative Strength Index)
             * @return RSI value (0-100)
             */
            double calculate_rsi() const;

            /**
             * @brief Calculate deviation from fair value
             * @param price Current price
             * @param fair_value Fair value
             * @return Deviation as percentage
             */
            double calculate_deviation(double price, double fair_value) const;

            /**
             * @brief Calculate order size based on deviation
             * @param deviation Deviation from fair value
             * @return Order quantity
             */
            uint32_t calculate_order_size(double deviation) const;

            /**
             * @brief Check if position is near limit
             * @return true if position exceeds threshold
             */
            bool is_position_near_limit() const;

            /**
             * @brief Calculate standard deviation of recent prices
             * @return Standard deviation
             */
            double calculate_price_stddev() const;

            // Random number generation (for slight price variations)
            std::mt19937_64 rng_;                            ///< Random number generator
            std::uniform_real_distribution<double> uniform_; ///< Uniform distribution

            // Internal state
            uint64_t tick_count_;       ///< Number of ticks since initialization
            double fair_value_;         ///< Fixed fair value (if not using SMA)
            double threshold_pct_;      ///< Threshold percentage for bands
            bool use_sma_fair_value_;   ///< Use SMA as fair value
            double position_threshold_; ///< Position threshold (default 0.8)

            // Bollinger Bands parameters
            uint32_t bollinger_period_; ///< Period for Bollinger calculation
            double bollinger_std_dev_;  ///< Standard deviations for bands

            // RSI parameters
            bool use_rsi_;          ///< Use RSI instead of fair value
            uint32_t rsi_period_;   ///< Period for RSI calculation
            double rsi_overbought_; ///< RSI overbought threshold
            double rsi_oversold_;   ///< RSI oversold threshold

            // Price history for calculations
            std::deque<double> price_history_;              ///< Recent price history
            std::deque<double> price_changes_;              ///< Recent price changes for RSI
            static constexpr size_t MAX_HISTORY_SIZE = 250; ///< Maximum history size
        };

    } // namespace agents
} // namespace lob
