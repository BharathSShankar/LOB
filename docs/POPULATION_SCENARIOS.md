# Population Scenarios Documentation

This document describes the three main population scenarios implemented for the Agent-Based Market Simulation (ABMS).

## Overview

The ABMS supports predefined population configurations that create different market dynamics. These scenarios are useful for:
- Testing market behavior under different conditions
- Demonstrating emergent properties of trader interactions
- Validating agent implementations

## Scenario A: Bull Run

**Expected Behavior:** Rising price trend over time

### Configuration
```cpp
auto config = lob::agents::create_bull_run_population();
```

### Composition (1000 agents)
- **40% Trend Followers (400 agents)**
  - Type: `TREND_FOLLOWER`
  - Aggression: 0.8 (aggressive)
  - Threshold: 2%
  - Behavior: Chase momentum, create positive feedback loops

- **10% Mean Reverters (100 agents)**
  - Type: `MEAN_REVERTER`
  - Aggression: 0.3 (conservative)
  - Fair Value: 100.0
  - Threshold: 10%
  - Behavior: Provide resistance but insufficient to stop trend

- **50% Market Makers (500 agents)**
  - Type: `MARKET_MAKER`
  - Spread: 0.1%
  - Behavior: Provide liquidity for trend execution

### Expected Results
- **Price Movement:** Upward trend (>5% over 10,000 ticks)
- **Volatility:** Medium to high
- **Mechanism:** Trend followers create positive feedback loops, overwhelming the mean reverters' resistance

## Scenario B: Consolidation

**Expected Behavior:** Price range-bound around fair value (100.0)

### Configuration
```cpp
auto config = lob::agents::create_consolidation_population();
```

### Composition (1000 agents)
- **0% Trend Followers**
  - No momentum traders to create directional movement

- **50% Mean Reverters (500 agents)**
  - Type: `MEAN_REVERTER`
  - Aggression: 0.7 (aggressive reversion)
  - Fair Value: 100.0
  - Threshold: 2% (tight range)
  - Behavior: Actively push price back to equilibrium

- **50% Market Makers (500 agents)**
  - Type: `MARKET_MAKER`
  - Spread: 0.05% (tight spread)
  - Behavior: Maintain tight market around equilibrium

### Expected Results
- **Price Movement:** Minimal (<2% deviation from 100.0)
- **Volatility:** Low (<10% total range)
- **Mechanism:** Mean reverters dominate, creating strong mean reversion to fair value

## Scenario C: Flash Crash

**Expected Behavior:** Sudden price drop at tick 5000

### Configuration
```cpp
auto config = lob::agents::create_flash_crash_population();
```

### Composition (601 agents)
- **30% Market Makers (300 agents)**
  - Provide baseline liquidity

- **20% Noise Traders (200 agents)**
  - Random activity

- **10% Trend Followers (100 agents)**
  - Will amplify the crash once triggered

- **1 Whale Agent**
  - Type: `WHALE`
  - Trigger Tick: 5000
  - Side: SELL
  - Size: 10,000 units
  - Behavior: Execute massive sell order at t=5000

### Expected Results
- **Before t=5000:** Normal market dynamics
- **At t=5000:** Sudden liquidity crisis, sharp price drop
- **After t=5000:** Trend followers amplify the crash
- **Total Price Drop:** >10% from pre-crash levels

## Using Population Scenarios

### Basic Usage

```cpp
#include "agents/AgentZoo.h"
#include "agents/AgentOrchestrator.h"

// Create components
lob::agents::AgentZoo zoo;
lob::agents::AgentOrchestrator orchestrator(/* ... */);

// Load scenario
auto config = lob::agents::create_bull_run_population();
zoo.set_population(config);

// Start simulation
orchestrator.start();
// ... run for desired ticks ...
orchestrator.stop();
```

### Custom Tuning

You can create custom population ratios using `tune_population_ratios()`:

```cpp
lob::agents::PopulationConfig config;

// Create custom mix: 60% momentum, 20% value, 20% market makers
lob::agents::tune_population_ratios(config, 0.60, 0.20, 0.20);

zoo.set_population(config);
```

### Parameters

- `momentum_pct`: Percentage of Trend Followers (0.0 - 1.0)
- `value_pct`: Percentage of Mean Reverters (0.0 - 1.0)
- `mm_pct`: Percentage of Market Makers (0.0 - 1.0)

**Note:** Percentages should sum to 1.0 (will be normalized if not)

## Testing Recommendations

### Test Duration
- **Minimum:** 5,000 ticks for basic behavior
- **Recommended:** 10,000 ticks for clear patterns
- **Flash Crash:** Minimum 6,000 ticks (must exceed trigger point)

### Metrics to Track
1. **Price Evolution**
   - Initial price
   - Final price
   - Min/Max prices
   - Price change percentage

2. **Order Flow**
   - Total orders submitted
   - Orders per tick
   - Order size distribution

3. **Market Microstructure**
   - Bid-ask spread
   - Order book depth
   - Trade volume

### Success Criteria

| Scenario      | Primary Metric | Success Threshold    |
| ------------- | -------------- | -------------------- |
| Bull Run      | Price Change   | > +5% over 10K ticks |
| Consolidation | Volatility     | < 10% price range    |
| Flash Crash   | Price Drop     | > -10% after t=5000  |

## Implementation Details

### File Locations
- **Declarations:** `include/agents/AgentZoo.h`
- **Implementations:** `src/agents/AgentZoo.cpp`
- **Documentation:** `src/agents/PopulationPresets.cpp`

### Agent Pool Capacities
```cpp
TREND_FOLLOWER_CAPACITY = 2000
MEAN_REVERTER_CAPACITY = 2000
MARKET_MAKER_CAPACITY = 3000
WHALE_CAPACITY = 10
MAX_TOTAL_AGENTS = 10000
```

### Thread Safety
- Population changes should be made before starting the orchestrator
- Use `orchestrator.stop()` before calling `zoo.set_population()`
- Market state updates are thread-safe

## Example: Running All Scenarios

```cpp
void run_all_scenarios() {
    lob::agents::AgentZoo zoo;
    // ... initialize orchestrator, matching engine ...
    
    // Scenario 1: Bull Run
    std::cout << "Running Bull Run Scenario...\n";
    auto bull_config = lob::agents::create_bull_run_population();
    zoo.set_population(bull_config);
    orchestrator.start();
    run_simulation(10000);
    orchestrator.stop();
    
    // Scenario 2: Consolidation
    std::cout << "Running Consolidation Scenario...\n";
    auto consol_config = lob::agents::create_consolidation_population();
    zoo.set_population(consol_config);
    orchestrator.start();
    run_simulation(10000);
    orchestrator.stop();
    
    // Scenario 3: Flash Crash
    std::cout << "Running Flash Crash Scenario...\n";
    auto crash_config = lob::agents::create_flash_crash_population();
    zoo.set_population(crash_config);
    orchestrator.start();
    run_simulation(10000);
    orchestrator.stop();
}
```

## Conclusion

These population scenarios demonstrate the power of agent-based modeling in financial markets. Each scenario produces distinct emergent behaviors that arise from the interaction of simple trading rules and population composition.

The scenarios can be extended or combined to create more complex market conditions, such as:
- Bull run followed by crash
- Multiple whales with different timing
- Gradual shift in population composition
- Regime changes based on market conditions

For more information, see:
- **ROADMAP_ABMS.md** - Development roadmap
- **AgentZoo.h** - API documentation
- **PopulationPresets.cpp** - Implementation notes
