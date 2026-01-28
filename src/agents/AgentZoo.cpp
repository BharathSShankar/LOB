#include "../../include/agents/AgentZoo.h"
#include <algorithm>
#include <cassert>

namespace lob
{
    namespace agents
    {

        // ============================================================================
        // PopulationConfig Implementation
        // ============================================================================

        void PopulationConfig::normalize()
        {
            // TODO: Implement normalization to fit within pool limits
            // For now, just validate that counts don't exceed pool capacities
            uint32_t total = total_count();
            if (total > 10000)
            {
                // Scale down proportionally
                double scale = 10000.0 / total;
                for (auto &[type, config] : populations)
                {
                    config.count = static_cast<uint32_t>(config.count * scale);
                }
            }
        }

        // ============================================================================
        // AgentZoo Implementation
        // ============================================================================

        AgentZoo::AgentZoo()
            : active_count_(0), next_agent_id_(1)
        {
            // Initialize active agents array to nullptr
            active_agents_.fill(nullptr);
        }

        Agent *AgentZoo::spawn_agent(AgentType type, const AgentConfig &config)
        {
            // Check if we have room for more agents
            if (active_count_ >= MAX_TOTAL_AGENTS)
            {
                return nullptr;
            }

            Agent *agent = nullptr;
            uint64_t agent_id = next_agent_id_.fetch_add(1);

            // Acquire agent from appropriate pool based on type
            switch (type)
            {
            case AgentType::NOISE_TRADER:
            {
                NoiseTrader *nt = noise_traders_.acquire();
                if (nt)
                {
                    nt->initialize(agent_id, config);
                    agent = nt;
                }
                break;
            }

            case AgentType::MARKET_MAKER:
            {
                MarketMaker *mm = market_makers_.acquire();
                if (mm)
                {
                    mm->initialize(agent_id, config);
                    agent = mm;
                }
                break;
            }

            case AgentType::TREND_FOLLOWER:
            case AgentType::MEAN_REVERTER:
            case AgentType::WHALE:
            case AgentType::ARBITRAGEUR:
                // TODO: Implement when these agent types are added
                return nullptr;

            default:
                return nullptr;
            }

            // Add to active agents list if successfully acquired
            if (agent)
            {
                add_to_active(agent);
            }

            return agent;
        }

        void AgentZoo::kill_agent(uint64_t agent_id)
        {
            // Find agent in active list
            for (uint32_t i = 0; i < active_count_; ++i)
            {
                if (active_agents_[i]->get_id() == agent_id)
                {
                    Agent *agent = active_agents_[i];

                    // Deactivate agent
                    agent->deactivate();

                    // Return to appropriate pool
                    switch (agent->get_type())
                    {
                    case AgentType::NOISE_TRADER:
                        noise_traders_.release(static_cast<NoiseTrader *>(agent));
                        break;

                    case AgentType::MARKET_MAKER:
                        market_makers_.release(static_cast<MarketMaker *>(agent));
                        break;

                    default:
                        // TODO: Handle other agent types
                        break;
                    }

                    // Remove from active list
                    remove_from_active(agent_id);
                    return;
                }
            }
        }

        void AgentZoo::reset_all()
        {
            // Return all agents to their pools
            for (uint32_t i = 0; i < active_count_; ++i)
            {
                Agent *agent = active_agents_[i];
                if (agent)
                {
                    agent->reset();

                    // Return to appropriate pool
                    switch (agent->get_type())
                    {
                    case AgentType::NOISE_TRADER:
                        noise_traders_.release(static_cast<NoiseTrader *>(agent));
                        break;

                    case AgentType::MARKET_MAKER:
                        market_makers_.release(static_cast<MarketMaker *>(agent));
                        break;

                    default:
                        // TODO: Handle other agent types
                        break;
                    }
                }
            }

            // Clear active list
            active_agents_.fill(nullptr);
            active_count_ = 0;

            // Reset all pools
            noise_traders_.reset();
            market_makers_.reset();
            // TODO: Reset other pools when implemented
        }

        void AgentZoo::set_population(const PopulationConfig &config)
        {
            // Clear existing population
            reset_all();

            // Spawn agents according to configuration
            for (const auto &[type, type_config] : config.populations)
            {
                for (uint32_t i = 0; i < type_config.count; ++i)
                {
                    Agent *agent = spawn_agent(type, type_config.base_config);
                    if (!agent)
                    {
                        // Pool exhausted or error - stop spawning this type
                        break;
                    }
                }
            }
        }

        std::vector<Agent *> AgentZoo::get_active_agents()
        {
            std::vector<Agent *> agents;
            agents.reserve(active_count_);

            for (uint32_t i = 0; i < active_count_; ++i)
            {
                if (active_agents_[i] && active_agents_[i]->is_active())
                {
                    agents.push_back(active_agents_[i]);
                }
            }

            return agents;
        }

        PopulationStats AgentZoo::get_stats() const
        {
            PopulationStats stats;
            stats.total_active = 0;

            for (uint32_t i = 0; i < active_count_; ++i)
            {
                if (active_agents_[i] && active_agents_[i]->is_active())
                {
                    AgentType type = active_agents_[i]->get_type();
                    stats.counts_by_type[type]++;
                    stats.total_active++;
                }
            }

            return stats;
        }

        void AgentZoo::add_to_active(Agent *agent)
        {
            assert(active_count_ < MAX_TOTAL_AGENTS);
            active_agents_[active_count_] = agent;
            active_count_++;
        }

        void AgentZoo::remove_from_active(uint64_t agent_id)
        {
            // Find and remove agent from active list
            for (uint32_t i = 0; i < active_count_; ++i)
            {
                if (active_agents_[i]->get_id() == agent_id)
                {
                    // Swap with last element and decrement count
                    active_agents_[i] = active_agents_[active_count_ - 1];
                    active_agents_[active_count_ - 1] = nullptr;
                    active_count_--;
                    return;
                }
            }
        }

        // ============================================================================
        // Population Presets
        // ============================================================================

        PopulationConfig create_bull_run_population()
        {
            PopulationConfig config;

            // 50% Market Makers (liquidity)
            AgentConfig mm_config;
            mm_config.type = AgentType::MARKET_MAKER;
            mm_config.aggression = 0.5;
            mm_config.risk_tolerance = 0.5;
            mm_config.max_position = 1000;
            mm_config.order_size_mean = 100.0;
            mm_config.order_size_stddev = 20.0;
            mm_config.params["spread_pct"] = 0.001; // 0.1% spread
            mm_config.params["base_quantity"] = 100.0;
            mm_config.params["skew_factor"] = 1.0;
            mm_config.params["quote_frequency"] = 1.0;

            config.populations[AgentType::MARKET_MAKER] = TypeConfig(500, mm_config);

            // 40% Noise Traders (random activity)
            AgentConfig nt_config;
            nt_config.type = AgentType::NOISE_TRADER;
            nt_config.aggression = 0.6;
            nt_config.risk_tolerance = 0.7;
            nt_config.max_position = 500;
            nt_config.order_size_mean = 80.0;
            nt_config.order_size_stddev = 15.0;
            nt_config.params["noise_stddev"] = 0.01;
            nt_config.params["position_threshold"] = 0.8;

            config.populations[AgentType::NOISE_TRADER] = TypeConfig(400, nt_config);

            // TODO: Add trend followers when implemented (10%)
            // AgentConfig tf_config;
            // tf_config.type = AgentType::TREND_FOLLOWER;
            // tf_config.aggression = 0.8;
            // tf_config.params["threshold_pct"] = 0.02;
            // config.populations[AgentType::TREND_FOLLOWER] = TypeConfig(100, tf_config);

            return config;
        }

        PopulationConfig create_consolidation_population()
        {
            PopulationConfig config;

            // 50% Market Makers (tight spreads)
            AgentConfig mm_config;
            mm_config.type = AgentType::MARKET_MAKER;
            mm_config.aggression = 0.5;
            mm_config.risk_tolerance = 0.5;
            mm_config.max_position = 1000;
            mm_config.order_size_mean = 100.0;
            mm_config.order_size_stddev = 20.0;
            mm_config.params["spread_pct"] = 0.0005; // 0.05% spread (tight)
            mm_config.params["base_quantity"] = 100.0;
            mm_config.params["skew_factor"] = 1.0;
            mm_config.params["quote_frequency"] = 1.0;

            config.populations[AgentType::MARKET_MAKER] = TypeConfig(500, mm_config);

            // 50% Noise Traders (conservative)
            AgentConfig nt_config;
            nt_config.type = AgentType::NOISE_TRADER;
            nt_config.aggression = 0.3;
            nt_config.risk_tolerance = 0.4;
            nt_config.max_position = 300;
            nt_config.order_size_mean = 50.0;
            nt_config.order_size_stddev = 10.0;
            nt_config.params["noise_stddev"] = 0.005; // Lower noise
            nt_config.params["position_threshold"] = 0.7;

            config.populations[AgentType::NOISE_TRADER] = TypeConfig(500, nt_config);

            // TODO: Add mean reverters when implemented (50%)
            // AgentConfig mr_config;
            // mr_config.type = AgentType::MEAN_REVERTER;
            // mr_config.aggression = 0.7;
            // mr_config.params["fair_value"] = 100.0;
            // mr_config.params["threshold_pct"] = 0.02;
            // config.populations[AgentType::MEAN_REVERTER] = TypeConfig(500, mr_config);

            return config;
        }

        PopulationConfig create_flash_crash_population()
        {
            PopulationConfig config;

            // Normal market composition
            // 30% Market Makers
            AgentConfig mm_config;
            mm_config.type = AgentType::MARKET_MAKER;
            mm_config.aggression = 0.5;
            mm_config.risk_tolerance = 0.5;
            mm_config.max_position = 1000;
            mm_config.order_size_mean = 100.0;
            mm_config.order_size_stddev = 20.0;
            mm_config.params["spread_pct"] = 0.001;
            mm_config.params["base_quantity"] = 100.0;
            mm_config.params["skew_factor"] = 1.0;
            mm_config.params["quote_frequency"] = 1.0;

            config.populations[AgentType::MARKET_MAKER] = TypeConfig(300, mm_config);

            // 20% Noise Traders
            AgentConfig nt_config;
            nt_config.type = AgentType::NOISE_TRADER;
            nt_config.aggression = 0.5;
            nt_config.risk_tolerance = 0.5;
            nt_config.max_position = 500;
            nt_config.order_size_mean = 80.0;
            nt_config.order_size_stddev = 15.0;
            nt_config.params["noise_stddev"] = 0.01;
            nt_config.params["position_threshold"] = 0.8;

            config.populations[AgentType::NOISE_TRADER] = TypeConfig(200, nt_config);

            // TODO: Add trend followers (10%) and whale (1) when implemented
            // AgentConfig whale_config;
            // whale_config.type = AgentType::WHALE;
            // whale_config.params["trigger_tick"] = 5000.0;
            // whale_config.params["whale_side"] = static_cast<double>(core::Side::SELL);
            // whale_config.params["whale_size"] = 10000.0;
            // config.populations[AgentType::WHALE] = TypeConfig(1, whale_config);

            return config;
        }

    } // namespace agents
} // namespace lob
