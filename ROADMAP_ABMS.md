# Agent-Based Market Simulation (ABMS) - 4-Week Implementation Roadmap

**Timeline:** 4 Weeks (~100 hours)  
**Primary Goal:** Transform the deterministic Matching Engine into a stochastic Market Simulator by replacing random noise with behavioral agents to generate verifiable Technical Analysis (TA) patterns.

---

## Project Overview

This module extends the High-Frequency Limit Order Book with intelligent trading agents that create realistic market dynamics and recognizable chart patterns.

### Core Constraints
- **Zero Dynamic Allocation** remains critical
- All agent objects must be pre-allocated in a "Zoo" pool at startup
- Agents operate lock-free through the existing Ring Buffer
- Behavioral parameters must be configurable at runtime

### Components
1. **Agent Interface** - Base class for polymorphic bot behavior
2. **The Zoo (Agent Pool)** - Pre-allocated collection of trading bots
3. **Agent Orchestrator** - Scheduler that triggers agent decisions
4. **Pattern Validator** - OHLCV aggregation and pattern detection
5. **Configuration UI** - ImGui-based controls for agent composition and behavior

---

## Week 7: The Foundation (Agent Architecture & Liquidity)

**Goal:** Create the agent framework and establish stable market liquidity

### Day 1-2: Agent Base Classes
**Files:** [`Agent.h`](include/agents/Agent.h), [`Agent.cpp`](src/agents/Agent.cpp)

- [x] Create Agent base class with virtual interface:
  ```cpp
  class Agent {
  public:
      virtual ~Agent() = default;
      
      // Core decision-making
      virtual void tick(const MarketState& state) = 0;
      
      // Returns nullptr if no order, or pointer to order
      virtual Order* decide(const MarketState& state) = 0;
      
      // Lifecycle
      virtual void initialize(uint64_t agent_id, const AgentConfig& config) = 0;
      virtual void reset() = 0;
      
      // State
      bool is_active() const { return active_; }
      AgentType get_type() const { return type_; }
      
  protected:
      uint64_t agent_id_{0};
      AgentType type_;
      bool active_{true};
      Position position_;  // Track inventory
  };
  ```

- [x] Define [`AgentType`](include/agents/Agent.h) enum:
  ```cpp
  enum class AgentType {
      NOISE_TRADER,
      MARKET_MAKER,
      TREND_FOLLOWER,
      MEAN_REVERTER,
      WHALE,
      ARBITRAGEUR
  };
  ```

- [x] Define [`MarketState`](include/agents/MarketState.h) structure:
  ```cpp
  struct MarketState {
      double last_price;
      double best_bid;
      double best_ask;
      double spread;
      uint64_t timestamp;
      double volume_24h;
      double price_sma_50;     // Simple Moving Average
      double price_sma_200;
      double volatility;        // Recent price std dev
      uint32_t bid_depth;       // Total quantity in top 5 levels
      uint32_t ask_depth;
  };
  ```

- [x] Define [`AgentConfig`](include/agents/Agent.h) structure:
  ```cpp
  struct AgentConfig {
      AgentType type;
      double aggression;      // 0.0 - 1.0
      double risk_tolerance;  // 0.0 - 1.0
      uint32_t max_position;  // Inventory limit
      double order_size_mean;
      double order_size_stddev;
      // Type-specific parameters
      std::unordered_map<std::string, double> params;
  };
  ```

**Key Concepts:**
- Polymorphic behavior through virtual functions
- Market state observation
- Agent configuration and parameterization

### Day 3-4: Noise Traders ✅ COMPLETED
**Files:** [`NoiseTrader.h`](include/agents/NoiseTrader.h), [`NoiseTrader.cpp`](src/agents/NoiseTrader.cpp)

- [x] Implement [`NoiseTrader::decide()`](src/agents/NoiseTrader.cpp)
  - Migrated from existing random order logic
  - Logic:
    ```cpp
    // Random walk around last price
    double epsilon = normal_dist(0, config_.params["noise_stddev"]);
    double price = state.last_price * (1.0 + epsilon);
    
    // Random side
    OrderSide side = (uniform_dist() < 0.5) ? BUY : SELL;
    
    // Random quantity
    uint32_t qty = lognormal_dist(config_.order_size_mean, 
                                   config_.order_size_stddev);
    
    return create_limit_order(side, price, qty);
    ```

- [x] Implement position tracking:
  - Update `position_` after each order
  - Reduce order frequency if position exceeds threshold

- [x] Add unit tests in [`NoiseTraderTest.cpp`](tests/agents/NoiseTraderTest.cpp):
  - Test random distribution
  - Test position limits
  - Test order creation

**Concepts:** Brownian Motion, Random Walks, Liquidity Noise

### Day 5-7: Market Makers ✅ COMPLETED
**Files:** [`MarketMaker.h`](include/agents/MarketMaker.h), [`MarketMaker.cpp`](src/agents/MarketMaker.cpp)

- [x] Implement [`MarketMaker::decide()`](src/agents/MarketMaker.cpp)
  - Objective: Maintain spread and provide liquidity
  - Core Logic:
    ```cpp
    double target_spread = state.last_price * config_.params["spread_pct"];
    double skew = calculate_inventory_skew();
    
    // If long inventory, widen ask and tighten bid (encourage selling)
    double bid_price = state.last_price - (target_spread / 2) * (1 - skew);
    double ask_price = state.last_price + (target_spread / 2) * (1 + skew);
    
    // Alternate between bid and ask orders
    if (tick_count_ % 2 == 0)
        return create_limit_order(BUY, bid_price, base_quantity_);
    else
        return create_limit_order(SELL, ask_price, base_quantity_);
    ```

- [x] Implement [`calculate_inventory_skew()`](src/agents/MarketMaker.cpp):
  ```cpp
  double MarketMaker::calculate_inventory_skew() {
      // Returns -1.0 (max short) to +1.0 (max long)
      double ratio = static_cast<double>(position_.net_position) / 
                     config_.max_position;
      return std::clamp(ratio, -1.0, 1.0);
  }
  ```

- [x] Implement inventory management:
  - Track net position (buys - sells)
  - If position_ exceeds max_position * 0.8, reduce quoting
  - If position_ reaches max_position, stop quoting on one side

- [x] Add advanced logic (optional):
  - Volatility-based spread widening
  - Adverse selection protection (cancel if price moves)
  - Tick-level positioning

- [x] Add unit tests in [`MarketMakerTest.cpp`](tests/agents/MarketMakerTest.cpp):
  - Test spread maintenance
  - Test inventory skew calculation
  - Test position limits

**Concepts:** Bid-Ask Spread, Inventory Risk, Market Making, Adverse Selection

### Day 8-9: Agent Pool ("The Zoo") ✅ COMPLETED
**Files:** [`AgentZoo.h`](include/agents/AgentZoo.h), [`AgentZoo.cpp`](src/agents/AgentZoo.cpp)

- [x] Create [`AgentZoo`](include/agents/AgentZoo.h) class:
  ```cpp
  class AgentZoo {
  public:
      AgentZoo(uint32_t max_agents = 10000);
      
      // Agent lifecycle
      Agent* spawn_agent(AgentType type, const AgentConfig& config);
      void kill_agent(uint64_t agent_id);
      void reset_all();
      
      // Population management
      void set_population(const PopulationConfig& pop);
      std::vector<Agent*> get_active_agents();
      
      // Statistics
      PopulationStats get_stats() const;
      
  private:
      // Pre-allocated pools for each agent type
      memory::ObjectPool<NoiseTrader, 5000> noise_traders_;
      memory::ObjectPool<MarketMaker, 3000> market_makers_;
      // ... more pools for other agent types
      
      std::array<Agent*, 10000> active_agents_;
      uint32_t active_count_{0};
      std::atomic<uint64_t> next_agent_id_{1};
  };
  ```

- [x] Define [`PopulationConfig`](include/agents/AgentZoo.h):
  ```cpp
  struct PopulationConfig {
      struct TypeConfig {
          uint32_t count;
          AgentConfig base_config;
      };
      
      std::unordered_map<AgentType, TypeConfig> populations;
      
      // Utility methods
      uint32_t total_count() const;
      void normalize(); // Ensure counts fit in pool limits
  };
  ```

- [ ] Implement [`AgentZoo::spawn_agent()`](src/agents/AgentZoo.cpp):
  - Acquire object from appropriate pool based on type
  - Initialize with unique ID and config
  - Add to active_agents_ array
  - Return pointer

- [ ] Implement population presets:
  ```cpp
  PopulationConfig create_bull_run_population();
  PopulationConfig create_consolidation_population();
  PopulationConfig create_flash_crash_population();
  ```

- [ ] Add unit tests in [`AgentZooTest.cpp`](tests/agents/AgentZooTest.cpp):
  - Test agent spawning
  - Test population limits
  - Test agent retrieval

**Concepts:** Object Pooling, Population Management, Zero Allocation

### Day 10: Agent Orchestrator (Scheduler)
**Files:** [`AgentOrchestrator.h`](include/agents/AgentOrchestrator.h), [`AgentOrchestrator.cpp`](src/agents/AgentOrchestrator.cpp)

- [ ] Create [`AgentOrchestrator`](include/agents/AgentOrchestrator.h) class:
  ```cpp
  class AgentOrchestrator {
  public:
      AgentOrchestrator(AgentZoo& zoo, 
                       concurrency::RingBuffer<Order>& order_buffer,
                       memory::ObjectPool<Order>& order_pool);
      
      // Lifecycle
      void start();
      void stop();
      
      // Main loop (runs in separate thread)
      void run();
      
      // Configuration
      void set_tick_rate(uint32_t ticks_per_second);
      void set_population(const PopulationConfig& pop);
      
      // Market state
      void update_market_state(const MarketState& state);
      
  private:
      AgentZoo& zoo_;
      concurrency::RingBuffer<Order>& order_buffer_;
      memory::ObjectPool<Order>& order_pool_;
      
      MarketState current_market_state_;
      std::atomic<bool> running_{false};
      std::thread orchestrator_thread_;
      
      uint32_t tick_rate_{100};  // Hz
      uint64_t tick_count_{0};
  };
  ```

- [ ] Implement [`AgentOrchestrator::run()`](src/agents/AgentOrchestrator.cpp):
  ```cpp
  void AgentOrchestrator::run() {
      auto tick_interval = std::chrono::microseconds(1'000'000 / tick_rate_);
      
      while (running_) {
          auto start = std::chrono::steady_clock::now();
          
          // Get all active agents
          auto agents = zoo_.get_active_agents();
          
          // Shuffle for fairness (avoid ordering bias)
          std::shuffle(agents.begin(), agents.end(), rng_);
          
          // Tick each agent
          for (Agent* agent : agents) {
              agent->tick(current_market_state_);
              
              Order* order = agent->decide(current_market_state_);
              if (order) {
                  // Push to ring buffer
                  if (!order_buffer_.push(order)) {
                      // Buffer full, release order
                      order_pool_.release(order);
                  }
              }
          }
          
          tick_count_++;
          
          // Sleep to maintain tick rate
          auto elapsed = std::chrono::steady_clock::now() - start;
          if (elapsed < tick_interval) {
              std::this_thread::sleep_for(tick_interval - elapsed);
          }
      }
  }
  ```

- [ ] Implement [`update_market_state()`](src/agents/AgentOrchestrator.cpp):
  - Called by main engine thread with latest market data
  - Thread-safe update of current_market_state_
  - Calculate derived metrics (SMA, volatility)

**Concepts:** Thread Orchestration, Tick Scheduling, Fairness

**Milestone:** Stable market with liquidity - price should oscillate around a mean without crashing to zero or exploding to infinity.

---

## Week 8: The Psychology (Trend & Mean Reversion)

**Goal:** Introduce momentum and contrarian behaviors to create trends and support/resistance

### Day 11-13: Trend Followers / Momentum Bots
**Files:** [`TrendFollower.h`](include/agents/TrendFollower.h), [`TrendFollower.cpp`](src/agents/TrendFollower.cpp)

- [ ] Implement [`TrendFollower::decide()`](src/agents/TrendFollower.cpp):
  - Calculate Simple Moving Average (SMA):
    ```cpp
    double TrendFollower::calculate_sma(uint32_t period) {
        // Use circular buffer of recent prices
        double sum = 0.0;
        for (uint32_t i = 0; i < period && i < price_history_.size(); i++) {
            sum += price_history_[i];
        }
        return sum / std::min(period, price_history_.size());
    }
    ```

  - Trend detection logic:
    ```cpp
    double sma_50 = state.price_sma_50;
    double sma_200 = state.price_sma_200;
    double threshold = config_.params["threshold_pct"];
    
    // Golden Cross: SMA50 > SMA200 (bullish)
    bool golden_cross = sma_50 > sma_200 * (1 + threshold);
    
    // Death Cross: SMA50 < SMA200 (bearish)  
    bool death_cross = sma_50 < sma_200 * (1 - threshold);
    
    // Price breakout above SMA
    bool breakout_up = state.last_price > sma_50 * (1 + threshold);
    bool breakout_down = state.last_price < sma_50 * (1 - threshold);
    
    if (golden_cross || breakout_up) {
        // PANIC BUY - Market orders for momentum
        uint32_t qty = config_.order_size_mean * config_.aggression;
        return create_market_order(BUY, qty);
    }
    else if (death_cross || breakout_down) {
        // PANIC SELL
        uint32_t qty = config_.order_size_mean * config_.aggression;
        return create_market_order(SELL, qty);
    }
    
    return nullptr;  // No signal
    ```

- [ ] Add momentum acceleration:
  - Stronger signal = larger order size
  - Track momentum strength: `|price - sma| / sma`
  - Scale order size by momentum strength

- [ ] Implement cooldown mechanism:
  - After large order, wait N ticks before next signal
  - Prevents over-trading

- [ ] Add unit tests in [`TrendFollowerTest.cpp`](tests/agents/TrendFollowerTest.cpp):
  - Test SMA calculation
  - Test golden/death cross detection
  - Test order generation on breakout

**Concepts:** Moving Averages, Momentum, Positive Feedback Loops, Trend Following

### Day 14-16: Mean Reverters / Value Bots
**Files:** [`MeanReverter.h`](include/agents/MeanReverter.h), [`MeanReverter.cpp`](src/agents/MeanReverter.cpp)

- [ ] Implement [`MeanReverter::decide()`](src/agents/MeanReverter.cpp):
  - Define fair value:
    ```cpp
    double fair_value = config_.params["fair_value"];  // e.g., 100.00
    double threshold = config_.params["threshold_pct"]; // e.g., 0.05 (5%)
    
    double upper_band = fair_value * (1 + threshold);
    double lower_band = fair_value * (1 - threshold);
    ```

  - Reversion logic:
    ```cpp
    if (state.last_price > upper_band) {
        // OVERVALUED - Sell
        // More aggressive if further from fair value
        double deviation = (state.last_price - fair_value) / fair_value;
        uint32_t qty = config_.order_size_mean * (1 + deviation);
        
        // Use limit orders slightly below market for better execution
        double price = state.last_price * 0.999;
        return create_limit_order(SELL, price, qty);
    }
    else if (state.last_price < lower_band) {
        // UNDERVALUED - Buy
        double deviation = (fair_value - state.last_price) / fair_value;
        uint32_t qty = config_.order_size_mean * (1 + deviation);
        
        double price = state.last_price * 1.001;
        return create_limit_order(BUY, price, qty);
    }
    
    return nullptr;  // Price within fair range
    ```

- [ ] Add Bollinger Bands variant:
  - Fair value = SMA(20)
  - Upper/Lower bands = SMA ± 2 * StdDev
  - Sell when price touches upper band
  - Buy when price touches lower band

- [ ] Add RSI (Relative Strength Index) variant:
  - Calculate RSI from recent price changes
  - Overbought: RSI > 70 → Sell
  - Oversold: RSI < 30 → Buy

- [ ] Implement position-aware trading:
  - Reduce order size if already have large position
  - More aggressive if position is small

- [ ] Add unit tests in [`MeanReverterTest.cpp`](tests/agents/MeanReverterTest.cpp):
  - Test fair value logic
  - Test Bollinger Bands
  - Test RSI calculation

**Concepts:** Mean Reversion, Support/Resistance, Bollinger Bands, RSI, Contrarian Trading

### Day 17-18: Whale Agent (Market Impact)
**Files:** [`Whale.h`](include/agents/Whale.h), [`Whale.cpp`](src/agents/Whale.cpp)

- [ ] Implement [`Whale::decide()`](src/agents/Whale.cpp):
  - Triggered by external event or time:
    ```cpp
    if (tick_count_ == trigger_tick_) {
        // Execute large market order
        OrderSide side = config_.params["whale_side"];
        uint32_t qty = config_.params["whale_size"];  // e.g., 10,000
        
        triggered_ = true;
        return create_market_order(side, qty);
    }
    ```

  - Iceberg order variant (split large order):
    ```cpp
    if (remaining_whale_qty_ > 0) {
        uint32_t slice = std::min(remaining_whale_qty_, 
                                  config_.params["slice_size"]);
        remaining_whale_qty_ -= slice;
        return create_market_order(whale_side_, slice);
    }
    ```

- [ ] Add TWAP (Time-Weighted Average Price) execution:
  - Split large order into N equal slices
  - Execute one slice every M ticks
  - Minimizes market impact

- [ ] Add configuration for different whale behaviors:
  - Instant dump (flash crash)
  - Gradual accumulation (smart money)
  - Iceberg orders (hidden size)

**Concepts:** Market Impact, Flash Crashes, Iceberg Orders, TWAP

### Day 19-21: Population Tuning & Scenarios
**Files:** [`PopulationPresets.cpp`](src/agents/PopulationPresets.cpp)

- [ ] Implement scenario configurations:

  **Scenario A: Bull Run**
  ```cpp
  PopulationConfig create_bull_run_population() {
      PopulationConfig config;
      
      // 40% Momentum (aggressive)
      config.populations[TREND_FOLLOWER] = {
          .count = 400,
          .base_config = {
              .aggression = 0.8,
              .params = {{"threshold_pct", 0.02}}
          }
      };
      
      // 10% Value (conservative)
      config.populations[MEAN_REVERTER] = {
          .count = 100,
          .base_config = {
              .aggression = 0.3,
              .params = {{"fair_value", 100.0}, {"threshold_pct", 0.10}}
          }
      };
      
      // 50% Market Makers (liquidity)
      config.populations[MARKET_MAKER] = {
          .count = 500,
          .base_config = {
              .params = {{"spread_pct", 0.001}}
          }
      };
      
      return config;
  }
  ```

  **Scenario B: Consolidation**
  ```cpp
  PopulationConfig create_consolidation_population() {
      PopulationConfig config;
      
      // 0% Momentum
      
      // 50% Value (tight range)
      config.populations[MEAN_REVERTER] = {
          .count = 500,
          .base_config = {
              .aggression = 0.7,
              .params = {{"fair_value", 100.0}, {"threshold_pct", 0.02}}
          }
      };
      
      // 50% Market Makers
      config.populations[MARKET_MAKER] = {
          .count = 500,
          .base_config = {
              .params = {{"spread_pct", 0.0005}}
          }
      };
      
      return config;
  }
  ```

  **Scenario C: Flash Crash**
  ```cpp
  PopulationConfig create_flash_crash_population() {
      PopulationConfig config;
      
      // Base population (normal market)
      config.populations[MARKET_MAKER] = {.count = 300};
      config.populations[NOISE_TRADER] = {.count = 200};
      config.populations[TREND_FOLLOWER] = {.count = 100};
      
      // Add Whale that triggers at t=5000
      config.populations[WHALE] = {
          .count = 1,
          .base_config = {
              .params = {
                  {"trigger_tick", 5000},
                  {"whale_side", static_cast<double>(SELL)},
                  {"whale_size", 10000}
              }
          }
      };
      
      return config;
  }
  ```

- [ ] Test each scenario for 10,000 ticks:
  - Bull Run: Expect rising price trend
  - Consolidation: Expect price range-bound
  - Flash Crash: Expect sudden drop at t=5000

- [ ] Add population ratio tuning utilities:
  ```cpp
  void tune_population_ratios(PopulationConfig& config, 
                              double momentum_pct,
                              double value_pct,
                              double mm_pct);
  ```

**Milestone:** Scenarios produce expected price dynamics

---

## Week 9: The Validation (Pattern Detection & Analytics)

**Goal:** Prove agents are creating recognizable TA patterns

### Day 22-24: OHLCV Aggregation
**Files:** [`OHLCVAggregator.h`](include/analytics/OHLCVAggregator.h), [`OHLCVAggregator.cpp`](src/analytics/OHLCVAggregator.cpp)

- [ ] Create [`Candle`](include/analytics/OHLCVAggregator.h) structure:
  ```cpp
  struct Candle {
      uint64_t timestamp;     // Start time of candle
      double open;
      double high;
      double low;
      double close;
      uint64_t volume;
      uint32_t trade_count;
      
      // Derived properties
      double body_size() const { return std::abs(close - open); }
      double upper_wick() const { return high - std::max(open, close); }
      double lower_wick() const { return std::min(open, close) - low; }
      bool is_bullish() const { return close > open; }
  };
  ```

- [ ] Implement [`OHLCVAggregator`](include/analytics/OHLCVAggregator.h) class:
  ```cpp
  class OHLCVAggregator {
  public:
      OHLCVAggregator(uint64_t candle_period_ms = 1000);
      
      // Feed trades
      void add_trade(const Trade& trade);
      
      // Get completed candles
      std::vector<Candle> get_candles(uint32_t max_count = 100);
      Candle get_current_candle() const;
      
      // Export
      void export_to_csv(const std::string& filename);
      
  private:
      uint64_t candle_period_ms_;
      Candle current_candle_;
      std::deque<Candle> completed_candles_;
      
      void finalize_current_candle(uint64_t timestamp);
  };
  ```

- [ ] Implement [`add_trade()`](src/analytics/OHLCVAggregator.cpp):
  ```cpp
  void OHLCVAggregator::add_trade(const Trade& trade) {
      // Check if we need to start a new candle
      if (current_candle_.timestamp == 0) {
          // First trade
          start_new_candle(trade.timestamp);
      }
      else if (trade.timestamp >= current_candle_.timestamp + candle_period_ms_) {
          // Candle period elapsed
          finalize_current_candle(trade.timestamp);
          start_new_candle(trade.timestamp);
      }
      
      // Update OHLC
      if (current_candle_.trade_count == 0) {
          current_candle_.open = trade.price;
          current_candle_.high = trade.price;
          current_candle_.low = trade.price;
      }
      else {
          current_candle_.high = std::max(current_candle_.high, trade.price);
          current_candle_.low = std::min(current_candle_.low, trade.price);
      }
      current_candle_.close = trade.price;
      current_candle_.volume += trade.quantity;
      current_candle_.trade_count++;
  }
  ```

- [ ] Add CSV export for analysis in Python/Excel:
  ```cpp
  void OHLCVAggregator::export_to_csv(const std::string& filename) {
      std::ofstream file(filename);
      file << "timestamp,open,high,low,close,volume,trades\n";
      for (const auto& candle : completed_candles_) {
          file << candle.timestamp << ","
               << candle.open << ","
               << candle.high << ","
               << candle.low << ","
               << candle.close << ","
               << candle.volume << ","
               << candle.trade_count << "\n";
      }
  }
  ```

- [ ] Add unit tests in [`OHLCVAggregatorTest.cpp`](tests/analytics/OHLCVAggregatorTest.cpp):
  - Test candle creation from trades
  - Test period boundaries
  - Test OHLC accuracy

**Concepts:** OHLCV, Candlesticks, Time Series Aggregation

### Day 25-27: Pattern Recognition
**Files:** [`PatternScanner.h`](include/analytics/PatternScanner.h), [`PatternScanner.cpp`](src/analytics/PatternScanner.cpp)

- [ ] Define [`PatternType`](include/analytics/PatternScanner.h) enum:
  ```cpp
  enum class PatternType {
      // Trend Patterns
      UPTREND,
      DOWNTREND,
      SIDEWAYS,
      
      // Reversal Patterns
      HEAD_AND_SHOULDERS,
      INVERSE_HEAD_AND_SHOULDERS,
      DOUBLE_TOP,
      DOUBLE_BOTTOM,
      
      // Continuation Patterns
      BULL_FLAG,
      BEAR_FLAG,
      TRIANGLE,
      
      // Candlestick Patterns
      DOJI,
      HAMMER,
      SHOOTING_STAR,
      ENGULFING_BULLISH,
      ENGULFING_BEARISH,
      
      // Gap Patterns
      GAP_UP,
      GAP_DOWN
  };
  ```

- [ ] Implement basic pattern detectors:

  **Check 1: Trend Detection**
  ```cpp
  bool PatternScanner::detect_uptrend(const std::vector<Candle>& candles, 
                                      uint32_t min_consecutive = 5) {
      if (candles.size() < min_consecutive) return false;
      
      for (size_t i = 1; i < min_consecutive; i++) {
          if (candles[i].close <= candles[i-1].close) {
              return false;
          }
      }
      return true;
  }
  ```

  **Check 2: Head & Shoulders**
  ```cpp
  bool PatternScanner::detect_head_and_shoulders(
      const std::vector<Candle>& candles) {
      
      if (candles.size() < 7) return false;
      
      // Find three peaks
      auto peaks = find_local_maxima(candles, 3);
      if (peaks.size() != 3) return false;
      
      // Check pattern: left_shoulder < head > right_shoulder
      double left = peaks[0].high;
      double head = peaks[1].high;
      double right = peaks[2].high;
      
      bool valid_pattern = 
          (head > left * 1.05) &&   // Head significantly higher
          (head > right * 1.05) &&
          (std::abs(left - right) < left * 0.05);  // Shoulders similar
      
      return valid_pattern;
  }
  ```

  **Check 3: Bull Flag**
  ```cpp
  bool PatternScanner::detect_bull_flag(const std::vector<Candle>& candles) {
      if (candles.size() < 10) return false;
      
      // Phase 1: Flagpole (strong uptrend)
      bool has_flagpole = false;
      for (size_t i = 1; i < 5; i++) {
          if (candles[i].close > candles[i-1].close * 1.02) {
              has_flagpole = true;
              break;
          }
      }
      
      // Phase 2: Flag (consolidation/slight downtrend)
      bool has_flag = true;
      for (size_t i = 5; i < candles.size(); i++) {
          double range = (candles[i].high - candles[i].low) / candles[i].low;
          if (range > 0.03) {  // Too volatile
              has_flag = false;
              break;
          }
      }
      
      return has_flagpole && has_flag;
  }
  ```

  **Check 4: Engulfing Pattern**
  ```cpp
  bool PatternScanner::detect_bullish_engulfing(
      const Candle& prev, const Candle& current) {
      
      bool prev_bearish = prev.close < prev.open;
      bool current_bullish = current.close > current.open;
      bool engulfs = current.open <= prev.close && 
                     current.close >= prev.open;
      
      return prev_bearish && current_bullish && engulfs;
  }
  ```

- [ ] Create [`PatternScanner`](include/analytics/PatternScanner.h) class:
  ```cpp
  class PatternScanner {
  public:
      struct PatternMatch {
          PatternType pattern;
          uint32_t start_index;
          uint32_t end_index;
          double confidence;  // 0.0 - 1.0
          std::string description;
      };
      
      std::vector<PatternMatch> scan(const std::vector<Candle>& candles);
      
      // Individual pattern detectors
      bool detect_uptrend(const std::vector<Candle>& candles);
      bool detect_head_and_shoulders(const std::vector<Candle>& candles);
      bool detect_bull_flag(const std::vector<Candle>& candles);
      // ... more patterns
      
  private:
      std::vector<size_t> find_local_maxima(const std::vector<Candle>& candles, 
                                           uint32_t count);
      std::vector<size_t> find_local_minima(const std::vector<Candle>& candles,
                                           uint32_t count);
  };
  ```

- [ ] Add unit tests in [`PatternScannerTest.cpp`](tests/analytics/PatternScannerTest.cpp):
  - Test each pattern detector with synthetic data
  - Test false positive rate

**Concepts:** Technical Analysis, Pattern Recognition, Peak Detection

### Day 28-30: Validation & Testing
**Files:** Multiple

- [ ] Run each scenario and validate patterns:

  **Scenario A (Bull Run):**
  - [ ] Run for 10,000 ticks
  - [ ] Export OHLCV to CSV
  - [ ] Scan for patterns
  - [ ] Expected: UPTREND detected, BULL_FLAG present
  - [ ] Verify Golden Cross (SMA50 > SMA200)

  **Scenario B (Consolidation):**
  - [ ] Run for 10,000 ticks
  - [ ] Expected: SIDEWAYS detected, TRIANGLE or RECTANGLE pattern
  - [ ] Price should stay within 2% range

  **Scenario C (Flash Crash):**
  - [ ] Run for 10,000 ticks
  - [ ] Expected: GAP_DOWN at t=5000
  - [ ] Expected: BEARISH_ENGULFING candle
  - [ ] Price should drop >10% rapidly

- [ ] Create validation script:
  ```cpp
  void validate_scenario(const std::string& name,
                        const PopulationConfig& config,
                        uint64_t ticks) {
      // Setup simulation
      // Run scenario
      // Export OHLCV
      // Scan patterns
      // Print report
      std::cout << "Scenario: " << name << "\n";
      std::cout << "Patterns found:\n";
      for (const auto& match : patterns) {
          std::cout << "  - " << match.description 
                   << " (confidence: " << match.confidence << ")\n";
      }
  }
  ```

- [ ] Add integration tests in [`SimulationTest.cpp`](tests/integration/SimulationTest.cpp):
  - Test full pipeline: Agents → Orders → Matching → Trades → OHLCV → Patterns
  - Test each scenario produces expected results

**Milestone:** Pattern detection validates that agents create realistic market dynamics

---

## Week 10: The Interface (GUI Configuration & Visualization)

**Goal:** Create interactive UI for configuring agent behaviors and visualizing results

### Day 31-33: Configuration Panel (ImGui)
**Files:** [`AgentConfigPanel.h`](include/ui/AgentConfigPanel.h), [`AgentConfigPanel.cpp`](src/ui/AgentConfigPanel.cpp)

- [ ] Create ImGui configuration window:
  ```cpp
  class AgentConfigPanel {
  public:
      void render();
      
      PopulationConfig get_config() const { return config_; }
      void set_config(const PopulationConfig& config);
      
      // Callbacks
      void set_on_apply_callback(std::function<void(PopulationConfig)> cb);
      
  private:
      PopulationConfig config_;
      
      void render_population_slider(AgentType type);
      void render_agent_params(AgentType type);
      void render_presets();
  };
  ```

- [ ] Implement population controls:
  ```cpp
  void AgentConfigPanel::render() {
      ImGui::Begin("Agent Configuration");
      
      // Preset selection
      if (ImGui::BeginCombo("Preset", current_preset_.c_str())) {
          if (ImGui::Selectable("Bull Run")) {
              config_ = create_bull_run_population();
              current_preset_ = "Bull Run";
          }
          if (ImGui::Selectable("Consolidation")) {
              config_ = create_consolidation_population();
              current_preset_ = "Consolidation";
          }
          if (ImGui::Selectable("Flash Crash")) {
              config_ = create_flash_crash_population();
              current_preset_ = "Flash Crash";
          }
          if (ImGui::Selectable("Custom")) {
              current_preset_ = "Custom";
          }
          ImGui::EndCombo();
      }
      
      ImGui::Separator();
      ImGui::Text("Population Mix");
      
      // Population sliders
      render_population_slider(NOISE_TRADER);
      render_population_slider(MARKET_MAKER);
      render_population_slider(TREND_FOLLOWER);
      render_population_slider(MEAN_REVERTER);
      render_population_slider(WHALE);
      
      ImGui::Separator();
      
      // Show total
      uint32_t total = config_.total_count();
      ImGui::Text("Total Agents: %u / %u", total, max_agents_);
      ImGui::ProgressBar(static_cast<float>(total) / max_agents_);
      
      // Apply button
      if (ImGui::Button("Apply Configuration")) {
          if (on_apply_callback_) {
              on_apply_callback_(config_);
          }
      }
      
      ImGui::End();
  }
  ```

- [ ] Implement parameter controls:
  ```cpp
  void AgentConfigPanel::render_population_slider(AgentType type) {
      std::string label = to_string(type);
      uint32_t& count = config_.populations[type].count;
      
      ImGui::SliderInt(label.c_str(), 
                      reinterpret_cast<int*>(&count), 
                      0, 1000);
      
      // Collapsible parameters
      if (ImGui::TreeNode((label + " Parameters").c_str())) {
          AgentConfig& agent_config = config_.populations[type].base_config;
          
          ImGui::SliderFloat("Aggression", &agent_config.aggression, 0.0f, 1.0f);
          ImGui::SliderFloat("Risk Tolerance", &agent_config.risk_tolerance, 0.0f, 1.0f);
          ImGui::InputInt("Max Position", 
                         reinterpret_cast<int*>(&agent_config.max_position));
          
          // Type-specific parameters
          switch (type) {
              case MARKET_MAKER:
                  ImGui::SliderFloat("Spread %", 
                                    &agent_config.params["spread_pct"], 
                                    0.001f, 0.01f, "%.4f");
                  break;
              case TREND_FOLLOWER:
                  ImGui::SliderFloat("Threshold %",
                                    &agent_config.params["threshold_pct"],
                                    0.01f, 0.10f, "%.3f");
                  break;
              case MEAN_REVERTER:
                  ImGui::InputDouble("Fair Value",
                                    &agent_config.params["fair_value"]);
                  ImGui::SliderFloat("Band %",
                                    &agent_config.params["threshold_pct"],
                                    0.01f, 0.20f, "%.3f");
                  break;
          }
          
          ImGui::TreePop();
      }
  }
  ```

**Concepts:** ImGui, UI State Management, Configuration Panels

### Day 34-36: Candlestick Chart Viewer
**Files:** [`CandlestickChart.h`](include/ui/CandlestickChart.h), [`CandlestickChart.cpp`](src/ui/CandlestickChart.cpp)

- [ ] Replace/extend existing DepthChartViewer with candlestick support:
  ```cpp
  class CandlestickChart {
  public:
      void render(const std::vector<Candle>& candles);
      void set_viewport(float x, float y, float width, float height);
      
      // Display options
      void set_show_volume(bool show) { show_volume_ = show; }
      void set_show_sma(bool show) { show_sma_ = show; }
      void set_show_patterns(bool show) { show_patterns_ = show; }
      
  private:
      void render_candle(const Candle& candle, float x, float y_scale);
      void render_volume_bars(const std::vector<Candle>& candles);
      void render_sma_overlay(const std::vector<Candle>& candles);
      void render_pattern_markers(const std::vector<PatternMatch>& patterns);
      
      bool show_volume_{true};
      bool show_sma_{true};
      bool show_patterns_{true};
  };
  ```

- [ ] Implement candlestick rendering (OpenGL):
  ```cpp
  void CandlestickChart::render_candle(const Candle& candle, 
                                       float x, float y_scale) {
      // Determine color
      ImVec4 color = candle.is_bullish() ? 
                     ImVec4(0.0f, 1.0f, 0.0f, 1.0f) :  // Green
                     ImVec4(1.0f, 0.0f, 0.0f, 1.0f);   // Red
      
      float candle_width = 5.0f;
      
      // Draw wick (high-low line)
      float high_y = candle.high * y_scale;
      float low_y = candle.low * y_scale;
      draw_line(x, high_y, x, low_y, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
      
      // Draw body (open-close rectangle)
      float open_y = candle.open * y_scale;
      float close_y = candle.close * y_scale;
      float top_y = std::max(open_y, close_y);
      float bottom_y = std::min(open_y, close_y);
      
      fill_rect(x - candle_width/2, bottom_y,
               candle_width, top_y - bottom_y, color);
  }
  ```

- [ ] Add SMA overlay:
  ```cpp
  void CandlestickChart::render_sma_overlay(const std::vector<Candle>& candles) {
      std::vector<float> sma_50 = calculate_sma(candles, 50);
      std::vector<float> sma_200 = calculate_sma(candles, 200);
      
      // Draw SMA50 (yellow line)
      for (size_t i = 1; i < sma_50.size(); i++) {
          draw_line(i-1, sma_50[i-1], i, sma_50[i],
                   ImVec4(1.0f, 1.0f, 0.0f, 0.8f));
      }
      
      // Draw SMA200 (blue line)
      for (size_t i = 1; i < sma_200.size(); i++) {
          draw_line(i-1, sma_200[i-1], i, sma_200[i],
                   ImVec4(0.0f, 0.5f, 1.0f, 0.8f));
      }
  }
  ```

- [ ] Add pattern markers:
  ```cpp
  void CandlestickChart::render_pattern_markers(
      const std::vector<PatternMatch>& patterns) {
      
      for (const auto& pattern : patterns) {
          // Draw box around pattern
          float x1 = pattern.start_index;
          float x2 = pattern.end_index;
          
          draw_rect_outline(x1, y_min, x2 - x1, y_max - y_min,
                          ImVec4(1.0f, 0.5f, 0.0f, 0.5f));
          
          // Draw label
          std::string label = to_string(pattern.pattern);
          draw_text(x1, y_max + 10, label.c_str(),
                   ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
      }
  }
  ```

**Concepts:** Candlestick Charts, Technical Indicators Visualization, OpenGL Rendering

### Day 37-38: Real-Time Statistics Dashboard
**Files:** [`StatsDashboard.h`](include/ui/StatsDashboard.h), [`StatsDashboard.cpp`](src/ui/StatsDashboard.cpp)

- [ ] Create real-time statistics panel:
  ```cpp
  class StatsDashboard {
  public:
      void render();
      void update(const MarketState& state, 
                 const PopulationStats& pop_stats);
      
  private:
      MarketState current_state_;
      PopulationStats pop_stats_;
      
      // Historical data for sparklines
      std::deque<double> price_history_;
      std::deque<double> volume_history_;
      std::deque<double> spread_history_;
  };
  ```

- [ ] Implement dashboard:
  ```cpp
  void StatsDashboard::render() {
      ImGui::Begin("Market Statistics");
      
      // Price information
      ImGui::Text("Last Price: %.2f", current_state_.last_price);
      ImGui::SameLine();
      ImGui::TextColored(price_change_color(), 
                        "%.2f%%", price_change_pct());
      
      ImGui::Text("Best Bid: %.2f", current_state_.best_bid);
      ImGui::Text("Best Ask: %.2f", current_state_.best_ask);
      ImGui::Text("Spread: %.2f (%.3f%%)", 
                 current_state_.spread,
                 current_state_.spread / current_state_.last_price * 100);
      
      // Sparklines
      ImGui::Separator();
      ImGui::Text("Price History");
      ImGui::PlotLines("", 
                      price_history_.data(), 
                      price_history_.size(),
                      0, nullptr, 
                      FLT_MAX, FLT_MAX,
                      ImVec2(0, 80));
      
      ImGui::Text("Volume History");
      ImGui::PlotHistogram("",
                          volume_history_.data(),
                          volume_history_.size(),
                          0, nullptr,
                          0, FLT_MAX,
                          ImVec2(0, 80));
      
      // Agent population
      ImGui::Separator();
      ImGui::Text("Active Agents: %u", pop_stats_.total_active);
      
      for (const auto& [type, count] : pop_stats_.counts_by_type) {
          float pct = static_cast<float>(count) / pop_stats_.total_active;
          ImGui::Text("%s: %u (%.1f%%)", 
                     to_string(type).c_str(), 
                     count, 
                     pct * 100);
      }
      
      // Pattern detection results
      ImGui::Separator();
      ImGui::Text("Detected Patterns:");
      for (const auto& pattern : detected_patterns_) {
          ImGui::BulletText("%s (%.1f%%)", 
                          pattern.description.c_str(),
                          pattern.confidence * 100);
      }
      
      ImGui::End();
  }
  ```

- [ ] Add simulation controls:
  ```cpp
  ImGui::Begin("Simulation Control");
  
  if (ImGui::Button(is_running_ ? "Pause" : "Start")) {
      is_running_ = !is_running_;
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
      reset_simulation();
  }
  
  ImGui::SliderInt("Speed", &tick_rate_, 10, 1000, "%d ticks/sec");
  
  ImGui::Text("Tick: %llu", current_tick_);
  ImGui::Text("Time: %.1f sec", current_tick_ / static_cast<float>(tick_rate_));
  
  ImGui::End();
  ```

**Concepts:** Real-Time Dashboards, Data Visualization, ImGui Widgets

### Day 39-40: Integration & Polish
**Files:** [`main_abms.cpp`](src/main_abms.cpp)

- [ ] Create main application with full UI:
  ```cpp
  int main() {
      // Initialize components
      MatchingEngine engine;
      AgentZoo zoo(10000);
      AgentOrchestrator orchestrator(zoo, ring_buffer, order_pool);
      OHLCVAggregator ohlcv_agg(1000);  // 1-second candles
      PatternScanner scanner;
      
      // UI components
      AgentConfigPanel config_panel;
      CandlestickChart chart;
      StatsDashboard dashboard;
      
      // Set default population
      config_panel.set_config(create_bull_run_population());
      
      // Callback: Apply new configuration
      config_panel.set_on_apply_callback([&](PopulationConfig config) {
          orchestrator.stop();
          zoo.reset_all();
          orchestrator.set_population(config);
          orchestrator.start();
      });
      
      // Main loop
      while (!should_quit) {
          // Process engine (main thread)
          process_engine_tick();
          
          // Update market state for agents
          MarketState state = extract_market_state(engine);
          orchestrator.update_market_state(state);
          
          // Aggregate trades to OHLCV
          for (const auto& trade : recent_trades) {
              ohlcv_agg.add_trade(trade);
          }
          
          // Scan for patterns
          auto candles = ohlcv_agg.get_candles(200);
          auto patterns = scanner.scan(candles);
          
          // Render UI
          ImGui::NewFrame();
          
          config_panel.render();
          chart.render(candles);
          dashboard.update(state, zoo.get_stats());
          dashboard.render();
          
          ImGui::Render();
          SwapBuffers();
      }
      
      return 0;
  }
  ```

- [ ] Add keyboard shortcuts:
  - Space: Pause/Resume
  - R: Reset simulation
  - S: Save OHLCV data
  - 1-5: Load preset scenarios

- [ ] Add export & import:
  - Export population config to JSON
  - Export OHLCV to CSV
  - Export screenshot

- [ ] Performance optimization:
  - Profile UI rendering overhead
  - Optimize candlestick rendering for large datasets
  - Add downsampling for >1000 candles

- [ ] Final testing:
  - [ ] Test all presets load correctly
  - [ ] Test pattern detection accuracy
  - [ ] Test UI responsiveness under load
  - [ ] Test export functionality

**Milestone:** ✅ **COMPLETE** - Fully interactive ABMS with visual configuration and validation

---

## Testing Strategy

### Unit Tests
```bash
# Test agent behaviors
./build/lob_tests --gtest_filter=NoiseTraderTest.*
./build/lob_tests --gtest_filter=MarketMakerTest.*
./build/lob_tests --gtest_filter=TrendFollowerTest.*
./build/lob_tests --gtest_filter=MeanReverterTest.*

# Test agent infrastructure
./build/lob_tests --gtest_filter=AgentZooTest.*
./build/lob_tests --gtest_filter=OHLCVAggregatorTest.*
./build/lob_tests --gtest_filter=PatternScannerTest.*
```

### Integration Tests
```bash
# Test full scenarios
./build/lob_tests --gtest_filter=SimulationTest.BullRunScenario
./build/lob_tests --gtest_filter=SimulationTest.ConsolidationScenario
./build/lob_tests --gtest_filter=SimulationTest.FlashCrashScenario
```

### Visual Validation
1. Run simulation for 10,000 ticks
2. Export OHLCV to CSV
3. Open in TradingView or Python (matplotlib/mplfinance)
4. Verify patterns match expectations

---

## Resources

### Agent-Based Modeling
- **Papers:**
  - J. D. Farmer (2002): "Market force, ecology and evolution"
  - LeBaron (2001): "Agent-based computational finance"
  - Arthur et al. (1997): "Asset Pricing Under Endogenous Expectations"

### Technical Analysis
- **Books:**
  - Thomas Bulkowski: "Encyclopedia of Chart Patterns"
  - John Murphy: "Technical Analysis of Financial Markets"
- **Libraries:**
  - TA-Lib: Technical Analysis Library (C/C++)
  - Tulip Indicators (C)

### Visualization
- **ImGui Documentation:** https://github.com/ocornut/imgui
- **ImPlot:** https://github.com/epezent/implot (for advanced plotting)

---

## Success Criteria

- [ ] All agent unit tests passing
- [ ] All integration tests passing
- [ ] Bull Run scenario produces uptrend with bull flags
- [ ] Consolidation scenario produces sideways movement
- [ ] Flash Crash scenario produces sudden price drop
- [ ] Pattern scanner detects at least 3 patterns per scenario
- [ ] UI allows live configuration changes
- [ ] Candlestick chart renders smoothly (>30 FPS)
- [ ] OHLCV export compatible with standard tools

---

## Current Progress Summary

### ✅ Week 7: Foundation (PARTIALLY COMPLETE)
- [x] Agent base classes (Days 1-2)
- [x] Noise traders (Days 3-4)
- [x] Market makers (Days 5-7)
- [ ] Agent pool (Zoo) (Days 8-9)
- [ ] Agent orchestrator (Day 10)

### ⏳ Week 8: Psychology (NOT STARTED)
- [ ] Trend followers
- [ ] Mean reverters
- [ ] Whale agent
- [ ] Population scenarios

### ⏳ Week 9: Validation (NOT STARTED)
- [ ] OHLCV aggregation
- [ ] Pattern recognition
- [ ] Scenario validation

### ⏳ Week 10: Interface (NOT STARTED)
- [ ] Configuration panel
- [ ] Candlestick chart
- [ ] Statistics dashboard
- [ ] Integration & polish

---

## Estimated File Structure

```
include/
  agents/
    Agent.h
    AgentZoo.h
    MarketState.h
    NoiseTrader.h
    MarketMaker.h
    TrendFollower.h
    MeanReverter.h
    Whale.h
    AgentOrchestrator.h
    PopulationPresets.h
  analytics/
    OHLCVAggregator.h
    PatternScanner.h
  ui/
    AgentConfigPanel.h
    CandlestickChart.h
    StatsDashboard.h

src/
  agents/
    Agent.cpp
    AgentZoo.cpp
    NoiseTrader.cpp
    MarketMaker.cpp
    TrendFollower.cpp
    MeanReverter.cpp
    Whale.cpp
    AgentOrchestrator.cpp
    PopulationPresets.cpp
  analytics/
    OHLCVAggregator.cpp
    PatternScanner.cpp
  ui/
    AgentConfigPanel.cpp
    CandlestickChart.cpp
    StatsDashboard.cpp
  main_abms.cpp

tests/
  agents/
    NoiseTraderTest.cpp
    MarketMakerTest.cpp
    TrendFollowerTest.cpp
    MeanReverterTest.cpp
    AgentZooTest.cpp
  analytics/
    OHLCVAggregatorTest.cpp
    PatternScannerTest.cpp
  integration/
    SimulationTest.cpp
```

---

## Next Steps

1. **Update [`CMakeLists.txt`](CMakeLists.txt)** to include new agent source files
2. **Start Week 7, Day 1:** Create [`Agent.h`](include/agents/Agent.h) base class
3. **Follow TDD:** Write tests first, then implement
4. **Iterate:** Run tests frequently, validate behavior early

**Ready to transform your matching engine into a living, breathing market! 🚀📈**
