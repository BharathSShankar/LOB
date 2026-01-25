#include "concurrency/OrderEntryGateway.h"
#include <chrono>

namespace lob::concurrency
{

    OrderEntryGateway::OrderEntryGateway() noexcept
        : running_(false),
          order_pool_(nullptr),
          rng_(std::random_device{}()),
          order_id_counter_(1)
    {
    }

    OrderEntryGateway::OrderEntryGateway(OrderPool &pool) noexcept
        : running_(false),
          order_pool_(&pool),
          rng_(std::random_device{}()),
          order_id_counter_(1)
    {
    }

    OrderEntryGateway::~OrderEntryGateway() noexcept
    {
        stop();
    }

    void OrderEntryGateway::start() noexcept
    {
        if (running_.load(std::memory_order_acquire))
        {
            return; // Already running
        }

        running_.store(true, std::memory_order_release);
        gateway_thread_ = std::thread(&OrderEntryGateway::run, this);
    }

    void OrderEntryGateway::stop() noexcept
    {
        if (!running_.load(std::memory_order_acquire))
        {
            return; // Not running
        }

        running_.store(false, std::memory_order_release);

        if (gateway_thread_.joinable())
        {
            gateway_thread_.join();
        }
    }

    bool OrderEntryGateway::submit_order(core::Order *order) noexcept
    {
        if (!order)
        {
            return false;
        }

        bool success = ring_buffer_.push(order);

        if (success)
        {
            stats_.total_orders_submitted++;

            // Call optional callback
            if (order_callback_)
            {
                order_callback_(order);
            }
        }
        else
        {
            stats_.total_orders_dropped++;
            stats_.buffer_full_count++;
        }

        return success;
    }

    bool OrderEntryGateway::pop_order(core::Order *&order) noexcept
    {
        return ring_buffer_.pop(order);
    }

    bool OrderEntryGateway::is_running() const noexcept
    {
        return running_.load(std::memory_order_acquire);
    }

    void OrderEntryGateway::set_order_callback(OrderCallback callback) noexcept
    {
        order_callback_ = callback;
    }

    void OrderEntryGateway::set_config(const Config &config) noexcept
    {
        config_ = config;
    }

    const OrderEntryGateway::Config &OrderEntryGateway::get_config() const noexcept
    {
        return config_;
    }

    size_t OrderEntryGateway::generate_orders(size_t count) noexcept
    {
        if (!order_pool_)
        {
            return 0;
        }

        size_t generated = 0;

        for (size_t i = 0; i < count; ++i)
        {
            core::Order *order = generate_single_order();
            if (order)
            {
                if (submit_order(order))
                {
                    generated++;
                    stats_.total_orders_generated++;
                }
                else
                {
                    // Buffer full, release order back to pool
                    order_pool_->release(order);
                }
            }
            else
            {
                // Pool exhausted
                break;
            }
        }

        return generated;
    }

    OrderEntryGateway::Statistics OrderEntryGateway::get_statistics() const noexcept
    {
        return stats_;
    }

    bool OrderEntryGateway::is_buffer_empty() const noexcept
    {
        return ring_buffer_.empty();
    }

    size_t OrderEntryGateway::buffer_size() const noexcept
    {
        return ring_buffer_.size();
    }

    void OrderEntryGateway::run() noexcept
    {
        // Main gateway loop
        // This simulates receiving orders from network

        while (running_.load(std::memory_order_acquire))
        {
            // Auto-generate orders if configured
            if (config_.auto_generate && order_pool_)
            {
                generate_random_orders();

                // Rate limiting with configurable delay
                if (config_.batch_delay_us > 0)
                {
                    std::this_thread::sleep_for(
                        std::chrono::microseconds(config_.batch_delay_us));
                }
            }
            else
            {
                // No auto-generation, just yield to prevent spinning
                std::this_thread::yield();
            }
        }
    }

    void OrderEntryGateway::generate_random_orders() noexcept
    {
        if (!order_pool_)
        {
            return;
        }

        // Generate a batch of random orders
        for (uint32_t i = 0; i < config_.orders_per_batch; ++i)
        {
            core::Order *order = generate_single_order();
            if (order)
            {
                if (submit_order(order))
                {
                    stats_.total_orders_generated++;
                }
                else
                {
                    // Buffer full, release order back to pool
                    order_pool_->release(order);
                    break; // Stop generating if buffer is full
                }
            }
            else
            {
                // Pool exhausted
                break;
            }
        }
    }

    core::Order *OrderEntryGateway::generate_single_order() noexcept
    {
        if (!order_pool_)
        {
            return nullptr;
        }

        core::Order *order = order_pool_->acquire();
        if (!order)
        {
            return nullptr;
        }

        // Generate random order data
        std::uniform_int_distribution<uint64_t> price_dist(
            config_.base_price - config_.price_range / 2,
            config_.base_price + config_.price_range / 2);

        std::uniform_int_distribution<uint64_t> qty_dist(
            config_.min_quantity,
            config_.max_quantity);

        std::uniform_real_distribution<double> ratio_dist(0.0, 1.0);

        // Determine side (buy/sell)
        core::Side side = (ratio_dist(rng_) < config_.buy_sell_ratio)
                              ? core::Side::BUY
                              : core::Side::SELL;

        // Determine order type (limit/market)
        core::OrderType type = (ratio_dist(rng_) < config_.limit_market_ratio)
                                   ? core::OrderType::LIMIT
                                   : core::OrderType::MARKET;

        // Generate unique order ID and timestamp
        uint64_t order_id = order_id_counter_.fetch_add(1, std::memory_order_relaxed);
        uint64_t timestamp = std::chrono::steady_clock::now().time_since_epoch().count();

        // Generate price (market orders get price 0)
        uint64_t price = (type == core::OrderType::MARKET) ? 0 : price_dist(rng_);

        // Generate quantity
        uint64_t quantity = qty_dist(rng_);

        // Initialize order
        *order = core::Order(
            order_id,
            timestamp,
            price,
            quantity,
            side,
            type);

        return order;
    }

} // namespace lob::concurrency
