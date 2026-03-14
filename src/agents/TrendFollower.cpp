#include "agents/TrendFollower.h"
#include <chrono>
#include <cmath>
#include <algorithm>

namespace lob
{
    namespace agents
    {

        TrendFollower::TrendFollower()
            : tick_count_(0), last_order_tick_(0), threshold_pct_(0.02), cooldown_ticks_(10), momentum_scaling_(1.5), position_threshold_(0.8)
        {
            type_ = AgentType::TREND_FOLLOWER;

            // Initialize RNG with a unique seed
            auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            rng_.seed(seed);

            // Initialize distributions
            uniform_ = std::uniform_real_distribution<double>(0.0, 1.0);
        }

        void TrendFollower::tick(const MarketState &state)
        {
            tick_count_++;

            // Update price history
            if (state.last_price > 0.0)
            {
                price_history_.push_back(state.last_price);

                // Maintain maximum history size
                if (price_history_.size() > MAX_HISTORY_SIZE)
                {
                    price_history_.pop_front();
                }
            }

            // Update unrealized PnL with current market price
            position_.mark_to_market(state.last_price);

            // Check position limits and deactivate if exceeded
            if (std::abs(position_.quantity) > static_cast<int64_t>(config_.max_position))
            {
                deactivate();
            }
        }

        core::Order *TrendFollower::decide(const MarketState &state)
        {
            // Don't trade if inactive or market price is invalid
            if (!is_active() || state.last_price <= 0.0)
            {
                return nullptr;
            }

            // Don't trade if still in cooldown period
            if (is_in_cooldown())
            {
                return nullptr;
            }

            // Don't trade if SMAs are not available (not enough history)
            if (state.price_sma_50 <= 0.0 || state.price_sma_200 <= 0.0)
            {
                return nullptr;
            }

            // Detect trend signals
            bool golden_cross = detect_golden_cross(state);
            bool death_cross = detect_death_cross(state);
            bool breakout_up = detect_breakout_up(state);
            bool breakout_down = detect_breakout_down(state);

            // Calculate momentum strength for position sizing
            double momentum_strength = calculate_momentum_strength(state);

            // Determine if we should trade
            bool should_buy = false;
            bool should_sell = false;
            double signal_strength = 0.0;

            if (golden_cross || breakout_up)
            {
                should_buy = true;
                signal_strength = momentum_strength;
            }
            else if (death_cross || breakout_down)
            {
                should_sell = true;
                signal_strength = momentum_strength;
            }
            else
            {
                // No signal
                return nullptr;
            }

            // Check position limits - reduce trading if near limit
            if (is_position_near_limit())
            {
                // If we're long and getting buy signal, skip
                if (should_buy && position_.quantity > 0)
                {
                    return nullptr;
                }
                // If we're short and getting sell signal, skip
                if (should_sell && position_.quantity < 0)
                {
                    return nullptr;
                }
            }

            // Additional filter: only trade with strong signals if position is significant
            if (std::abs(position_.quantity) > 0)
            {
                double position_ratio = static_cast<double>(std::abs(position_.quantity)) /
                                        static_cast<double>(config_.max_position);

                // Require stronger signal if we already have a position
                if (signal_strength < position_ratio * 0.5)
                {
                    return nullptr;
                }
            }

            // Calculate order size based on momentum
            uint32_t quantity = calculate_order_size(signal_strength);

            // Record that we placed an order (before pool allocation to avoid skipping)
            last_order_tick_ = tick_count_;

            // Allocate Order from the shared pool injected by the orchestrator
            core::Order *order = alloc_order();
            if (!order)
                return nullptr; // Pool exhausted

            // Trend followers use aggressive limit orders (effectively market orders)
            // Price is set aggressively to cross the spread and obtain immediate fills
            core::Side side = should_buy ? core::Side::BUY : core::Side::SELL;
            double price = should_buy
                               ? state.last_price * 1.01  // 1% above market → hits asks
                               : state.last_price * 0.99; // 1% below market → hits bids

            uint64_t fixed_price = static_cast<uint64_t>(price * 100.0);
            if (fixed_price == 0)
                fixed_price = 1;

            *order = core::Order(order_id_counter()++, /*timestamp=*/0,
                                 fixed_price,
                                 static_cast<uint64_t>(quantity),
                                 side,
                                 core::OrderType::LIMIT);

            position_.update(side, quantity, price);
            return order;
        }

        void TrendFollower::initialize(uint64_t agent_id, const AgentConfig &config)
        {
            agent_id_ = agent_id;
            config_ = config;
            type_ = AgentType::TREND_FOLLOWER;
            active_ = true;

            // Extract trend follower specific parameters
            auto it = config.params.find("threshold_pct");
            if (it != config.params.end())
            {
                threshold_pct_ = it->second;
            }
            else
            {
                threshold_pct_ = 0.02; // Default 2% threshold
            }

            it = config.params.find("cooldown_ticks");
            if (it != config.params.end())
            {
                cooldown_ticks_ = static_cast<uint32_t>(it->second);
            }
            else
            {
                cooldown_ticks_ = 10; // Default 10 ticks cooldown
            }

            it = config.params.find("momentum_scaling");
            if (it != config.params.end())
            {
                momentum_scaling_ = it->second;
            }
            else
            {
                momentum_scaling_ = 1.5; // Default 1.5x scaling
            }

            it = config.params.find("position_threshold");
            if (it != config.params.end())
            {
                position_threshold_ = it->second;
            }
            else
            {
                position_threshold_ = 0.8; // Default 80% of max position
            }

            // Reseed RNG with agent_id for reproducibility while maintaining uniqueness
            rng_.seed(agent_id + std::chrono::high_resolution_clock::now().time_since_epoch().count());

            // Configure distributions
            uniform_ = std::uniform_real_distribution<double>(0.0, 1.0);

            // Reset state
            reset();
        }

        void TrendFollower::reset()
        {
            position_ = Position();
            tick_count_ = 0;
            last_order_tick_ = 0;
            active_ = true;
            price_history_.clear();
        }

        bool TrendFollower::detect_golden_cross(const MarketState &state) const
        {
            // Golden Cross: SMA50 > SMA200 (bullish)
            // We require a threshold to avoid false signals from noise
            return state.price_sma_50 > state.price_sma_200 * (1.0 + threshold_pct_);
        }

        bool TrendFollower::detect_death_cross(const MarketState &state) const
        {
            // Death Cross: SMA50 < SMA200 (bearish)
            return state.price_sma_50 < state.price_sma_200 * (1.0 - threshold_pct_);
        }

        bool TrendFollower::detect_breakout_up(const MarketState &state) const
        {
            // Price breaks above SMA50
            return state.last_price > state.price_sma_50 * (1.0 + threshold_pct_);
        }

        bool TrendFollower::detect_breakout_down(const MarketState &state) const
        {
            // Price breaks below SMA50
            return state.last_price < state.price_sma_50 * (1.0 - threshold_pct_);
        }

        double TrendFollower::calculate_momentum_strength(const MarketState &state) const
        {
            // Calculate momentum as the deviation from SMA
            // Stronger deviation = stronger momentum

            if (state.price_sma_50 <= 0.0)
            {
                return 0.0;
            }

            double deviation = std::abs(state.last_price - state.price_sma_50) / state.price_sma_50;

            // Normalize to [0, 1] range (cap at 10% deviation)
            double strength = std::min(deviation / 0.10, 1.0);

            return strength;
        }

        bool TrendFollower::is_in_cooldown() const
        {
            // Check if enough ticks have passed since last order
            return (tick_count_ - last_order_tick_) < cooldown_ticks_;
        }

        uint32_t TrendFollower::calculate_order_size(double momentum_strength) const
        {
            // Base order size from configuration
            double base_size = config_.order_size_mean;

            // Scale by momentum strength and aggression
            double size = base_size * config_.aggression * (1.0 + momentum_strength * (momentum_scaling_ - 1.0));

            // Clamp to reasonable range
            uint32_t quantity = static_cast<uint32_t>(std::max(1.0, std::min(size, 10000.0)));

            return quantity;
        }

        bool TrendFollower::is_position_near_limit() const
        {
            double position_ratio = static_cast<double>(std::abs(position_.quantity)) /
                                    static_cast<double>(config_.max_position);

            return position_ratio >= position_threshold_;
        }

    } // namespace agents
} // namespace lob
