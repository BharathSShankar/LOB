#include "core/OrderBook.h"
#include <algorithm>

namespace lob::core
{

    // ============================================================================
    // PriceLevel Implementation
    // ============================================================================

    PriceLevel::PriceLevel(uint64_t price) noexcept
        : price_(price), total_quantity_(0)
    {
    }

    void PriceLevel::add_order(Order *order) noexcept
    {
        // TODO (Week 1): Add order to FIFO queue
        // Update total_quantity_
        orders_.push_back(order);
        total_quantity_ += order->remaining_quantity;
    }

    bool PriceLevel::remove_order(uint64_t order_id) noexcept
    {
        // TODO (Week 1): Find and remove order from queue
        // Update total_quantity_
        for (auto it = orders_.begin(); it != orders_.end(); ++it)
        {
            if ((*it)->order_id == order_id)
            {
                total_quantity_ -= (*it)->remaining_quantity;
                orders_.erase(it);
                return true;
            }
        }
        return false;
    }

    uint64_t PriceLevel::get_total_quantity() const noexcept
    {
        return total_quantity_;
    }

    bool PriceLevel::is_empty() const noexcept
    {
        return orders_.empty();
    }

    const std::deque<Order *> &PriceLevel::get_orders() const noexcept
    {
        return orders_;
    }

    // ============================================================================
    // OrderBook Implementation
    // ============================================================================

    std::vector<Trade> OrderBook::add_order(Order *order) noexcept
    {
        // TODO (Week 1-2): Implement order adding logic
        // 1. Check order type (LIMIT vs MARKET)
        // 2. Try to match against opposite side
        // 3. If not fully filled, add remaining to book

        std::vector<Trade> trades;

        if (order->type == OrderType::MARKET)
        {
            // TODO: Match market order
            trades = match_market_order(order);
        }
        else if (order->type == OrderType::LIMIT)
        {
            // TODO: Match limit order
            trades = match_limit_order(order);
        }

        return trades;
    }

    bool OrderBook::cancel_order(uint64_t order_id) noexcept
    {
        auto it = order_map_.find(order_id);
        if (it == order_map_.end())
        {
            return false; // Order not found
        }

        Order *order = it->second;

        if (order->side == Side::BUY)
        {
            auto pl_it = bids_.find(order->price);
            if (pl_it != bids_.end())
            {
                PriceLevel &pl = pl_it->second;
                if (pl.remove_order(order_id))
                {
                    order->cancel();
                    if (pl.is_empty())
                    {
                        bids_.erase(pl_it);
                    }
                    order_map_.erase(it);
                    return true;
                }
            }
        }
        else
        {
            auto pl_it = asks_.find(order->price);
            if (pl_it != asks_.end())
            {
                PriceLevel &pl = pl_it->second;
                if (pl.remove_order(order_id))
                {
                    order->cancel();
                    if (pl.is_empty())
                    {
                        asks_.erase(pl_it);
                    }
                    order_map_.erase(it);
                    return true;
                }
            }
        }
        return false; // Order not found in price level
    }

    std::vector<Order *> OrderBook::drain_filled_orders() noexcept
    {
        std::vector<Order *> result;
        result.swap(filled_orders_);
        return result;
    }

    std::optional<uint64_t> OrderBook::get_best_bid() const noexcept
    {
        // TODO (Week 1): Return highest bid price
        if (bids_.empty())
        {
            return std::nullopt;
        }
        return bids_.begin()->first;
    }

    std::optional<uint64_t> OrderBook::get_best_ask() const noexcept
    {
        // TODO (Week 1): Return lowest ask price
        if (asks_.empty())
        {
            return std::nullopt;
        }
        return asks_.begin()->first;
    }

    std::optional<uint64_t> OrderBook::get_spread() const noexcept
    {
        // TODO (Week 1): Calculate spread
        auto best_bid = get_best_bid();
        auto best_ask = get_best_ask();

        if (!best_bid || !best_ask)
        {
            return std::nullopt;
        }

        return *best_ask - *best_bid;
    }

    uint64_t OrderBook::get_quantity_at_price(uint64_t price, Side side) const noexcept
    {
        if (side == Side::BUY)
        {
            auto it = bids_.find(price);
            if (it != bids_.end())
            {
                return it->second.get_total_quantity();
            }
        }
        else
        {
            auto it = asks_.find(price);
            if (it != asks_.end())
            {
                return it->second.get_total_quantity();
            }
        }
        return 0;
    }

    void OrderBook::get_market_depth(std::vector<DepthLevel> &bids,
                                     std::vector<DepthLevel> &asks,
                                     size_t levels) const noexcept
    {
        bids.clear();
        asks.clear();
        bids.reserve(levels);
        asks.reserve(levels);

        // Collect top N bid levels (already sorted by price descending)
        size_t count = 0;
        for (const auto &[price, level] : bids_)
        {
            if (count >= levels)
                break;
            bids.push_back({price, level.get_total_quantity()});
            ++count;
        }

        // Collect top N ask levels (already sorted by price ascending)
        count = 0;
        for (const auto &[price, level] : asks_)
        {
            if (count >= levels)
                break;
            asks.push_back({price, level.get_total_quantity()});
            ++count;
        }
    }

    std::vector<Trade> OrderBook::match_limit_order(Order *order) noexcept
    {
        std::vector<Trade> trades;

        if (order->side == Side::BUY)
        {
            // Match against asks at price <= order price
            while (order->remaining_quantity > 0 && !asks_.empty())
            {
                auto best_ask_it = asks_.begin();
                if (best_ask_it->first > order->price)
                {
                    break; // No more matching prices
                }

                PriceLevel &level = best_ask_it->second;
                auto &orders_at_level = const_cast<std::deque<Order *> &>(level.get_orders());

                while (order->remaining_quantity > 0 && !orders_at_level.empty())
                {
                    Order *resting = orders_at_level.front();
                    uint64_t match_qty = std::min(order->remaining_quantity, resting->remaining_quantity);

                    Trade trade = execute_trade(order, resting, match_qty);
                    trades.push_back(trade);

                    if (resting->is_filled())
                    {
                        order_map_.erase(resting->order_id);
                        orders_at_level.pop_front();
                        filled_orders_.push_back(resting);
                    }
                }

                // Remove empty price level
                if (level.is_empty())
                {
                    asks_.erase(best_ask_it);
                }
            }

            // Add remaining quantity to book if not fully filled
            if (order->remaining_quantity > 0 && order->is_active())
            {
                bids_[order->price].add_order(order);
                order_map_[order->order_id] = order;
            }
        }
        else // SELL
        {
            // Match against bids at price >= order price
            while (order->remaining_quantity > 0 && !bids_.empty())
            {
                auto best_bid_it = bids_.begin();
                if (best_bid_it->first < order->price)
                {
                    break; // No more matching prices
                }

                PriceLevel &level = best_bid_it->second;
                auto &orders_at_level = const_cast<std::deque<Order *> &>(level.get_orders());

                while (order->remaining_quantity > 0 && !orders_at_level.empty())
                {
                    Order *resting = orders_at_level.front();
                    uint64_t match_qty = std::min(order->remaining_quantity, resting->remaining_quantity);

                    Trade trade = execute_trade(order, resting, match_qty);
                    trades.push_back(trade);

                    if (resting->is_filled())
                    {
                        order_map_.erase(resting->order_id);
                        orders_at_level.pop_front();
                        filled_orders_.push_back(resting);
                    }
                }

                // Remove empty price level
                if (level.is_empty())
                {
                    bids_.erase(best_bid_it);
                }
            }

            // Add remaining quantity to book if not fully filled
            if (order->remaining_quantity > 0 && order->is_active())
            {
                asks_[order->price].add_order(order);
                order_map_[order->order_id] = order;
            }
        }

        return trades;
    }

    std::vector<Trade> OrderBook::match_market_order(Order *order) noexcept
    {
        std::vector<Trade> trades;

        if (order->side == Side::BUY)
        {
            // Execute at best available ask prices until filled
            while (order->remaining_quantity > 0 && !asks_.empty())
            {
                auto best_ask_it = asks_.begin();
                PriceLevel &level = best_ask_it->second;
                auto &orders_at_level = const_cast<std::deque<Order *> &>(level.get_orders());

                while (order->remaining_quantity > 0 && !orders_at_level.empty())
                {
                    Order *resting = orders_at_level.front();
                    uint64_t match_qty = std::min(order->remaining_quantity, resting->remaining_quantity);

                    Trade trade = execute_trade(order, resting, match_qty);
                    trades.push_back(trade);

                    if (resting->is_filled())
                    {
                        order_map_.erase(resting->order_id);
                        orders_at_level.pop_front();
                        filled_orders_.push_back(resting);
                    }
                }

                if (level.is_empty())
                {
                    asks_.erase(best_ask_it);
                }
            }
        }
        else // SELL
        {
            // Execute at best available bid prices until filled
            while (order->remaining_quantity > 0 && !bids_.empty())
            {
                auto best_bid_it = bids_.begin();
                PriceLevel &level = best_bid_it->second;
                auto &orders_at_level = const_cast<std::deque<Order *> &>(level.get_orders());

                while (order->remaining_quantity > 0 && !orders_at_level.empty())
                {
                    Order *resting = orders_at_level.front();
                    uint64_t match_qty = std::min(order->remaining_quantity, resting->remaining_quantity);

                    Trade trade = execute_trade(order, resting, match_qty);
                    trades.push_back(trade);

                    if (resting->is_filled())
                    {
                        order_map_.erase(resting->order_id);
                        orders_at_level.pop_front();
                        filled_orders_.push_back(resting);
                    }
                }

                if (level.is_empty())
                {
                    bids_.erase(best_bid_it);
                }
            }
        }

        // Market orders are not added to the book - they either fill or expire
        return trades;
    }

    Trade OrderBook::execute_trade(Order *incoming, Order *resting,
                                   uint64_t quantity) noexcept
    {
        // Determine buy and sell order IDs
        uint64_t buy_id = (incoming->side == Side::BUY) ? incoming->order_id : resting->order_id;
        uint64_t sell_id = (incoming->side == Side::SELL) ? incoming->order_id : resting->order_id;

        // Update both orders' remaining quantities
        incoming->fill(quantity);
        resting->fill(quantity);

        // Create trade at resting order's price
        Trade trade;
        trade.buy_order_id = buy_id;
        trade.sell_order_id = sell_id;
        trade.price = resting->price;
        trade.quantity = quantity;
        trade.timestamp = incoming->timestamp; // Use incoming order's timestamp

        return trade;
    }

    void OrderBook::print_book(size_t levels, std::ostream &out) const noexcept
    {
        out << to_string(levels);
    }

    std::string OrderBook::to_string(size_t levels) const noexcept
    {
        std::ostringstream ss;

        // Get market depth
        std::vector<DepthLevel> bid_levels, ask_levels;
        get_market_depth(bid_levels, ask_levels, levels);

        // Find max quantity for scaling the bar chart
        uint64_t max_qty = 1;
        for (const auto &level : bid_levels)
            max_qty = std::max(max_qty, level.quantity);
        for (const auto &level : ask_levels)
            max_qty = std::max(max_qty, level.quantity);

        const int bar_width = 30;
        auto make_bar = [bar_width, max_qty](uint64_t qty, bool left_align) -> std::string
        {
            int filled = static_cast<int>((static_cast<double>(qty) / max_qty) * bar_width);
            if (filled == 0 && qty > 0)
                filled = 1;
            std::string bar;
            if (left_align)
            {
                bar = std::string(bar_width - filled, ' ') + std::string(filled, '#');
            }
            else
            {
                bar = std::string(filled, '#') + std::string(bar_width - filled, ' ');
            }
            return bar;
        };

        ss << "\n";
        ss << "╔══════════════════════════════════════════════════════════════════════════════════╗\n";
        ss << "║                              ORDER BOOK                                          ║\n";
        ss << "╠══════════════════════════════════════════════════════════════════════════════════╣\n";

        // Get best bid/ask and spread
        auto best_bid = get_best_bid();
        auto best_ask = get_best_ask();
        auto spread = get_spread();

        ss << "║ Best Bid: " << std::setw(10) << (best_bid.has_value() ? std::to_string(*best_bid) : "N/A");
        ss << "  │  Best Ask: " << std::setw(10) << (best_ask.has_value() ? std::to_string(*best_ask) : "N/A");
        ss << "  │  Spread: " << std::setw(8) << (spread.has_value() ? std::to_string(*spread) : "N/A") << " ║\n";

        ss << "╠══════════════════════════════════════════════════════════════════════════════════╣\n";
        ss << "║           BIDS (BUY)                  │            ASKS (SELL)                   ║\n";
        ss << "║   Quantity      Price     Volume      │      Price     Quantity     Volume       ║\n";
        ss << "╠───────────────────────────────────────┼──────────────────────────────────────────╣\n";

        // Display levels side by side
        size_t max_levels = std::max(bid_levels.size(), ask_levels.size());
        for (size_t i = 0; i < max_levels; ++i)
        {
            ss << "║ ";

            // Bid side
            if (i < bid_levels.size())
            {
                std::string bar = make_bar(bid_levels[i].quantity, true);
                ss << std::setw(10) << bid_levels[i].quantity << "  "
                   << std::setw(10) << bid_levels[i].price << "  "
                   << bar.substr(bar.length() - 15);
            }
            else
            {
                ss << std::setw(37) << " ";
            }

            ss << " │ ";

            // Ask side
            if (i < ask_levels.size())
            {
                std::string bar = make_bar(ask_levels[i].quantity, false);
                ss << std::setw(10) << ask_levels[i].price << "  "
                   << std::setw(10) << ask_levels[i].quantity << "  "
                   << bar.substr(0, 15);
            }
            else
            {
                ss << std::setw(38) << " ";
            }

            ss << " ║\n";
        }

        if (max_levels == 0)
        {
            ss << "║                  (empty)              │              (empty)                     ║\n";
        }

        ss << "╚══════════════════════════════════════════════════════════════════════════════════╝\n";

        return ss.str();
    }

} // namespace lob::core
