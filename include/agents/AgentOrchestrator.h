#pragma once

#include "AgentZoo.h"
#include "MarketState.h"
#include "../core/Order.h"
#include "../concurrency/RingBuffer.h"
#include "../memory/ObjectPool.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <random>

namespace lob
{
    namespace agents
    {

        /**
         * @brief Agent Orchestrator (Scheduler) - Coordinates agent execution
         *
         * This class manages the agent simulation loop:
         * - Ticks all active agents at a specified rate
         * - Collects orders from agents and submits them to the matching engine
         * - Updates agents with current market state
         * - Runs in a separate thread to avoid blocking the matching engine
         *
         * Key Features:
         * - Configurable tick rate (Hz)
         * - Fair agent scheduling (random shuffling)
         * - Lock-free communication via ring buffer
         * - Zero allocation during runtime
         *
         * Thread Safety:
         * - Runs in its own thread
         * - Communicates with matching engine via lock-free ring buffer
         * - Market state updates are thread-safe
         */
        class AgentOrchestrator
        {
        public:
            /**
             * @brief Construct agent orchestrator
             * @param zoo Reference to agent pool
             * @param order_buffer Reference to order ring buffer
             * @param order_pool Reference to order object pool
             */
            AgentOrchestrator(AgentZoo &zoo,
                              concurrency::RingBuffer<core::Order *, 8192> &order_buffer,
                              memory::ObjectPool<core::Order, 10000> &order_pool);

            /**
             * @brief Destructor - stops orchestrator thread
             */
            ~AgentOrchestrator();

            // Non-copyable, non-movable
            AgentOrchestrator(const AgentOrchestrator &) = delete;
            AgentOrchestrator &operator=(const AgentOrchestrator &) = delete;

            /**
             * @brief Start the orchestrator thread
             */
            void start();

            /**
             * @brief Stop the orchestrator thread
             */
            void stop();

            /**
             * @brief Check if orchestrator is running
             * @return true if running
             */
            bool is_running() const { return running_.load(); }

            /**
             * @brief Set tick rate (agents per second)
             * @param ticks_per_second Tick rate in Hz (default: 100)
             */
            void set_tick_rate(uint32_t ticks_per_second);

            /**
             * @brief Get current tick rate
             * @return Tick rate in Hz
             */
            uint32_t get_tick_rate() const { return tick_rate_; }

            /**
             * @brief Set population configuration
             * @param config Population configuration
             *
             * This will reset the zoo and spawn new agents.
             */
            void set_population(const PopulationConfig &config);

            /**
             * @brief Update market state for agents
             * @param state Current market state
             *
             * Thread-safe: Can be called from matching engine thread.
             */
            void update_market_state(const MarketState &state);

            /**
             * @brief Get current market state
             * @return Current market state
             */
            MarketState get_market_state() const;

            /**
             * @brief Get total tick count
             * @return Number of ticks executed
             */
            uint64_t get_tick_count() const { return tick_count_; }

            /**
             * @brief Get number of orders submitted
             * @return Total orders submitted to ring buffer
             */
            uint64_t get_orders_submitted() const { return orders_submitted_; }

            /**
             * @brief Get number of orders dropped (buffer full)
             * @return Total orders dropped
             */
            uint64_t get_orders_dropped() const { return orders_dropped_; }

        private:
            /**
             * @brief Main orchestrator loop (runs in separate thread)
             */
            void run();

            /**
             * @brief Tick all active agents once
             */
            void tick_agents();

            /**
             * @brief Sleep to maintain tick rate
             * @param start_time Start time of current tick
             * @param tick_interval Desired interval between ticks
             */
            void sleep_for_next_tick(
                const std::chrono::steady_clock::time_point &start_time,
                const std::chrono::microseconds &tick_interval);

            // References to shared resources
            AgentZoo &zoo_;
            concurrency::RingBuffer<core::Order *, 8192> &order_buffer_;
            memory::ObjectPool<core::Order, 10000> &order_pool_;

            // Market state (updated by matching engine thread)
            MarketState current_market_state_;
            mutable std::atomic<bool> market_state_dirty_; // Flag for updates

            // Thread control
            std::atomic<bool> running_;
            std::thread orchestrator_thread_;

            // Tick configuration
            uint32_t tick_rate_;        ///< Ticks per second (Hz)
            uint64_t tick_count_;       ///< Total ticks executed
            uint64_t orders_submitted_; ///< Total orders submitted
            uint64_t orders_dropped_;   ///< Total orders dropped (buffer full)

            // Random number generator for agent shuffling
            std::mt19937_64 rng_;
        };

    } // namespace agents
} // namespace lob
