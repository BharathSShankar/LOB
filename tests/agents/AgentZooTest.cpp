#include <gtest/gtest.h>
#include "../../include/agents/AgentZoo.h"

using namespace lob::agents;

class AgentZooTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        zoo = std::make_unique<AgentZoo>();
    }

    void TearDown() override
    {
        zoo.reset();
    }

    std::unique_ptr<AgentZoo> zoo;
};

// ============================================================================
// Basic Agent Spawning Tests
// ============================================================================

TEST_F(AgentZooTest, SpawnNoiseTrader)
{
    AgentConfig config;
    config.type = AgentType::NOISE_TRADER;
    config.aggression = 0.5;
    config.risk_tolerance = 0.5;
    config.max_position = 1000;
    config.order_size_mean = 100.0;
    config.order_size_stddev = 20.0;

    Agent *agent = zoo->spawn_agent(AgentType::NOISE_TRADER, config);

    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(agent->get_type(), AgentType::NOISE_TRADER);
    EXPECT_TRUE(agent->is_active());
    EXPECT_GT(agent->get_id(), 0u);
}

TEST_F(AgentZooTest, SpawnMarketMaker)
{
    AgentConfig config;
    config.type = AgentType::MARKET_MAKER;
    config.aggression = 0.5;
    config.risk_tolerance = 0.5;
    config.max_position = 1000;
    config.order_size_mean = 100.0;
    config.order_size_stddev = 20.0;

    Agent *agent = zoo->spawn_agent(AgentType::MARKET_MAKER, config);

    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(agent->get_type(), AgentType::MARKET_MAKER);
    EXPECT_TRUE(agent->is_active());
    EXPECT_GT(agent->get_id(), 0u);
}

TEST_F(AgentZooTest, SpawnMultipleAgents)
{
    AgentConfig config;
    config.type = AgentType::NOISE_TRADER;

    // Spawn 10 noise traders
    for (int i = 0; i < 10; ++i)
    {
        Agent *agent = zoo->spawn_agent(AgentType::NOISE_TRADER, config);
        ASSERT_NE(agent, nullptr);
    }

    // Check stats
    PopulationStats stats = zoo->get_stats();
    EXPECT_EQ(stats.total_active, 10u);
    EXPECT_EQ(stats.counts_by_type[AgentType::NOISE_TRADER], 10u);
}

TEST_F(AgentZooTest, UniqueAgentIDs)
{
    AgentConfig config;
    config.type = AgentType::NOISE_TRADER;

    // Spawn multiple agents and collect IDs
    std::set<uint64_t> ids;
    for (int i = 0; i < 10; ++i)
    {
        Agent *agent = zoo->spawn_agent(AgentType::NOISE_TRADER, config);
        ASSERT_NE(agent, nullptr);
        ids.insert(agent->get_id());
    }

    // All IDs should be unique
    EXPECT_EQ(ids.size(), 10u);
}

// ============================================================================
// Agent Lifecycle Tests
// ============================================================================

TEST_F(AgentZooTest, KillAgent)
{
    AgentConfig config;
    config.type = AgentType::NOISE_TRADER;

    Agent *agent = zoo->spawn_agent(AgentType::NOISE_TRADER, config);
    ASSERT_NE(agent, nullptr);
    uint64_t agent_id = agent->get_id();

    // Kill agent
    zoo->kill_agent(agent_id);

    // Stats should show no active agents
    PopulationStats stats = zoo->get_stats();
    EXPECT_EQ(stats.total_active, 0u);
}

TEST_F(AgentZooTest, ResetAll)
{
    AgentConfig config;

    // Spawn multiple agents of different types
    zoo->spawn_agent(AgentType::NOISE_TRADER, config);
    zoo->spawn_agent(AgentType::NOISE_TRADER, config);
    zoo->spawn_agent(AgentType::MARKET_MAKER, config);

    // Verify they're active
    PopulationStats stats = zoo->get_stats();
    EXPECT_EQ(stats.total_active, 3u);

    // Reset all
    zoo->reset_all();

    // Stats should show no active agents
    stats = zoo->get_stats();
    EXPECT_EQ(stats.total_active, 0u);
}

// ============================================================================
// Population Management Tests
// ============================================================================

TEST_F(AgentZooTest, SetPopulation)
{
    PopulationConfig config;

    // Configure population
    AgentConfig nt_config;
    nt_config.type = AgentType::NOISE_TRADER;
    config.populations[AgentType::NOISE_TRADER] = TypeConfig(10, nt_config);

    AgentConfig mm_config;
    mm_config.type = AgentType::MARKET_MAKER;
    config.populations[AgentType::MARKET_MAKER] = TypeConfig(5, mm_config);

    // Set population
    zoo->set_population(config);

    // Check stats
    PopulationStats stats = zoo->get_stats();
    EXPECT_EQ(stats.total_active, 15u);
    EXPECT_EQ(stats.counts_by_type[AgentType::NOISE_TRADER], 10u);
    EXPECT_EQ(stats.counts_by_type[AgentType::MARKET_MAKER], 5u);
}

TEST_F(AgentZooTest, GetActiveAgents)
{
    AgentConfig config;

    // Spawn some agents
    zoo->spawn_agent(AgentType::NOISE_TRADER, config);
    zoo->spawn_agent(AgentType::NOISE_TRADER, config);
    zoo->spawn_agent(AgentType::MARKET_MAKER, config);

    // Get active agents
    std::vector<Agent *> agents = zoo->get_active_agents();

    EXPECT_EQ(agents.size(), 3u);

    // Verify all agents are active
    for (Agent *agent : agents)
    {
        EXPECT_TRUE(agent->is_active());
    }
}

// ============================================================================
// Population Stats Tests
// ============================================================================

TEST_F(AgentZooTest, PopulationStats)
{
    AgentConfig config;

    // Spawn mixed population
    zoo->spawn_agent(AgentType::NOISE_TRADER, config);
    zoo->spawn_agent(AgentType::NOISE_TRADER, config);
    zoo->spawn_agent(AgentType::NOISE_TRADER, config);
    zoo->spawn_agent(AgentType::MARKET_MAKER, config);
    zoo->spawn_agent(AgentType::MARKET_MAKER, config);

    PopulationStats stats = zoo->get_stats();

    EXPECT_EQ(stats.total_active, 5u);
    EXPECT_EQ(stats.counts_by_type[AgentType::NOISE_TRADER], 3u);
    EXPECT_EQ(stats.counts_by_type[AgentType::MARKET_MAKER], 2u);
}

// ============================================================================
// Population Config Tests
// ============================================================================

TEST_F(AgentZooTest, PopulationConfigTotalCount)
{
    PopulationConfig config;

    AgentConfig nt_config;
    nt_config.type = AgentType::NOISE_TRADER;
    config.populations[AgentType::NOISE_TRADER] = TypeConfig(10, nt_config);

    AgentConfig mm_config;
    mm_config.type = AgentType::MARKET_MAKER;
    config.populations[AgentType::MARKET_MAKER] = TypeConfig(5, mm_config);

    EXPECT_EQ(config.total_count(), 15u);
}

// ============================================================================
// Preset Population Tests
// ============================================================================

TEST_F(AgentZooTest, BullRunPreset)
{
    PopulationConfig config = create_bull_run_population();

    EXPECT_GT(config.total_count(), 0u);

    // Set and verify
    zoo->set_population(config);
    PopulationStats stats = zoo->get_stats();

    EXPECT_GT(stats.total_active, 0u);
}

TEST_F(AgentZooTest, ConsolidationPreset)
{
    PopulationConfig config = create_consolidation_population();

    EXPECT_GT(config.total_count(), 0u);

    // Set and verify
    zoo->set_population(config);
    PopulationStats stats = zoo->get_stats();

    EXPECT_GT(stats.total_active, 0u);
}

TEST_F(AgentZooTest, FlashCrashPreset)
{
    PopulationConfig config = create_flash_crash_population();

    EXPECT_GT(config.total_count(), 0u);

    // Set and verify
    zoo->set_population(config);
    PopulationStats stats = zoo->get_stats();

    EXPECT_GT(stats.total_active, 0u);
}

// ============================================================================
// Capacity Tests
// ============================================================================

TEST_F(AgentZooTest, MaxAgentsCapacity)
{
    EXPECT_EQ(zoo->max_agents(), 10000u);
}

TEST_F(AgentZooTest, RespawnAfterKill)
{
    AgentConfig config;
    config.type = AgentType::NOISE_TRADER;

    // Spawn, kill, and respawn
    Agent *agent1 = zoo->spawn_agent(AgentType::NOISE_TRADER, config);
    ASSERT_NE(agent1, nullptr);
    uint64_t id1 = agent1->get_id();

    zoo->kill_agent(id1);

    Agent *agent2 = zoo->spawn_agent(AgentType::NOISE_TRADER, config);
    ASSERT_NE(agent2, nullptr);

    // Should get a different ID
    EXPECT_NE(agent2->get_id(), id1);

    PopulationStats stats = zoo->get_stats();
    EXPECT_EQ(stats.total_active, 1u);
}

// ============================================================================
// Population Tuning Tests
// ============================================================================

TEST_F(AgentZooTest, TunePopulationRatios_EqualDistribution)
{
    PopulationConfig config;

    // Tune to equal distribution
    tune_population_ratios(config, 0.333, 0.333, 0.334);

    // Verify total count
    EXPECT_GT(config.total_count(), 0u);

    // Verify all three types are present
    EXPECT_GT(config.populations[AgentType::TREND_FOLLOWER].count, 0u);
    EXPECT_GT(config.populations[AgentType::MEAN_REVERTER].count, 0u);
    EXPECT_GT(config.populations[AgentType::MARKET_MAKER].count, 0u);

    // Apply to zoo
    zoo->set_population(config);
    PopulationStats stats = zoo->get_stats();
    EXPECT_EQ(stats.total_active, config.total_count());
}

TEST_F(AgentZooTest, TunePopulationRatios_MomentumHeavy)
{
    PopulationConfig config;

    // Tune to momentum-heavy (60/20/20)
    tune_population_ratios(config, 0.60, 0.20, 0.20);

    uint32_t total = config.total_count();
    EXPECT_GT(total, 0u);

    // Trend followers should be ~60%
    uint32_t tf_count = config.populations[AgentType::TREND_FOLLOWER].count;
    double tf_pct = static_cast<double>(tf_count) / total;
    EXPECT_NEAR(tf_pct, 0.60, 0.05); // Within 5%

    // Apply to zoo
    zoo->set_population(config);
    PopulationStats stats = zoo->get_stats();
    EXPECT_EQ(stats.total_active, total);
}

TEST_F(AgentZooTest, TunePopulationRatios_ValueHeavy)
{
    PopulationConfig config;

    // Tune to value-heavy (20/60/20)
    tune_population_ratios(config, 0.20, 0.60, 0.20);

    uint32_t total = config.total_count();
    EXPECT_GT(total, 0u);

    // Mean reverters should be ~60%
    uint32_t mr_count = config.populations[AgentType::MEAN_REVERTER].count;
    double mr_pct = static_cast<double>(mr_count) / total;
    EXPECT_NEAR(mr_pct, 0.60, 0.05); // Within 5%

    // Apply to zoo
    zoo->set_population(config);
    PopulationStats stats = zoo->get_stats();
    EXPECT_EQ(stats.total_active, total);
}

TEST_F(AgentZooTest, TunePopulationRatios_NormalizesPercentages)
{
    PopulationConfig config;

    // Provide percentages that don't sum to 1.0
    tune_population_ratios(config, 0.50, 0.30, 0.30); // Sums to 1.10

    // Should still create valid population
    EXPECT_GT(config.total_count(), 0u);

    // Apply to zoo should work
    zoo->set_population(config);
    PopulationStats stats = zoo->get_stats();
    EXPECT_GT(stats.total_active, 0u);
}
