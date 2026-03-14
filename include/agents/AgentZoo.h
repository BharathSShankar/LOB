#pragma once

#include "Agent.h"
#include "NoiseTrader.h"
#include "MarketMaker.h"
#include "TrendFollower.h"
#include "MeanReverter.h"
#include "Whale.h"
#include "../memory/ObjectPool.h"
#include <array>
#include <atomic>
#include <vector>
#include <unordered_map>

namespace lob
{
    namespace agents
    {

        /**
         * @brief Population statistics for active agents
         */
        struct PopulationStats
        {
            uint32_t total_active;                                  ///< Total number of active agents
            std::unordered_map<AgentType, uint32_t> counts_by_type; ///< Count of agents by type

            PopulationStats() : total_active(0) {}
        };

        /**
         * @brief Configuration for a single agent type's population
         */
        struct TypeConfig
        {
            uint32_t count;          ///< Number of agents of this type
            AgentConfig base_config; ///< Base configuration for this agent type

            TypeConfig() : count(0) {}
            TypeConfig(uint32_t c, const AgentConfig &config) : count(c), base_config(config) {}
        };

        /**
         * @brief Configuration for the entire agent population
         */
        struct PopulationConfig
        {
            std::unordered_map<AgentType, TypeConfig> populations;

            /**
             * @brief Calculate total agent count across all types
             * @return Total number of agents
             */
            uint32_t total_count() const
            {
                uint32_t total = 0;
                for (const auto &[type, config] : populations)
                {
                    total += config.count;
                }
                return total;
            }

            /**
             * @brief Normalize population counts to fit within pool limits
             */
            void normalize();
        };

        /**
         * @brief Agent Pool ("The Zoo") - Pre-allocated collection of trading agents
         *
         * This class manages the lifecycle of all trading agents in the simulation.
         * All agents are pre-allocated at startup in separate object pools by type,
         * ensuring zero dynamic allocation during runtime.
         *
         * Key Features:
         * - Zero heap allocation during simulation
         * - O(1) agent spawning and cleanup
         * - Type-safe agent management
         * - Population composition control
         *
         * Design:
         * - Each agent type has its own ObjectPool
         * - Active agents tracked in a flat array for iteration
         * - Unique agent IDs assigned atomically
         */
        class AgentZoo
        {
        public:
            /**
             * @brief Construct agent zoo with default pool sizes
             */
            AgentZoo();

            /**
             * @brief Destructor
             */
            ~AgentZoo() = default;

            // Non-copyable, non-movable
            AgentZoo(const AgentZoo &) = delete;
            AgentZoo &operator=(const AgentZoo &) = delete;

            /**
             * @brief Spawn a new agent of specified type
             * @param type Type of agent to spawn
             * @param config Configuration for the agent
             * @return Pointer to spawned agent, nullptr if pool exhausted
             */
            Agent *spawn_agent(AgentType type, const AgentConfig &config);

            /**
             * @brief Deactivate and return agent to pool
             * @param agent_id ID of agent to kill
             */
            void kill_agent(uint64_t agent_id);

            /**
             * @brief Reset all agents and clear active list
             */
            void reset_all();

            /**
             * @brief Set population based on configuration
             * @param config Population configuration
             *
             * Clears existing population and spawns new agents according to config.
             */
            void set_population(const PopulationConfig &config);

            /**
             * @brief Get all currently active agents
             * @return Vector of pointers to active agents
             */
            std::vector<Agent *> get_active_agents();

            /**
             * @brief Get population statistics
             * @return Current population stats
             */
            PopulationStats get_stats() const;

            /**
             * @brief Get maximum total agents supported
             * @return Maximum agent capacity
             */
            constexpr uint32_t max_agents() const { return MAX_TOTAL_AGENTS; }

        private:
            /**
             * @brief Add agent to active tracking
             * @param agent Pointer to agent
             */
            void add_to_active(Agent *agent);

            /**
             * @brief Remove agent from active tracking
             * @param agent_id ID of agent to remove
             */
            void remove_from_active(uint64_t agent_id);

            // Pool capacities per agent type
            static constexpr size_t NOISE_TRADER_CAPACITY = 5000;
            static constexpr size_t MARKET_MAKER_CAPACITY = 3000;
            static constexpr size_t TREND_FOLLOWER_CAPACITY = 2000;
            static constexpr size_t MEAN_REVERTER_CAPACITY = 2000;
            static constexpr size_t WHALE_CAPACITY = 10;
            static constexpr size_t MAX_TOTAL_AGENTS = 10000;

            // Pre-allocated pools for each agent type
            memory::ObjectPool<NoiseTrader, NOISE_TRADER_CAPACITY> noise_traders_;
            memory::ObjectPool<MarketMaker, MARKET_MAKER_CAPACITY> market_makers_;
            memory::ObjectPool<TrendFollower, TREND_FOLLOWER_CAPACITY> trend_followers_;
            memory::ObjectPool<MeanReverter, MEAN_REVERTER_CAPACITY> mean_reverters_;
            memory::ObjectPool<Whale, WHALE_CAPACITY> whales_;

            // Active agents tracking
            std::array<Agent *, MAX_TOTAL_AGENTS> active_agents_;
            uint32_t active_count_;

            // Atomic counter for unique agent IDs
            std::atomic<uint64_t> next_agent_id_;
        };

        /**
         * @brief Create a bull run population preset
         * @return Population configuration for bull trend
         *
         * Characteristics:
         * - High proportion of trend followers (40%)
         * - Low proportion of mean reverters (10%)
         * - Market makers for liquidity (50%)
         */
        PopulationConfig create_bull_run_population();

        /**
         * @brief Create a consolidation (sideways) population preset
         * @return Population configuration for range-bound market
         *
         * Characteristics:
         * - No trend followers
         * - High proportion of mean reverters (50%)
         * - Market makers for liquidity (50%)
         */
        PopulationConfig create_consolidation_population();

        /**
         * @brief Create a flash crash population preset
         * @return Population configuration for sudden crash scenario
         *
         * Characteristics:
         * - Normal market composition
         * - Single whale configured to dump at specific time
         */
        PopulationConfig create_flash_crash_population();

        /**
         * @brief Tune population ratios in an existing configuration
         * @param config Population configuration to modify
         * @param momentum_pct Percentage of trend followers (0.0 - 1.0)
         * @param value_pct Percentage of mean reverters (0.0 - 1.0)
         * @param mm_pct Percentage of market makers (0.0 - 1.0)
         *
         * Adjusts population ratios to match desired percentages.
         * The total population count is preserved.
         */
        void tune_population_ratios(PopulationConfig &config,
                                    double momentum_pct,
                                    double value_pct,
                                    double mm_pct);

    } // namespace agents
} // namespace lob
