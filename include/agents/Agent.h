#pragma once

#include "MarketState.h"
#include "../core/Order.h"
#include "../memory/ObjectPool.h"
#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <string>

namespace lob
{
    namespace agents
    {

        /**
         * @brief Types of trading agents in the market
         */
        enum class AgentType : uint8_t
        {
            NOISE_TRADER = 0,   ///< Random traders providing liquidity noise
            MARKET_MAKER = 1,   ///< Provides liquidity on both sides
            TREND_FOLLOWER = 2, ///< Follows price momentum
            MEAN_REVERTER = 3,  ///< Bets on price reversal
            WHALE = 4,          ///< Large trader with significant impact
            ARBITRAGEUR = 5     ///< Exploits price inefficiencies
        };

        /**
         * @brief Tracks an agent's inventory position
         */
        struct Position
        {
            int64_t quantity;      ///< Net position (positive = long, negative = short)
            double avg_price;      ///< Average entry price
            double realized_pnl;   ///< Realized profit/loss
            double unrealized_pnl; ///< Unrealized profit/loss

            Position()
                : quantity(0), avg_price(0.0), realized_pnl(0.0), unrealized_pnl(0.0)
            {
            }

            /**
             * @brief Update position after a trade
             * @param side Trade side (BUY or SELL)
             * @param qty Trade quantity
             * @param price Trade price
             */
            void update(core::Side side, uint64_t qty, double price);

            /**
             * @brief Calculate unrealized PnL at current market price
             * @param current_price Current market price
             */
            void mark_to_market(double current_price);
        };

        /**
         * @brief Configuration parameters for an agent
         */
        struct AgentConfig
        {
            AgentType type;           ///< Agent type
            double aggression;        ///< Trading aggression level [0.0 - 1.0]
            double risk_tolerance;    ///< Risk tolerance level [0.0 - 1.0]
            uint32_t max_position;    ///< Maximum inventory limit
            double order_size_mean;   ///< Mean order size
            double order_size_stddev; ///< Standard deviation of order size

            /// Type-specific parameters stored as key-value pairs
            std::unordered_map<std::string, double> params;

            AgentConfig()
                : type(AgentType::NOISE_TRADER), aggression(0.5), risk_tolerance(0.5), max_position(1000), order_size_mean(100.0), order_size_stddev(20.0)
            {
            }
        };

        /**
         * @brief Abstract base class for all trading agents
         *
         * This class defines the interface that all trading agents must implement.
         * Agents observe market state and make trading decisions by generating orders.
         *
         * Key Responsibilities:
         * - Observe market conditions via MarketState
         * - Make trading decisions and generate orders
         * - Track inventory position and risk limits
         * - Manage agent lifecycle (initialization, reset)
         */
        class Agent
        {
        public:
            virtual ~Agent() = default;

            /**
             * @brief Update agent state based on market conditions
             * @param state Current market state
             *
             * Called every simulation tick to update internal state,
             * technical indicators, or strategies.
             */
            virtual void tick(const MarketState &state) = 0;

            /**
             * @brief Make a trading decision
             * @param state Current market state
             * @return Pointer to order if agent wants to trade, nullptr otherwise
             *
             * Core decision-making method. Agent analyzes market state and
             * returns an order if it wants to trade, or nullptr to skip this tick.
             */
            virtual core::Order *decide(const MarketState &state) = 0;

            /**
             * @brief Initialize the agent
             * @param agent_id Unique identifier for this agent
             * @param config Configuration parameters
             */
            virtual void initialize(uint64_t agent_id, const AgentConfig &config) = 0;

            /**
             * @brief Reset agent to initial state
             *
             * Clears positions, resets internal state, preserves configuration.
             */
            virtual void reset() = 0;

            /**
             * @brief Check if agent is active
             * @return true if agent can trade, false otherwise
             */
            bool is_active() const { return active_; }

            /**
             * @brief Get agent type
             * @return Agent's type
             */
            AgentType get_type() const { return type_; }

            /**
             * @brief Get agent ID
             * @return Unique agent identifier
             */
            uint64_t get_id() const { return agent_id_; }

            /**
             * @brief Get current position
             * @return Agent's current position
             */
            const Position &get_position() const { return position_; }

            /**
             * @brief Deactivate agent
             *
             * Prevents agent from trading. Used for risk management
             * or when position limits are exceeded.
             */
            void deactivate() { active_ = false; }

            /**
             * @brief Reactivate agent
             */
            void activate() { active_ = true; }

            /**
             * @brief Wire the shared order-pool into the agent.
             *
             * Called by AgentOrchestrator before every tick.  The pool
             * must outlive the agent; no ownership is transferred.
             */
            void set_order_pool(memory::ObjectPool<core::Order, 10000> *pool)
            {
                order_pool_ = pool;
            }

        protected:
            uint64_t agent_id_{0}; ///< Unique agent identifier
            AgentType type_;       ///< Agent type
            bool active_{true};    ///< Whether agent can trade
            Position position_;    ///< Current inventory position
            AgentConfig config_;   ///< Agent configuration

            /// Pointer to the shared order pool (set by orchestrator)
            memory::ObjectPool<core::Order, 10000> *order_pool_{nullptr};

            /**
             * @brief Allocate one Order from the shared pool.
             * @return Pointer to a fresh Order, or nullptr if pool is exhausted
             *         or has not been set yet.
             */
            core::Order *alloc_order() const noexcept
            {
                return order_pool_ ? order_pool_->acquire() : nullptr;
            }

            /**
             * @brief Global atomic counter for unique per-order IDs.
             *
             * Seeded at 1 000 000 so simulation seed orders (1-30) don't
             * collide with agent-generated orders.
             */
            static std::atomic<uint64_t> &order_id_counter()
            {
                static std::atomic<uint64_t> ctr{1'000'000};
                return ctr;
            }
        };

    } // namespace agents
} // namespace lob
