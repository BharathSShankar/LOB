cmake_minimum_required(VERSION 3.20)
project(HighPerformanceLOB VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDEs
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
    
    # Check for ARM architecture (Apple Silicon M1/M2/M3)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        message(STATUS "Detected ARM64 architecture (Apple Silicon)")
        set(CMAKE_CXX_FLAGS_RELEASE "-O3 -mcpu=native -DNDEBUG")
        # ARM-specific cache line size (typically 128 bytes on M1)
        add_compile_definitions(ARM_CACHE_LINE_SIZE=128)
    else()
        # x86-64 architecture
        set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
    endif()
elseif(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Source files
set(CORE_SOURCES
    src/core/Order.cpp
    src/core/OrderBook.cpp
    src/core/MatchingEngine.cpp
)

set(MEMORY_SOURCES
    src/memory/ObjectPool.cpp
)

set(CONCURRENCY_SOURCES
    src/concurrency/RingBuffer.cpp
    src/concurrency/OrderEntryGateway.cpp
)

set(MARKET_DATA_SOURCES
    src/market_data/MarketDataPublisher.cpp
)

set(PROFILING_SOURCES
    src/profiling/MemoryProfiler.cpp
    src/profiling/HotPathProfiler.cpp
)

set(AGENT_SOURCES
    src/agents/Agent.cpp
    src/agents/NoiseTrader.cpp
    src/agents/MarketMaker.cpp
    src/agents/TrendFollower.cpp
    src/agents/MeanReverter.cpp
    src/agents/Whale.cpp
    src/agents/AgentZoo.cpp
    src/agents/AgentOrchestrator.cpp
    src/agents/PopulationPresets.cpp
)

set(ANALYTICS_SOURCES
    src/analytics/OHLCVAggregator.cpp
    src/analytics/PatternScanner.cpp
)

# Week 10 UI sources (compiled only into the ImGui visualization build)
set(UI_SOURCES
    src/ui/AgentConfigPanel.cpp
    src/ui/CandlestickChart.cpp
    src/ui/StatsDashboard.cpp
)

# Library with all sources
add_library(lob_lib STATIC
    ${CORE_SOURCES}
    ${MEMORY_SOURCES}
    ${CONCURRENCY_SOURCES}
    ${MARKET_DATA_SOURCES}
    ${PROFILING_SOURCES}
    ${AGENT_SOURCES}
    ${ANALYTICS_SOURCES}
)

target_include_directories(lob_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Main executable
add_executable(lob_matching_engine src/main.cpp)
target_link_libraries(lob_matching_engine PRIVATE lob_lib)

# Enable testing with CTest
enable_testing()
include(CTest)

# Google Test
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Test files
set(TEST_SOURCES
    tests/core/OrderTest.cpp
    tests/core/OrderBookTest.cpp
    tests/core/MatchingEngineTest.cpp
    tests/memory/ObjectPoolTest.cpp
    tests/concurrency/RingBufferTest.cpp
    tests/agents/AgentTest.cpp
    tests/agents/NoiseTraderTest.cpp
    tests/agents/MarketMakerTest.cpp
    tests/agents/TrendFollowerTest.cpp
    tests/agents/MeanReverterTest.cpp
    tests/agents/WhaleTest.cpp
    tests/agents/AgentZooTest.cpp
    tests/agents/AgentOrchestratorTest.cpp
    tests/analytics/OHLCVAggregatorTest.cpp
    tests/analytics/PatternScannerTest.cpp
)

# Create test executable
add_executable(lob_tests ${TEST_SOURCES})
target_link_libraries(lob_tests PRIVATE lob_lib gtest gtest_main)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(lob_tests)

# Code Coverage (optional)
option(ENABLE_COVERAGE "Enable code coverage reporting" OFF)
if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        message(STATUS "Code coverage enabled (GCC)")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
        target_compile_options(lob_lib PRIVATE --coverage)
        target_link_options(lob_lib PRIVATE --coverage)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(STATUS "Code coverage enabled (Clang)")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-instr-generate -fcoverage-mapping")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-instr-generate")
        target_compile_options(lob_lib PRIVATE -fprofile-instr-generate -fcoverage-mapping)
        target_link_options(lob_lib PRIVATE -fprofile-instr-generate)
    else()
        message(WARNING "Code coverage only supported with GCC or Clang")
    endif()
endif()

# Profiling options
option(ENABLE_MEMORY_PROFILING "Enable memory profiling" OFF)
option(ENABLE_HOTPATH_PROFILING "Enable hot path profiling" OFF)
option(BUILD_PROFILING "Build with profiling support" OFF)

if(BUILD_PROFILING OR ENABLE_MEMORY_PROFILING OR ENABLE_HOTPATH_PROFILING)
    message(STATUS "Profiling enabled")
    if(ENABLE_MEMORY_PROFILING)
        target_compile_definitions(lob_lib PUBLIC ENABLE_MEMORY_PROFILING)
        message(STATUS "  - Memory profiling: ON")
    endif()
    if(ENABLE_HOTPATH_PROFILING)
        target_compile_definitions(lob_lib PUBLIC ENABLE_HOTPATH_PROFILING)
        message(STATUS "  - Hot path profiling: ON")
    endif()
endif()

# Google Benchmark (optional, for Week 6)
option(BUILD_BENCHMARK "Build benchmark suite" OFF)
if(BUILD_BENCHMARK)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.8.3
    )
    FetchContent_MakeAvailable(benchmark)
    
    add_executable(lob_benchmark benchmarks/MatchingEngineBenchmark.cpp)
    target_link_libraries(lob_benchmark PRIVATE lob_lib benchmark::benchmark)
    
    # Add profiling benchmark
    add_executable(lob_profiling_benchmark benchmarks/ProfilingBenchmark.cpp)
    target_link_libraries(lob_profiling_benchmark PRIVATE lob_lib benchmark::benchmark)
    target_compile_definitions(lob_profiling_benchmark PRIVATE ENABLE_MEMORY_PROFILING ENABLE_HOTPATH_PROFILING)
    
    # Add Tick-to-Trade latency benchmark (Week 6)
    add_executable(lob_tick_to_trade_benchmark benchmarks/TickToTradeBenchmark.cpp)
    target_link_libraries(lob_tick_to_trade_benchmark PRIVATE lob_lib benchmark::benchmark)
    target_include_directories(lob_tick_to_trade_benchmark PRIVATE ${CMAKE_SOURCE_DIR}/include)

    # Week 10: Scenario benchmark (population scenarios & OHLCV metrics)
    add_executable(lob_scenario_benchmark benchmarks/ScenarioBenchmark.cpp)
    target_link_libraries(lob_scenario_benchmark PRIVATE lob_lib)
    target_include_directories(lob_scenario_benchmark PRIVATE ${CMAKE_SOURCE_DIR}/include)
endif()

# Visualization (optional, requires GLFW and OpenGL)
option(BUILD_VISUALIZATION "Build visualization tools" OFF)
option(ENABLE_LIVE_WEBSOCKET "Enable live WebSocket streaming" OFF)

if(BUILD_VISUALIZATION)
    find_package(OpenGL REQUIRED)
    find_package(glfw3 3.3 QUIET)
    
    # Check for libwebsockets
    find_library(LIBWEBSOCKETS_LIB websockets)
    find_path(LIBWEBSOCKETS_INCLUDE libwebsockets.h PATH_SUFFIXES include)
    
    if(LIBWEBSOCKETS_LIB AND LIBWEBSOCKETS_INCLUDE)
        set(HAVE_LIBWEBSOCKETS TRUE)
        message(STATUS "libwebsockets found: ${LIBWEBSOCKETS_LIB}")
    else()
        set(HAVE_LIBWEBSOCKETS FALSE)
        message(STATUS "libwebsockets not found - live streaming disabled")
    endif()
    
    if(glfw3_FOUND)
        message(STATUS "GLFW found, building visualization with OpenGL/ImGui")
        
        # Fetch Dear ImGui
        FetchContent_Declare(
            imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG v1.90
        )
        FetchContent_MakeAvailable(imgui)
        
        # ImGui sources (core + backends)
        set(IMGUI_SOURCES
            ${imgui_SOURCE_DIR}/imgui.cpp
            ${imgui_SOURCE_DIR}/imgui_demo.cpp
            ${imgui_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_SOURCE_DIR}/imgui_widgets.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        )
        
        # Visualization source files
        set(VIEWER_SOURCES
            src/visualization/DepthChartViewerGL.cpp
            src/visualization/main_viewer.cpp
        )
        
        # Add live WebSocket source if available
        if(HAVE_LIBWEBSOCKETS AND ENABLE_LIVE_WEBSOCKET)
            list(APPEND VIEWER_SOURCES src/market_data/LiveWebSocketClient.cpp)
        endif()
        
        # Add visualization executable (LOB depth chart)
        add_executable(lob_depth_viewer
            ${VIEWER_SOURCES}
            ${IMGUI_SOURCES}
        )
        
        target_include_directories(lob_depth_viewer PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
        )
        
        target_link_libraries(lob_depth_viewer PRIVATE
            lob_lib
            OpenGL::GL
            glfw
        )
        
        target_compile_definitions(lob_depth_viewer PRIVATE ENABLE_OPENGL_VIEWER)
        
        # Link libwebsockets if available
        if(HAVE_LIBWEBSOCKETS AND ENABLE_LIVE_WEBSOCKET)
            target_include_directories(lob_depth_viewer PRIVATE ${LIBWEBSOCKETS_INCLUDE})
            target_link_libraries(lob_depth_viewer PRIVATE ${LIBWEBSOCKETS_LIB})
            target_compile_definitions(lob_depth_viewer PRIVATE ENABLE_LIVE_WEBSOCKET)
            message(STATUS "Live WebSocket streaming: ENABLED")
        else()
            message(STATUS "Live WebSocket streaming: DISABLED")
        endif()

        # ── Week 10: ABMS Interactive Viewer ─────────────────────────────────
        add_executable(lob_abms_viewer
            src/main_abms.cpp
            ${UI_SOURCES}
            ${IMGUI_SOURCES}
        )

        target_include_directories(lob_abms_viewer PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
        )

        target_link_libraries(lob_abms_viewer PRIVATE
            lob_lib
            OpenGL::GL
            glfw
        )

        target_compile_definitions(lob_abms_viewer PRIVATE
            ENABLE_OPENGL_VIEWER
            GL_SILENCE_DEPRECATION   # suppress macOS 10.14+ OpenGL deprecation warnings
        )
        message(STATUS "ABMS viewer (Week 10): ENABLED → lob_abms_viewer")
    else()
        message(STATUS "GLFW not found, building terminal-only visualization")
        
        # Terminal-only viewer
        add_executable(lob_depth_viewer_terminal
            src/visualization/DepthChartViewerGL.cpp
            src/visualization/main_viewer.cpp
        )
        
        target_include_directories(lob_depth_viewer_terminal PRIVATE
            ${CMAKE_SOURCE_DIR}/include
        )
        
        target_link_libraries(lob_depth_viewer_terminal PRIVATE lob_lib)
    endif()
endif()

# Install targets
install(TARGETS lob_matching_engine DESTINATION bin)
install(DIRECTORY include/ DESTINATION include)

# Print build configuration
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
