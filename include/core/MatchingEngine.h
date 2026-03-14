#pragma once

#include "core/OrderBook.h"
#include "core/Order.h"
#include "memory/ObjectPool.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace lob::core
{

    /**
     * @brief Central Matching Engine
     * Manages order books for multiple instruments
     *
     * This is the core of the exchange - handles all order processing
     * and trade execution with Price-Time Priority.
     */
    class MatchingEngine
    {
    public:
        MatchingEngine() = default;
        ~MatchingEngine() = default;

        // Non-copyable, non-movable
        MatchingEngine(const MatchingEngine &) = delete;
        MatchingEngine &operator=(const MatchingEngine &) = delete;

        /**
         * @brief Initialize the matching engine
         */
        void initialize() noexcept;

        /**
         * @brief Process a new order
         * @param order Pointer to order (from object pool)
         * @return Vector of trades generated
         */
        std::vector<Trade> process_order(Order *order) noexcept;

        /**
         * @brief Cancel an existing order
         * @param order_id Order ID to cancel
         * @param instrument Instrument symbol
         * @return true if order was cancelled successfully
         */
        bool cancel_order(uint64_t order_id, const std::string &instrument) noexcept;

        /**
         * @brief Get order book for an instrument
         * @param instrument Instrument symbol
         * @return Pointer to order book, nullptr if not found
         */
        OrderBook *get_order_book(const std::string &instrument) noexcept;

        /**
         * @brief Create order book for new instrument
         * @param instrument Instrument symbol
         */
        void create_order_book(const std::string &instrument) noexcept;

        /**
         * @brief Get current statistics
         */
        struct Statistics
        {
            uint64_t total_orders_processed = 0;
            uint64_t total_trades_executed = 0;
            uint64_t total_orders_cancelled = 0;
            uint64_t total_orders_rejected = 0;
            uint64_t total_volume = 0;
        };

        Statistics get_statistics() const noexcept;

        /**
         * @brief Reset statistics
         */
        void reset_statistics() noexcept;

        /**
         * @brief Get object pool for orders
         * @return Reference to order pool
         */
        memory::ObjectPool<Order, 1000000> &get_order_pool() noexcept;

        /**
         * @brief Get pool utilization statistics
         */
        struct PoolStats
        {
            size_t available = 0;
            size_t capacity = 0;
            size_t in_use = 0;
        };

        PoolStats get_pool_statistics() const noexcept;

    private:
        bool validate_order(const Order *order) const noexcept;

        // Order books for each instrument (keyed by symbol)
        std::unordered_map<std::string, std::unique_ptr<OrderBook>> order_books_;

        // Engine statistics
        Statistics stats_;

        // Week 3-4: Object pool for zero-allocation order management
        // Heap-allocated due to large size (64MB), but accessed via pointer (no indirection cost)
        std::unique_ptr<memory::ObjectPool<Order, 1000000>> order_pool_;

        // Default instrument for single-instrument mode
        static constexpr const char *DEFAULT_INSTRUMENT = "DEFAULT";
    };

} // namespace lob::core
