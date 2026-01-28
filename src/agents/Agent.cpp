#include "agents/Agent.h"
#include <cmath>

namespace lob
{
    namespace agents
    {

        void Position::update(core::Side side, uint64_t qty, double price)
        {
            int64_t signed_qty = static_cast<int64_t>(qty);

            // Determine trade direction
            if (side == core::Side::SELL)
            {
                signed_qty = -signed_qty;
            }

            // Calculate realized PnL if closing/reducing position
            if ((quantity > 0 && signed_qty < 0) || (quantity < 0 && signed_qty > 0))
            {
                // Position is being reduced or reversed
                int64_t closing_qty = std::min(std::abs(quantity), std::abs(signed_qty));
                double pnl_per_unit = (quantity > 0) ? (price - avg_price) : (avg_price - price);
                realized_pnl += pnl_per_unit * closing_qty;
            }

            // Update position and average price
            int64_t new_quantity = quantity + signed_qty;

            if (new_quantity == 0)
            {
                // Position fully closed
                avg_price = 0.0;
                quantity = 0;
            }
            else if ((quantity > 0 && new_quantity < 0) || (quantity < 0 && new_quantity > 0))
            {
                // Position reversed - new position opened in opposite direction
                avg_price = price;
                quantity = new_quantity;
            }
            else if ((quantity >= 0 && new_quantity > quantity) ||
                     (quantity <= 0 && new_quantity < quantity))
            {
                // Position increased in same direction - update average price
                double old_value = std::abs(quantity) * avg_price;
                double new_value = std::abs(signed_qty) * price;
                avg_price = (old_value + new_value) / std::abs(new_quantity);
                quantity = new_quantity;
            }
            else
            {
                // Position reduced but not reversed - avg_price stays the same
                quantity = new_quantity;
            }
        }

        void Position::mark_to_market(double current_price)
        {
            if (quantity == 0)
            {
                unrealized_pnl = 0.0;
                return;
            }

            // Calculate unrealized PnL based on position direction
            if (quantity > 0)
            {
                // Long position: profit when price rises
                unrealized_pnl = (current_price - avg_price) * quantity;
            }
            else
            {
                // Short position: profit when price falls
                unrealized_pnl = (avg_price - current_price) * std::abs(quantity);
            }
        }

    } // namespace agents
} // namespace lob
