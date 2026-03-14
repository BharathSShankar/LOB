#include "agents/Whale.h"
#include <chrono>
#include <cmath>
#include <algorithm>

namespace lob
{
    namespace agents
    {

        Whale::Whale()
            : trigger_tick_(0), price_trigger_(0.0), trigger_above_(true), triggered_(false), whale_side_(core::Side::SELL), whale_size_(10000), remaining_quantity_(0), execution_mode_(ExecutionMode::INSTANT), slice_size_(100), slices_interval_(10), last_slice_tick_(0), slices_executed_(0), tick_count_(0)
        {
            type_ = AgentType::WHALE;

            // Initialize RNG with a unique seed
            auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            rng_.seed(seed);

            // Initialize distributions
            uniform_ = std::uniform_real_distribution<double>(0.0, 1.0);
        }

        void Whale::tick(const MarketState &state)
        {
            tick_count_++;

            // Update unrealized PnL with current market price
            position_.mark_to_market(state.last_price);

            // Check if we should trigger
            if (!triggered_)
            {
                if (check_trigger_condition(state))
                {
                    triggered_ = true;
                    remaining_quantity_ = whale_size_;
                    last_slice_tick_ = tick_count_;
                    slices_executed_ = 0;
                }
            }

            // Note: We don't deactivate based on position limits for whales
            // as they are meant to execute large orders regardless
        }

        core::Order *Whale::decide(const MarketState &state)
        {
            // Don't trade if inactive or market price is invalid
            if (!is_active() || state.last_price <= 0.0)
            {
                return nullptr;
            }

            // Don't trade if not triggered yet
            if (!triggered_)
            {
                return nullptr;
            }

            // Don't trade if execution is complete
            if (remaining_quantity_ == 0)
            {
                return nullptr;
            }

            // Determine order size based on execution mode
            uint32_t order_quantity = 0;

            switch (execution_mode_)
            {
            case ExecutionMode::INSTANT:
                // Execute entire remaining quantity at once
                order_quantity = remaining_quantity_;
                remaining_quantity_ = 0;
                break;

            case ExecutionMode::ICEBERG:
                // Execute one slice at a time, as fast as possible
                order_quantity = calculate_slice_size();
                remaining_quantity_ -= order_quantity;
                slices_executed_++;
                break;

            case ExecutionMode::TWAP:
                // Execute slices with time interval
                if (is_ready_for_next_slice())
                {
                    order_quantity = calculate_slice_size();
                    remaining_quantity_ -= order_quantity;
                    last_slice_tick_ = tick_count_;
                    slices_executed_++;
                }
                else
                {
                    // Not ready yet, wait for interval
                    return nullptr;
                }
                break;

            default:
                return nullptr;
            }

            if (order_quantity == 0)
            {
                return nullptr;
            }

            // Allocate Order from the shared pool injected by the orchestrator
            core::Order *order = alloc_order();
            if (!order)
                return nullptr; // Pool exhausted

            // Whales use aggressive limit orders (effectively market orders)
            // Price crosses the spread to guarantee immediate execution
            double price = (whale_side_ == core::Side::SELL)
                               ? state.last_price * 0.95  // Deep below market → hits all bids
                               : state.last_price * 1.05; // Deep above market → hits all asks

            uint64_t fp = static_cast<uint64_t>(price * 100.0);
            if (fp == 0)
                fp = 1;

            *order = core::Order(order_id_counter()++, /*timestamp=*/0,
                                 fp,
                                 static_cast<uint64_t>(order_quantity),
                                 whale_side_,
                                 core::OrderType::LIMIT);

            position_.update(whale_side_, order_quantity, price);
            return order;
        }

        void Whale::initialize(uint64_t agent_id, const AgentConfig &config)
        {
            agent_id_ = agent_id;
            config_ = config;
            type_ = AgentType::WHALE;
            active_ = true;

            // Extract whale specific parameters
            auto it = config.params.find("trigger_tick");
            if (it != config.params.end())
            {
                trigger_tick_ = static_cast<uint64_t>(it->second);
            }
            else
            {
                trigger_tick_ = 0; // Immediate trigger
            }

            it = config.params.find("whale_side");
            if (it != config.params.end())
            {
                whale_side_ = (it->second > 0.5) ? core::Side::SELL : core::Side::BUY;
            }
            else
            {
                whale_side_ = core::Side::SELL; // Default to sell (flash crash)
            }

            it = config.params.find("whale_size");
            if (it != config.params.end())
            {
                whale_size_ = static_cast<uint32_t>(it->second);
            }
            else
            {
                whale_size_ = 10000; // Default 10k units
            }

            it = config.params.find("execution_mode");
            if (it != config.params.end())
            {
                uint8_t mode = static_cast<uint8_t>(it->second);
                execution_mode_ = static_cast<ExecutionMode>(mode);
            }
            else
            {
                execution_mode_ = ExecutionMode::INSTANT;
            }

            it = config.params.find("slice_size");
            if (it != config.params.end())
            {
                slice_size_ = static_cast<uint32_t>(it->second);
            }
            else
            {
                slice_size_ = 100; // Default slice size
            }

            it = config.params.find("slices_interval");
            if (it != config.params.end())
            {
                slices_interval_ = static_cast<uint32_t>(it->second);
            }
            else
            {
                slices_interval_ = 10; // Default 10 ticks between slices
            }

            it = config.params.find("price_trigger");
            if (it != config.params.end())
            {
                price_trigger_ = it->second;
            }
            else
            {
                price_trigger_ = 0.0; // Disabled
            }

            it = config.params.find("trigger_above");
            if (it != config.params.end())
            {
                trigger_above_ = (it->second > 0.0);
            }
            else
            {
                trigger_above_ = true;
            }

            // Reseed RNG with agent_id for reproducibility while maintaining uniqueness
            rng_.seed(agent_id + std::chrono::high_resolution_clock::now().time_since_epoch().count());

            // Configure distributions
            uniform_ = std::uniform_real_distribution<double>(0.0, 1.0);

            // Reset state
            reset();
        }

        void Whale::reset()
        {
            position_ = Position();
            tick_count_ = 0;
            triggered_ = false;
            remaining_quantity_ = 0;
            last_slice_tick_ = 0;
            slices_executed_ = 0;
            active_ = true;
        }

        bool Whale::check_trigger_condition(const MarketState &state)
        {
            // Check tick-based trigger
            if (trigger_tick_ > 0 && tick_count_ >= trigger_tick_)
            {
                return true;
            }

            // Check price-based trigger
            if (price_trigger_ > 0.0)
            {
                return check_price_trigger(state);
            }

            // If trigger_tick is 0 and no price trigger, trigger immediately
            if (trigger_tick_ == 0 && price_trigger_ == 0.0)
            {
                return true;
            }

            return false;
        }

        bool Whale::check_price_trigger(const MarketState &state) const
        {
            if (trigger_above_)
            {
                // Trigger when price goes above threshold
                return state.last_price >= price_trigger_;
            }
            else
            {
                // Trigger when price goes below threshold
                return state.last_price <= price_trigger_;
            }
        }

        uint32_t Whale::calculate_slice_size()
        {
            // Calculate the size of the next slice
            uint32_t slice = std::min(slice_size_, remaining_quantity_);

            // Add some randomness to slice size (±10%) to appear more natural
            if (execution_mode_ == ExecutionMode::ICEBERG)
            {
                double variation = uniform_(rng_) * 0.2 - 0.1; // -10% to +10%
                double adjusted_slice = slice * (1.0 + variation);
                slice = static_cast<uint32_t>(std::max(1.0, std::min(adjusted_slice, static_cast<double>(remaining_quantity_))));
            }

            return slice;
        }

        bool Whale::is_ready_for_next_slice() const
        {
            // For TWAP, check if enough ticks have elapsed
            return (tick_count_ - last_slice_tick_) >= slices_interval_;
        }

    } // namespace agents
} // namespace lob
