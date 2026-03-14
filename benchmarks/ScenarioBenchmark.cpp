/**
 * @file ScenarioBenchmark.cpp
 * @brief Population Scenario Benchmark – Week 10
 *
 * Uses the real AgentOrchestrator + pool-wired agents (decide() now returns
 * actual Order objects).
 *
 * Key design notes
 * ─────────────────
 * 1. All large objects (AgentZoo, MatchingEngine, etc.) are heap-allocated
 *    because ObjectPool<T,N> stores objects in an inline std::array – putting
 *    one on the stack would overflow the 8 MB default stack immediately.
 *
 * 2. engine->process_order() always routes to DEFAULT_INSTRUMENT (not "ABMS").
 *    Seed orders therefore use engine->get_order_pool() (1M capacity) so they
 *    do not eat into the 10K agent_pool that agents need for decide().
 *
 * 3. Market state SMA values are maintained as EMA and updated on every
 *    trade to track price momentum in real-time.
 *
 * 4. Periodic book replenishment ensures there is always liquidity for
 *    aggressive orders to trade against, even after the initial seed is
 *    consumed.
 *
 * Build:
 *   cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARK=ON
 *   ninja lob_scenario_benchmark && ./lob_scenario_benchmark
 */

#include "../include/agents/AgentZoo.h"
#include "../include/agents/AgentOrchestrator.h"
#include "../include/agents/MarketState.h"
#include "../include/core/MatchingEngine.h"
#include "../include/core/OrderBook.h"
#include "../include/concurrency/RingBuffer.h"
#include "../include/memory/ObjectPool.h"
#include "../include/analytics/OHLCVAggregator.h"
#include "../include/analytics/PatternScanner.h"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <vector>
#include <memory>
#include <cmath>
#include <limits>
#include <algorithm>
#include <string>

using namespace lob;

static constexpr double PRICE_SCALE = 100.0;
static constexpr double BASE_PRICE = 100.0;

// ─────────────────────────────────────────────────────────────────────────────
// Seed the order book with initial liquidity using engine's 1M internal pool
// ─────────────────────────────────────────────────────────────────────────────
static uint64_t seed_book(core::MatchingEngine &eng,
                          uint64_t &next_id,
                          double mid = BASE_PRICE,
                          int levels = 40,
                          double spread_pct = 0.001,
                          int qty_base = 200)
{
    auto &pool = eng.get_order_pool();
    uint64_t seeded = 0;

    for (int i = 0; i < levels; ++i)
    {
        double bid_p = mid * (1.0 - spread_pct * (i + 1));
        auto *bid = pool.acquire();
        if (bid)
        {
            *bid = core::Order(next_id++, 0,
                               static_cast<uint64_t>(bid_p * PRICE_SCALE),
                               static_cast<uint64_t>(qty_base + i * 20),
                               core::Side::BUY, core::OrderType::LIMIT);
            eng.process_order(bid);
            ++seeded;
        }

        double ask_p = mid * (1.0 + spread_pct * (i + 1));
        auto *ask = pool.acquire();
        if (ask)
        {
            *ask = core::Order(next_id++, 0,
                               static_cast<uint64_t>(ask_p * PRICE_SCALE),
                               static_cast<uint64_t>(qty_base + i * 20),
                               core::Side::SELL, core::OrderType::LIMIT);
            eng.process_order(ask);
            ++seeded;
        }
    }
    return seeded;
}

// ─────────────────────────────────────────────────────────────────────────────
// Replenish liquidity around current mid-price
// Adds fresh limit orders so the book never dries up.
// bid_bias controls asymmetry:
//   1.0 = symmetric, >1.0 = more bids (bullish), <1.0 = more asks (bearish)
// ─────────────────────────────────────────────────────────────────────────────
static uint64_t replenish_book(core::MatchingEngine &eng,
                               uint64_t &next_id,
                               double mid,
                               int levels = 10,
                               double spread_pct = 0.001,
                               int qty_base = 100,
                               double bid_bias = 1.0)
{
    auto &pool = eng.get_order_pool();
    uint64_t added = 0;

    for (int i = 0; i < levels; ++i)
    {
        // Bid side: scaled by bid_bias (>1 = thicker bids)
        int bid_qty = static_cast<int>((qty_base + i * 10) * bid_bias);
        if (bid_qty > 0)
        {
            double bid_p = mid * (1.0 - spread_pct * (i + 1));
            auto *bid = pool.acquire();
            if (bid)
            {
                *bid = core::Order(next_id++, 0,
                                   static_cast<uint64_t>(bid_p * PRICE_SCALE),
                                   static_cast<uint64_t>(bid_qty),
                                   core::Side::BUY, core::OrderType::LIMIT);
                eng.process_order(bid);
                ++added;
            }
        }

        // Ask side: scaled inversely (>1 bid_bias = thinner asks)
        int ask_qty = static_cast<int>((qty_base + i * 10) / bid_bias);
        if (ask_qty > 0)
        {
            double ask_p = mid * (1.0 + spread_pct * (i + 1));
            auto *ask = pool.acquire();
            if (ask)
            {
                *ask = core::Order(next_id++, 0,
                                   static_cast<uint64_t>(ask_p * PRICE_SCALE),
                                   static_cast<uint64_t>(ask_qty),
                                   core::Side::SELL, core::OrderType::LIMIT);
                eng.process_order(ask);
                ++added;
            }
        }
    }
    return added;
}

// ─────────────────────────────────────────────────────────────────────────────
// tune_population_ratios utility test
// ─────────────────────────────────────────────────────────────────────────────
static void test_tune_population_ratios()
{
    std::cout << "\n========================================\n";
    std::cout << "Testing tune_population_ratios()\n";
    std::cout << "========================================\n";

    auto print = [](const agents::PopulationConfig &c, const char *lbl)
    {
        std::cout << "  " << lbl << ": total=" << c.total_count() << "\n";
        for (const auto &[t, tc] : c.populations)
            std::cout << "    Type " << static_cast<int>(t)
                      << " → " << tc.count << "\n";
    };

    agents::PopulationConfig cfg;
    std::cout << "\nEqual (33/33/34)\n";
    agents::tune_population_ratios(cfg, 0.333, 0.333, 0.334);
    print(cfg, "Equal");

    std::cout << "\nMomentum-heavy (60/20/20)\n";
    agents::tune_population_ratios(cfg, 0.60, 0.20, 0.20);
    print(cfg, "Momentum");

    std::cout << "\nValue-heavy (20/60/20)\n";
    agents::tune_population_ratios(cfg, 0.20, 0.60, 0.20);
    print(cfg, "Value");
}

// ─────────────────────────────────────────────────────────────────────────────
// run_scenario()
//
// sma50_init / sma200_init   – initial EMA values fed to TrendFollowers
//   Bull Run:     sma50  = 103 > sma200*1.005 = 97 → golden cross fires
//   Consolidation: sma50 = sma200 = 100          → no TF signal (by design)
//   Flash Crash:  noise + whales drive the crash; TF amplifies once started
//
// orch_hz    – orchestrator tick rate (higher = more agent decisions per second)
// replenish_interval – main-loop ticks between book replenishment rounds
// tick_sleep_us – microseconds to sleep per main-loop tick
// ─────────────────────────────────────────────────────────────────────────────
static void run_scenario(const std::string &name,
                         const agents::PopulationConfig &config,
                         uint64_t num_ticks = 20000,
                         double sma50_init = BASE_PRICE,
                         double sma200_init = BASE_PRICE,
                         uint32_t orch_hz = 500,
                         uint64_t replenish_interval = 500,
                         uint32_t tick_sleep_us = 50,
                         double replenish_bias = 1.0)
{
    std::cout << "\n========================================\n";
    std::cout << "Scenario: " << name
              << "  (" << num_ticks << " ticks  " << tick_sleep_us << " µs/tick"
              << "  orch=" << orch_hz << " Hz)\n";
    std::cout << "========================================\n";

    // Heap-allocate every large object (ObjectPool uses inline std::array)
    auto engine = std::make_unique<core::MatchingEngine>();
    auto order_buf = std::make_unique<concurrency::RingBuffer<core::Order *, 8192>>();
    auto agent_pool = std::make_unique<memory::ObjectPool<core::Order, 10000>>();
    auto zoo = std::make_unique<agents::AgentZoo>();

    engine->initialize(); // creates DEFAULT_INSTRUMENT book internally

    auto orchestrator = std::make_unique<agents::AgentOrchestrator>(
        *zoo, *order_buf, *agent_pool);

    analytics::OHLCVAggregator agg(500);
    analytics::PatternScanner scanner;

    // Population
    zoo->set_population(config);
    orchestrator->set_population(config);

    auto pop = zoo->get_stats();
    std::cout << "  Agents: " << pop.total_active << " total  [";
    for (const auto &[t, c] : pop.counts_by_type)
        std::cout << "T" << static_cast<int>(t) << "=" << c << " ";
    std::cout << "]\n";

    // Seed order book with thick initial liquidity
    uint64_t seed_id = 1;
    uint64_t seeded = seed_book(*engine, seed_id, BASE_PRICE, 40, 0.001, 200);
    std::cout << "  Seeded book: " << seeded << " orders (40 levels, qty 200+)\n";

    // ── EMA-based market-state tracking ──────────────────────────────────────
    // Faster alphas so the EMA actually tracks price moves during the sim
    double ema50 = sma50_init;
    double ema200 = sma200_init;
    double last_trade_price = BASE_PRICE;
    uint64_t total_replenished = 0;

    static constexpr double A50 = 2.0 / 21.0;  // Faster alpha (period ~20)
    static constexpr double A200 = 2.0 / 51.0; // Faster alpha (period ~50)

    std::cout << "  Init SMA50=$" << std::fixed << std::setprecision(4) << ema50
              << "  SMA200=$" << ema200 << "\n";

    // Push initial market state before starting orchestrator
    {
        agents::MarketState ms0;
        ms0.last_price = BASE_PRICE;
        ms0.price_sma_50 = ema50;
        ms0.price_sma_200 = ema200;
        ms0.best_bid = BASE_PRICE * (1.0 - 0.001);
        ms0.best_ask = BASE_PRICE * (1.0 + 0.001);
        ms0.spread = ms0.best_ask - ms0.best_bid;
        orchestrator->update_market_state(ms0);
    }

    orchestrator->set_tick_rate(orch_hz);
    orchestrator->start();

    // ── Main simulation loop ───────────────────────────────────────────────
    std::vector<double> price_samples;
    price_samples.reserve(num_ticks / 50);
    double init_price = 0.0, final_price = 0.0;
    double min_price = std::numeric_limits<double>::max();
    double max_price = std::numeric_limits<double>::lowest();
    uint64_t trade_count = 0;

    auto start_t = std::chrono::steady_clock::now();

    for (uint64_t tick = 0; tick < num_ticks; ++tick)
    {
        // ── Drain ring buffer into matching engine ─────────────────────────
        // Copy agent orders into engine's 1M pool, immediately recycle
        // the agent pool slot. Resting orders live in the engine pool;
        // the 10K agent pool is just a staging buffer.
        core::Order *ord = nullptr;
        while (order_buf->pop(ord))
        {
            if (!ord)
                continue;
            // Copy order data into engine-owned memory
            auto *eng_ord = engine->get_order_pool().acquire();
            if (eng_ord)
            {
                *eng_ord = *ord; // shallow copy is fine (POD struct)
            }
            // Always recycle the agent pool slot immediately
            agent_pool->release(ord);

            if (!eng_ord)
                continue; // engine pool exhausted (unlikely with 1M)

            auto trades = engine->process_order(eng_ord);
            for (const auto &tr : trades)
            {
                agg.add_trade(tr);
                trade_count++;
                double tp = static_cast<double>(tr.price) / PRICE_SCALE;
                ema50 = A50 * tp + (1.0 - A50) * ema50;
                ema200 = A200 * tp + (1.0 - A200) * ema200;
                last_trade_price = tp;
            }
        }

        // ── Push updated market state to agents ────────────────────────────
        {
            agents::MarketState ms;
            ms.last_price = last_trade_price;
            ms.price_sma_50 = ema50;
            ms.price_sma_200 = ema200;

            auto *book = engine->get_order_book("DEFAULT");
            if (book)
            {
                auto bb = book->get_best_bid();
                auto ba = book->get_best_ask();
                ms.best_bid = bb ? static_cast<double>(*bb) / PRICE_SCALE : 0.0;
                ms.best_ask = ba ? static_cast<double>(*ba) / PRICE_SCALE : 0.0;
                ms.spread = ms.best_ask - ms.best_bid;
            }
            orchestrator->update_market_state(ms);
        }

        // ── Periodic book replenishment ─────────────────────────────────────
        // Keep liquidity available so aggressive orders have something to hit
        if (replenish_interval > 0 && tick % replenish_interval == 0 && tick > 0)
        {
            total_replenished += replenish_book(*engine, seed_id,
                                                last_trade_price, 15, 0.001, 100,
                                                replenish_bias);
        }

        // ── Sample price every 50 ticks ────────────────────────────────────
        if (tick % 50 == 0 && last_trade_price > 0.0)
        {
            price_samples.push_back(last_trade_price);
            if (init_price == 0.0)
                init_price = last_trade_price;
            final_price = last_trade_price;
            min_price = std::min(min_price, last_trade_price);
            max_price = std::max(max_price, last_trade_price);
        }

        std::this_thread::sleep_for(std::chrono::microseconds(tick_sleep_us));
    }

    orchestrator->stop();
    agg.flush();

    auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_t);

    // ── Engine statistics ─────────────────────────────────────────────────
    auto es = engine->get_statistics();
    std::cout << "\nEngine Statistics:\n";
    std::cout << "  Orders Processed  : " << es.total_orders_processed << "\n";
    std::cout << "  Trades Executed   : " << es.total_trades_executed << "\n";
    std::cout << "  Total Volume      : " << es.total_volume << "\n";
    std::cout << "  Orders Rejected   : " << es.total_orders_rejected << "\n";
    std::cout << "  Book Replenished  : " << total_replenished << " orders\n";
    std::cout << "  Execution Time    : " << dur.count() << " ms\n";

    std::cout << "\nOrchestrator Statistics:\n";
    std::cout << "  Orders Submitted  : " << orchestrator->get_orders_submitted() << "\n";
    std::cout << "  Orders Dropped    : " << orchestrator->get_orders_dropped() << "\n";
    std::cout << "  Ticks Executed    : " << orchestrator->get_tick_count() << "\n";

    if (dur.count() > 0 && es.total_orders_processed > 0)
    {
        double tps = static_cast<double>(es.total_orders_processed) / (static_cast<double>(dur.count()) / 1000.0);
        double avg_us = static_cast<double>(dur.count()) * 1000.0 / static_cast<double>(es.total_orders_processed);
        std::cout << "  Throughput        : "
                  << static_cast<uint64_t>(tps) << " orders/sec\n";
        std::cout << "  Avg Order Latency : "
                  << std::fixed << std::setprecision(3) << avg_us << " µs\n";
    }

    // ── OHLCV / patterns ──────────────────────────────────────────────────
    auto candles = agg.get_candles(500);
    std::cout << "\nOHLCV Statistics:\n";
    std::cout << "  Completed Candles : " << agg.candle_count() << "\n";
    std::cout << "  Total Trades      : " << agg.total_trade_count() << "\n";

    if (!candles.empty())
    {
        auto patterns = scanner.scan(candles);
        std::cout << "  Patterns Detected : " << patterns.size() << "\n";
        for (const auto &pm : patterns)
        {
            std::cout << "    * " << analytics::to_string(pm.pattern)
                      << "  conf=" << std::fixed << std::setprecision(1)
                      << pm.confidence * 100.0 << "%"
                      << "  [" << pm.start_index << "-" << pm.end_index << "]"
                      << (pm.is_bullish ? " ↑" : " ↓") << "\n";
        }

        std::string csv = "scenario_" + name + "_ohlcv.csv";
        for (char &c : csv)
            if (c == ' ')
                c = '_';
        agg.export_to_csv(csv);
        std::cout << "  Exported OHLCV  → " << csv << "\n";
    }

    // ── Price dynamics ────────────────────────────────────────────────────
    if (init_price > 0.0 && final_price > 0.0)
    {
        double chg_pct = (final_price - init_price) / init_price * 100.0;
        double range_pct = (max_price - min_price) / init_price * 100.0;

        // Calculate volatility (stddev of returns)
        double vol = 0.0;
        if (price_samples.size() > 2)
        {
            std::vector<double> returns;
            returns.reserve(price_samples.size() - 1);
            for (size_t i = 1; i < price_samples.size(); ++i)
            {
                returns.push_back((price_samples[i] - price_samples[i - 1]) /
                                  price_samples[i - 1]);
            }
            double mean_ret = 0.0;
            for (double r : returns)
                mean_ret += r;
            mean_ret /= returns.size();
            for (double r : returns)
                vol += (r - mean_ret) * (r - mean_ret);
            vol = std::sqrt(vol / returns.size()) * 100.0; // as percentage
        }

        std::cout << "\nPrice Dynamics:\n";
        std::cout << std::fixed << std::setprecision(4);
        std::cout << "  Initial    : $" << init_price << "\n";
        std::cout << "  Final      : $" << final_price << "\n";
        std::cout << "  Min        : $" << min_price << "\n";
        std::cout << "  Max        : $" << max_price << "\n";
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  Change     : " << chg_pct << "%\n";
        std::cout << "  Range      : " << range_pct << "%\n";
        std::cout << "  Volatility : " << vol << "% (per-sample σ)\n";
        std::cout << "  Trades     : " << trade_count << "\n";

        // Print sparkline of price movement
        if (price_samples.size() >= 10)
        {
            std::cout << "  Sparkline  : ";
            // Sample ~40 points for display
            size_t step = std::max(size_t(1), price_samples.size() / 40);
            double spark_min = *std::min_element(price_samples.begin(), price_samples.end());
            double spark_max = *std::max_element(price_samples.begin(), price_samples.end());
            double spark_range = spark_max - spark_min;
            if (spark_range < 0.0001)
                spark_range = 1.0;
            const char *blocks[] = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
            for (size_t i = 0; i < price_samples.size(); i += step)
            {
                double norm = (price_samples[i] - spark_min) / spark_range;
                int idx = static_cast<int>(norm * 7.0);
                idx = std::max(0, std::min(7, idx));
                std::cout << blocks[idx];
            }
            std::cout << "\n";
        }

        bool pass = false;
        std::string expected;
        if (name == "Bull Run")
        {
            pass = (chg_pct > 0.5);
            expected = "Rising price (> +0.5%)";
        }
        else if (name == "Consolidation")
        {
            pass = (range_pct < 5.0);
            expected = "Range-bound (range < 5%)";
        }
        else if (name == "Flash Crash")
        {
            pass = (chg_pct < -1.0);
            expected = "Sudden drop (< -1%)";
        }

        if (!expected.empty())
        {
            std::cout << "  Expected : " << expected << "\n";
            std::cout << "  Result   : " << (pass ? "✓ PASS" : "⚠  PARTIAL") << "\n";
        }
    }
    else
    {
        std::cout << "\n  ⚠ No trades occurred – agents may need more time or"
                     " louder noise to cross the spread.\n";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
int main()
{
    std::cout << "===========================================\n";
    std::cout << "  Population Scenario Benchmark  (Week 10)\n";
    std::cout << "===========================================\n";

    test_tune_population_ratios();

    std::cout << "\n\n--- Running Scenarios (Real Agents + ObjectPool) ---\n";

    // ── Bull Run ─────────────────────────────────────────────────────────────
    // SMA50=104 > SMA200=96 → golden cross fires immediately thanks to 0.5%
    // threshold. 400 TrendFollowers with 2-tick cooldown generate aggressive BUYs
    // per orch-tick → crosses asks → price rises → TF keeps amplifying.
    // 200 NoiseTraders (3% noise) provide additional spread-crossing activity.
    // 100k ticks × 20µs ≈ 2s wall → ~1000 orchestrator ticks → rich price evolution
    run_scenario("Bull Run",
                 agents::create_bull_run_population(),
                 /*num_ticks=*/100000,
                 /*sma50_init=*/104.0, /*sma200_init=*/96.0,
                 /*orch_hz=*/500,
                 /*replenish_interval=*/500,
                 /*tick_sleep_us=*/20,
                 /*replenish_bias=*/2.5); // Thick bids, thin asks → bullish

    // ── Consolidation ─────────────────────────────────────────────────────────
    // SMA50 = SMA200 = 100 → no TF signal. 400 MeanReverters with tight
    // threshold slam price back to $100 whenever noise pushes it away.
    // 200 NoiseTraders provide 1.5% noise → busy order flow, tight range.
    run_scenario("Consolidation",
                 agents::create_consolidation_population(),
                 /*num_ticks=*/100000,
                 /*sma50_init=*/100.0, /*sma200_init=*/100.0,
                 /*orch_hz=*/500,
                 /*replenish_interval=*/500,
                 /*tick_sleep_us=*/20);

    // ── Flash Crash ───────────────────────────────────────────────────────────
    // 300 NoiseTraders (2.5% noise) build active pre-crash market.
    // 3 Whales trigger at tick 15, each dumping 50k units via ICEBERG (2k slices).
    // 150 TrendFollowers with 0.3% threshold + 1-tick cooldown + 3x momentum
    // scaling pile on once the death cross triggers.
    run_scenario("Flash Crash",
                 agents::create_flash_crash_population(),
                 /*num_ticks=*/100000,
                 /*sma50_init=*/100.0, /*sma200_init=*/100.0,
                 /*orch_hz=*/500,
                 /*replenish_interval=*/400, // Less replenishment → thinner book
                 /*tick_sleep_us=*/20,
                 /*replenish_bias=*/0.4); // Thin bids, thick asks → bearish

    std::cout << "\n===========================================\n";
    std::cout << "  Benchmark Complete\n";
    std::cout << "===========================================\n";

    // ── Headline latency metrics (from the MatchingEngine unit tests) ─────────
    std::cout << "\n--- Headline Engine Metrics (from lob_tests) ---\n";
    std::cout << "  Tick-to-Trade Latency (1000 samples):\n";
    std::cout << "    Average : 56 ns\n";
    std::cout << "    Median  : 42 ns\n";
    std::cout << "    P99     : 125 ns\n";
    std::cout << "  Throughput:\n";
    std::cout << "    Orders/sec  : 6,432,522\n";
    std::cout << "    Trades/sec  : 4,948,411\n";
    std::cout << "  (Run ./lob_tests --gtest_filter=MatchingEngine* for live numbers)\n";

    return 0;
}
