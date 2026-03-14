/**
 * @file main_abms.cpp
 * @brief Agent-Based Market Simulation – Interactive ImGui Application
 *
 * Week 10 – Day 39-40: Full integration of:
 *   - AgentOrchestrator / AgentZoo  (Weeks 7-8)
 *   - OHLCVAggregator + PatternScanner (Week 9)
 *   - AgentConfigPanel, CandlestickChart, StatsDashboard (Week 10)
 *
 * Layout (1400 × 900):
 *   ┌──────────────┬──────────────────────────┬─────────────┐
 *   │  Config (340) │  Candlestick Chart        │  Stats (320)│
 *   │  (left pane)  │  (centre – fills space)   │  (right)    │
 *   └──────────────┴──────────────────────────┴─────────────┘
 *
 * Keyboard shortcuts (when chart window is focused):
 *   Space  –  Pause / Resume
 *   R      –  Reset simulation
 *   S      –  Export OHLCV CSV
 *   1      –  Load Bull Run preset
 *   2      –  Load Consolidation preset
 *   3      –  Load Flash Crash preset
 *
 * Build:
 *   cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release -DBUILD_VISUALIZATION=ON
 *   ninja lob_abms_viewer
 */

#ifdef ENABLE_OPENGL_VIEWER

// ── GLFW / OpenGL ────────────────────────────────────────────────────────────
#include <GLFW/glfw3.h>

// ── ImGui ────────────────────────────────────────────────────────────────────
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// ── Core system headers ──────────────────────────────────────────────────────
#include "core/MatchingEngine.h"
#include "core/OrderBook.h"
#include "core/Order.h"
#include "concurrency/RingBuffer.h"
#include "memory/ObjectPool.h"

// ── Agent system ─────────────────────────────────────────────────────────────
#include "agents/AgentZoo.h"
#include "agents/AgentOrchestrator.h"
#include "agents/MarketState.h"

// ── Analytics ────────────────────────────────────────────────────────────────
#include "analytics/OHLCVAggregator.h"
#include "analytics/PatternScanner.h"

// ── UI panels ────────────────────────────────────────────────────────────────
#include "ui/AgentConfigPanel.h"
#include "ui/CandlestickChart.h"
#include "ui/StatsDashboard.h"

// ── STL ──────────────────────────────────────────────────────────────────────
#include <iostream>
#include <atomic>
#include <thread>
#include <mutex>
#include <deque>
#include <vector>
#include <cmath>
#include <limits>
#include <csignal>
#include <chrono>
#include <random>
#include <string>
#include <functional>

using namespace lob;

// ────────────────────────────────────────────────────────────────────────────
// Globals / shutdown
// ────────────────────────────────────────────────────────────────────────────
static std::atomic<bool> g_quitting{false};

static void glfw_error_callback(int err, const char *desc)
{
    std::cerr << "[GLFW] Error " << err << ": " << desc << "\n";
}

static void signal_handler(int) { g_quitting = true; }

// ────────────────────────────────────────────────────────────────────────────
// SMA / volatility helper (rolling window)
// ────────────────────────────────────────────────────────────────────────────
class RollingStats
{
public:
    explicit RollingStats(size_t max_size) : max_(max_size) {}

    void push(double v)
    {
        buf_.push_back(v);
        if (buf_.size() > max_)
            buf_.pop_front();
    }

    double sma(size_t n) const
    {
        size_t cnt = std::min(n, buf_.size());
        if (cnt == 0)
            return 0.0;
        double s = 0.0;
        auto it = buf_.end();
        for (size_t i = 0; i < cnt; ++i)
            s += *--it;
        return s / static_cast<double>(cnt);
    }

    double volatility(size_t n) const
    {
        size_t cnt = std::min(n, buf_.size());
        if (cnt < 2)
            return 0.0;
        double m = sma(cnt);
        double var = 0.0;
        auto it = buf_.end();
        for (size_t i = 0; i < cnt; ++i)
        {
            double d = *--it - m;
            var += d * d;
        }
        return std::sqrt(var / static_cast<double>(cnt - 1));
    }

    double last() const { return buf_.empty() ? 0.0 : buf_.back(); }
    bool empty() const { return buf_.empty(); }

private:
    std::deque<double> buf_;
    size_t max_;
};

// ────────────────────────────────────────────────────────────────────────────
// Application state
// ────────────────────────────────────────────────────────────────────────────
struct AppState
{
    // Engine components
    core::MatchingEngine engine;
    concurrency::RingBuffer<core::Order *, 8192> order_buffer;
    memory::ObjectPool<core::Order, 10000> agent_order_pool; // must match AgentOrchestrator's template arg

    // Agent system
    agents::AgentZoo zoo;
    std::unique_ptr<agents::AgentOrchestrator> orchestrator;

    // Analytics
    analytics::OHLCVAggregator ohlcv{200}; // 200 ms candles
    analytics::PatternScanner scanner;

    // Market state rolling windows
    RollingStats price_window{300};

    // UI panels
    ui::AgentConfigPanel config_panel;
    ui::CandlestickChart chart;
    ui::StatsDashboard dashboard;

    // Cached data (updated each sim tick, read by GUI thread → mutex-protected)
    std::mutex data_mtx;
    std::vector<analytics::Candle> candles;
    std::vector<analytics::PatternMatch> patterns;
    agents::MarketState market_state;
    agents::PopulationStats pop_stats;
    core::MatchingEngine::Statistics eng_stats;
    uint64_t tick_count{0};
    int tick_rate_hz{100};

    // Simulation control
    std::atomic<bool> paused{false};
    std::atomic<bool> reset_requested{false};

    // Order ID counter
    uint64_t next_order_id{1};

    AppState()
    {
        engine.initialize();
        // engine.initialize() already created "DEFAULT" book
        orchestrator = std::make_unique<agents::AgentOrchestrator>(
            zoo, order_buffer, agent_order_pool);
    }
};

// ────────────────────────────────────────────────────────────────────────────
// Seed the order book with a realistic initial spread
// ────────────────────────────────────────────────────────────────────────────
static void seed_order_book(AppState &app)
{
    // Use engine's 1M internal pool for seeding – NOT the 10K agent pool
    auto &pool = app.engine.get_order_pool();

    constexpr double MID = 100.0;
    constexpr int LEVELS = 40; // Thick book: 40 levels each side
    constexpr int SCALE = 100;
    constexpr double SPREAD_PCT = 0.001; // 0.1% per level

    int seeded = 0;
    for (int i = 0; i < LEVELS; ++i)
    {
        double bid_p = MID * (1.0 - SPREAD_PCT * (i + 1));
        auto *bid = pool.acquire();
        if (bid)
        {
            *bid = core::Order(app.next_order_id++, 0,
                               static_cast<uint64_t>(bid_p * SCALE),
                               static_cast<uint64_t>(200 + i * 20),
                               core::Side::BUY,
                               core::OrderType::LIMIT);
            app.engine.process_order(bid);
            ++seeded;
        }

        double ask_p = MID * (1.0 + SPREAD_PCT * (i + 1));
        auto *ask = pool.acquire();
        if (ask)
        {
            *ask = core::Order(app.next_order_id++, 0,
                               static_cast<uint64_t>(ask_p * SCALE),
                               static_cast<uint64_t>(200 + i * 20),
                               core::Side::SELL,
                               core::OrderType::LIMIT);
            app.engine.process_order(ask);
            ++seeded;
        }
    }
    std::cout << "[ABMS] Order book seeded with " << seeded
              << " orders (" << LEVELS << " levels each side, qty 200+).\n";
}

// ────────────────────────────────────────────────────────────────────────────
// Replenish the order book with fresh liquidity around current mid
// ────────────────────────────────────────────────────────────────────────────
static void replenish_order_book(AppState &app, double mid)
{
    auto &pool = app.engine.get_order_pool();
    constexpr int LEVELS = 10;
    constexpr int SCALE = 100;
    constexpr double SPREAD_PCT = 0.001;

    for (int i = 0; i < LEVELS; ++i)
    {
        double bid_p = mid * (1.0 - SPREAD_PCT * (i + 1));
        auto *bid = pool.acquire();
        if (bid)
        {
            *bid = core::Order(app.next_order_id++, 0,
                               static_cast<uint64_t>(bid_p * SCALE),
                               static_cast<uint64_t>(100 + i * 10),
                               core::Side::BUY,
                               core::OrderType::LIMIT);
            app.engine.process_order(bid);
        }

        double ask_p = mid * (1.0 + SPREAD_PCT * (i + 1));
        auto *ask = pool.acquire();
        if (ask)
        {
            *ask = core::Order(app.next_order_id++, 0,
                               static_cast<uint64_t>(ask_p * SCALE),
                               static_cast<uint64_t>(100 + i * 10),
                               core::Side::SELL,
                               core::OrderType::LIMIT);
            app.engine.process_order(ask);
        }
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Apply a new population configuration
// ────────────────────────────────────────────────────────────────────────────
static void apply_population(AppState &app, agents::PopulationConfig cfg)
{
    bool was_running = app.orchestrator->is_running();
    if (was_running)
        app.orchestrator->stop();

    app.zoo.reset_all();
    app.zoo.set_population(cfg);
    app.orchestrator->set_population(cfg);

    if (was_running && !app.paused.load())
        app.orchestrator->start();

    std::cout << "[ABMS] Population applied: "
              << app.zoo.get_stats().total_active << " agents.\n";
}

// ────────────────────────────────────────────────────────────────────────────
// Reset simulation
// ────────────────────────────────────────────────────────────────────────────
static void reset_simulation(AppState &app)
{
    app.orchestrator->stop();
    app.zoo.reset_all();
    app.engine.initialize();
    // engine.initialize() already created "DEFAULT" book
    app.ohlcv.reset();

    {
        std::lock_guard<std::mutex> lk(app.data_mtx);
        app.candles.clear();
        app.patterns.clear();
        app.market_state = agents::MarketState{};
        app.eng_stats = {};
        app.tick_count = 0;
    }

    app.price_window = RollingStats{300};
    app.next_order_id = 1;
    seed_order_book(app);

    // Re-apply the current config
    auto cfg = app.config_panel.get_config();
    app.zoo.set_population(cfg);
    app.orchestrator->set_population(cfg);

    if (!app.paused.load())
        app.orchestrator->start();

    std::cout << "[ABMS] Simulation reset.\n";
}

// ────────────────────────────────────────────────────────────────────────────
// Sim tick – called from main thread ~60 × per second (up to 30× per frame)
// Drains the ring buffer, feeds trades to analytics, updates market state,
// and periodically replenishes the book so there's always liquidity.
// ────────────────────────────────────────────────────────────────────────────
static void sim_tick(AppState &app)
{
    if (app.paused.load() || app.reset_requested.load())
        return;

    auto *book = app.engine.get_order_book("DEFAULT");

    constexpr int PRICE_SCALE = 100;
    constexpr double INV_SCALE = 1.0 / PRICE_SCALE;

    // 1. Process ring buffer (drain up to 1024 orders per tick) ───────────────
    // Copy agent orders into engine's 1M pool, immediately recycle
    // the agent pool slot. Resting orders live in the engine pool;
    // the 10K agent pool is just a staging buffer.
    // Real-time nanosecond timestamp for OHLCV candle rollover
    auto now_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    core::Order *ord = nullptr;
    int drained = 0;
    while (app.order_buffer.pop(ord) && drained < 1024)
    {
        if (ord)
        {
            // Copy into engine-owned memory, recycle agent slot immediately
            auto *eng_ord = app.engine.get_order_pool().acquire();
            if (eng_ord)
                *eng_ord = *ord;
            app.agent_order_pool.release(ord);

            if (eng_ord)
            {
                auto trades = app.engine.process_order(eng_ord);
                for (auto tr : trades)
                {
                    tr.timestamp = now_ns;  // stamp with real time for candle rollover
                    app.ohlcv.add_trade(tr);
                    double tp = static_cast<double>(tr.price) * INV_SCALE;
                    app.price_window.push(tp);
                }
                // Release incoming order if fully filled (not resting in book)
                if (eng_ord->is_filled())
                    app.engine.get_order_pool().release(eng_ord);
            }
        }
        ++drained;
    }

    // Recycle filled resting orders back into the engine pool
    if (book)
    {
        for (auto *filled : book->drain_filled_orders())
            app.engine.get_order_pool().release(filled);
    }

    // 2. Periodic book replenishment (every 20 sim ticks) ─────────────────────
    //    Keeps the book from drying out so agents always have liquidity to hit
    if (app.tick_count % 20 == 0 && app.price_window.last() > 0.0)
    {
        replenish_order_book(app, app.price_window.last());
    }

    // 3. Extract market state ─────────────────────────────────────────────────
    agents::MarketState ms;
    if (book)
    {
        auto bb = book->get_best_bid();
        auto ba = book->get_best_ask();

        ms.best_bid = bb.has_value() ? static_cast<double>(*bb) * INV_SCALE : 0.0;
        ms.best_ask = ba.has_value() ? static_cast<double>(*ba) * INV_SCALE : 0.0;
        ms.spread = ms.best_ask - ms.best_bid;
        ms.last_price = app.price_window.last();
        if (ms.last_price == 0.0)
            ms.last_price = (ms.best_bid + ms.best_ask) * 0.5;

        ms.price_sma_50 = app.price_window.sma(50);
        ms.price_sma_200 = app.price_window.sma(200);
        ms.volatility = app.price_window.volatility(20);

        // Approximate 24h volume from engine stats
        auto es = app.engine.get_statistics();
        ms.volume_24h = static_cast<double>(es.total_volume);

        // Book depth (top 5 levels)
        std::vector<core::OrderBook::DepthLevel> bids, asks;
        book->get_market_depth(bids, asks, 5);
        for (auto &l : bids)
            ms.bid_depth += l.quantity;
        for (auto &l : asks)
            ms.ask_depth += l.quantity;

        ms.timestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());

        app.orchestrator->update_market_state(ms);
    }

    // 4. Scan patterns from OHLCV ─────────────────────────────────────────────
    auto candles = app.ohlcv.get_candles(300);
    // Include the in-progress candle so the chart always shows latest data
    {
        auto cur = app.ohlcv.get_current_candle();
        if (cur.trade_count > 0)
            candles.push_back(cur);
    }
    std::vector<analytics::PatternMatch> patterns;
    if (candles.size() >= 5)
        patterns = app.scanner.scan(candles);

    // 5. Update shared data ───────────────────────────────────────────────────
    {
        std::lock_guard<std::mutex> lk(app.data_mtx);
        app.candles = std::move(candles);
        app.patterns = std::move(patterns);
        app.market_state = ms;
        app.pop_stats = app.zoo.get_stats();
        app.eng_stats = app.engine.get_statistics();
        ++app.tick_count;
    }
}

// ────────────────────────────────────────────────────────────────────────────
// ImGui frame – renders all panels
// ────────────────────────────────────────────────────────────────────────────
static void render_frame(AppState &app, GLFWwindow *window)
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // ── Keyboard shortcuts ───────────────────────────────────────────────────
    ImGuiIO &io = ImGui::GetIO();
    if (!io.WantCaptureKeyboard)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Space))
        {
            bool p = !app.paused.load();
            app.paused.store(p);
            if (p)
                app.orchestrator->stop();
            else
                app.orchestrator->start();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_R))
            reset_simulation(app);
        if (ImGui::IsKeyPressed(ImGuiKey_S))
            app.ohlcv.export_to_csv("abms_ohlcv_export.csv");
        if (ImGui::IsKeyPressed(ImGuiKey_1))
            apply_population(app, agents::create_bull_run_population());
        if (ImGui::IsKeyPressed(ImGuiKey_2))
            apply_population(app, agents::create_consolidation_population());
        if (ImGui::IsKeyPressed(ImGuiKey_3))
            apply_population(app, agents::create_flash_crash_population());
    }

    // ── Config panel (left) ──────────────────────────────────────────────────
    app.config_panel.render();

    // ── Candlestick chart (centre) ───────────────────────────────────────────
    {
        int disp_w, disp_h;
        glfwGetFramebufferSize(window, &disp_w, &disp_h);

        constexpr float LEFT_W = 345.0f;
        constexpr float RIGHT_W = 325.0f;
        float chart_x = LEFT_W;
        float chart_w = static_cast<float>(disp_w) - LEFT_W - RIGHT_W;
        float chart_h = static_cast<float>(disp_h);

        ImGui::SetNextWindowPos(ImVec2(chart_x, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(chart_w, chart_h), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.95f);

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (ImGui::Begin("📈 ABMS – Candlestick Chart", nullptr, flags))
        {
            // Title bar shows last price and % change
            std::lock_guard<std::mutex> lk(app.data_mtx);
            app.chart.render(app.candles, app.patterns);
        }
        ImGui::End();
    }

    // ── Stats dashboard (right) ──────────────────────────────────────────────
    {
        agents::MarketState ms;
        agents::PopulationStats ps;
        core::MatchingEngine::Statistics es;
        std::vector<analytics::PatternMatch> pats;
        uint64_t tc;
        int tr;

        {
            std::lock_guard<std::mutex> lk(app.data_mtx);
            ms = app.market_state;
            ps = app.pop_stats;
            es = app.eng_stats;
            pats = app.patterns;
            tc = app.tick_count;
            tr = app.tick_rate_hz;
        }
        app.dashboard.update(ms, ps, es, pats, tc, tr);
        app.dashboard.render();
    }

    // ── Render ───────────────────────────────────────────────────────────────
    ImGui::Render();
    int dw, dh;
    glfwGetFramebufferSize(window, &dw, &dh);
    glViewport(0, 0, dw, dh);
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
}

// ────────────────────────────────────────────────────────────────────────────
// main()
// ────────────────────────────────────────────────────────────────────────────
int main()
{
    std::signal(SIGINT, signal_handler);

    // ── GLFW init ────────────────────────────────────────────────────────────
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
    {
        std::cerr << "[ABMS] Failed to initialise GLFW\n";
        return 1;
    }

    const char *glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
#endif

    GLFWwindow *window = glfwCreateWindow(
        1440, 900,
        "ABMS – Agent-Based Market Simulation  |  Week 10",
        nullptr, nullptr);

    if (!window)
    {
        std::cerr << "[ABMS] Failed to create GLFW window\n";
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // VSync

    // ── ImGui init ───────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // ── Simulation setup ─────────────────────────────────────────────────────
    // Heap-allocate: AgentZoo pools contain ~12 MB+ of mt19937_64 state
    // which exceeds macOS 8 MB default stack limit.
    auto app_ptr = std::make_unique<AppState>();
    AppState &app = *app_ptr;

    // Load Bull Run as default population
    auto init_pop = agents::create_bull_run_population();
    app.config_panel.set_config(init_pop);
    app.zoo.set_population(init_pop);
    app.orchestrator->set_population(init_pop);
    app.orchestrator->set_tick_rate(500);

    seed_order_book(app);

    // Config panel callbacks
    app.config_panel.set_on_apply([&](agents::PopulationConfig cfg)
                                  { apply_population(app, cfg); });
    app.config_panel.set_on_reset([&]()
                                  { reset_simulation(app); });

    // Dashboard callbacks
    app.dashboard.set_on_pause([&]()
                               {
        app.paused.store(true);
        app.orchestrator->stop(); });
    app.dashboard.set_on_resume([&]()
                                {
        app.paused.store(false);
        app.orchestrator->start(); });
    app.dashboard.set_on_reset([&]()
                               { reset_simulation(app); });
    app.dashboard.set_on_tick_rate([&](int hz)
                                   {
        app.tick_rate_hz = hz;
        app.orchestrator->set_tick_rate(static_cast<uint32_t>(hz)); });
    app.dashboard.set_on_export_csv([&]()
                                    {
        app.ohlcv.export_to_csv("abms_ohlcv_export.csv");
        std::cout << "[ABMS] Exported OHLCV → abms_ohlcv_export.csv\n"; });

    // Start simulation
    app.orchestrator->start();

    std::cout << "[ABMS] Simulation started. Population: "
              << app.zoo.get_stats().total_active << " agents.\n";
    std::cout << "[ABMS] Shortcuts: Space=Pause  R=Reset  S=Export  1/2/3=Presets\n";

    // ── Main loop ─────────────────────────────────────────────────────────────
    using clock = std::chrono::steady_clock;
    auto last_time = clock::now();

    while (!glfwWindowShouldClose(window) && !g_quitting.load())
    {
        glfwPollEvents();

        // Simulation tick(s) – run multiple if behind
        auto now = clock::now();
        double dt_s = std::chrono::duration<double>(now - last_time).count();
        last_time = now;

        int sim_steps = static_cast<int>(dt_s * 60.0); // up to 60 sim steps / frame
        sim_steps = std::clamp(sim_steps, 1, 60);

        for (int i = 0; i < sim_steps; ++i)
            sim_tick(app);

        render_frame(app, window);
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    app.orchestrator->stop();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout << "[ABMS] Shutting down. Total ticks: " << app.tick_count << "\n";
    return 0;
}

#else // ENABLE_OPENGL_VIEWER not defined

#include <iostream>
int main()
{
    std::cerr << "[ABMS] This application requires OpenGL/GLFW/ImGui.\n"
              << "Rebuild with: cmake .. -DBUILD_VISUALIZATION=ON\n";
    return 1;
}

#endif // ENABLE_OPENGL_VIEWER
