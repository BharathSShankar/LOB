#pragma once

#include <cstdint>

namespace lob
{
    namespace agents
    {

        /**
         * @brief Represents the current state of the market
         *
         * This structure encapsulates all market observables that agents
         * use to make trading decisions. It provides a snapshot of:
         * - Price levels (last, bid, ask)
         * - Liquidity metrics (spread, depth)
         * - Technical indicators (moving averages, volatility)
         * - Volume statistics
         */
        struct MarketState
        {
            double last_price;    ///< Most recent trade price
            double best_bid;      ///< Highest bid price in order book
            double best_ask;      ///< Lowest ask price in order book
            double spread;        ///< Bid-ask spread (best_ask - best_bid)
            uint64_t timestamp;   ///< Market state timestamp (microseconds)
            double volume_24h;    ///< 24-hour trading volume
            double price_sma_50;  ///< 50-period Simple Moving Average
            double price_sma_200; ///< 200-period Simple Moving Average
            double volatility;    ///< Recent price standard deviation
            uint32_t bid_depth;   ///< Total quantity in top 5 bid levels
            uint32_t ask_depth;   ///< Total quantity in top 5 ask levels

            /**
             * @brief Default constructor - initializes all fields to zero
             */
            MarketState()
                : last_price(0.0), best_bid(0.0), best_ask(0.0), spread(0.0), timestamp(0), volume_24h(0.0), price_sma_50(0.0), price_sma_200(0.0), volatility(0.0), bid_depth(0), ask_depth(0)
            {
            }
        };

    } // namespace agents
} // namespace lob
