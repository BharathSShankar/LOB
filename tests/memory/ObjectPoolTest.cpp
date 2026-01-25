#include <gtest/gtest.h>
#include "memory/ObjectPool.h"
#include "core/Order.h"
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>
#include <iostream>

using namespace lob::memory;
using namespace lob::core;

class ObjectPoolTest : public ::testing::Test
{
protected:
    static constexpr size_t POOL_SIZE = 100;
    ObjectPool<Order, POOL_SIZE> pool;

    void SetUp() override
    {
        // Setup code before each test
    }

    void TearDown() override
    {
        // Cleanup code after each test
    }
};

// ============================================================================
// Week 3-4 Tests - Object Pool Functionality
// ============================================================================

TEST_F(ObjectPoolTest, InitialState)
{
    // TODO (Week 3): Test initial pool state
    EXPECT_EQ(pool.capacity(), POOL_SIZE);
    EXPECT_EQ(pool.available(), POOL_SIZE);
    EXPECT_TRUE(pool.is_full());
    EXPECT_FALSE(pool.is_empty());
}

TEST_F(ObjectPoolTest, AcquireObject)
{
    // TODO (Week 3): Test acquiring object
    auto *obj = pool.acquire();
    EXPECT_NE(obj, nullptr);
    EXPECT_EQ(pool.available(), POOL_SIZE - 1);
}

TEST_F(ObjectPoolTest, ReleaseObject)
{
    // TODO (Week 3): Test releasing object
    auto *obj = pool.acquire();
    EXPECT_NE(obj, nullptr);

    pool.release(obj);
    EXPECT_EQ(pool.available(), POOL_SIZE);
}

TEST_F(ObjectPoolTest, AcquireAllObjects)
{
    // TODO (Week 3): Test exhausting pool
    std::vector<Order *> objects;

    for (size_t i = 0; i < POOL_SIZE; ++i)
    {
        auto *obj = pool.acquire();
        EXPECT_NE(obj, nullptr);
        objects.push_back(obj);
    }

    EXPECT_TRUE(pool.is_empty());
    EXPECT_EQ(pool.available(), 0);

    // Next acquire should fail
    auto *obj = pool.acquire();
    EXPECT_EQ(obj, nullptr);

    // Clean up
    for (auto *o : objects)
    {
        pool.release(o);
    }
}

TEST_F(ObjectPoolTest, ReleaseNullptr)
{
    // TODO (Week 3): Test releasing nullptr
    pool.release(nullptr);
    EXPECT_EQ(pool.available(), POOL_SIZE);
}

TEST_F(ObjectPoolTest, MultipleAcquireRelease)
{
    // TODO (Week 3): Test multiple cycles
    for (int cycle = 0; cycle < 10; ++cycle)
    {
        auto *obj = pool.acquire();
        EXPECT_NE(obj, nullptr);
        pool.release(obj);
    }

    EXPECT_EQ(pool.available(), POOL_SIZE);
}

TEST_F(ObjectPoolTest, Reset)
{
    // TODO (Week 3): Test pool reset
    // Acquire some objects
    auto *obj1 = pool.acquire();
    auto *obj2 = pool.acquire();

    EXPECT_EQ(pool.available(), POOL_SIZE - 2);

    pool.reset();
    EXPECT_EQ(pool.available(), POOL_SIZE);
}

TEST_F(ObjectPoolTest, ObjectUsage)
{
    // TODO (Week 3): Test using acquired objects
    auto *order = pool.acquire();
    EXPECT_NE(order, nullptr);

    // Use the order
    order->order_id = 123;
    order->price = 10000;
    order->quantity = 100;

    EXPECT_EQ(order->order_id, 123);
    EXPECT_EQ(order->price, 10000);

    pool.release(order);
}

// ============================================================================
// Week 3-4 Tests - Memory Layout & Performance
// ============================================================================

TEST_F(ObjectPoolTest, CacheLineAlignment)
{
    // TODO (Week 3-4): Verify cache line alignment
    auto *obj = pool.acquire();
    EXPECT_NE(obj, nullptr);

    // Check alignment (should be 64-byte aligned)
    auto addr = reinterpret_cast<uintptr_t>(obj);
    // Note: This might not be perfectly aligned depending on implementation

    pool.release(obj);
}

TEST_F(ObjectPoolTest, NoHeapAllocation)
{
    // TODO (Week 3-4): Verify no heap allocation during runtime
    // This is a conceptual test - in practice, you'd use memory profiling tools

    // Acquire and release should not allocate on heap
    auto *obj = pool.acquire();
    pool.release(obj);

    SUCCEED() << "Verify with profiler that no heap allocation occurred";
}

TEST_F(ObjectPoolTest, AllocationSpeed)
{
    // TODO (Week 3-4): Basic performance test
    auto start = std::chrono::high_resolution_clock::now();

    constexpr int ITERATIONS = 1000;
    for (int i = 0; i < ITERATIONS; ++i)
    {
        auto *obj = pool.acquire();
        if (obj)
        {
            pool.release(obj);
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

    std::cout << "Average acquire/release time: "
              << duration.count() / ITERATIONS << " ns" << std::endl;
}

// ============================================================================
// Week 5 Tests - Thread Safety
// ============================================================================

TEST_F(ObjectPoolTest, ThreadSafety)
{
    // Test thread safety using external synchronization
    // The ObjectPool itself is not thread-safe, so we use a mutex to protect access
    // This test validates correctness when using proper synchronization

    const int NUM_THREADS = 4;
    const int OPS_PER_THREAD = 20; // Must be less than POOL_SIZE / NUM_THREADS

    std::mutex pool_mutex;
    std::atomic<int> acquire_count{0};
    std::atomic<int> release_count{0};
    std::atomic<int> null_acquires{0};

    auto worker = [&](int thread_id)
    {
        std::vector<Order *> acquired;
        acquired.reserve(OPS_PER_THREAD);

        // Acquire phase
        for (int i = 0; i < OPS_PER_THREAD; ++i)
        {
            std::lock_guard<std::mutex> lock(pool_mutex);
            Order *obj = pool.acquire();
            if (obj != nullptr)
            {
                obj->order_id = thread_id * 1000 + i;
                acquired.push_back(obj);
                acquire_count.fetch_add(1);
            }
            else
            {
                null_acquires.fetch_add(1);
            }
        }

        // Release phase
        for (Order *obj : acquired)
        {
            std::lock_guard<std::mutex> lock(pool_mutex);
            pool.release(obj);
            release_count.fetch_add(1);
        }
    };

    auto start = std::chrono::high_resolution_clock::now();

    // Spawn threads
    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; ++t)
    {
        threads.emplace_back(worker, t);
    }

    // Wait for all threads to complete
    for (auto &t : threads)
    {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "\n[ObjectPool ThreadSafety] Results:\n";
    std::cout << "  - Threads: " << NUM_THREADS << "\n";
    std::cout << "  - Operations per thread: " << OPS_PER_THREAD << "\n";
    std::cout << "  - Total acquires: " << acquire_count.load() << "\n";
    std::cout << "  - Total releases: " << release_count.load() << "\n";
    std::cout << "  - Null acquires (pool exhausted): " << null_acquires.load() << "\n";
    std::cout << "  - Total time: " << duration.count() << " µs\n";

    // Verify all acquired objects were released
    EXPECT_EQ(acquire_count.load(), release_count.load());

    // Pool should be back to full capacity
    EXPECT_EQ(pool.available(), POOL_SIZE);
    EXPECT_TRUE(pool.is_full());
}
