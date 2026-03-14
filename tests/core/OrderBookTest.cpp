#include <gtest/gtest.h>
#include "core/OrderBook.h"
#include "core/Order.h"
#include <chrono>
#include <vector>
#include <random>
#include <thread>
#include <iostream>

using namespace lob::core;

class OrderBookTest : public ::testing::Test
{
protected:
    OrderBook book;

    void SetUp() override
    {
        // Setup code before each test
    }

    void TearDown() override
    {
        // Cleanup code after each test
    }
};

// ============================================================================
// Week 1 Tests - Basic Order Book Operations
// ============================================================================

TEST_F(OrderBookTest, EmptyBookHasNoSpread)
{
    auto spread = book.get_spread();
    EXPECT_FALSE(spread.has_value());
}

TEST_F(OrderBookTest, EmptyBookHasNoBestBid)
{
    auto best_bid = book.get_best_bid();
    EXPECT_FALSE(best_bid.has_value());
}

TEST_F(OrderBookTest, EmptyBookHasNoBestAsk)
{
    auto best_ask = book.get_best_ask();
    EXPECT_FALSE(best_ask.has_value());
}

TEST_F(OrderBookTest, AddSingleBuyOrder)
{
    Order order(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);
    auto trades = book.add_order(&order);

    EXPECT_TRUE(trades.empty()) << "No trades should occur with single order";

    auto best_bid = book.get_best_bid();
    EXPECT_TRUE(best_bid.has_value());
    EXPECT_EQ(*best_bid, 10000);
}

TEST_F(OrderBookTest, AddSingleSellOrder)
{
    Order order(1, 1000, 10100, 100, Side::SELL, OrderType::LIMIT);
    auto trades = book.add_order(&order);

    EXPECT_TRUE(trades.empty());

    auto best_ask = book.get_best_ask();
    EXPECT_TRUE(best_ask.has_value());
    EXPECT_EQ(*best_ask, 10100);
}

TEST_F(OrderBookTest, SpreadCalculation)
{
    Order buy_order(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);
    Order sell_order(2, 1001, 10100, 100, Side::SELL, OrderType::LIMIT);

    book.add_order(&buy_order);
    book.add_order(&sell_order);

    auto spread = book.get_spread();
    EXPECT_TRUE(spread.has_value());
    EXPECT_EQ(*spread, 100);
}

// ============================================================================
// Week 2 Tests - Matching Logic
// ============================================================================

TEST_F(OrderBookTest, SimpleMatch)
{
    Order buy_order(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);
    Order sell_order(2, 1001, 10000, 100, Side::SELL, OrderType::LIMIT);

    book.add_order(&buy_order);
    auto trades = book.add_order(&sell_order);

    EXPECT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 100);
    EXPECT_EQ(trades[0].price, 10000);
}

TEST_F(OrderBookTest, PartialMatch)
{
    Order buy_order(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);
    Order sell_order(2, 1001, 10000, 50, Side::SELL, OrderType::LIMIT);

    book.add_order(&buy_order);
    auto trades = book.add_order(&sell_order);

    EXPECT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 50);
}

TEST_F(OrderBookTest, PriceTimePriority)
{
    // Earlier orders at same price should be matched first
    Order buy1(1, 1000, 10000, 50, Side::BUY, OrderType::LIMIT);
    Order buy2(2, 1001, 10000, 50, Side::BUY, OrderType::LIMIT);
    Order sell(3, 1002, 10000, 75, Side::SELL, OrderType::LIMIT);

    book.add_order(&buy1);
    book.add_order(&buy2);
    auto trades = book.add_order(&sell);

    // Should match with buy1 first (earlier timestamp)
    EXPECT_EQ(trades.size(), 2);
}

TEST_F(OrderBookTest, MarketOrderBuy)
{
    Order sell_order(1, 1000, 10000, 100, Side::SELL, OrderType::LIMIT);
    Order market_buy(2, 1001, 0, 50, Side::BUY, OrderType::MARKET);

    book.add_order(&sell_order);
    auto trades = book.add_order(&market_buy);

    EXPECT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 50);
}

TEST_F(OrderBookTest, CancelOrder)
{
    Order order(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);
    book.add_order(&order);

    bool cancelled = book.cancel_order(1);
    EXPECT_TRUE(cancelled);

    auto best_bid = book.get_best_bid();
    EXPECT_FALSE(best_bid.has_value());
}

TEST_F(OrderBookTest, GetMarketDepth)
{
    std::vector<OrderBook::DepthLevel> bids, asks;
    book.get_market_depth(bids, asks, 5);

    EXPECT_TRUE(bids.empty());
    EXPECT_TRUE(asks.empty());
}

// ============================================================================
// Week 1-2 Extended Tests - Edge Cases & Error Handling
// ============================================================================

TEST_F(OrderBookTest, CancelNonExistentOrder)
{
    // Test canceling an order that doesn't exist
    bool cancelled = book.cancel_order(999);
    EXPECT_FALSE(cancelled);
}

TEST_F(OrderBookTest, CancelSellOrder)
{
    // Test canceling a sell order (different code path)
    Order sell_order(1, 1000, 10000, 100, Side::SELL, OrderType::LIMIT);
    book.add_order(&sell_order);

    bool cancelled = book.cancel_order(1);
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(sell_order.status, OrderStatus::CANCELLED);

    auto best_ask = book.get_best_ask();
    EXPECT_FALSE(best_ask.has_value());
}

TEST_F(OrderBookTest, MarketOrderSell)
{
    // Test market sell order
    Order buy_order(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);
    Order market_sell(2, 1001, 0, 50, Side::SELL, OrderType::MARKET);

    book.add_order(&buy_order);
    auto trades = book.add_order(&market_sell);

    EXPECT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 50);
    EXPECT_EQ(trades[0].price, 10000);
}

TEST_F(OrderBookTest, MarketOrderNoLiquidity)
{
    // Test market order when no matching orders exist
    Order market_buy(1, 1000, 0, 100, Side::BUY, OrderType::MARKET);
    auto trades = book.add_order(&market_buy);

    // Should not create any trades
    EXPECT_EQ(trades.size(), 0);
}

TEST_F(OrderBookTest, MultipleOrdersAtSamePrice)
{
    // Test multiple orders at the same price level
    Order buy1(1, 1000, 10000, 50, Side::BUY, OrderType::LIMIT);
    Order buy2(2, 1001, 10000, 75, Side::BUY, OrderType::LIMIT);
    Order buy3(3, 1002, 10000, 100, Side::BUY, OrderType::LIMIT);

    book.add_order(&buy1);
    book.add_order(&buy2);
    book.add_order(&buy3);

    auto best_bid = book.get_best_bid();
    EXPECT_TRUE(best_bid.has_value());
    EXPECT_EQ(*best_bid, 10000);
}

TEST_F(OrderBookTest, GetMarketDepthWithOrders)
{
    // Test market depth retrieval with actual orders
    Order buy1(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);
    Order buy2(2, 1001, 9900, 50, Side::BUY, OrderType::LIMIT);
    Order sell1(3, 1002, 10100, 75, Side::SELL, OrderType::LIMIT);
    Order sell2(4, 1003, 10200, 25, Side::SELL, OrderType::LIMIT);

    book.add_order(&buy1);
    book.add_order(&buy2);
    book.add_order(&sell1);
    book.add_order(&sell2);

    std::vector<OrderBook::DepthLevel> bids, asks;
    book.get_market_depth(bids, asks, 5);

    EXPECT_EQ(bids.size(), 2);
    EXPECT_EQ(asks.size(), 2);

    // Check bid side (highest price first)
    EXPECT_EQ(bids[0].price, 10000);
    EXPECT_EQ(bids[0].quantity, 100);
    EXPECT_EQ(bids[1].price, 9900);
    EXPECT_EQ(bids[1].quantity, 50);

    // Check ask side (lowest price first)
    EXPECT_EQ(asks[0].price, 10100);
    EXPECT_EQ(asks[0].quantity, 75);
    EXPECT_EQ(asks[1].price, 10200);
    EXPECT_EQ(asks[1].quantity, 25);
}

TEST_F(OrderBookTest, MatchAcrossMultiplePriceLevels)
{
    // Test matching an order against multiple price levels
    Order buy1(1, 1000, 10000, 50, Side::BUY, OrderType::LIMIT);
    Order buy2(2, 1001, 9900, 50, Side::BUY, OrderType::LIMIT);
    Order sell(3, 1002, 9900, 100, Side::SELL, OrderType::LIMIT);

    book.add_order(&buy1);
    book.add_order(&buy2);
    auto trades = book.add_order(&sell);

    // Should match against both buy orders
    EXPECT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].price, 10000); // Higher price first
    EXPECT_EQ(trades[0].quantity, 50);
    EXPECT_EQ(trades[1].price, 9900);
    EXPECT_EQ(trades[1].quantity, 50);
}

TEST_F(OrderBookTest, PartialFillLeavesRemainder)
{
    // Test that partial fill leaves remainder in book
    Order buy_order(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);
    Order sell_order(2, 1001, 10000, 40, Side::SELL, OrderType::LIMIT);

    book.add_order(&buy_order);
    auto trades = book.add_order(&sell_order);

    EXPECT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 40);
    EXPECT_EQ(buy_order.remaining_quantity, 60);
    EXPECT_EQ(buy_order.status, OrderStatus::PARTIAL);

    // Best bid should still be 10000 with remaining quantity
    auto best_bid = book.get_best_bid();
    EXPECT_TRUE(best_bid.has_value());
    EXPECT_EQ(*best_bid, 10000);
}

TEST_F(OrderBookTest, CancelPartiallyFilledOrder)
{
    // Test canceling an order that was partially filled
    Order buy_order(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);
    Order sell_order(2, 1001, 10000, 40, Side::SELL, OrderType::LIMIT);

    book.add_order(&buy_order);
    book.add_order(&sell_order);

    // Cancel the partially filled buy order
    bool cancelled = book.cancel_order(1);
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(buy_order.status, OrderStatus::CANCELLED);

    // Book should be empty now
    auto best_bid = book.get_best_bid();
    EXPECT_FALSE(best_bid.has_value());
}

// ============================================================================
// Week 3-4 Tests - Performance
// ============================================================================

TEST_F(OrderBookTest, HighVolumeOrders)
{
    // Test with high volume of orders to validate performance and correctness
    const int NUM_ORDERS = 10000;
    std::vector<Order> orders;
    orders.reserve(NUM_ORDERS);

    // Create random price generator
    std::mt19937 gen(42); // Fixed seed for reproducibility
    std::uniform_int_distribution<uint64_t> price_dist(9000, 11000);
    std::uniform_int_distribution<uint64_t> qty_dist(10, 100);
    std::uniform_int_distribution<int> side_dist(0, 1);

    auto start = std::chrono::high_resolution_clock::now();

    // Add many orders
    size_t total_trades = 0;
    for (int i = 0; i < NUM_ORDERS; ++i)
    {
        uint64_t price = price_dist(gen);
        uint64_t qty = qty_dist(gen);
        Side side = side_dist(gen) == 0 ? Side::BUY : Side::SELL;

        orders.emplace_back(i + 1, 1000 + i, price, qty, side, OrderType::LIMIT);
        auto trades = book.add_order(&orders.back());
        total_trades += trades.size();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // Print performance metrics
    std::cout << "\n[HighVolumeOrders] Performance Metrics:\n";
    std::cout << "  - Orders processed: " << NUM_ORDERS << "\n";
    std::cout << "  - Trades executed: " << total_trades << "\n";
    std::cout << "  - Total time: " << duration.count() << " µs\n";
    std::cout << "  - Avg time per order: " << (duration.count() / static_cast<double>(NUM_ORDERS)) << " µs\n";

    // Print order book visualization
    std::cout << book.to_string(5);

    // Verify order book is in valid state
    auto best_bid = book.get_best_bid();
    auto best_ask = book.get_best_ask();

    // If there are orders on both sides, spread should be positive or zero (no cross)
    if (best_bid.has_value() && best_ask.has_value())
    {
        EXPECT_GE(*best_ask, *best_bid) << "Order book crossed: ask < bid";
    }

    // Performance assertion: should handle 10k orders in under 1 second
    EXPECT_LT(duration.count(), 1000000) << "Processing 10k orders took over 1 second";
}
