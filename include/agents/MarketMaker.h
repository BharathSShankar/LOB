#pragma once

#include "Agent.h"
#include <random>

namespace lob
{
    namespace agents
    {

        /**
         * @brief Market Maker agent - provides liquidity on both sides of the market
         *
         * Market makers continuously quote bid and ask prices to provide liquidity.
         * They profit from the bid-ask spread while managing inventory risk.
         *
         * Characteristics:
         * - Maintains spread around fair value (typically last price)
         * - Adjusts quotes based on inventory position (inventory skew)
         * - Uses limit orders on both sides
         * - Manages risk through position limits
         *
         * Strategy:
         * - When long inventory: widen ask, tighten bid (encourage selling)
         * - When short inventory: widen bid, tighten ask (encourage buying)
         * - Alternates between posting bid and ask orders
         *
         * Configuration Parameters:
         * - "spread_pct": Target spread as percentage of price (default: 0.001 = 0.1%)
         * - "base_quantity": Base order size (default: 100)
         * - "skew_factor": How much to skew quotes based on inventory (default: 1.0)
         * - "quote_frequency": Ticks between quotes (default: 1)
         */
        class MarketMaker : public Agent
        {
        public:
            MarketMaker();
            ~MarketMaker() override = default;

            // Agent interface implementation
            void tick(const MarketState &state) override;
            core::Order *decide(const MarketState &state) override;
            void initialize(uint64_t agent_id, const AgentConfig &config) override;
            void reset() override;

        private:
            /**
             * @brief Calculate inventory skew factor
             * @return Skew factor from -1.0 (max short) to +1.0 (max long)
             *
             * Used to adjust quote prices based on current inventory.
             * Positive skew (long inventory) widens ask and tightens bid.
             * Negative skew (short inventory) widens bid and tightens ask.
             */
            double calculate_inventory_skew() const;

            /**
             * @brief Calculate bid price based on fair value and inventory
             * @param fair_value Current fair value (typically last price)
             * @param target_spread Target spread width
             * @param skew Inventory skew factor
             * @return Bid price
             */
            double calculate_bid_price(double fair_value, double target_spread, double skew) const;

            /**
             * @brief Calculate ask price based on fair value and inventory
             * @param fair_value Current fair value (typically last price)
             * @param target_spread Target spread width
             * @param skew Inventory skew factor
             * @return Ask price
             */
            double calculate_ask_price(double fair_value, double target_spread, double skew) const;

            /**
             * @brief Check if should quote based on position limits and state
             * @return true if should continue quoting
             */
            bool should_quote() const;

            /**
             * @brief Calculate order quantity based on inventory and configuration
             * @return Order quantity
             */
            uint32_t calculate_order_quantity() const;

            /**
             * @brief Convert double price to fixed-point uint64_t
             * @param price Price in double format
             * @return Fixed-point price (multiplied by 100 for cents)
             */
            uint64_t price_to_fixed(double price) const;

            // Random number generation for tie-breaking
            std::mt19937_64 rng_;
            std::uniform_real_distribution<double> uniform_;

            // Configuration parameters
            double spread_pct_;        ///< Target spread as percentage
            uint32_t base_quantity_;   ///< Base order size
            double skew_factor_;       ///< Inventory skew multiplier
            uint32_t quote_frequency_; ///< Ticks between quotes

            // Internal state
            uint64_t tick_count_; ///< Number of ticks since initialization
            bool last_was_bid_;   ///< Tracks alternation between bid and ask quotes
        };

    } // namespace agents
} // namespace lob
