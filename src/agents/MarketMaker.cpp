#include "agents/MarketMaker.h"
#include <chrono>
#include <cmath>
#include <algorithm>

namespace lob
{
    namespace agents
    {

        MarketMaker::MarketMaker()
            : spread_pct_(0.001), base_quantity_(100), skew_factor_(1.0),
              quote_frequency_(1), tick_count_(0), last_was_bid_(false)
        {
            type_ = AgentType::MARKET_MAKER;

            // Initialize RNG
            auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            rng_.seed(seed);
            uniform_ = std::uniform_real_distribution<double>(0.0, 1.0);
        }

        void MarketMaker::tick(const MarketState &state)
        {
            tick_count_++;

            // Update unrealized PnL with current market price
            position_.mark_to_market(state.last_price);

            // Check position limits
            if (std::abs(position_.quantity) > static_cast<int64_t>(config_.max_position))
            {
                // Don't deactivate completely, but stop quoting until position reduces
                // Market makers should stay active but modify behavior
            }
        }

        core::Order *MarketMaker::decide(const MarketState &state)
        {
            // Don't quote if inactive or market price is invalid
            if (!is_active() || state.last_price <= 0.0)
            {
                return nullptr;
            }

            // Check if we should quote this tick
            if (!should_quote())
            {
                return nullptr;
            }

            // Only quote every N ticks based on quote_frequency
            if (tick_count_ % quote_frequency_ != 0)
            {
                return nullptr;
            }

            // Calculate fair value (use last price as proxy)
            double fair_value = state.last_price;

            // Calculate target spread
            double target_spread = fair_value * spread_pct_;

            // Calculate inventory skew
            double skew = calculate_inventory_skew();

            // Decide whether to post bid or ask (alternate)
            core::Side side;
            double price;

            // If at position limit on one side, only quote the other side
            double position_ratio = static_cast<double>(position_.quantity) /
                                    static_cast<double>(config_.max_position);

            if (position_ratio >= 0.9)
            {
                // Very long - only post asks (sell)
                side = core::Side::SELL;
                price = calculate_ask_price(fair_value, target_spread, skew);
            }
            else if (position_ratio <= -0.9)
            {
                // Very short - only post bids (buy)
                side = core::Side::BUY;
                price = calculate_bid_price(fair_value, target_spread, skew);
            }
            else
            {
                // Normal two-sided quoting - alternate between bid and ask
                if (last_was_bid_)
                {
                    side = core::Side::SELL;
                    price = calculate_ask_price(fair_value, target_spread, skew);
                }
                else
                {
                    side = core::Side::BUY;
                    price = calculate_bid_price(fair_value, target_spread, skew);
                }

                // Toggle for next time
                last_was_bid_ = !last_was_bid_;
            }

            // Calculate order quantity
            uint32_t quantity = calculate_order_quantity();

            // Ensure quantity is at least 1
            if (quantity == 0)
            {
                quantity = 1;
            }

            // Ensure price is positive
            if (price <= 0.0)
            {
                return nullptr;
            }

            // Note: Order creation will be handled by ObjectPool in orchestrator
            // For now, return nullptr (same as NoiseTrader)
            return nullptr;
        }

        void MarketMaker::initialize(uint64_t agent_id, const AgentConfig &config)
        {
            agent_id_ = agent_id;
            config_ = config;
            type_ = AgentType::MARKET_MAKER;
            active_ = true;

            // Extract market maker specific parameters
            auto it = config.params.find("spread_pct");
            if (it != config.params.end())
            {
                spread_pct_ = it->second;
            }
            else
            {
                spread_pct_ = 0.001; // Default 0.1% spread
            }

            it = config.params.find("base_quantity");
            if (it != config.params.end())
            {
                base_quantity_ = static_cast<uint32_t>(it->second);
            }
            else
            {
                base_quantity_ = 100;
            }

            it = config.params.find("skew_factor");
            if (it != config.params.end())
            {
                skew_factor_ = it->second;
            }
            else
            {
                skew_factor_ = 1.0;
            }

            it = config.params.find("quote_frequency");
            if (it != config.params.end())
            {
                quote_frequency_ = static_cast<uint32_t>(it->second);
            }
            else
            {
                quote_frequency_ = 1; // Quote every tick
            }

            // Reseed RNG with agent_id for reproducibility
            rng_.seed(agent_id + std::chrono::high_resolution_clock::now().time_since_epoch().count());

            // Reset state
            reset();
        }

        void MarketMaker::reset()
        {
            position_ = Position();
            tick_count_ = 0;
            last_was_bid_ = false;
            active_ = true;
        }

        double MarketMaker::calculate_inventory_skew() const
        {
            if (config_.max_position == 0)
            {
                return 0.0;
            }

            // Calculate position as ratio of max position
            // Returns -1.0 (max short) to +1.0 (max long)
            double ratio = static_cast<double>(position_.quantity) /
                           static_cast<double>(config_.max_position);

            return std::clamp(ratio, -1.0, 1.0);
        }

        double MarketMaker::calculate_bid_price(double fair_value, double target_spread, double skew) const
        {
            // Base bid is fair_value - half_spread
            double half_spread = target_spread / 2.0;
            double base_bid = fair_value - half_spread;

            // If long inventory (positive skew), tighten bid to discourage buying
            // If short inventory (negative skew), widen bid to encourage buying
            double skew_adjustment = half_spread * skew * skew_factor_;

            double bid_price = base_bid - skew_adjustment;

            return bid_price;
        }

        double MarketMaker::calculate_ask_price(double fair_value, double target_spread, double skew) const
        {
            // Base ask is fair_value + half_spread
            double half_spread = target_spread / 2.0;
            double base_ask = fair_value + half_spread;

            // If long inventory (positive skew), widen ask to encourage selling
            // If short inventory (negative skew), tighten ask to discourage selling
            double skew_adjustment = half_spread * skew * skew_factor_;

            double ask_price = base_ask + skew_adjustment;

            return ask_price;
        }

        bool MarketMaker::should_quote() const
        {
            // Stop quoting if position exceeds maximum
            if (std::abs(position_.quantity) > static_cast<int64_t>(config_.max_position))
            {
                return false;
            }

            // Reduce quoting if position exceeds 80% of limit
            double position_ratio = static_cast<double>(std::abs(position_.quantity)) /
                                    static_cast<double>(config_.max_position);

            if (position_ratio > 0.8)
            {
                // Only quote 50% of the time when position is large
                // This could be enhanced with proper random sampling
                return (tick_count_ % 2 == 0);
            }

            return true;
        }

        uint32_t MarketMaker::calculate_order_quantity() const
        {
            // Base quantity from configuration
            uint32_t quantity = base_quantity_;

            // Reduce quantity if position is large (risk management)
            double position_ratio = static_cast<double>(std::abs(position_.quantity)) /
                                    static_cast<double>(config_.max_position);

            if (position_ratio > 0.5)
            {
                // Scale down quantity as position grows
                double scale_factor = 1.0 - (position_ratio - 0.5);
                quantity = static_cast<uint32_t>(quantity * std::max(0.1, scale_factor));
            }

            // Apply aggression factor
            quantity = static_cast<uint32_t>(quantity * config_.aggression);

            // Ensure minimum quantity
            quantity = std::max(1u, quantity);

            return quantity;
        }

        uint64_t MarketMaker::price_to_fixed(double price) const
        {
            // Convert price to fixed-point representation (cents)
            return static_cast<uint64_t>(price * 100.0);
        }

    } // namespace agents
} // namespace lob
