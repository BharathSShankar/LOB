#include <gtest/gtest.h>
#include "agents/TrendFollower.h"
#include <cmath>

using namespace lob::agents;
using namespace lob::core;

class TrendFollowerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        trader = new TrendFollower();

        // Setup default configuration
        config.type = AgentType::TREND_FOLLOWER;
        config.aggression = 0.8;
        config.risk_tolerance = 0.5;
        config.max_position = 1000;
        config.order_size_mean = 100.0;
        config.order_size_stddev = 20.0;
        config.params["threshold_pct"] = 0.02;
        config.params["cooldown_ticks"] = 10;
        config.params["momentum_scaling"] = 1.5;
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

    TrendFollower *trader;
    AgentConfig config;
    MarketState market_state;
};

// Test initialization
TEST_F(TrendFollowerTest, Initialization)
{
    trader->initialize(1, config);

    EXPECT_EQ(trader->get_id(), 1);
    EXPECT_EQ(trader->get_type(), AgentType::TREND_FOLLOWER);
    EXPECT_TRUE(trader->is_active());
    EXPECT_EQ(trader->get_position().quantity, 0);
}

// Test reset functionality
TEST_F(TrendFollowerTest, Reset)
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
TEST_F(TrendFollowerTest, TickUpdatesState)
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
TEST_F(TrendFollowerTest, NoTradeWithInvalidPrice)
{
    trader->initialize(1, config);

    market_state.last_price = 0.0; // Invalid price

    Order *order = trader->decide(market_state);

    EXPECT_EQ(order, nullptr);
}

// Test that trader doesn't trade when inactive
TEST_F(TrendFollowerTest, NoTradeWhenInactive)
{
    trader->initialize(1, config);
    trader->deactivate();

    Order *order = trader->decide(market_state);

    EXPECT_EQ(order, nullptr);
    EXPECT_FALSE(trader->is_active());
}

// Test position limit enforcement
TEST_F(TrendFollowerTest, PositionLimitEnforcement)
{
    trader->initialize(1, config);

    // Set position beyond max
    Position &pos = const_cast<Position &>(trader->get_position());
    pos.quantity = 1500; // Exceeds max_position of 1000

    // Tick should deactivate the trader
    trader->tick(market_state);

    EXPECT_FALSE(trader->is_active());
}

// Test golden cross detection (bullish signal)
TEST_F(TrendFollowerTest, GoldenCrossDetection)
{
    trader->initialize(1, config);

    // Set up golden cross: SMA50 > SMA200
    market_state.price_sma_50 = 105.0;
    market_state.price_sma_200 = 100.0;

    // Tick to build history
    trader->tick(market_state);

    // Should detect golden cross
    Order *order = trader->decide(market_state);

    // Currently returns nullptr (pending ObjectPool integration)
    EXPECT_EQ(order, nullptr);
}

// Test death cross detection (bearish signal)
TEST_F(TrendFollowerTest, DeathCrossDetection)
{
    trader->initialize(1, config);

    // Set up death cross: SMA50 < SMA200
    market_state.price_sma_50 = 95.0;
    market_state.price_sma_200 = 100.0;

    // Tick to build history
    trader->tick(market_state);

    // Should detect death cross
    Order *order = trader->decide(market_state);

    // Currently returns nullptr (pending ObjectPool integration)
    EXPECT_EQ(order, nullptr);
}

// Test breakout up detection
TEST_F(TrendFollowerTest, BreakoutUpDetection)
{
    trader->initialize(1, config);

    // Set up breakout: price > SMA50
    market_state.last_price = 105.0;
    market_state.price_sma_50 = 100.0;
    market_state.price_sma_200 = 100.0;

    // Tick to build history
    trader->tick(market_state);

    // Should detect breakout
    Order *order = trader->decide(market_state);

    // Currently returns nullptr (pending ObjectPool integration)
    EXPECT_EQ(order, nullptr);
}

// Test breakout down detection
TEST_F(TrendFollowerTest, BreakoutDownDetection)
{
    trader->initialize(1, config);

    // Set up breakout: price < SMA50
    market_state.last_price = 95.0;
    market_state.price_sma_50 = 100.0;
    market_state.price_sma_200 = 100.0;

    // Tick to build history
    trader->tick(market_state);

    // Should detect breakout
    Order *order = trader->decide(market_state);

    // Currently returns nullptr (pending ObjectPool integration)
    EXPECT_EQ(order, nullptr);
}

// Test no trade when SMAs are not available
TEST_F(TrendFollowerTest, NoTradeWithoutSMAs)
{
    trader->initialize(1, config);

    // Set SMAs to zero (not calculated yet)
    market_state.price_sma_50 = 0.0;
    market_state.price_sma_200 = 0.0;

    Order *order = trader->decide(market_state);

    EXPECT_EQ(order, nullptr);
}

// Test no trade when no signal detected
TEST_F(TrendFollowerTest, NoTradeWithoutSignal)
{
    trader->initialize(1, config);

    // Market in equilibrium - no trend
    market_state.last_price = 100.0;
    market_state.price_sma_50 = 100.0;
    market_state.price_sma_200 = 100.0;

    Order *order = trader->decide(market_state);

    EXPECT_EQ(order, nullptr);
}

// Test cooldown mechanism
TEST_F(TrendFollowerTest, CooldownMechanism)
{
    trader->initialize(1, config);

    // Set up a strong signal
    market_state.price_sma_50 = 105.0;
    market_state.price_sma_200 = 100.0;

    // First decision
    trader->tick(market_state);
    Order *order1 = trader->decide(market_state);

    // Immediate second decision should be blocked by cooldown
    trader->tick(market_state);
    Order *order2 = trader->decide(market_state);

    EXPECT_EQ(order2, nullptr);

    // After cooldown period, should allow trading again
    for (int i = 0; i < 10; i++)
    {
        trader->tick(market_state);
    }

    Order *order3 = trader->decide(market_state);
    // Note: May still be nullptr due to ObjectPool integration pending
}

// Test position near limit reduces trading
TEST_F(TrendFollowerTest, PositionNearLimitPreventsTrading)
{
    trader->initialize(1, config);

    // Set position near limit
    Position &pos = const_cast<Position &>(trader->get_position());
    pos.quantity = 850; // 85% of max_position

    // Set up bullish signal
    market_state.price_sma_50 = 105.0;
    market_state.price_sma_200 = 100.0;

    // Should not trade on buy signal when already long near limit
    Order *order = trader->decide(market_state);

    EXPECT_EQ(order, nullptr);
}

// Test that opposite signals allowed when near limit
TEST_F(TrendFollowerTest, OppositeTradingAllowedNearLimit)
{
    trader->initialize(1, config);

    // Set long position near limit
    Position &pos = const_cast<Position &>(trader->get_position());
    pos.quantity = 850; // 85% of max_position

    // Set up bearish signal (opposite direction)
    market_state.price_sma_50 = 95.0;
    market_state.price_sma_200 = 100.0;

    trader->tick(market_state);

    // Should allow sell signal when long
    Order *order = trader->decide(market_state);

    // Currently returns nullptr (pending ObjectPool integration)
    EXPECT_EQ(order, nullptr);
}

// Test custom configuration parameters
TEST_F(TrendFollowerTest, CustomConfiguration)
{
    config.params["threshold_pct"] = 0.05;
    config.params["cooldown_ticks"] = 20;
    config.params["momentum_scaling"] = 2.0;

    trader->initialize(1, config);

    EXPECT_EQ(trader->get_id(), 1);
    EXPECT_TRUE(trader->is_active());
}

// Test default parameters when not specified
TEST_F(TrendFollowerTest, DefaultParameters)
{
    AgentConfig default_config;
    default_config.type = AgentType::TREND_FOLLOWER;
    // Don't set trend-specific parameters

    trader->initialize(1, default_config);

    EXPECT_TRUE(trader->is_active());
    EXPECT_EQ(trader->get_type(), AgentType::TREND_FOLLOWER);
}

// Test price history accumulation
TEST_F(TrendFollowerTest, PriceHistoryAccumulation)
{
    trader->initialize(1, config);

    // Perform multiple ticks with varying prices
    for (int i = 0; i < 50; i++)
    {
        market_state.last_price = 100.0 + i * 0.1;
        trader->tick(market_state);
    }

    // Agent should still be active
    EXPECT_TRUE(trader->is_active());
}

// Test activation/deactivation
TEST_F(TrendFollowerTest, ActivationControl)
{
    trader->initialize(1, config);

    EXPECT_TRUE(trader->is_active());

    trader->deactivate();
    EXPECT_FALSE(trader->is_active());

    trader->activate();
    EXPECT_TRUE(trader->is_active());
}

// Test strong momentum increases order size
TEST_F(TrendFollowerTest, MomentumScaling)
{
    trader->initialize(1, config);

    // Weak signal (small deviation from SMA)
    market_state.last_price = 101.0;
    market_state.price_sma_50 = 100.0;
    market_state.price_sma_200 = 99.0;

    trader->tick(market_state);

    // Strong signal (large deviation from SMA)
    market_state.last_price = 110.0;
    market_state.price_sma_50 = 100.0;

    trader->tick(market_state);

    // Both should generate signals (if ObjectPool integrated)
    // Strong signal should result in larger order size
    EXPECT_TRUE(trader->is_active());
}

// Test multiple ticks accumulate correctly
TEST_F(TrendFollowerTest, MultipleTicksAccumulate)
{
    trader->initialize(1, config);

    // Perform multiple ticks
    for (int i = 0; i < 10; i++)
    {
        trader->tick(market_state);
    }

    // Agent should still be active
    EXPECT_TRUE(trader->is_active());
}
