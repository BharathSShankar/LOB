#pragma once

#include <cstdint>
#include <string>

namespace lob::core
{

    /**
     * @brief Order side (Buy or Sell)
     */
    enum class Side : uint8_t
    {
        BUY = 0,
        SELL = 1
    };

    /**
     * @brief Order type
     */
    enum class OrderType : uint8_t
    {
        LIMIT = 0,  // Limit order - placed in the book at specified price
        MARKET = 1, // Market order - executed immediately at best available price
        CANCEL = 2  // Cancel order - removes existing order from book
    };

    /**
     * @brief Order status
     */
    enum class OrderStatus : uint8_t
    {
        NEW = 0,       // Order just created
        PARTIAL = 1,   // Partially filled
        FILLED = 2,    // Completely filled
        CANCELLED = 3, // Cancelled by user
        REJECTED = 4   // Rejected by system
    };

    /**
     * @brief Represents a single order in the matching engine
     *
     * Memory layout optimized for cache efficiency.
     * Size: Designed to fit in a single cache line (64 bytes)
     */
    struct Order
    {
        uint64_t order_id;           // Unique order identifier
        uint64_t timestamp;          // Nanosecond timestamp for price-time priority
        uint64_t price;              // Price in fixed-point (e.g., cents, ticks)
        uint64_t quantity;           // Original quantity
        uint64_t remaining_quantity; // Remaining unfilled quantity

        Side side;          // Buy or Sell
        OrderType type;     // Order type
        OrderStatus status; // Current status

        uint8_t padding[5]; // Padding to align to cache line

        // Constructor
        Order() noexcept;

        Order(uint64_t id, uint64_t ts, uint64_t px, uint64_t qty,
              Side s, OrderType t) noexcept;

        // Check if order is completely filled
        bool is_filled() const noexcept;

        // Check if order can be matched
        bool is_active() const noexcept;

        // Update quantity after partial fill
        void fill(uint64_t filled_quantity) noexcept;

        // Cancel the order
        void cancel() noexcept;
    };

} // namespace lob::core
