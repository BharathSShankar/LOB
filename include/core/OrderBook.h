#pragma once

#include "core/Order.h"
#include <map>
#include <deque>
#include <optional>
#include <vector>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>

namespace lob::core
{

    /**
     * @brief Trade result after matching orders
     */
    struct Trade
    {
        uint64_t buy_order_id;
        uint64_t sell_order_id;
        uint64_t price;
        uint64_t quantity;
        uint64_t timestamp;
    };

    /**
     * @brief Price level in the order book
     * Contains all orders at a specific price, ordered by time (FIFO)
     */
    class PriceLevel
    {
    public:
        PriceLevel() = default;
        explicit PriceLevel(uint64_t price) noexcept;

        // Add order to this price level
        void add_order(Order *order) noexcept;

        // Remove order from this price level
        bool remove_order(uint64_t order_id) noexcept;

        // Get total quantity at this price level
        uint64_t get_total_quantity() const noexcept;

        // Check if price level is empty
        bool is_empty() const noexcept;

        // Get orders at this level (time-ordered)
        const std::deque<Order *> &get_orders() const noexcept;

    private:
        uint64_t price_ = 0;
        std::deque<Order *> orders_; // FIFO queue for time priority
        uint64_t total_quantity_ = 0;
    };

    /**
     * @brief Order Book for a single instrument
     * Implements Price-Time Priority matching algorithm
     *
     * TODO (Weeks 1-2):
     * - Currently uses std::map (Red-Black tree) which is O(log n)
     * - Optimize to flat_map or fixed-size array for known tick sizes
     */
    class OrderBook
    {
    public:
        OrderBook() = default;
        ~OrderBook() = default;

        // Non-copyable, non-movable (for now)
        OrderBook(const OrderBook &) = delete;
        OrderBook &operator=(const OrderBook &) = delete;

        /**
         * @brief Add a new order to the book
         * @return Vector of trades if order matched immediately
         */
        std::vector<Trade> add_order(Order *order) noexcept;

        /**
         * @brief Cancel an existing order
         * @return true if order was found and cancelled
         */
        bool cancel_order(uint64_t order_id) noexcept;

        /**
         * @brief Get best bid price
         */
        std::optional<uint64_t> get_best_bid() const noexcept;

        /**
         * @brief Get best ask price
         */
        std::optional<uint64_t> get_best_ask() const noexcept;

        /**
         * @brief Get spread (difference between best ask and best bid)
         */
        std::optional<uint64_t> get_spread() const noexcept;

        /**
         * @brief Get total quantity at a price level
         */
        uint64_t get_quantity_at_price(uint64_t price, Side side) const noexcept;

        /**
         * @brief Get market depth (top N levels on each side)
         */
        struct DepthLevel
        {
            uint64_t price;
            uint64_t quantity;
        };

        void get_market_depth(std::vector<DepthLevel> &bids,
                              std::vector<DepthLevel> &asks,
                              size_t levels = 10) const noexcept;

        /**
         * @brief Print ASCII visualization of the order book
         * @param levels Number of price levels to display on each side
         * @param out Output stream (defaults to std::cout)
         */
        void print_book(size_t levels = 10, std::ostream &out = std::cout) const noexcept;

        /**
         * @brief Get string representation of order book visualization
         * @param levels Number of price levels to display on each side
         * @return String containing the visualization
         */
        std::string to_string(size_t levels = 10) const noexcept;

    private:
        // TODO (Week 1-2): Match a limit order against the book
        std::vector<Trade> match_limit_order(Order *order) noexcept;

        // TODO (Week 1-2): Match a market order against the book
        std::vector<Trade> match_market_order(Order *order) noexcept;

        // TODO (Week 1-2): Helper to execute a trade between two orders
        Trade execute_trade(Order *incoming, Order *resting, uint64_t quantity) noexcept;

        // Price levels ordered by price
        // Bids: Higher prices first (descending)
        // Asks: Lower prices first (ascending)
        std::map<uint64_t, PriceLevel, std::greater<uint64_t>> bids_; // Descending
        std::map<uint64_t, PriceLevel, std::less<uint64_t>> asks_;    // Ascending

        // Fast order lookup by ID
        std::map<uint64_t, Order *> order_map_;
    };

} // namespace lob::core
