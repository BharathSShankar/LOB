#pragma once

#include "Agent.h"
#include <random>

namespace lob
{
    namespace agents
    {

        /**
         * @brief Noise Trader agent - provides liquidity through random trading
         *
         * Noise traders generate random orders around the current market price,
         * mimicking uninformed traders who trade for reasons other than information
         * (e.g., liquidity needs, portfolio rebalancing).
         *
         * Characteristics:
         * - Random walk around last price
         * - Random side selection (buy/sell)
         * - Lognormal distributed order sizes
         * - Position-aware to prevent excessive inventory buildup
         *
         * Configuration Parameters:
         * - "noise_stddev": Standard deviation for price noise (default: 0.01)
         * - "position_threshold": Threshold for position reduction (default: 0.8)
         */
        class NoiseTrader : public Agent
        {
        public:
            NoiseTrader();
            ~NoiseTrader() override = default;

            // Agent interface implementation
            void tick(const MarketState &state) override;
            core::Order *decide(const MarketState &state) override;
            void initialize(uint64_t agent_id, const AgentConfig &config) override;
            void reset() override;

        private:
            /**
             * @brief Generate a random price around the market price
             * @param market_price Current market price
             * @return Random price with noise
             */
            double generate_random_price(double market_price);

            /**
             * @brief Generate a random order quantity
             * @return Random quantity using lognormal distribution
             */
            uint32_t generate_random_quantity();

            /**
             * @brief Check if position is near limit
             * @return true if position exceeds threshold
             */
            bool is_position_near_limit() const;

            /**
             * @brief Convert double price to fixed-point uint64_t
             * @param price Price in double format
             * @return Fixed-point price (multiplied by 100 for cents)
             */
            uint64_t price_to_fixed(double price) const;

            // Random number generation
            std::mt19937_64 rng_;                            ///< Random number generator
            std::normal_distribution<double> normal_;        ///< Normal distribution for price noise
            std::uniform_real_distribution<double> uniform_; ///< Uniform distribution for side selection
            std::lognormal_distribution<double> lognormal_;  ///< Lognormal distribution for order size

            // Internal state
            uint64_t tick_count_;       ///< Number of ticks since initialization
            double noise_stddev_;       ///< Standard deviation for price noise
            double position_threshold_; ///< Position threshold for reducing trading
        };

    } // namespace agents
} // namespace lob
