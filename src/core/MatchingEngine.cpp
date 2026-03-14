#include "core/MatchingEngine.h"

namespace lob::core
{

    void MatchingEngine::initialize() noexcept
    {
        // Initialize object pool (heap-allocated to avoid stack overflow)
        if (!order_pool_)
        {
            order_pool_ = std::make_unique<memory::ObjectPool<Order, 1000000>>();
        }

        // Create default order book
        create_order_book(DEFAULT_INSTRUMENT);
    }

    std::vector<Trade> MatchingEngine::process_order(Order *order) noexcept
    {
        std::vector<Trade> trades;

        if (!order)
        {
            return trades;
        }

        // 1. Validate order
        if (!validate_order(order))
        {
            order->status = OrderStatus::REJECTED;
            stats_.total_orders_rejected++;
            return trades;
        }

        // 2. Get or create order book for instrument
        // For now, use default instrument
        auto *book = get_order_book(DEFAULT_INSTRUMENT);
        if (!book)
        {
            order->status = OrderStatus::REJECTED;
            stats_.total_orders_rejected++;
            return trades;
        }

        // 3. Process order based on type
        if (order->type == OrderType::CANCEL)
        {
            stats_.total_orders_cancelled++;
        }
        else
        {
            // Add/match order in book
            trades = book->add_order(order);
            stats_.total_orders_processed++;
            stats_.total_trades_executed += trades.size();

            // Update volume statistics
            for (const auto &trade : trades)
            {
                stats_.total_volume += trade.quantity;
            }
        }

        return trades;
    }

    bool MatchingEngine::cancel_order(uint64_t order_id,
                                      const std::string &instrument) noexcept
    {
        auto *book = get_order_book(instrument);
        if (!book)
        {
            return false;
        }

        bool cancelled = book->cancel_order(order_id);
        if (cancelled)
        {
            stats_.total_orders_cancelled++;
        }

        return cancelled;
    }

    OrderBook *MatchingEngine::get_order_book(const std::string &instrument) noexcept
    {
        auto it = order_books_.find(instrument);
        if (it != order_books_.end())
        {
            return it->second.get();
        }
        return nullptr;
    }

    void MatchingEngine::create_order_book(const std::string &instrument) noexcept
    {
        if (order_books_.find(instrument) == order_books_.end())
        {
            order_books_[instrument] = std::make_unique<OrderBook>();
        }
    }

    MatchingEngine::Statistics MatchingEngine::get_statistics() const noexcept
    {
        return stats_;
    }

    void MatchingEngine::reset_statistics() noexcept
    {
        stats_ = Statistics{};
    }

    memory::ObjectPool<Order, 1000000> &MatchingEngine::get_order_pool() noexcept
    {
        return *order_pool_;
    }

    MatchingEngine::PoolStats MatchingEngine::get_pool_statistics() const noexcept
    {
        PoolStats pool_stats;
        if (order_pool_)
        {
            pool_stats.available = order_pool_->available();
            pool_stats.capacity = order_pool_->capacity();
            pool_stats.in_use = pool_stats.capacity - pool_stats.available;
        }
        return pool_stats;
    }

    bool MatchingEngine::validate_order(const Order *order) const noexcept
    {
        if (!order)
        {
            return false;
        }

        if (order->order_id == 0)
        {
            return false;
        }

        // Market orders don't need price validation
        if (order->type == OrderType::LIMIT && order->price == 0)
        {
            return false;
        }

        if (order->quantity == 0)
        {
            return false;
        }

        return true;
    }

} // namespace lob::core
