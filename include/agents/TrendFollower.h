#pragma once

#include "Agent.h"
#include <deque>
#include <random>

namespace lob
{
    namespace agents
    {

        /**
         * @brief Trend Follower agent - follows price momentum and trends
         *
         * Trend followers detect directional price movements and jump on board,
         * creating positive feedback loops. They use technical indicators like
         * moving averages and breakouts to identify trends.
         *
         * Characteristics:
         * - Golden Cross (SMA50 > SMA200) → Buy signal
         * - Death Cross (SMA50 < SMA200) → Sell signal
         * - Price breakouts above/below moving averages
         * - Market orders for immediate execution (momentum chasing)
         * - Position-aware to prevent excessive inventory
         * - Cooldown mechanism to prevent overtrading
         *
         * Configuration Parameters:
         * - "threshold_pct": Threshold percentage for trend detection (default: 0.02 = 2%)
         * - "cooldown_ticks": Number of ticks to wait after placing an order (default: 10)
         * - "momentum_scaling": Scale order size by momentum strength (default: 1.5)
         */
        class TrendFollower : public Agent
        {
        public:
            TrendFollower();
            ~TrendFollower() override = default;

            // Agent interface implementation
            void tick(const MarketState &state) override;
            core::Order *decide(const MarketState &state) override;
            void initialize(uint64_t agent_id, const AgentConfig &config) override;
            void reset() override;

        private:
            /**
             * @brief Detect if there's a golden cross (bullish signal)
             * @param state Current market state
             * @return true if SMA50 > SMA200 with threshold
             */
            bool detect_golden_cross(const MarketState &state) const;

            /**
             * @brief Detect if there's a death cross (bearish signal)
             * @param state Current market state
             * @return true if SMA50 < SMA200 with threshold
             */
            bool detect_death_cross(const MarketState &state) const;

            /**
             * @brief Detect if price breaks out above SMA
             * @param state Current market state
             * @return true if price > SMA50 with threshold
             */
            bool detect_breakout_up(const MarketState &state) const;

            /**
             * @brief Detect if price breaks down below SMA
             * @param state Current market state
             * @return true if price < SMA50 with threshold
             */
            bool detect_breakout_down(const MarketState &state) const;

            /**
             * @brief Calculate momentum strength
             * @param state Current market state
             * @return Momentum strength (0.0 - 1.0+)
             */
            double calculate_momentum_strength(const MarketState &state) const;

            /**
             * @brief Check if agent is in cooldown period
             * @return true if still in cooldown
             */
            bool is_in_cooldown() const;

            /**
             * @brief Calculate order size based on momentum and config
             * @param momentum_strength Current momentum strength
             * @return Order quantity
             */
            uint32_t calculate_order_size(double momentum_strength) const;

            /**
             * @brief Check if position is near limit
             * @return true if position exceeds threshold
             */
            bool is_position_near_limit() const;

            // Random number generation (for some randomness in execution)
            std::mt19937_64 rng_;                            ///< Random number generator
            std::uniform_real_distribution<double> uniform_; ///< Uniform distribution

            // Internal state
            uint64_t tick_count_;       ///< Number of ticks since initialization
            uint64_t last_order_tick_;  ///< Tick when last order was placed
            double threshold_pct_;      ///< Threshold percentage for trend detection
            uint32_t cooldown_ticks_;   ///< Cooldown period in ticks
            double momentum_scaling_;   ///< Scale factor for momentum-based sizing
            double position_threshold_; ///< Position threshold (default 0.8)

            // Price history for local calculations (if needed)
            std::deque<double> price_history_;              ///< Recent price history
            static constexpr size_t MAX_HISTORY_SIZE = 250; ///< Maximum history size
        };

    } // namespace agents
} // namespace lob
