#include "core/Order.h"

namespace lob::core
{

    Order::Order() noexcept
        : order_id(0), timestamp(0), price(0), quantity(0), remaining_quantity(0), side(Side::BUY), type(OrderType::LIMIT), status(OrderStatus::NEW), padding{0}
    {
        // Default initialization complete via initializer list
    }

    Order::Order(uint64_t id, uint64_t ts, uint64_t px, uint64_t qty,
                 Side s, OrderType t) noexcept
        : order_id(id), timestamp(ts), price(px), quantity(qty), remaining_quantity(qty), side(s), type(t), status(OrderStatus::NEW), padding{0}
    {
        // Parameterized initialization complete via initializer list
    }

    bool Order::is_filled() const noexcept
    {
        // Returns true when order has no remaining quantity to fill
        return remaining_quantity == 0;
    }

    bool Order::is_active() const noexcept
    {
        // Active orders are either NEW or PARTIAL (can still be matched)
        return status == OrderStatus::NEW || status == OrderStatus::PARTIAL;
    }

    void Order::fill(uint64_t filled_quantity) noexcept
    {
        // Partial fill: subtract filled amount and update status
        // Defensive: handle case where filled_quantity >= remaining_quantity
        if (filled_quantity >= remaining_quantity)
        {
            remaining_quantity = 0;
            status = OrderStatus::FILLED;
        }
        else
        {
            remaining_quantity -= filled_quantity;
            status = OrderStatus::PARTIAL;
        }
    }

    void Order::cancel() noexcept
    {
        // Mark order as cancelled - prevents further matching
        status = OrderStatus::CANCELLED;
    }

} // namespace lob::core
