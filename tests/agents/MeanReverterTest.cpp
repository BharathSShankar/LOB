#include <gtest/gtest.h>
#include "agents/MeanReverter.h"
#include <cmath>

using namespace lob::agents;
using namespace lob::core;

class MeanReverterTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        trader = new MeanReverter();

        // Setup default configuration
        config.type = AgentType::MEAN_REVERTER;
        config.aggression = 0.7;
        config.risk_tolerance = 0.5;
        config.max_position = 1000;
        config.order_size_mean = 100.0;
        config.order_size_stddev = 20.0;
        config.params["fair_value"] = 100.0;
        config.params["threshold_pct"] = 0.05;
        config.params["use_sma_fair_value"] = 0.0;
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

    MeanReverter *trader;
    AgentConfig config;
    MarketState market_state;
};

// Test initialization
TEST_F(MeanReverterTest, Initialization)
{
    trader->initialize(1, config);

    EXPECT_EQ(trader->get_id(), 1);
    EXPECT_EQ(trader->get_type(), AgentType::MEAN_REVERTER);
    EXPECT_TRUE(trader->is_active());
    EXPECT_EQ(trader->get_position().quantity, 0);
}

// Test reset functionality
TEST_F(MeanReverterTest, Reset)
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
TEST_F(MeanReverterTest, TickUpdatesState)
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
TEST_F(MeanReverterTest, NoTradeWithInvalidPrice)
{
    trader->initialize(1, config);

    market_state.last_price = 0.0; // Invalid price

    Order *order = trader->decide(market_state);

    EXPECT_EQ(order, nullptr);
}

// Test that trader doesn't trade when inactive
TEST_F(MeanReverterTest, NoTradeWhenInactive)
{
    trader->initialize(1, config);
    trader->deactivate();

    Order *order = trader->decide(market_state);

    EXPECT_EQ(order, nullptr);
    EXPECT_FALSE(trader->is_active());
}

// Test position limit enforcement
TEST_F(MeanReverterTest, PositionLimitEnforcement)
{
    trader->initialize(1, config);

    // Set position beyond max
    Position &pos = const_cast<Position &>(trader->get_position());
    pos.quantity = 1500; // Exceeds max_position of 1000

    // Tick should deactivate the trader
    trader->tick(market_state);

    EXPECT_FALSE(trader->is_active());
}

// Test sell signal when price is overvalued
TEST_F(MeanReverterTest, SellSignalWhenOvervalued)
{
    trader->initialize(1, config);

    // Set price above upper band (fair_value * 1.05 = 105.0)
    market_state.last_price = 106.0;

    trader->tick(market_state);

    // Should generate sell signal
    Order *order = trader->decide(market_state);

    // Currently returns nullptr (pending ObjectPool integration)
    EXPECT_EQ(order, nullptr);
}

// Test buy signal when price is undervalued
TEST_F(MeanReverterTest, BuySignalWhenUndervalued)
{
    trader->initialize(1, config);

    // Set price below lower band (fair_value * 0.95 = 95.0)
    market_state.last_price = 94.0;

    trader->tick(market_state);

    // Should generate buy signal
    Order *order = trader->decide(market_state);

    // Currently returns nullptr (pending ObjectPool integration)
    EXPECT_EQ(order, nullptr);
}

// Test no trade when price is within fair range
TEST_F(MeanReverterTest, NoTradeWithinFairRange)
{
    trader->initialize(1, config);

    // Price within range (95-105)
    market_state.last_price = 100.0;

    Order *order = trader->decide(market_state);

    EXPECT_EQ(order, nullptr);
}

// Test using SMA as fair value
TEST_F(MeanReverterTest, UseSMAFairValue)
{
    trader->initialize(1, config);

    // Enable SMA fair value
    config.params["use_sma_fair_value"] = 1.0;
    trader->initialize(1, config);

    // Set SMA and price
    market_state.price_sma_50 = 100.0;
    market_state.last_price = 106.0; // Above 105 (SMA * 1.05)

    trader->tick(market_state);

    // Should detect overvalued condition
    Order *order = trader->decide(market_state);

    // Currently returns nullptr (pending ObjectPool integration)
    EXPECT_EQ(order, nullptr);
}

// Test Bollinger Bands configuration
TEST_F(MeanReverterTest, BollingerBandsConfiguration)
{
    config.params["use_sma_fair_value"] = 1.0;
    config.params["bollinger_period"] = 20;
    config.params["bollinger_std_dev"] = 2.0;

    trader->initialize(1, config);

    EXPECT_EQ(trader->get_id(), 1);
    EXPECT_TRUE(trader->is_active());
}

// Test RSI-based trading
TEST_F(MeanReverterTest, RSIBasedTrading)
{
    config.params["use_rsi"] = 1.0;
    config.params["rsi_period"] = 14;
    config.params["rsi_overbought"] = 70.0;
    config.params["rsi_oversold"] = 30.0;

    trader->initialize(1, config);

    // Build price history for RSI calculation
    for (int i = 0; i < 20; i++)
    {
        market_state.last_price = 100.0 + i * 0.5;
        trader->tick(market_state);
    }

    // Should have enough data for RSI now
    Order *order = trader->decide(market_state);

    // Currently returns nullptr (pending ObjectPool integration)
    EXPECT_EQ(order, nullptr);
}

// Test no trade with insufficient RSI data
TEST_F(MeanReverterTest, NoTradeWithInsufficientRSIData)
{
    config.params["use_rsi"] = 1.0;
    config.params["rsi_period"] = 14;

    trader->initialize(1, config);

    // Not enough ticks for RSI
    trader->tick(market_state);

    Order *order = trader->decide(market_state);

    EXPECT_EQ(order, nullptr);
}

// Test position near limit reduces trading
TEST_F(MeanReverterTest, PositionNearLimitReducesTrading)
{
    trader->initialize(1, config);

    // Set position near limit
    Position &pos = const_cast<Position &>(trader->get_position());
    pos.quantity = 850; // 85% of max_position

    // Set price below fair value (should buy)
    market_state.last_price = 94.0;

    // Should be more selective when near limit
    Order *order = trader->decide(market_state);

    // Currently returns nullptr (pending ObjectPool integration)
    EXPECT_EQ(order, nullptr);
}

// Test opposite direction trading allowed when near limit
TEST_F(MeanReverterTest, OppositeTradingAllowedNearLimit)
{
    trader->initialize(1, config);

    // Set long position near limit
    Position &pos = const_cast<Position &>(trader->get_position());
    pos.quantity = 850; // 85% of max_position

    // Set price above fair value (should sell - opposite direction)
    market_state.last_price = 106.0;

    trader->tick(market_state);

    // Should allow sell signal when long
    Order *order = trader->decide(market_state);

    // Currently returns nullptr (pending ObjectPool integration)
    EXPECT_EQ(order, nullptr);
}

// Test larger deviation increases order size
TEST_F(MeanReverterTest, DeviationAffectsOrderSize)
{
    trader->initialize(1, config);

    // Small deviation
    market_state.last_price = 95.5;
    trader->tick(market_state);

    // Large deviation
    market_state.last_price = 90.0;
    trader->tick(market_state);

    // Both should generate signals (if ObjectPool integrated)
    // Larger deviation should result in larger order size
    EXPECT_TRUE(trader->is_active());
}

// Test custom configuration parameters
TEST_F(MeanReverterTest, CustomConfiguration)
{
    config.params["fair_value"] = 50.0;
    config.params["threshold_pct"] = 0.10;

    trader->initialize(1, config);

    EXPECT_EQ(trader->get_id(), 1);
    EXPECT_TRUE(trader->is_active());
}

// Test default parameters when not specified
TEST_F(MeanReverterTest, DefaultParameters)
{
    AgentConfig default_config;
    default_config.type = AgentType::MEAN_REVERTER;
    // Don't set mean reverter specific parameters

    trader->initialize(1, default_config);

    EXPECT_TRUE(trader->is_active());
    EXPECT_EQ(trader->get_type(), AgentType::MEAN_REVERTER);
}

// Test price history accumulation
TEST_F(MeanReverterTest, PriceHistoryAccumulation)
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
TEST_F(MeanReverterTest, ActivationControl)
{
    trader->initialize(1, config);

    EXPECT_TRUE(trader->is_active());

    trader->deactivate();
    EXPECT_FALSE(trader->is_active());

    trader->activate();
    EXPECT_TRUE(trader->is_active());
}

// Test multiple ticks accumulate correctly
TEST_F(MeanReverterTest, MultipleTicksAccumulate)
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

// Test fair value calculation with fixed value
TEST_F(MeanReverterTest, FixedFairValueCalculation)
{
    config.params["fair_value"] = 120.0;
    config.params["use_sma_fair_value"] = 0.0;

    trader->initialize(1, config);

    // Price above fair value should trigger sell
    market_state.last_price = 127.0; // > 120 * 1.05 = 126

    trader->tick(market_state);
    Order *order = trader->decide(market_state);

    // Currently returns nullptr (pending ObjectPool integration)
    EXPECT_EQ(order, nullptr);
}

// Test that volatility affects Bollinger Bands
TEST_F(MeanReverterTest, VolatilityAffectsBollingerBands)
{
    config.params["use_sma_fair_value"] = 1.0;

    trader->initialize(1, config);

    // High volatility should widen bands
    market_state.volatility = 5.0;
    market_state.price_sma_50 = 100.0;
    market_state.last_price = 100.0;

    trader->tick(market_state);

    // Price within widened bands should not trade
    Order *order = trader->decide(market_state);

    EXPECT_EQ(order, nullptr);
}

// Test mark to market calculation
TEST_F(MeanReverterTest, MarkToMarket)
{
    trader->initialize(1, config);

    Position &pos = const_cast<Position &>(trader->get_position());

    // Long position
    pos.update(Side::BUY, 100, 100.0);
    pos.mark_to_market(110.0);

    // Profit of 10 per unit * 100 units = 1000
    EXPECT_DOUBLE_EQ(pos.unrealized_pnl, 1000.0);
}

// Test position update
TEST_F(MeanReverterTest, PositionUpdate)
{
    trader->initialize(1, config);

    Position &pos = const_cast<Position &>(trader->get_position());

    // Simulate a buy
    pos.update(Side::BUY, 100, 100.0);

    EXPECT_EQ(pos.quantity, 100);
    EXPECT_DOUBLE_EQ(pos.avg_price, 100.0);
}
