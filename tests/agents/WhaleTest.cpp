#include <gtest/gtest.h>
#include "agents/Whale.h"
#include <cmath>

using namespace lob::agents;
using namespace lob::core;

class WhaleTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        trader = new Whale();

        // Setup default configuration
        config.type = AgentType::WHALE;
        config.aggression = 1.0;
        config.risk_tolerance = 1.0;
        config.max_position = 100000; // Whales have large limits
        config.order_size_mean = 10000.0;
        config.order_size_stddev = 1000.0;
        config.params["trigger_tick"] = 0;
        config.params["whale_side"] = 1.0; // SELL
        config.params["whale_size"] = 10000;
        config.params["execution_mode"] = 0; // INSTANT
        config.params["slice_size"] = 100;
        config.params["slices_interval"] = 10;

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

    Whale *trader;
    AgentConfig config;
    MarketState market_state;
};

// Test initialization
TEST_F(WhaleTest, Initialization)
{
    // Set trigger_tick to a future value to prevent immediate triggering
    config.params["trigger_tick"] = 100;
    trader->initialize(1, config);

    EXPECT_EQ(trader->get_id(), 1);
    EXPECT_EQ(trader->get_type(), AgentType::WHALE);
    EXPECT_TRUE(trader->is_active());
    EXPECT_FALSE(trader->is_triggered());
    // Before triggering, remaining_quantity is 0, so is_complete() returns true
    // This is expected behavior - whale is "complete" until triggered
    EXPECT_EQ(trader->get_remaining_quantity(), 0);
}

// Test reset functionality
TEST_F(WhaleTest, Reset)
{
    trader->initialize(1, config);

    // Trigger the whale
    trader->tick(market_state);

    // Reset should clear state
    trader->reset();

    EXPECT_FALSE(trader->is_triggered());
    EXPECT_EQ(trader->get_remaining_quantity(), 0);
    EXPECT_TRUE(trader->is_active());
}

// Test immediate trigger (trigger_tick = 0)
TEST_F(WhaleTest, ImmediateTrigger)
{
    config.params["trigger_tick"] = 0;
    trader->initialize(1, config);

    EXPECT_FALSE(trader->is_triggered());

    // First tick should trigger
    trader->tick(market_state);

    EXPECT_TRUE(trader->is_triggered());
    EXPECT_EQ(trader->get_remaining_quantity(), 10000);
}

// Test delayed trigger
TEST_F(WhaleTest, DelayedTrigger)
{
    config.params["trigger_tick"] = 100;
    trader->initialize(1, config);

    // Before trigger tick
    for (int i = 0; i < 99; i++)
    {
        trader->tick(market_state);
    }

    EXPECT_FALSE(trader->is_triggered());

    // At trigger tick
    trader->tick(market_state);

    EXPECT_TRUE(trader->is_triggered());
}

// Test instant execution mode
TEST_F(WhaleTest, InstantExecutionMode)
{
    config.params["execution_mode"] = 0; // INSTANT
    config.params["whale_size"] = 5000;
    trader->initialize(1, config);

    // Trigger the whale
    trader->tick(market_state);

    EXPECT_TRUE(trader->is_triggered());

    // First decision should execute entire order
    trader->decide(market_state);

    EXPECT_TRUE(trader->is_complete());
    EXPECT_EQ(trader->get_remaining_quantity(), 0);
}

// Test iceberg execution mode
TEST_F(WhaleTest, IcebergExecutionMode)
{
    config.params["execution_mode"] = 1; // ICEBERG
    config.params["whale_size"] = 1000;
    config.params["slice_size"] = 100;
    trader->initialize(1, config);

    // Trigger the whale
    trader->tick(market_state);

    EXPECT_TRUE(trader->is_triggered());
    EXPECT_FALSE(trader->is_complete());

    // Execute slices
    int slices = 0;
    while (!trader->is_complete() && slices < 20)
    {
        trader->decide(market_state);
        slices++;
    }

    // Should have executed around 10 slices (1000 / 100)
    EXPECT_TRUE(trader->is_complete());
    EXPECT_GE(slices, 8);  // At least 8 slices (accounting for randomness)
    EXPECT_LE(slices, 12); // At most 12 slices (accounting for randomness)
}

// Test TWAP execution mode
TEST_F(WhaleTest, TWAPExecutionMode)
{
    config.params["execution_mode"] = 2; // TWAP
    config.params["whale_size"] = 500;
    config.params["slice_size"] = 100;
    config.params["slices_interval"] = 5;
    trader->initialize(1, config);

    // Trigger the whale
    trader->tick(market_state);

    EXPECT_TRUE(trader->is_triggered());

    // First slice should execute immediately
    trader->decide(market_state);
    EXPECT_FALSE(trader->is_complete());

    // Next few ticks shouldn't execute (waiting for interval)
    for (int i = 0; i < 4; i++)
    {
        trader->tick(market_state);
        Order *order = trader->decide(market_state);
        EXPECT_EQ(order, nullptr);
    }

    // After interval, should execute next slice
    trader->tick(market_state);
    trader->decide(market_state);

    // Continue until complete
    int ticks = 0;
    while (!trader->is_complete() && ticks < 100)
    {
        trader->tick(market_state);
        trader->decide(market_state);
        ticks++;
    }

    EXPECT_TRUE(trader->is_complete());
}

// Test price trigger - above
TEST_F(WhaleTest, PriceTriggerAbove)
{
    config.params["trigger_tick"] = 0; // Could trigger any time
    config.params["price_trigger"] = 105.0;
    config.params["trigger_above"] = 1.0; // Trigger when price >= 105
    trader->initialize(1, config);

    // Price below trigger
    market_state.last_price = 104.0;
    trader->tick(market_state);

    EXPECT_FALSE(trader->is_triggered());

    // Price reaches trigger
    market_state.last_price = 105.0;
    trader->tick(market_state);

    EXPECT_TRUE(trader->is_triggered());
}

// Test price trigger - below
TEST_F(WhaleTest, PriceTriggerBelow)
{
    config.params["trigger_tick"] = 0;
    config.params["price_trigger"] = 95.0;
    config.params["trigger_above"] = 0.0; // Trigger when price <= 95
    trader->initialize(1, config);

    // Price above trigger
    market_state.last_price = 96.0;
    trader->tick(market_state);

    EXPECT_FALSE(trader->is_triggered());

    // Price reaches trigger
    market_state.last_price = 95.0;
    trader->tick(market_state);

    EXPECT_TRUE(trader->is_triggered());
}

// Test buy side whale
TEST_F(WhaleTest, BuySideWhale)
{
    config.params["whale_side"] = 0.0; // BUY
    config.params["whale_size"] = 1000;
    trader->initialize(1, config);

    trader->tick(market_state);

    EXPECT_TRUE(trader->is_triggered());
    EXPECT_EQ(trader->get_remaining_quantity(), 1000);
}

// Test sell side whale (flash crash scenario)
TEST_F(WhaleTest, SellSideWhale)
{
    config.params["whale_side"] = 1.0; // SELL
    config.params["whale_size"] = 5000;
    trader->initialize(1, config);

    trader->tick(market_state);

    EXPECT_TRUE(trader->is_triggered());
    EXPECT_EQ(trader->get_remaining_quantity(), 5000);
}

// Test that whale doesn't trade when inactive
TEST_F(WhaleTest, NoTradeWhenInactive)
{
    trader->initialize(1, config);
    trader->tick(market_state);
    trader->deactivate();

    Order *order = trader->decide(market_state);

    EXPECT_EQ(order, nullptr);
    EXPECT_FALSE(trader->is_active());
}

// Test that whale doesn't trade with invalid price
TEST_F(WhaleTest, NoTradeWithInvalidPrice)
{
    trader->initialize(1, config);

    market_state.last_price = 0.0; // Invalid price
    trader->tick(market_state);

    Order *order = trader->decide(market_state);

    EXPECT_EQ(order, nullptr);
}

// Test that whale doesn't trade before trigger
TEST_F(WhaleTest, NoTradeBeforeTrigger)
{
    config.params["trigger_tick"] = 1000;
    trader->initialize(1, config);

    trader->tick(market_state);

    Order *order = trader->decide(market_state);

    EXPECT_EQ(order, nullptr);
    EXPECT_FALSE(trader->is_triggered());
}

// Test that whale doesn't trade after completion
TEST_F(WhaleTest, NoTradeAfterCompletion)
{
    config.params["execution_mode"] = 0; // INSTANT
    config.params["whale_size"] = 100;
    trader->initialize(1, config);

    trader->tick(market_state);
    trader->decide(market_state); // Execute entire order

    EXPECT_TRUE(trader->is_complete());

    // Subsequent decisions should return nullptr
    Order *order = trader->decide(market_state);

    EXPECT_EQ(order, nullptr);
}

// Test custom configuration
TEST_F(WhaleTest, CustomConfiguration)
{
    config.params["whale_size"] = 50000;
    config.params["slice_size"] = 500;
    config.params["slices_interval"] = 20;

    trader->initialize(1, config);

    EXPECT_EQ(trader->get_id(), 1);
    EXPECT_TRUE(trader->is_active());
}

// Test default parameters
TEST_F(WhaleTest, DefaultParameters)
{
    AgentConfig default_config;
    default_config.type = AgentType::WHALE;
    // Don't set whale-specific parameters

    trader->initialize(1, default_config);

    EXPECT_TRUE(trader->is_active());
    EXPECT_EQ(trader->get_type(), AgentType::WHALE);
}

// Test activation/deactivation
TEST_F(WhaleTest, ActivationControl)
{
    trader->initialize(1, config);

    EXPECT_TRUE(trader->is_active());

    trader->deactivate();
    EXPECT_FALSE(trader->is_active());

    trader->activate();
    EXPECT_TRUE(trader->is_active());
}

// Test tick counting
TEST_F(WhaleTest, TickCounting)
{
    config.params["trigger_tick"] = 50;
    trader->initialize(1, config);

    // Tick multiple times
    for (int i = 0; i < 100; i++)
    {
        trader->tick(market_state);
    }

    // Should have triggered at tick 50
    EXPECT_TRUE(trader->is_triggered());
}

// Test mark to market
TEST_F(WhaleTest, MarkToMarket)
{
    trader->initialize(1, config);

    Position &pos = const_cast<Position &>(trader->get_position());

    // Simulate a position
    pos.update(Side::SELL, 1000, 100.0);
    pos.mark_to_market(95.0);

    // Profit of 5 per unit * 1000 units = 5000 (for short position)
    EXPECT_DOUBLE_EQ(pos.unrealized_pnl, 5000.0);
}

// Test large whale order
TEST_F(WhaleTest, LargeWhaleOrder)
{
    config.params["whale_size"] = 100000;
    trader->initialize(1, config);

    trader->tick(market_state);

    EXPECT_TRUE(trader->is_triggered());
    EXPECT_EQ(trader->get_remaining_quantity(), 100000);
}

// Test multiple slices completion
TEST_F(WhaleTest, MultipleSlicesCompletion)
{
    config.params["execution_mode"] = 1; // ICEBERG
    config.params["whale_size"] = 550;
    config.params["slice_size"] = 100;
    trader->initialize(1, config);

    trader->tick(market_state);

    // Execute all slices
    while (!trader->is_complete())
    {
        trader->decide(market_state);
    }

    EXPECT_EQ(trader->get_remaining_quantity(), 0);
    EXPECT_TRUE(trader->is_complete());
}
