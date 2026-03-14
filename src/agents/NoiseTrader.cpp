#include "agents/NoiseTrader.h"
#include <chrono>
#include <cmath>

namespace lob
{
    namespace agents
    {

        NoiseTrader::NoiseTrader()
            : tick_count_(0), noise_stddev_(0.01), position_threshold_(0.8)
        {
            type_ = AgentType::NOISE_TRADER;

            // Initialize RNG with a unique seed
            auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            rng_.seed(seed);

            // Initialize distributions with default parameters (will be reconfigured in initialize())
            normal_ = std::normal_distribution<double>(0.0, 0.01);
            uniform_ = std::uniform_real_distribution<double>(0.0, 1.0);
            lognormal_ = std::lognormal_distribution<double>(std::log(100.0), 0.2);
        }

        void NoiseTrader::tick(const MarketState &state)
        {
            tick_count_++;

            // Update unrealized PnL with current market price
            position_.mark_to_market(state.last_price);

            // Check position limits and deactivate if exceeded
            if (std::abs(position_.quantity) > static_cast<int64_t>(config_.max_position))
            {
                deactivate();
            }
        }

        core::Order *NoiseTrader::decide(const MarketState &state)
        {
            // Don't trade if inactive or market price is invalid
            if (!is_active() || state.last_price <= 0.0)
            {
                return nullptr;
            }

            // Reduce trading frequency if position is near limit
            if (is_position_near_limit())
            {
                // Trade only 20% of the time when near position limit
                if (uniform_(rng_) > 0.2)
                {
                    return nullptr;
                }
            }

            // Generate random price with noise around last price
            double price = generate_random_price(state.last_price);

            // Ensure price is positive
            if (price <= 0.0)
            {
                price = state.last_price;
            }

            // Random side selection
            core::Side side = (uniform_(rng_) < 0.5) ? core::Side::BUY : core::Side::SELL;

            // Bias against increasing position if near limit
            if (is_position_near_limit())
            {
                // If long, prefer selling; if short, prefer buying
                if (position_.quantity > 0)
                {
                    side = core::Side::SELL;
                }
                else if (position_.quantity < 0)
                {
                    side = core::Side::BUY;
                }
            }

            // Generate random quantity
            uint32_t quantity = generate_random_quantity();

            // Ensure quantity is at least 1
            if (quantity == 0)
            {
                quantity = 1;
            }

            // Allocate Order from the shared pool injected by the orchestrator
            core::Order *order = alloc_order();
            if (!order)
                return nullptr; // Pool exhausted

            uint64_t fixed_price = static_cast<uint64_t>(price * 100.0);
            if (fixed_price == 0)
                fixed_price = 1;

            *order = core::Order(order_id_counter()++, /*timestamp=*/0,
                                 fixed_price,
                                 static_cast<uint64_t>(quantity),
                                 side,
                                 core::OrderType::LIMIT);

            // Update position tracking (approximate; real fill callback pending)
            position_.update(side, quantity, price);
            return order;
        }

        void NoiseTrader::initialize(uint64_t agent_id, const AgentConfig &config)
        {
            agent_id_ = agent_id;
            config_ = config;
            type_ = AgentType::NOISE_TRADER;
            active_ = true;

            // Extract noise trader specific parameters
            auto it = config.params.find("noise_stddev");
            if (it != config.params.end())
            {
                noise_stddev_ = it->second;
            }
            else
            {
                noise_stddev_ = 0.01; // Default 1% noise
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

            // Reseed RNG with agent_id for reproducibility
            rng_.seed(agent_id + std::chrono::high_resolution_clock::now().time_since_epoch().count());

            // Configure distributions
            normal_ = std::normal_distribution<double>(0.0, noise_stddev_);
            uniform_ = std::uniform_real_distribution<double>(0.0, 1.0);

            // Lognormal for order sizes: mean = order_size_mean, stddev = order_size_stddev
            // For lognormal, we need to calculate mu and sigma from desired mean and stddev
            double mean = config.order_size_mean;
            double stddev = config.order_size_stddev;
            double variance = stddev * stddev;
            double mean_sq = mean * mean;

            double mu = std::log(mean_sq / std::sqrt(mean_sq + variance));
            double sigma = std::sqrt(std::log(1.0 + variance / mean_sq));

            lognormal_ = std::lognormal_distribution<double>(mu, sigma);

            // Reset state
            reset();
        }

        void NoiseTrader::reset()
        {
            position_ = Position();
            tick_count_ = 0;
            active_ = true;
        }

        double NoiseTrader::generate_random_price(double market_price)
        {
            // Random walk: epsilon ~ N(0, noise_stddev)
            double epsilon = normal_(rng_);

            // Price = market_price * (1 + epsilon)
            double price = market_price * (1.0 + epsilon);

            return price;
        }

        uint32_t NoiseTrader::generate_random_quantity()
        {
            // Sample from lognormal distribution
            double qty_double = lognormal_(rng_);

            // Clamp to reasonable range and convert to integer
            uint32_t qty = static_cast<uint32_t>(std::max(1.0, std::min(qty_double, 10000.0)));

            return qty;
        }

        bool NoiseTrader::is_position_near_limit() const
        {
            double position_ratio = static_cast<double>(std::abs(position_.quantity)) /
                                    static_cast<double>(config_.max_position);

            return position_ratio >= position_threshold_;
        }

        uint64_t NoiseTrader::price_to_fixed(double price) const
        {
            // Convert price to fixed-point representation (cents)
            // Multiply by 100 to convert dollars to cents
            return static_cast<uint64_t>(price * 100.0);
        }

    } // namespace agents
} // namespace lob
