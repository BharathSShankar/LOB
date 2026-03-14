#include "../../include/agents/AgentZoo.h"
#include "../../include/core/Order.h"
#include <algorithm>
#include <cassert>
#include <cmath>

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
            {
                TrendFollower *tf = trend_followers_.acquire();
                if (tf)
                {
                    tf->initialize(agent_id, config);
                    agent = tf;
                }
                break;
            }

            case AgentType::MEAN_REVERTER:
            {
                MeanReverter *mr = mean_reverters_.acquire();
                if (mr)
                {
                    mr->initialize(agent_id, config);
                    agent = mr;
                }
                break;
            }

            case AgentType::WHALE:
            {
                Whale *w = whales_.acquire();
                if (w)
                {
                    w->initialize(agent_id, config);
                    agent = w;
                }
                break;
            }

            case AgentType::ARBITRAGEUR:
                // TODO: Implement when Arbitrageur agent is added
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

                    case AgentType::TREND_FOLLOWER:
                        trend_followers_.release(static_cast<TrendFollower *>(agent));
                        break;

                    case AgentType::MEAN_REVERTER:
                        mean_reverters_.release(static_cast<MeanReverter *>(agent));
                        break;

                    case AgentType::WHALE:
                        whales_.release(static_cast<Whale *>(agent));
                        break;

                    default:
                        // Unknown agent type
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

                    case AgentType::TREND_FOLLOWER:
                        trend_followers_.release(static_cast<TrendFollower *>(agent));
                        break;

                    case AgentType::MEAN_REVERTER:
                        mean_reverters_.release(static_cast<MeanReverter *>(agent));
                        break;

                    case AgentType::WHALE:
                        whales_.release(static_cast<Whale *>(agent));
                        break;

                    default:
                        // Unknown agent type
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
            trend_followers_.reset();
            mean_reverters_.reset();
            whales_.reset();
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

            // 40% Momentum (Trend Followers) - very aggressive, fast cooldown
            AgentConfig tf_config;
            tf_config.type = AgentType::TREND_FOLLOWER;
            tf_config.aggression = 0.9;
            tf_config.risk_tolerance = 0.8;
            tf_config.max_position = 50000; // High limit so they keep firing
            tf_config.order_size_mean = 150.0;
            tf_config.order_size_stddev = 30.0;
            tf_config.params["threshold_pct"] = 0.005;     // 0.5% threshold (easier to trigger)
            tf_config.params["cooldown_ticks"] = 2.0;      // Fire every 2 ticks instead of 10
            tf_config.params["momentum_scaling"] = 2.0;    // Bigger orders on strong signals
            tf_config.params["position_threshold"] = 0.95; // Don't self-limit until 95%

            config.populations[AgentType::TREND_FOLLOWER] = TypeConfig(400, tf_config);

            // 10% Value (Mean Reverters) - conservative, but with room to trade
            AgentConfig mr_config;
            mr_config.type = AgentType::MEAN_REVERTER;
            mr_config.aggression = 0.4;
            mr_config.risk_tolerance = 0.4;
            mr_config.max_position = 20000;
            mr_config.order_size_mean = 80.0;
            mr_config.order_size_stddev = 15.0;
            mr_config.params["fair_value"] = 100.0;
            mr_config.params["threshold_pct"] = 0.10; // 10% threshold (only acts on big moves)
            mr_config.params["position_threshold"] = 0.95;

            config.populations[AgentType::MEAN_REVERTER] = TypeConfig(100, mr_config);

            // 30% Noise Traders - inject random crosses to kick-start price movement
            AgentConfig nt_config;
            nt_config.type = AgentType::NOISE_TRADER;
            nt_config.aggression = 0.6;
            nt_config.risk_tolerance = 0.5;
            nt_config.max_position = 20000;
            nt_config.order_size_mean = 120.0;
            nt_config.order_size_stddev = 40.0;
            nt_config.params["noise_stddev"] = 0.03; // 3% noise → crosses spread often
            nt_config.params["position_threshold"] = 0.95;

            config.populations[AgentType::NOISE_TRADER] = TypeConfig(200, nt_config);

            // 20% Market Makers (thinner liquidity → price moves easier)
            AgentConfig mm_config;
            mm_config.type = AgentType::MARKET_MAKER;
            mm_config.aggression = 0.5;
            mm_config.risk_tolerance = 0.5;
            mm_config.max_position = 20000;
            mm_config.order_size_mean = 60.0; // Smaller quotes → less resistance
            mm_config.order_size_stddev = 15.0;
            mm_config.params["spread_pct"] = 0.002; // 0.2% spread
            mm_config.params["base_quantity"] = 60.0;
            mm_config.params["skew_factor"] = 1.5; // Stronger inventory skew
            mm_config.params["quote_frequency"] = 1.0;

            config.populations[AgentType::MARKET_MAKER] = TypeConfig(300, mm_config);

            return config;
        }

        PopulationConfig create_consolidation_population()
        {
            PopulationConfig config;

            // 20% Noise Traders - generate activity even in consolidation
            AgentConfig nt_config;
            nt_config.type = AgentType::NOISE_TRADER;
            nt_config.aggression = 0.4;
            nt_config.risk_tolerance = 0.4;
            nt_config.max_position = 20000;
            nt_config.order_size_mean = 80.0;
            nt_config.order_size_stddev = 20.0;
            nt_config.params["noise_stddev"] = 0.015; // 1.5% noise → some crosses
            nt_config.params["position_threshold"] = 0.95;

            config.populations[AgentType::NOISE_TRADER] = TypeConfig(200, nt_config);

            // 40% Value (Mean Reverters) - aggressive reversion keeps range tight
            AgentConfig mr_config;
            mr_config.type = AgentType::MEAN_REVERTER;
            mr_config.aggression = 0.8;
            mr_config.risk_tolerance = 0.6;
            mr_config.max_position = 30000;
            mr_config.order_size_mean = 150.0; // Large orders press price back
            mr_config.order_size_stddev = 30.0;
            mr_config.params["fair_value"] = 100.0;
            mr_config.params["threshold_pct"] = 0.005; // 0.5% threshold → quick to respond
            mr_config.params["position_threshold"] = 0.95;

            config.populations[AgentType::MEAN_REVERTER] = TypeConfig(400, mr_config);

            // 40% Market Makers (tight spreads, thick book)
            AgentConfig mm_config;
            mm_config.type = AgentType::MARKET_MAKER;
            mm_config.aggression = 0.5;
            mm_config.risk_tolerance = 0.5;
            mm_config.max_position = 20000;
            mm_config.order_size_mean = 120.0;
            mm_config.order_size_stddev = 25.0;
            mm_config.params["spread_pct"] = 0.0005; // 0.05% spread (tight)
            mm_config.params["base_quantity"] = 120.0;
            mm_config.params["skew_factor"] = 2.0; // Strong inventory correction
            mm_config.params["quote_frequency"] = 1.0;

            config.populations[AgentType::MARKET_MAKER] = TypeConfig(400, mm_config);

            return config;
        }

        PopulationConfig create_flash_crash_population()
        {
            PopulationConfig config;

            // 20% Market Makers - thin liquidity makes crash sharper
            AgentConfig mm_config;
            mm_config.type = AgentType::MARKET_MAKER;
            mm_config.aggression = 0.4;
            mm_config.risk_tolerance = 0.3;
            mm_config.max_position = 20000;
            mm_config.order_size_mean = 50.0; // Small quotes → thin book
            mm_config.order_size_stddev = 10.0;
            mm_config.params["spread_pct"] = 0.002; // Wider spread
            mm_config.params["base_quantity"] = 50.0;
            mm_config.params["skew_factor"] = 2.0; // Strong skew away from risk
            mm_config.params["quote_frequency"] = 1.0;

            config.populations[AgentType::MARKET_MAKER] = TypeConfig(200, mm_config);

            // 30% Noise Traders - active pre-crash market
            AgentConfig nt_config;
            nt_config.type = AgentType::NOISE_TRADER;
            nt_config.aggression = 0.6;
            nt_config.risk_tolerance = 0.5;
            nt_config.max_position = 20000;
            nt_config.order_size_mean = 100.0;
            nt_config.order_size_stddev = 30.0;
            nt_config.params["noise_stddev"] = 0.025; // 2.5% noise → lots of crosses
            nt_config.params["position_threshold"] = 0.95;

            config.populations[AgentType::NOISE_TRADER] = TypeConfig(300, nt_config);

            // 15% Trend Followers - amplify the crash once it starts
            AgentConfig tf_config;
            tf_config.type = AgentType::TREND_FOLLOWER;
            tf_config.aggression = 0.9;
            tf_config.risk_tolerance = 0.8;
            tf_config.max_position = 50000;
            tf_config.order_size_mean = 200.0; // Large momentum orders
            tf_config.order_size_stddev = 40.0;
            tf_config.params["threshold_pct"] = 0.003;  // 0.3% threshold → triggers fast
            tf_config.params["cooldown_ticks"] = 1.0;   // Fire every tick
            tf_config.params["momentum_scaling"] = 3.0; // Huge orders on strong signals
            tf_config.params["position_threshold"] = 0.99;

            config.populations[AgentType::TREND_FOLLOWER] = TypeConfig(150, tf_config);

            // 3 Whales that trigger early and dump massive volume via ICEBERG
            AgentConfig whale_config;
            whale_config.type = AgentType::WHALE;
            whale_config.aggression = 1.0;
            whale_config.risk_tolerance = 1.0;
            whale_config.max_position = 500000;
            whale_config.order_size_mean = 50000.0;
            whale_config.order_size_stddev = 0.0;
            whale_config.params["trigger_tick"] = 15.0;  // Fire early
            whale_config.params["whale_side"] = 1.0;     // SELL
            whale_config.params["whale_size"] = 50000.0; // 50k units each
            whale_config.params["execution_mode"] = 1.0; // ICEBERG
            whale_config.params["slice_size"] = 2000.0;  // 2k per slice

            config.populations[AgentType::WHALE] = TypeConfig(3, whale_config);

            return config;
        }

        void tune_population_ratios(PopulationConfig &config,
                                    double momentum_pct,
                                    double value_pct,
                                    double mm_pct)
        {
            // Calculate total desired population
            uint32_t current_total = config.total_count();
            if (current_total == 0)
            {
                current_total = 1000; // Default if no population exists
            }

            // Validate percentages sum to approximately 1.0 (allowing small tolerance)
            double total_pct = momentum_pct + value_pct + mm_pct;
            if (std::abs(total_pct - 1.0) > 0.01)
            {
                // Normalize percentages if they don't sum to 1.0
                double scale = 1.0 / total_pct;
                momentum_pct *= scale;
                value_pct *= scale;
                mm_pct *= scale;
            }

            // Clear existing populations
            config.populations.clear();

            // Calculate agent counts based on percentages
            uint32_t momentum_count = static_cast<uint32_t>(std::round(current_total * momentum_pct));
            uint32_t value_count = static_cast<uint32_t>(std::round(current_total * value_pct));
            uint32_t mm_count = static_cast<uint32_t>(std::round(current_total * mm_pct));

            // Adjust for rounding errors to maintain exact total
            uint32_t calculated_total = momentum_count + value_count + mm_count;
            if (calculated_total < current_total)
            {
                // Add extra agents to market makers (most stable)
                mm_count += (current_total - calculated_total);
            }
            else if (calculated_total > current_total)
            {
                // Remove from market makers
                mm_count -= (calculated_total - current_total);
            }

            // Add Trend Followers (Momentum)
            if (momentum_count > 0)
            {
                AgentConfig tf_config;
                tf_config.type = AgentType::TREND_FOLLOWER;
                tf_config.aggression = 0.7;
                tf_config.risk_tolerance = 0.6;
                tf_config.max_position = 1000;
                tf_config.order_size_mean = 100.0;
                tf_config.order_size_stddev = 20.0;
                tf_config.params["threshold_pct"] = 0.02;

                config.populations[AgentType::TREND_FOLLOWER] = TypeConfig(momentum_count, tf_config);
            }

            // Add Mean Reverters (Value)
            if (value_count > 0)
            {
                AgentConfig mr_config;
                mr_config.type = AgentType::MEAN_REVERTER;
                mr_config.aggression = 0.6;
                mr_config.risk_tolerance = 0.5;
                mr_config.max_position = 1000;
                mr_config.order_size_mean = 100.0;
                mr_config.order_size_stddev = 20.0;
                mr_config.params["fair_value"] = 100.0;
                mr_config.params["threshold_pct"] = 0.05;

                config.populations[AgentType::MEAN_REVERTER] = TypeConfig(value_count, mr_config);
            }

            // Add Market Makers
            if (mm_count > 0)
            {
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

                config.populations[AgentType::MARKET_MAKER] = TypeConfig(mm_count, mm_config);
            }
        }

    } // namespace agents
} // namespace lob
