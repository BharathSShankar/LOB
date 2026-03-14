/**
 * @file PopulationPresets.cpp
 * @brief Population scenario configurations for ABMS
 *
 * This file provides documented implementation of population presets.
 * The actual implementations are in AgentZoo.cpp
 */

#include "../../include/agents/AgentZoo.h"

namespace lob
{
    namespace agents
    {
        // Implementations are in AgentZoo.cpp
        // This file serves as documentation for the population scenarios

        /**
         * SCENARIO A: BULL RUN
         *
         * Expected Behavior: Rising price trend
         *
         * Composition:
         * - 40% Trend Followers (Momentum traders)
         *   - Aggression: 0.8 (aggressive)
         *   - Threshold: 2%
         *   - Creates positive feedback loops driving price up
         *
         * - 10% Mean Reverters (Value traders)
         *   - Aggression: 0.3 (conservative)
         *   - Threshold: 10%
         *   - Provides resistance but not enough to stop the trend
         *
         * - 50% Market Makers
         *   - Spread: 0.1%
         *   - Provides liquidity for trend execution
         *
         * Total: 1000 agents
         */

        /**
         * SCENARIO B: CONSOLIDATION
         *
         * Expected Behavior: Price range-bound around fair value
         *
         * Composition:
         * - 0% Trend Followers (No momentum traders)
         *
         * - 50% Mean Reverters (Value traders)
         *   - Aggression: 0.7 (aggressive reversion)
         *   - Threshold: 2% (tight range)
         *   - Actively pushes price back to fair value
         *
         * - 50% Market Makers
         *   - Spread: 0.05% (tight spread)
         *   - Maintains tight market around equilibrium
         *
         * Total: 1000 agents
         */

        /**
         * SCENARIO C: FLASH CRASH
         *
         * Expected Behavior: Sudden price drop at t=5000
         *
         * Composition:
         * - 30% Market Makers
         *   - Normal liquidity provision
         *
         * - 20% Noise Traders
         *   - Random activity
         *
         * - 10% Trend Followers
         *   - Will amplify the crash once triggered
         *
         * - 1 Whale (triggers at tick 5000)
         *   - Side: SELL
         *   - Size: 10,000 units
         *   - Triggers sudden liquidity crisis
         *
         * Total: 601 agents
         */

    } // namespace agents
} // namespace lob
