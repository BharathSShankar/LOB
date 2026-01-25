#pragma once

#include "concurrency/RingBuffer.h"
#include "core/Order.h"
#include "memory/ObjectPool.h"
#include <atomic>
#include <thread>
#include <functional>
#include <random>

namespace lob::concurrency
{

    /**
     * @brief Order Entry Gateway - simulates network packet parsing
     *
     * Week 5 Focus: This component runs in a separate thread and:
     * 1. Generates/receives orders (simulating network traffic)
     * 2. Pushes them to the ring buffer
     * 3. The matching engine thread consumes from the buffer
     *
     * This demonstrates the producer-consumer pattern with lock-free communication.
     */
    class OrderEntryGateway
    {
    public:
        using OrderCallback = std::function<void(core::Order *)>;
        using OrderPool = memory::ObjectPool<core::Order, 100000>;

        /**
         * @brief Configuration for order generation
         */
        struct Config
        {
            uint64_t base_price = 10000;     // Base price for order generation
            uint64_t price_range = 200;      // Price variance (+/- from base)
            uint64_t min_quantity = 1;       // Minimum order quantity
            uint64_t max_quantity = 1000;    // Maximum order quantity
            double buy_sell_ratio = 0.5;     // Probability of buy order (0.0-1.0)
            double limit_market_ratio = 0.9; // Probability of limit order (0.0-1.0)
            uint32_t orders_per_batch = 100; // Orders to generate per batch
            uint32_t batch_delay_us = 100;   // Delay between batches (microseconds)
            bool auto_generate = false;      // Auto-generate orders when running
        };

        /**
         * @brief Construct gateway
         */
        explicit OrderEntryGateway() noexcept;

        /**
         * @brief Construct gateway with object pool for order generation
         * @param pool Object pool to acquire orders from
         */
        explicit OrderEntryGateway(OrderPool &pool) noexcept;

        ~OrderEntryGateway() noexcept;

        // Non-copyable, non-movable
        OrderEntryGateway(const OrderEntryGateway &) = delete;
        OrderEntryGateway &operator=(const OrderEntryGateway &) = delete;

        /**
         * @brief Start the gateway thread
         */
        void start() noexcept;

        /**
         * @brief Stop the gateway thread
         */
        void stop() noexcept;

        /**
         * @brief Submit an order (push to ring buffer)
         * @param order Pointer to order
         * @return true if submitted successfully
         */
        bool submit_order(core::Order *order) noexcept;

        /**
         * @brief Pop an order from the ring buffer (consumer side)
         * @param order Output pointer to order
         * @return true if order was popped
         */
        bool pop_order(core::Order *&order) noexcept;

        /**
         * @brief Check if gateway is running
         */
        bool is_running() const noexcept;

        /**
         * @brief Set callback for order submission
         */
        void set_order_callback(OrderCallback callback) noexcept;

        /**
         * @brief Set configuration for order generation
         */
        void set_config(const Config &config) noexcept;

        /**
         * @brief Get current configuration
         */
        const Config &get_config() const noexcept;

        /**
         * @brief Generate a batch of random orders manually
         * @param count Number of orders to generate
         * @return Number of orders successfully submitted
         */
        size_t generate_orders(size_t count) noexcept;

        /**
         * @brief Get statistics
         */
        struct Statistics
        {
            uint64_t total_orders_submitted = 0;
            uint64_t total_orders_dropped = 0;
            uint64_t buffer_full_count = 0;
            uint64_t total_orders_generated = 0;
        };

        Statistics get_statistics() const noexcept;

        /**
         * @brief Check if ring buffer is empty
         */
        bool is_buffer_empty() const noexcept;

        /**
         * @brief Get current buffer size
         */
        size_t buffer_size() const noexcept;

    private:
        // Gateway thread main loop
        void run() noexcept;

        // Generate random orders for testing
        void generate_random_orders() noexcept;

        // Generate a single random order
        core::Order *generate_single_order() noexcept;

        // Ring buffer for order communication (capacity: 64K orders)
        static constexpr size_t RING_BUFFER_SIZE = 65536;
        RingBuffer<core::Order *, RING_BUFFER_SIZE> ring_buffer_;

        // Gateway thread
        std::thread gateway_thread_;

        // Running flag
        std::atomic<bool> running_;

        // Statistics
        Statistics stats_;

        // Order callback (optional)
        OrderCallback order_callback_;

        // Object pool for order allocation (optional, external)
        OrderPool *order_pool_;

        // Configuration
        Config config_;

        // Random number generator
        std::mt19937 rng_;

        // Order ID counter
        std::atomic<uint64_t> order_id_counter_;
    };

} // namespace lob::concurrency
