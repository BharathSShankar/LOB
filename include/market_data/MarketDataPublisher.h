#pragma once

#include "core/OrderBook.h"
#include <vector>
#include <functional>
#include <cstdint>

namespace lob::market_data
{

    /**
     * @brief Market Data Publisher
     *
     * Publishes market data updates (trades, order book snapshots, depth changes)
     * to subscribers (visualization, logging, network broadcast, etc.)
     *
     * Week 6 Focus: This will feed data to the visualization system
     */
    class MarketDataPublisher
    {
    public:
        /**
         * @brief Level 1 Market Data (BBO - Best Bid and Offer)
         */
        struct Level1Data
        {
            uint64_t timestamp;
            uint64_t best_bid_price;
            uint64_t best_bid_quantity;
            uint64_t best_ask_price;
            uint64_t best_ask_quantity;
            uint64_t spread;
        };

        /**
         * @brief Level 2 Market Data (Market Depth)
         */
        struct Level2Data
        {
            uint64_t timestamp;
            std::vector<core::OrderBook::DepthLevel> bids;
            std::vector<core::OrderBook::DepthLevel> asks;
        };

        /**
         * @brief Trade data
         */
        struct TradeData
        {
            uint64_t timestamp;
            uint64_t trade_id;
            uint64_t price;
            uint64_t quantity;
            bool is_buyer_maker;
        };

        // Callback types
        using Level1Callback = std::function<void(const Level1Data &)>;
        using Level2Callback = std::function<void(const Level2Data &)>;
        using TradeCallback = std::function<void(const TradeData &)>;

        MarketDataPublisher() = default;
        ~MarketDataPublisher() = default;

        /**
         * @brief Subscribe to Level 1 data (BBO)
         */
        void subscribe_level1(Level1Callback callback) noexcept;

        /**
         * @brief Subscribe to Level 2 data (Depth)
         */
        void subscribe_level2(Level2Callback callback) noexcept;

        /**
         * @brief Subscribe to trades
         */
        void subscribe_trades(TradeCallback callback) noexcept;

        /**
         * @brief Publish Level 1 update
         */
        void publish_level1(const Level1Data &data) noexcept;

        /**
         * @brief Publish Level 2 update
         */
        void publish_level2(const Level2Data &data) noexcept;

        /**
         * @brief Publish trade
         */
        void publish_trade(const TradeData &data) noexcept;

        /**
         * @brief Publish full trade from core trade object
         */
        void publish_trade(const core::Trade &trade) noexcept;

        /**
         * @brief Generate and publish market data snapshot from order book
         */
        void publish_snapshot(const core::OrderBook &book, size_t depth = 10) noexcept;

        /**
         * @brief Get statistics
         */
        struct Statistics
        {
            uint64_t total_level1_published = 0;
            uint64_t total_level2_published = 0;
            uint64_t total_trades_published = 0;
        };

        Statistics get_statistics() const noexcept;

    private:
        // TODO (Week 6): Implement publisher logic
        // Consider using observer pattern or pub-sub

        std::vector<Level1Callback> level1_subscribers_;
        std::vector<Level2Callback> level2_subscribers_;
        std::vector<TradeCallback> trade_subscribers_;

        Statistics stats_;

        uint64_t trade_id_counter_ = 0;
    };

} // namespace lob::market_data
