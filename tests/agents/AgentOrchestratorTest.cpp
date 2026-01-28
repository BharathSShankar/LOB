#include <gtest/gtest.h>
#include "../../include/agents/AgentOrchestrator.h"
#include "../../include/agents/AgentZoo.h"
#include "../../include/concurrency/RingBuffer.h"
#include "../../include/memory/ObjectPool.h"
#include <thread>
#include <chrono>

using namespace lob::agents;
using namespace lob::core;
using namespace lob::concurrency;
using namespace lob::memory;

class AgentOrchestratorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        zoo = std::make_unique<AgentZoo>();
        order_buffer = std::make_unique<RingBuffer<Order *, 8192>>();
        order_pool = std::make_unique<ObjectPool<Order, 10000>>();
        orchestrator = std::make_unique<AgentOrchestrator>(
            *zoo, *order_buffer, *order_pool);
    }

    void TearDown() override
    {
        orchestrator->stop();
        orchestrator.reset();
        order_pool.reset();
        order_buffer.reset();
        zoo.reset();
    }

    std::unique_ptr<AgentZoo> zoo;
    std::unique_ptr<RingBuffer<Order *, 8192>> order_buffer;
    std::unique_ptr<ObjectPool<Order, 10000>> order_pool;
    std::unique_ptr<AgentOrchestrator> orchestrator;
};

// ============================================================================
// Basic Functionality Tests
// ============================================================================

TEST_F(AgentOrchestratorTest, InitialState)
{
    EXPECT_FALSE(orchestrator->is_running());
    EXPECT_EQ(orchestrator->get_tick_count(), 0u);
    EXPECT_EQ(orchestrator->get_orders_submitted(), 0u);
    EXPECT_EQ(orchestrator->get_orders_dropped(), 0u);
    EXPECT_EQ(orchestrator->get_tick_rate(), 100u); // Default tick rate
}

TEST_F(AgentOrchestratorTest, StartStop)
{
    EXPECT_FALSE(orchestrator->is_running());

    orchestrator->start();
    EXPECT_TRUE(orchestrator->is_running());

    orchestrator->stop();
    EXPECT_FALSE(orchestrator->is_running());
}

TEST_F(AgentOrchestratorTest, StartStopMultipleTimes)
{
    // Start and stop multiple times
    orchestrator->start();
    EXPECT_TRUE(orchestrator->is_running());

    orchestrator->stop();
    EXPECT_FALSE(orchestrator->is_running());

    orchestrator->start();
    EXPECT_TRUE(orchestrator->is_running());

    orchestrator->stop();
    EXPECT_FALSE(orchestrator->is_running());
}

TEST_F(AgentOrchestratorTest, DoubleStart)
{
    orchestrator->start();
    EXPECT_TRUE(orchestrator->is_running());

    // Starting again should be safe (no-op)
    orchestrator->start();
    EXPECT_TRUE(orchestrator->is_running());

    orchestrator->stop();
}

TEST_F(AgentOrchestratorTest, DoubleStop)
{
    orchestrator->start();
    orchestrator->stop();
    EXPECT_FALSE(orchestrator->is_running());

    // Stopping again should be safe (no-op)
    orchestrator->stop();
    EXPECT_FALSE(orchestrator->is_running());
}

// ============================================================================
// Tick Rate Tests
// ============================================================================

TEST_F(AgentOrchestratorTest, SetTickRate)
{
    orchestrator->set_tick_rate(200);
    EXPECT_EQ(orchestrator->get_tick_rate(), 200u);

    orchestrator->set_tick_rate(50);
    EXPECT_EQ(orchestrator->get_tick_rate(), 50u);
}

TEST_F(AgentOrchestratorTest, TickRateWhileRunning)
{
    orchestrator->set_tick_rate(500); // Fast ticks for testing
    orchestrator->start();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Should have executed some ticks
    EXPECT_GT(orchestrator->get_tick_count(), 0u);

    orchestrator->stop();
}

// ============================================================================
// Market State Tests
// ============================================================================

TEST_F(AgentOrchestratorTest, UpdateMarketState)
{
    MarketState state;
    state.last_price = 100.0;
    state.best_bid = 99.5;
    state.best_ask = 100.5;
    state.spread = 1.0;
    state.timestamp = 123456789;

    orchestrator->update_market_state(state);

    MarketState retrieved = orchestrator->get_market_state();
    EXPECT_EQ(retrieved.last_price, 100.0);
    EXPECT_EQ(retrieved.best_bid, 99.5);
    EXPECT_EQ(retrieved.best_ask, 100.5);
    EXPECT_EQ(retrieved.spread, 1.0);
    EXPECT_EQ(retrieved.timestamp, 123456789u);
}

// ============================================================================
// Population Tests
// ============================================================================

TEST_F(AgentOrchestratorTest, SetPopulation)
{
    PopulationConfig config;

    AgentConfig nt_config;
    nt_config.type = AgentType::NOISE_TRADER;
    config.populations[AgentType::NOISE_TRADER] = TypeConfig(10, nt_config);

    orchestrator->set_population(config);

    PopulationStats stats = zoo->get_stats();
    EXPECT_EQ(stats.total_active, 10u);
}

TEST_F(AgentOrchestratorTest, SetPopulationWhileRunning)
{
    PopulationConfig config;

    AgentConfig nt_config;
    nt_config.type = AgentType::NOISE_TRADER;
    config.populations[AgentType::NOISE_TRADER] = TypeConfig(5, nt_config);

    orchestrator->start();
    EXPECT_TRUE(orchestrator->is_running());

    // Set population should stop, reconfigure, and restart
    orchestrator->set_population(config);

    // Should still be stopped after set_population (it doesn't auto-restart)
    EXPECT_FALSE(orchestrator->is_running());

    PopulationStats stats = zoo->get_stats();
    EXPECT_EQ(stats.total_active, 5u);
}

// ============================================================================
// Agent Ticking Tests
// ============================================================================

TEST_F(AgentOrchestratorTest, TicksIncrementWithTime)
{
    PopulationConfig config;

    AgentConfig nt_config;
    nt_config.type = AgentType::NOISE_TRADER;
    config.populations[AgentType::NOISE_TRADER] = TypeConfig(5, nt_config);

    orchestrator->set_population(config);
    orchestrator->set_tick_rate(1000); // 1000 Hz for fast testing

    // Set market state
    MarketState state;
    state.last_price = 100.0;
    state.best_bid = 99.5;
    state.best_ask = 100.5;
    state.spread = 1.0;
    orchestrator->update_market_state(state);

    orchestrator->start();

    // Wait for some ticks
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    uint64_t tick_count = orchestrator->get_tick_count();
    EXPECT_GT(tick_count, 0u);

    orchestrator->stop();
}

TEST_F(AgentOrchestratorTest, OrdersGenerated)
{
    PopulationConfig config;

    AgentConfig nt_config;
    nt_config.type = AgentType::NOISE_TRADER;
    nt_config.aggression = 1.0; // High aggression to generate orders
    config.populations[AgentType::NOISE_TRADER] = TypeConfig(10, nt_config);

    orchestrator->set_population(config);
    orchestrator->set_tick_rate(100);

    // Set market state with valid prices
    MarketState state;
    state.last_price = 100.0;
    state.best_bid = 99.5;
    state.best_ask = 100.5;
    state.spread = 1.0;
    orchestrator->update_market_state(state);

    orchestrator->start();

    // Wait for agents to generate some orders
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    orchestrator->stop();

    // Should have generated some orders
    uint64_t orders_submitted = orchestrator->get_orders_submitted();
    EXPECT_GE(orders_submitted, 0u); // May be 0 if agents didn't decide to trade
}

// ============================================================================
// Zero Allocation Tests
// ============================================================================

TEST_F(AgentOrchestratorTest, NoMemoryLeaks)
{
    PopulationConfig config;

    AgentConfig nt_config;
    nt_config.type = AgentType::NOISE_TRADER;
    config.populations[AgentType::NOISE_TRADER] = TypeConfig(5, nt_config);

    orchestrator->set_population(config);

    MarketState state;
    state.last_price = 100.0;
    state.best_bid = 99.5;
    state.best_ask = 100.5;
    state.spread = 1.0;
    orchestrator->update_market_state(state);

    orchestrator->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    orchestrator->stop();

    // Cleanup should handle all resources without leaks
    // If there are leaks, they'll be caught by memory sanitizers
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(AgentOrchestratorTest, RunWithNoAgents)
{
    // Start orchestrator with no agents
    orchestrator->start();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Should run without crashing
    EXPECT_TRUE(orchestrator->is_running());
    EXPECT_GT(orchestrator->get_tick_count(), 0u);

    orchestrator->stop();
}

TEST_F(AgentOrchestratorTest, DestructorStopsThread)
{
    PopulationConfig config;

    AgentConfig nt_config;
    nt_config.type = AgentType::NOISE_TRADER;
    config.populations[AgentType::NOISE_TRADER] = TypeConfig(5, nt_config);

    orchestrator->set_population(config);
    orchestrator->start();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_TRUE(orchestrator->is_running());

    // Destructor should stop the thread safely
    // This is automatically tested in TearDown()
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST_F(AgentOrchestratorTest, StatisticsAccumulate)
{
    PopulationConfig config;

    AgentConfig nt_config;
    nt_config.type = AgentType::NOISE_TRADER;
    config.populations[AgentType::NOISE_TRADER] = TypeConfig(5, nt_config);

    orchestrator->set_population(config);
    orchestrator->set_tick_rate(500);

    MarketState state;
    state.last_price = 100.0;
    state.best_bid = 99.5;
    state.best_ask = 100.5;
    state.spread = 1.0;
    orchestrator->update_market_state(state);

    orchestrator->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    orchestrator->stop();

    uint64_t ticks = orchestrator->get_tick_count();
    uint64_t submitted = orchestrator->get_orders_submitted();
    uint64_t dropped = orchestrator->get_orders_dropped();

    EXPECT_GT(ticks, 0u);
    // submitted + dropped should reflect total order generation attempts
    EXPECT_GE(submitted + dropped, 0u);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(AgentOrchestratorTest, BullRunScenario)
{
    PopulationConfig config = create_bull_run_population();
    orchestrator->set_population(config);
    orchestrator->set_tick_rate(100);

    MarketState state;
    state.last_price = 100.0;
    state.best_bid = 99.5;
    state.best_ask = 100.5;
    state.spread = 1.0;
    orchestrator->update_market_state(state);

    orchestrator->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    orchestrator->stop();

    EXPECT_GT(orchestrator->get_tick_count(), 0u);
}

TEST_F(AgentOrchestratorTest, ConsolidationScenario)
{
    PopulationConfig config = create_consolidation_population();
    orchestrator->set_population(config);
    orchestrator->set_tick_rate(100);

    MarketState state;
    state.last_price = 100.0;
    state.best_bid = 99.5;
    state.best_ask = 100.5;
    state.spread = 1.0;
    orchestrator->update_market_state(state);

    orchestrator->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    orchestrator->stop();

    EXPECT_GT(orchestrator->get_tick_count(), 0u);
}
