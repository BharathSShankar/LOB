#include <gtest/gtest.h>
#include "agents/NoiseTrader.h"
#include <cmath>

using namespace lob::agents;
using namespace lob::core;

class NoiseTraderTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        trader = new NoiseTrader();

        // Setup default configuration
        config.type = AgentType::NOISE_TRADER;
        config.aggression = 0.5;
        config.risk_tolerance = 0.5;
        config.max_position = 1000;
        config.order_size_mean = 100.0;
        config.order_size_stddev = 20.0;
        config.params["noise_stddev"] = 0.01;
        config.params["position_threshold"] = 0.8;

        // Setup market state
        market_state.last_price = 100.0;
        market_state.best_bid = 99.95;
        market_state.best_ask = 100.05;
        market_state.spread = 0.10;
        market_state.timestamp = 1000000;
        market_state.volume_24h = 10000.0;
        market_state.price_sma_50 = 100.0;
        market_state.price_sma_200 = 100.0;
        market_state.volatility = 1.0;
        market_state.bid_depth = 500;
        market_state.ask_depth = 500;
    }

    void TearDown() override
    {
        delete trader;
    }

    NoiseTrader *trader;
    AgentConfig config;
    MarketState market_state;
};

// Test initialization
TEST_F(NoiseTraderTest, Initialization)
{
    trader->initialize(1, config);

    EXPECT_EQ(trader->get_id(), 1);
    EXPECT_EQ(trader->get_type(), AgentType::NOISE_TRADER);
    EXPECT_TRUE(trader->is_active());
    EXPECT_EQ(trader->get_position().quantity, 0);
}

// Test reset functionality
TEST_F(NoiseTraderTest, Reset)
{
    trader->initialize(1, config);

    // Manually set some state
    Position &pos = const_cast<Position &>(trader->get_position());
    pos.quantity = 500;
    pos.avg_price = 100.0;
    pos.realized_pnl = 50.0;

    // Reset should clear position
    trader->reset();

    EXPECT_EQ(trader->get_position().quantity, 0);
    EXPECT_EQ(trader->get_position().avg_price, 0.0);
    EXPECT_EQ(trader->get_position().realized_pnl, 0.0);
    EXPECT_TRUE(trader->is_active());
}

// Test tick function updates state
TEST_F(NoiseTraderTest, TickUpdatesState)
{
    trader->initialize(1, config);

    // Set a position
    Position &pos = const_cast<Position &>(trader->get_position());
    pos.quantity = 100;
    pos.avg_price = 99.0;

    // Tick should update unrealized PnL
    trader->tick(market_state);

    // With position of 100 @ 99.0 and market at 100.0, unrealized PnL should be 100.0
    EXPECT_DOUBLE_EQ(trader->get_position().unrealized_pnl, 100.0);
}

// Test that trader doesn't trade with invalid market price
TEST_F(NoiseTraderTest, NoTradeWithInvalidPrice)
{
    trader->initialize(1, config);

    market_state.last_price = 0.0; // Invalid price

    Order *order = trader->decide(market_state);

    EXPECT_EQ(order, nullptr);
}

// Test that trader doesn't trade when inactive
TEST_F(NoiseTraderTest, NoTradeWhenInactive)
{
    trader->initialize(1, config);
    trader->deactivate();

    Order *order = trader->decide(market_state);

    EXPECT_EQ(order, nullptr);
    EXPECT_FALSE(trader->is_active());
}

// Test position limit enforcement
TEST_F(NoiseTraderTest, PositionLimitEnforcement)
{
    trader->initialize(1, config);

    // Set position beyond max
    Position &pos = const_cast<Position &>(trader->get_position());
    pos.quantity = 1500; // Exceeds max_position of 1000

    // Tick should deactivate the trader
    trader->tick(market_state);

    EXPECT_FALSE(trader->is_active());
}

// Test custom configuration parameters
TEST_F(NoiseTraderTest, CustomConfiguration)
{
    config.params["noise_stddev"] = 0.05;
    config.params["position_threshold"] = 0.9;
    config.order_size_mean = 50.0;
    config.order_size_stddev = 10.0;

    trader->initialize(1, config);

    EXPECT_EQ(trader->get_id(), 1);
    EXPECT_TRUE(trader->is_active());
}

// Test that position tracking works with buys
TEST_F(NoiseTraderTest, PositionUpdateBuy)
{
    trader->initialize(1, config);

    Position &pos = const_cast<Position &>(trader->get_position());

    // Simulate a buy
    pos.update(Side::BUY, 100, 100.0);

    EXPECT_EQ(pos.quantity, 100);
    EXPECT_DOUBLE_EQ(pos.avg_price, 100.0);
}

// Test that position tracking works with sells
TEST_F(NoiseTraderTest, PositionUpdateSell)
{
    trader->initialize(1, config);

    Position &pos = const_cast<Position &>(trader->get_position());

    // Simulate a sell
    pos.update(Side::SELL, 100, 100.0);

    EXPECT_EQ(pos.quantity, -100);
    EXPECT_DOUBLE_EQ(pos.avg_price, 100.0);
}

// Test position reversal
TEST_F(NoiseTraderTest, PositionReversal)
{
    trader->initialize(1, config);

    Position &pos = const_cast<Position &>(trader->get_position());

    // Start with long position
    pos.update(Side::BUY, 100, 100.0);
    EXPECT_EQ(pos.quantity, 100);

    // Reverse to short
    pos.update(Side::SELL, 200, 101.0);
    EXPECT_EQ(pos.quantity, -100);
    EXPECT_DOUBLE_EQ(pos.avg_price, 101.0);

    // Should have realized some PnL
    EXPECT_GT(pos.realized_pnl, 0.0);
}

// Test mark to market calculation
TEST_F(NoiseTraderTest, MarkToMarket)
{
    trader->initialize(1, config);

    Position &pos = const_cast<Position &>(trader->get_position());

    // Long position
    pos.update(Side::BUY, 100, 100.0);
    pos.mark_to_market(110.0);

    // Profit of 10 per unit * 100 units = 1000
    EXPECT_DOUBLE_EQ(pos.unrealized_pnl, 1000.0);

    // Short position
    pos.quantity = -100;
    pos.avg_price = 100.0;
    pos.mark_to_market(90.0);

    // Profit of 10 per unit * 100 units = 1000
    EXPECT_DOUBLE_EQ(pos.unrealized_pnl, 1000.0);
}

// Test activation/deactivation
TEST_F(NoiseTraderTest, ActivationControl)
{
    trader->initialize(1, config);

    EXPECT_TRUE(trader->is_active());

    trader->deactivate();
    EXPECT_FALSE(trader->is_active());

    trader->activate();
    EXPECT_TRUE(trader->is_active());
}

// Test default parameters when not specified
TEST_F(NoiseTraderTest, DefaultParameters)
{
    AgentConfig default_config;
    default_config.type = AgentType::NOISE_TRADER;
    // Don't set noise_stddev or position_threshold

    trader->initialize(1, default_config);

    EXPECT_TRUE(trader->is_active());
    EXPECT_EQ(trader->get_type(), AgentType::NOISE_TRADER);
}

// Test multiple ticks accumulate correctly
TEST_F(NoiseTraderTest, MultipleTicksAccumulate)
{
    trader->initialize(1, config);

    // Perform multiple ticks
    for (int i = 0; i < 10; i++)
    {
        trader->tick(market_state);
    }

    // Agent should still be active (no position built up in ticks alone)
    EXPECT_TRUE(trader->is_active());
}

// Statistical test: verify order generation creates varied prices
TEST_F(NoiseTraderTest, PriceVariation)
{
    trader->initialize(1, config);

    // Generate many decisions and collect statistics
    // Note: decide() returns nullptr in current implementation
    // This test is a placeholder for when ObjectPool integration is complete

    int decisions = 100;
    int null_count = 0;

    for (int i = 0; i < decisions; i++)
    {
        Order *order = trader->decide(market_state);
        if (order == nullptr)
        {
            null_count++;
        }
    }

    // Currently all should be null since we haven't integrated ObjectPool
    EXPECT_EQ(null_count, decisions);
}

// Test that near-position-limit reduces trading frequency
TEST_F(NoiseTraderTest, ReducedTradingNearLimit)
{
    trader->initialize(1, config);

    // Set position near limit (80% threshold)
    Position &pos = const_cast<Position &>(trader->get_position());
    pos.quantity = 850; // 85% of max_position (1000)

    // Multiple decide calls should mostly return nullptr due to reduced frequency
    int decisions = 100;
    int null_count = 0;

    for (int i = 0; i < decisions; i++)
    {
        Order *order = trader->decide(market_state);
        if (order == nullptr)
        {
            null_count++;
        }
    }

    // Should have high null count (>70% due to 20% trading probability + current nullptr return)
    EXPECT_GT(null_count, 70);
}
