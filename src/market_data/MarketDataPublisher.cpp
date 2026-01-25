#include "market_data/MarketDataPublisher.h"
#include <chrono>

namespace lob::market_data
{

    void MarketDataPublisher::subscribe_level1(Level1Callback callback) noexcept
    {
        // TODO (Week 6): Add subscriber
        if (callback)
        {
            level1_subscribers_.push_back(callback);
        }
    }

    void MarketDataPublisher::subscribe_level2(Level2Callback callback) noexcept
    {
        // TODO (Week 6): Add subscriber
        if (callback)
        {
            level2_subscribers_.push_back(callback);
        }
    }

    void MarketDataPublisher::subscribe_trades(TradeCallback callback) noexcept
    {
        // TODO (Week 6): Add subscriber
        if (callback)
        {
            trade_subscribers_.push_back(callback);
        }
    }

    void MarketDataPublisher::publish_level1(const Level1Data &data) noexcept
    {
        // TODO (Week 6): Notify all Level 1 subscribers
        for (const auto &callback : level1_subscribers_)
        {
            callback(data);
        }
        stats_.total_level1_published++;
    }

    void MarketDataPublisher::publish_level2(const Level2Data &data) noexcept
    {
        // TODO (Week 6): Notify all Level 2 subscribers
        for (const auto &callback : level2_subscribers_)
        {
            callback(data);
        }
        stats_.total_level2_published++;
    }

    void MarketDataPublisher::publish_trade(const TradeData &data) noexcept
    {
        // TODO (Week 6): Notify all trade subscribers
        for (const auto &callback : trade_subscribers_)
        {
            callback(data);
        }
        stats_.total_trades_published++;
    }

    void MarketDataPublisher::publish_trade(const core::Trade &trade) noexcept
    {
        // TODO (Week 6): Convert core::Trade to TradeData and publish
        TradeData data;
        data.timestamp = trade.timestamp;
        data.trade_id = ++trade_id_counter_;
        data.price = trade.price;
        data.quantity = trade.quantity;
        data.is_buyer_maker = false; // Determine from order sides

        publish_trade(data);
    }

    void MarketDataPublisher::publish_snapshot(const core::OrderBook &book,
                                               size_t depth) noexcept
    {
        // TODO (Week 6): Generate snapshot from order book

        // Publish Level 1 (BBO)
        auto best_bid = book.get_best_bid();
        auto best_ask = book.get_best_ask();
        auto spread = book.get_spread();

        if (best_bid && best_ask)
        {
            Level1Data l1_data;
            l1_data.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
            l1_data.best_bid_price = *best_bid;
            l1_data.best_ask_price = *best_ask;
            l1_data.best_bid_quantity = book.get_quantity_at_price(*best_bid, core::Side::BUY);
            l1_data.best_ask_quantity = book.get_quantity_at_price(*best_ask, core::Side::SELL);
            l1_data.spread = spread.value_or(0);

            publish_level1(l1_data);
        }

        // Publish Level 2 (Depth)
        Level2Data l2_data;
        l2_data.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        book.get_market_depth(l2_data.bids, l2_data.asks, depth);

        publish_level2(l2_data);
    }

    MarketDataPublisher::Statistics MarketDataPublisher::get_statistics() const noexcept
    {
        return stats_;
    }

} // namespace lob::market_data
