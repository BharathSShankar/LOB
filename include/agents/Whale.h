#pragma once

#include "Agent.h"
#include <random>

namespace lob
{
    namespace agents
    {

        /**
         * @brief Whale agent - large trader with significant market impact
         *
         * Whales represent large institutional traders or wealthy individuals
         * who execute significant orders that can move the market. They can
         * trigger flash crashes or create rapid price movements.
         *
         * Characteristics:
         * - Triggered at specific tick or by price conditions
         * - Can execute massive market orders (flash crash scenario)
         * - Can split orders into slices (iceberg orders)
         * - Can execute TWAP (Time-Weighted Average Price) strategy
         * - Different execution modes: instant, gradual, hidden
         *
         * Configuration Parameters:
         * - "trigger_tick": Tick number when whale activates (default: 0 = immediate)
         * - "whale_side": Order side (0 = BUY, 1 = SELL) (default: 1 = SELL)
         * - "whale_size": Total order size (default: 10000)
         * - "execution_mode": 0 = instant, 1 = iceberg, 2 = TWAP (default: 0)
         * - "slice_size": Size of each slice for iceberg/TWAP (default: 100)
         * - "slices_interval": Ticks between slices for TWAP (default: 10)
         * - "price_trigger": Optional price level to trigger whale (default: 0.0 = disabled)
         * - "trigger_above": Trigger when price goes above price_trigger (default: 1.0)
         */
        class Whale : public Agent
        {
        public:
            /**
             * @brief Execution modes for whale orders
             */
            enum class ExecutionMode : uint8_t
            {
                INSTANT = 0, ///< Execute entire order immediately (flash crash)
                ICEBERG = 1, ///< Split order into slices, execute as fast as possible
                TWAP = 2     ///< Time-Weighted Average Price - spread over time
            };

            Whale();
            ~Whale() override = default;

            // Agent interface implementation
            void tick(const MarketState &state) override;
            core::Order *decide(const MarketState &state) override;
            void initialize(uint64_t agent_id, const AgentConfig &config) override;
            void reset() override;

            /**
             * @brief Check if whale has been triggered
             * @return true if whale has activated
             */
            bool is_triggered() const { return triggered_; }

            /**
             * @brief Check if whale has completed execution
             * @return true if all orders executed
             */
            bool is_complete() const { return remaining_quantity_ == 0; }

            /**
             * @brief Get remaining quantity to execute
             * @return Remaining quantity
             */
            uint32_t get_remaining_quantity() const { return remaining_quantity_; }

        private:
            /**
             * @brief Check if trigger condition is met
             * @param state Current market state
             * @return true if whale should activate
             */
            bool check_trigger_condition(const MarketState &state);

            /**
             * @brief Check if price trigger condition is met
             * @param state Current market state
             * @return true if price condition satisfied
             */
            bool check_price_trigger(const MarketState &state) const;

            /**
             * @brief Calculate next slice size for execution
             * @return Slice size
             */
            uint32_t calculate_slice_size();

            /**
             * @brief Check if ready for next slice (for TWAP)
             * @return true if interval elapsed
             */
            bool is_ready_for_next_slice() const;

            // Random number generation (for some randomness in slice sizes)
            std::mt19937_64 rng_;                            ///< Random number generator
            std::uniform_real_distribution<double> uniform_; ///< Uniform distribution

            // Trigger parameters
            uint64_t trigger_tick_; ///< Tick when whale should activate
            double price_trigger_;  ///< Price level trigger (0 = disabled)
            bool trigger_above_;    ///< Trigger when price goes above (true) or below (false)
            bool triggered_;        ///< Whether whale has been triggered

            // Order parameters
            core::Side whale_side_;        ///< Direction of whale order
            uint32_t whale_size_;          ///< Total order size
            uint32_t remaining_quantity_;  ///< Quantity remaining to execute
            ExecutionMode execution_mode_; ///< How to execute the order

            // Execution state
            uint32_t slice_size_;      ///< Size of each slice
            uint32_t slices_interval_; ///< Ticks between slices for TWAP
            uint64_t last_slice_tick_; ///< Tick when last slice was executed
            uint32_t slices_executed_; ///< Number of slices executed so far

            // Internal state
            uint64_t tick_count_; ///< Number of ticks since initialization
        };

    } // namespace agents
} // namespace lob
