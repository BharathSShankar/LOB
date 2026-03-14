#include "agents/MeanReverter.h"
#include <chrono>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace lob
{
    namespace agents
    {

        MeanReverter::MeanReverter()
            : tick_count_(0), fair_value_(100.0), threshold_pct_(0.05), use_sma_fair_value_(false), position_threshold_(0.8), bollinger_period_(20), bollinger_std_dev_(2.0), use_rsi_(false), rsi_period_(14), rsi_overbought_(70.0), rsi_oversold_(30.0)
        {
            type_ = AgentType::MEAN_REVERTER;

            // Initialize RNG with a unique seed
            auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            rng_.seed(seed);

            // Initialize distributions
            uniform_ = std::uniform_real_distribution<double>(0.0, 1.0);
        }

        void MeanReverter::tick(const MarketState &state)
        {
            tick_count_++;

            // Update price history
            if (state.last_price > 0.0)
            {
                // Track price changes for RSI
                if (!price_history_.empty())
                {
                    double change = state.last_price - price_history_.back();
                    price_changes_.push_back(change);

                    // Maintain RSI history size
                    if (price_changes_.size() > rsi_period_ + 1)
                    {
                        price_changes_.pop_front();
                    }
                }

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

        core::Order *MeanReverter::decide(const MarketState &state)
        {
            // Don't trade if inactive or market price is invalid
            if (!is_active() || state.last_price <= 0.0)
            {
                return nullptr;
            }

            // Use RSI strategy if enabled
            if (use_rsi_)
            {
                if (price_changes_.size() < rsi_period_)
                {
                    return nullptr; // Not enough data for RSI
                }

                double rsi = calculate_rsi();

                bool should_buy = rsi < rsi_oversold_;    // Oversold
                bool should_sell = rsi > rsi_overbought_; // Overbought

                if (!should_buy && !should_sell)
                {
                    return nullptr;
                }

                // Check position limits
                if (is_position_near_limit())
                {
                    if (should_buy && position_.quantity > 0)
                    {
                        return nullptr;
                    }
                    if (should_sell && position_.quantity < 0)
                    {
                        return nullptr;
                    }
                }

                // Calculate order size based on RSI extremity
                double deviation = should_buy ? (rsi_oversold_ - rsi) / rsi_oversold_
                                              : (rsi - rsi_overbought_) / (100.0 - rsi_overbought_);
                uint32_t quantity = calculate_order_size(std::abs(deviation));

                // Allocate Order from the shared pool
                core::Order *order = alloc_order();
                if (!order)
                    return nullptr;

                core::Side side = should_buy ? core::Side::BUY : core::Side::SELL;
                double price = should_buy
                                   ? state.last_price * 1.001
                                   : state.last_price * 0.999;
                uint64_t fp = static_cast<uint64_t>(price * 100.0);
                if (fp == 0)
                    fp = 1;

                *order = core::Order(order_id_counter()++, 0, fp,
                                     static_cast<uint64_t>(quantity),
                                     side, core::OrderType::LIMIT);
                position_.update(side, quantity, price);
                return order;
            }

            // Standard fair value strategy
            double fair_value = calculate_fair_value(state);

            if (fair_value <= 0.0)
            {
                return nullptr; // Invalid fair value
            }

            double upper_band = calculate_upper_band(fair_value, state);
            double lower_band = calculate_lower_band(fair_value, state);

            bool should_sell = state.last_price > upper_band; // Overvalued
            bool should_buy = state.last_price < lower_band;  // Undervalued

            if (!should_buy && !should_sell)
            {
                return nullptr; // Price within fair range
            }

            // Check position limits - reduce trading if near limit
            if (is_position_near_limit())
            {
                // If we're long and getting buy signal, be more selective
                if (should_buy && position_.quantity > 0)
                {
                    // Only buy if significantly undervalued
                    double extra_threshold = fair_value * threshold_pct_ * 0.5;
                    if (state.last_price > (lower_band - extra_threshold))
                    {
                        return nullptr;
                    }
                }
                // If we're short and getting sell signal, be more selective
                if (should_sell && position_.quantity < 0)
                {
                    double extra_threshold = fair_value * threshold_pct_ * 0.5;
                    if (state.last_price < (upper_band + extra_threshold))
                    {
                        return nullptr;
                    }
                }
            }

            // Calculate deviation from fair value for position sizing
            double deviation = calculate_deviation(state.last_price, fair_value);

            // More aggressive if further from fair value
            uint32_t quantity = calculate_order_size(deviation);

            // Allocate Order from the shared pool
            core::Order *order = alloc_order();
            if (!order)
                return nullptr;

            core::Side side = should_buy ? core::Side::BUY : core::Side::SELL;
            // Price slightly inside market for better fills
            double price = should_buy
                               ? state.last_price * 1.001
                               : state.last_price * 0.999;
            uint64_t fp = static_cast<uint64_t>(price * 100.0);
            if (fp == 0)
                fp = 1;

            *order = core::Order(order_id_counter()++, 0, fp,
                                 static_cast<uint64_t>(quantity),
                                 side, core::OrderType::LIMIT);
            position_.update(side, quantity, price);
            return order;
        }

        void MeanReverter::initialize(uint64_t agent_id, const AgentConfig &config)
        {
            agent_id_ = agent_id;
            config_ = config;
            type_ = AgentType::MEAN_REVERTER;
            active_ = true;

            // Extract mean reverter specific parameters
            auto it = config.params.find("fair_value");
            if (it != config.params.end())
            {
                fair_value_ = it->second;
            }
            else
            {
                fair_value_ = 100.0; // Default fair value
            }

            it = config.params.find("threshold_pct");
            if (it != config.params.end())
            {
                threshold_pct_ = it->second;
            }
            else
            {
                threshold_pct_ = 0.05; // Default 5% threshold
            }

            it = config.params.find("use_sma_fair_value");
            if (it != config.params.end())
            {
                use_sma_fair_value_ = (it->second > 0.0);
            }
            else
            {
                use_sma_fair_value_ = false;
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

            // Bollinger Bands parameters
            it = config.params.find("bollinger_period");
            if (it != config.params.end())
            {
                bollinger_period_ = static_cast<uint32_t>(it->second);
            }
            else
            {
                bollinger_period_ = 20;
            }

            it = config.params.find("bollinger_std_dev");
            if (it != config.params.end())
            {
                bollinger_std_dev_ = it->second;
            }
            else
            {
                bollinger_std_dev_ = 2.0;
            }

            // RSI parameters
            it = config.params.find("use_rsi");
            if (it != config.params.end())
            {
                use_rsi_ = (it->second > 0.0);
            }
            else
            {
                use_rsi_ = false;
            }

            it = config.params.find("rsi_period");
            if (it != config.params.end())
            {
                rsi_period_ = static_cast<uint32_t>(it->second);
            }
            else
            {
                rsi_period_ = 14;
            }

            it = config.params.find("rsi_overbought");
            if (it != config.params.end())
            {
                rsi_overbought_ = it->second;
            }
            else
            {
                rsi_overbought_ = 70.0;
            }

            it = config.params.find("rsi_oversold");
            if (it != config.params.end())
            {
                rsi_oversold_ = it->second;
            }
            else
            {
                rsi_oversold_ = 30.0;
            }

            // Reseed RNG with agent_id for reproducibility while maintaining uniqueness
            rng_.seed(agent_id + std::chrono::high_resolution_clock::now().time_since_epoch().count());

            // Configure distributions
            uniform_ = std::uniform_real_distribution<double>(0.0, 1.0);

            // Reset state
            reset();
        }

        void MeanReverter::reset()
        {
            position_ = Position();
            tick_count_ = 0;
            active_ = true;
            price_history_.clear();
            price_changes_.clear();
        }

        double MeanReverter::calculate_fair_value(const MarketState &state) const
        {
            if (use_sma_fair_value_)
            {
                // Use SMA as fair value (e.g., SMA20 for Bollinger Bands)
                if (state.price_sma_50 > 0.0)
                {
                    return state.price_sma_50;
                }
                // Fallback to price if SMA not available
                return state.last_price;
            }
            else
            {
                // Use fixed fair value from configuration
                return fair_value_;
            }
        }

        double MeanReverter::calculate_upper_band(double fair_value, const MarketState &state) const
        {
            if (use_sma_fair_value_ && state.volatility > 0.0)
            {
                // Bollinger Bands: upper = SMA + (k * stddev)
                double upper, lower;
                calculate_bollinger_bands(fair_value, state.volatility, upper, lower);
                return upper;
            }
            else
            {
                // Simple percentage band
                return fair_value * (1.0 + threshold_pct_);
            }
        }

        double MeanReverter::calculate_lower_band(double fair_value, const MarketState &state) const
        {
            if (use_sma_fair_value_ && state.volatility > 0.0)
            {
                // Bollinger Bands: lower = SMA - (k * stddev)
                double upper, lower;
                calculate_bollinger_bands(fair_value, state.volatility, upper, lower);
                return lower;
            }
            else
            {
                // Simple percentage band
                return fair_value * (1.0 - threshold_pct_);
            }
        }

        void MeanReverter::calculate_bollinger_bands(double sma, double std_dev, double &upper, double &lower) const
        {
            upper = sma + (bollinger_std_dev_ * std_dev);
            lower = sma - (bollinger_std_dev_ * std_dev);
        }

        double MeanReverter::calculate_rsi() const
        {
            if (price_changes_.size() < rsi_period_)
            {
                return 50.0; // Neutral RSI if not enough data
            }

            // Calculate average gains and losses
            double total_gain = 0.0;
            double total_loss = 0.0;
            size_t count = 0;

            // Use the last 'rsi_period_' changes
            size_t start_idx = price_changes_.size() > rsi_period_ ? price_changes_.size() - rsi_period_ : 0;

            for (size_t i = start_idx; i < price_changes_.size(); i++)
            {
                double change = price_changes_[i];
                if (change > 0.0)
                {
                    total_gain += change;
                }
                else if (change < 0.0)
                {
                    total_loss += std::abs(change);
                }
                count++;
            }

            if (count == 0)
            {
                return 50.0;
            }

            double avg_gain = total_gain / count;
            double avg_loss = total_loss / count;

            if (avg_loss == 0.0)
            {
                return 100.0; // All gains, maximum RSI
            }

            double rs = avg_gain / avg_loss;
            double rsi = 100.0 - (100.0 / (1.0 + rs));

            return rsi;
        }

        double MeanReverter::calculate_deviation(double price, double fair_value) const
        {
            return std::abs(price - fair_value) / fair_value;
        }

        uint32_t MeanReverter::calculate_order_size(double deviation) const
        {
            // Base order size from configuration
            double base_size = config_.order_size_mean;

            // Scale by deviation and aggression
            // More aggressive when further from fair value
            double size = base_size * config_.aggression * (1.0 + deviation);

            // Clamp to reasonable range
            uint32_t quantity = static_cast<uint32_t>(std::max(1.0, std::min(size, 10000.0)));

            return quantity;
        }

        bool MeanReverter::is_position_near_limit() const
        {
            double position_ratio = static_cast<double>(std::abs(position_.quantity)) /
                                    static_cast<double>(config_.max_position);

            return position_ratio >= position_threshold_;
        }

        double MeanReverter::calculate_price_stddev() const
        {
            if (price_history_.size() < 2)
            {
                return 0.0;
            }

            // Calculate mean
            double sum = std::accumulate(price_history_.begin(), price_history_.end(), 0.0);
            double mean = sum / price_history_.size();

            // Calculate variance
            double variance = 0.0;
            for (double price : price_history_)
            {
                double diff = price - mean;
                variance += diff * diff;
            }
            variance /= price_history_.size();

            return std::sqrt(variance);
        }

    } // namespace agents
} // namespace lob
