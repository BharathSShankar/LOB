#include <gtest/gtest.h>
#include "agents/Agent.h"
#include "agents/MarketState.h"

using namespace lob::agents;
using namespace lob::core;

// Mock agent implementation for testing
class MockAgent : public Agent
{
public:
    MockAgent()
    {
        type_ = AgentType::NOISE_TRADER;
    }

    void tick(const MarketState &state) override
    {
        last_tick_price_ = state.last_price;
        tick_count_++;
    }

    Order *decide(const MarketState &state) override
    {
        if (!should_trade_)
        {
            return nullptr;
        }

        // Simple mock decision: return a test order
        test_order_ = Order(
            agent_id_,
            state.timestamp,
            static_cast<uint64_t>(state.last_price * 100), // Convert to fixed point
            100,
            Side::BUY,
            OrderType::LIMIT);
        return &test_order_;
    }

    void initialize(uint64_t agent_id, const AgentConfig &config) override
    {
        agent_id_ = agent_id;
        config_ = config;
        type_ = config.type;
        active_ = true;
    }

    void reset() override
    {
        position_ = Position();
        tick_count_ = 0;
        last_tick_price_ = 0.0;
        active_ = true;
    }

    // Test helpers
    void set_should_trade(bool value) { should_trade_ = value; }
    int get_tick_count() const { return tick_count_; }
    double get_last_tick_price() const { return last_tick_price_; }

private:
    bool should_trade_ = false;
    int tick_count_ = 0;
    double last_tick_price_ = 0.0;
    Order test_order_;
};

// --- Position Tests ---

TEST(PositionTest, InitialStateIsZero)
{
    Position pos;

    EXPECT_EQ(pos.quantity, 0);
    EXPECT_DOUBLE_EQ(pos.avg_price, 0.0);
    EXPECT_DOUBLE_EQ(pos.realized_pnl, 0.0);
    EXPECT_DOUBLE_EQ(pos.unrealized_pnl, 0.0);
}

TEST(PositionTest, BuyCreatesLongPosition)
{
    Position pos;

    pos.update(Side::BUY, 100, 50.0);

    EXPECT_EQ(pos.quantity, 100);
    EXPECT_DOUBLE_EQ(pos.avg_price, 50.0);
    EXPECT_DOUBLE_EQ(pos.realized_pnl, 0.0);
}

TEST(PositionTest, SellCreatesShortPosition)
{
    Position pos;

    pos.update(Side::SELL, 100, 50.0);

    EXPECT_EQ(pos.quantity, -100);
    EXPECT_DOUBLE_EQ(pos.avg_price, 50.0);
    EXPECT_DOUBLE_EQ(pos.realized_pnl, 0.0);
}

TEST(PositionTest, IncreaseLongPositionUpdatesAvgPrice)
{
    Position pos;

    // Buy 100 at $50
    pos.update(Side::BUY, 100, 50.0);
    EXPECT_EQ(pos.quantity, 100);
    EXPECT_DOUBLE_EQ(pos.avg_price, 50.0);

    // Buy 100 more at $60
    pos.update(Side::BUY, 100, 60.0);
    EXPECT_EQ(pos.quantity, 200);
    EXPECT_DOUBLE_EQ(pos.avg_price, 55.0); // (100*50 + 100*60) / 200
}

TEST(PositionTest, ReduceLongPositionCalculatesRealizedPnL)
{
    Position pos;

    // Buy 100 at $50
    pos.update(Side::BUY, 100, 50.0);

    // Sell 50 at $60 - should realize profit
    pos.update(Side::SELL, 50, 60.0);

    EXPECT_EQ(pos.quantity, 50);
    EXPECT_DOUBLE_EQ(pos.avg_price, 50.0);     // Avg price doesn't change when reducing
    EXPECT_DOUBLE_EQ(pos.realized_pnl, 500.0); // 50 * (60 - 50)
}

TEST(PositionTest, CloseLongPositionCalculatesRealizedPnL)
{
    Position pos;

    // Buy 100 at $50
    pos.update(Side::BUY, 100, 50.0);

    // Sell all at $55
    pos.update(Side::SELL, 100, 55.0);

    EXPECT_EQ(pos.quantity, 0);
    EXPECT_DOUBLE_EQ(pos.avg_price, 0.0);
    EXPECT_DOUBLE_EQ(pos.realized_pnl, 500.0); // 100 * (55 - 50)
}

TEST(PositionTest, CloseShortPositionCalculatesRealizedPnL)
{
    Position pos;

    // Sell 100 at $50
    pos.update(Side::SELL, 100, 50.0);
    EXPECT_EQ(pos.quantity, -100);

    // Buy back at $45 - should realize profit
    pos.update(Side::BUY, 100, 45.0);

    EXPECT_EQ(pos.quantity, 0);
    EXPECT_DOUBLE_EQ(pos.avg_price, 0.0);
    EXPECT_DOUBLE_EQ(pos.realized_pnl, 500.0); // 100 * (50 - 45)
}

TEST(PositionTest, CloseShortPositionWithLossCalculatesRealizedPnL)
{
    Position pos;

    // Sell 100 at $50
    pos.update(Side::SELL, 100, 50.0);

    // Buy back at $55 - should realize loss
    pos.update(Side::BUY, 100, 55.0);

    EXPECT_EQ(pos.quantity, 0);
    EXPECT_DOUBLE_EQ(pos.realized_pnl, -500.0); // 100 * (50 - 55)
}

TEST(PositionTest, ReverseLongToShortCalculatesRealizedPnL)
{
    Position pos;

    // Buy 100 at $50
    pos.update(Side::BUY, 100, 50.0);

    // Sell 150 at $60 - close long and open short
    pos.update(Side::SELL, 150, 60.0);

    EXPECT_EQ(pos.quantity, -50);
    EXPECT_DOUBLE_EQ(pos.avg_price, 60.0);      // New short position at 60
    EXPECT_DOUBLE_EQ(pos.realized_pnl, 1000.0); // Closed 100 * (60 - 50)
}

TEST(PositionTest, MarkToMarketLongPosition)
{
    Position pos;

    // Buy 100 at $50
    pos.update(Side::BUY, 100, 50.0);

    // Mark to market at $55
    pos.mark_to_market(55.0);

    EXPECT_DOUBLE_EQ(pos.unrealized_pnl, 500.0); // 100 * (55 - 50)

    // Mark to market at $45
    pos.mark_to_market(45.0);

    EXPECT_DOUBLE_EQ(pos.unrealized_pnl, -500.0); // 100 * (45 - 50)
}

TEST(PositionTest, MarkToMarketShortPosition)
{
    Position pos;

    // Sell 100 at $50
    pos.update(Side::SELL, 100, 50.0);

    // Mark to market at $45 - profit for short
    pos.mark_to_market(45.0);

    EXPECT_DOUBLE_EQ(pos.unrealized_pnl, 500.0); // 100 * (50 - 45)

    // Mark to market at $55 - loss for short
    pos.mark_to_market(55.0);

    EXPECT_DOUBLE_EQ(pos.unrealized_pnl, -500.0); // 100 * (50 - 55)
}

TEST(PositionTest, MarkToMarketZeroPosition)
{
    Position pos;

    pos.mark_to_market(100.0);

    EXPECT_DOUBLE_EQ(pos.unrealized_pnl, 0.0);
}

// --- AgentConfig Tests ---

TEST(AgentConfigTest, DefaultConstructor)
{
    AgentConfig config;

    EXPECT_EQ(config.type, AgentType::NOISE_TRADER);
    EXPECT_DOUBLE_EQ(config.aggression, 0.5);
    EXPECT_DOUBLE_EQ(config.risk_tolerance, 0.5);
    EXPECT_EQ(config.max_position, 1000);
    EXPECT_DOUBLE_EQ(config.order_size_mean, 100.0);
    EXPECT_DOUBLE_EQ(config.order_size_stddev, 20.0);
}

TEST(AgentConfigTest, CustomParameters)
{
    AgentConfig config;
    config.params["custom_param"] = 42.0;
    config.params["another_param"] = 3.14;

    EXPECT_DOUBLE_EQ(config.params["custom_param"], 42.0);
    EXPECT_DOUBLE_EQ(config.params["another_param"], 3.14);
}

// --- MarketState Tests ---

TEST(MarketStateTest, DefaultConstructor)
{
    MarketState state;

    EXPECT_DOUBLE_EQ(state.last_price, 0.0);
    EXPECT_DOUBLE_EQ(state.best_bid, 0.0);
    EXPECT_DOUBLE_EQ(state.best_ask, 0.0);
    EXPECT_DOUBLE_EQ(state.spread, 0.0);
    EXPECT_EQ(state.timestamp, 0);
    EXPECT_DOUBLE_EQ(state.volume_24h, 0.0);
    EXPECT_DOUBLE_EQ(state.price_sma_50, 0.0);
    EXPECT_DOUBLE_EQ(state.price_sma_200, 0.0);
    EXPECT_DOUBLE_EQ(state.volatility, 0.0);
    EXPECT_EQ(state.bid_depth, 0);
    EXPECT_EQ(state.ask_depth, 0);
}

TEST(MarketStateTest, SetValues)
{
    MarketState state;
    state.last_price = 100.5;
    state.best_bid = 100.0;
    state.best_ask = 101.0;
    state.spread = 1.0;
    state.timestamp = 123456789;

    EXPECT_DOUBLE_EQ(state.last_price, 100.5);
    EXPECT_DOUBLE_EQ(state.best_bid, 100.0);
    EXPECT_DOUBLE_EQ(state.best_ask, 101.0);
    EXPECT_DOUBLE_EQ(state.spread, 1.0);
    EXPECT_EQ(state.timestamp, 123456789);
}

// --- MockAgent Tests ---

TEST(MockAgentTest, InitializeAgent)
{
    MockAgent agent;
    AgentConfig config;
    config.type = AgentType::MARKET_MAKER;
    config.aggression = 0.8;

    agent.initialize(42, config);

    EXPECT_EQ(agent.get_id(), 42);
    EXPECT_EQ(agent.get_type(), AgentType::MARKET_MAKER);
    EXPECT_TRUE(agent.is_active());
}

TEST(MockAgentTest, TickUpdatesState)
{
    MockAgent agent;
    AgentConfig config;
    agent.initialize(1, config);

    MarketState state;
    state.last_price = 100.0;
    state.timestamp = 1000;

    agent.tick(state);

    EXPECT_EQ(agent.get_tick_count(), 1);
    EXPECT_DOUBLE_EQ(agent.get_last_tick_price(), 100.0);

    state.last_price = 105.0;
    agent.tick(state);

    EXPECT_EQ(agent.get_tick_count(), 2);
    EXPECT_DOUBLE_EQ(agent.get_last_tick_price(), 105.0);
}

TEST(MockAgentTest, DecideReturnsNullptrWhenNotTrading)
{
    MockAgent agent;
    AgentConfig config;
    agent.initialize(1, config);

    MarketState state;
    state.last_price = 100.0;

    agent.set_should_trade(false);
    Order *order = agent.decide(state);

    EXPECT_EQ(order, nullptr);
}

TEST(MockAgentTest, DecideReturnsOrderWhenTrading)
{
    MockAgent agent;
    AgentConfig config;
    agent.initialize(1, config);

    MarketState state;
    state.last_price = 100.0;
    state.timestamp = 5000;

    agent.set_should_trade(true);
    Order *order = agent.decide(state);

    ASSERT_NE(order, nullptr);
    EXPECT_EQ(order->order_id, 1);
    EXPECT_EQ(order->timestamp, 5000);
    EXPECT_EQ(order->quantity, 100);
    EXPECT_EQ(order->side, Side::BUY);
}

TEST(MockAgentTest, ResetClearsState)
{
    MockAgent agent;
    AgentConfig config;
    agent.initialize(1, config);

    // Set some state
    MarketState state;
    state.last_price = 100.0;
    agent.tick(state);
    agent.tick(state);

    Position &pos = const_cast<Position &>(agent.get_position());
    pos.update(Side::BUY, 100, 50.0);

    agent.deactivate();

    EXPECT_FALSE(agent.is_active());
    EXPECT_EQ(agent.get_tick_count(), 2);
    EXPECT_EQ(agent.get_position().quantity, 100);

    // Reset
    agent.reset();

    EXPECT_TRUE(agent.is_active());
    EXPECT_EQ(agent.get_tick_count(), 0);
    EXPECT_EQ(agent.get_position().quantity, 0);
}

TEST(MockAgentTest, ActivateDeactivate)
{
    MockAgent agent;
    AgentConfig config;
    agent.initialize(1, config);

    EXPECT_TRUE(agent.is_active());

    agent.deactivate();
    EXPECT_FALSE(agent.is_active());

    agent.activate();
    EXPECT_TRUE(agent.is_active());
}

TEST(MockAgentTest, GetPosition)
{
    MockAgent agent;
    AgentConfig config;
    agent.initialize(1, config);

    const Position &pos = agent.get_position();
    EXPECT_EQ(pos.quantity, 0);

    // Modify position
    Position &mutable_pos = const_cast<Position &>(pos);
    mutable_pos.update(Side::BUY, 50, 100.0);

    EXPECT_EQ(agent.get_position().quantity, 50);
    EXPECT_DOUBLE_EQ(agent.get_position().avg_price, 100.0);
}
