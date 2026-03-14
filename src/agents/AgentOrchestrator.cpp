#include "../../include/agents/AgentOrchestrator.h"
#include <algorithm>
#include <cassert>

namespace lob
{
    namespace agents
    {

        AgentOrchestrator::AgentOrchestrator(
            AgentZoo &zoo,
            concurrency::RingBuffer<core::Order *, 8192> &order_buffer,
            memory::ObjectPool<core::Order, 10000> &order_pool)
            : zoo_(zoo),
              order_buffer_(order_buffer),
              order_pool_(order_pool),
              market_state_dirty_(false),
              running_(false),
              tick_rate_(100), // Default: 100 Hz
              tick_count_(0),
              orders_submitted_(0),
              orders_dropped_(0),
              rng_(std::random_device{}())
        {
        }

        AgentOrchestrator::~AgentOrchestrator()
        {
            stop();
        }

        void AgentOrchestrator::start()
        {
            if (running_.load())
            {
                return; // Already running
            }

            running_.store(true);
            orchestrator_thread_ = std::thread(&AgentOrchestrator::run, this);
        }

        void AgentOrchestrator::stop()
        {
            if (!running_.load())
            {
                return; // Not running
            }

            running_.store(false);

            if (orchestrator_thread_.joinable())
            {
                orchestrator_thread_.join();
            }
        }

        void AgentOrchestrator::set_tick_rate(uint32_t ticks_per_second)
        {
            assert(ticks_per_second > 0 && ticks_per_second <= 10000);
            tick_rate_ = ticks_per_second;
        }

        void AgentOrchestrator::set_population(const PopulationConfig &config)
        {
            // Stop orchestrator if running
            if (running_.load())
            {
                stop();
            }

            // Set population in zoo
            zoo_.set_population(config);

            // Reset counters
            tick_count_ = 0;
            orders_submitted_ = 0;
            orders_dropped_ = 0;

            // Note: Does not auto-restart - caller must restart if desired
        }

        void AgentOrchestrator::update_market_state(const MarketState &state)
        {
            current_market_state_ = state;
            market_state_dirty_.store(true, std::memory_order_release);
        }

        MarketState AgentOrchestrator::get_market_state() const
        {
            return current_market_state_;
        }

        void AgentOrchestrator::run()
        {
            // Calculate tick interval in microseconds
            auto tick_interval = std::chrono::microseconds(1'000'000 / tick_rate_);

            while (running_.load())
            {
                auto start_time = std::chrono::steady_clock::now();

                // Tick all agents
                tick_agents();

                // Increment tick counter
                tick_count_++;

                // Sleep to maintain tick rate
                sleep_for_next_tick(start_time, tick_interval);
            }
        }

        void AgentOrchestrator::tick_agents()
        {
            // Get all active agents
            std::vector<Agent *> agents = zoo_.get_active_agents();

            if (agents.empty())
            {
                return; // No agents to tick
            }

            // Shuffle agents for fairness (avoid ordering bias)
            std::shuffle(agents.begin(), agents.end(), rng_);

            // Tick each agent and collect orders
            for (Agent *agent : agents)
            {
                if (!agent || !agent->is_active())
                {
                    continue;
                }

                // Inject the order pool so decide() can allocate Order objects
                agent->set_order_pool(&order_pool_);

                // Tick agent to update internal state
                agent->tick(current_market_state_);

                // Ask agent to make a decision
                core::Order *order = agent->decide(current_market_state_);

                if (order)
                {
                    // Try to push order to ring buffer
                    if (order_buffer_.push(order))
                    {
                        orders_submitted_++;
                    }
                    else
                    {
                        // Buffer full - release order back to pool
                        order_pool_.release(order);
                        orders_dropped_++;
                    }
                }
            }
        }

        void AgentOrchestrator::sleep_for_next_tick(
            const std::chrono::steady_clock::time_point &start_time,
            const std::chrono::microseconds &tick_interval)
        {
            auto elapsed = std::chrono::steady_clock::now() - start_time;

            if (elapsed < tick_interval)
            {
                // Sleep for remaining time
                std::this_thread::sleep_for(tick_interval - elapsed);
            }
            // If we're running behind, just continue immediately
        }

    } // namespace agents
} // namespace lob
