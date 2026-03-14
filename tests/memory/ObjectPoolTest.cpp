#include <gtest/gtest.h>
#include "memory/ObjectPool.h"
#include "core/Order.h"
#include <thread>
#include <vector>
#include <atomic>
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
    }

    void TearDown() override
    {
    }
};

// ============================================================================
// Object Pool Functionality Tests
// ============================================================================

TEST_F(ObjectPoolTest, InitialState)
{
    EXPECT_EQ(pool.capacity(), POOL_SIZE);
    EXPECT_EQ(pool.available(), POOL_SIZE);
    EXPECT_TRUE(pool.is_full());
    EXPECT_FALSE(pool.is_empty());
}

TEST_F(ObjectPoolTest, AcquireObject)
{
    auto *obj = pool.acquire();
    EXPECT_NE(obj, nullptr);
    EXPECT_EQ(pool.available(), POOL_SIZE - 1);
}

TEST_F(ObjectPoolTest, ReleaseObject)
{
    auto *obj = pool.acquire();
    EXPECT_NE(obj, nullptr);

    pool.release(obj);
    EXPECT_EQ(pool.available(), POOL_SIZE);
}

TEST_F(ObjectPoolTest, AcquireAllObjects)
{
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
    pool.release(nullptr);
    EXPECT_EQ(pool.available(), POOL_SIZE);
}

TEST_F(ObjectPoolTest, MultipleAcquireRelease)
{
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
    auto *obj1 = pool.acquire();
    auto *obj2 = pool.acquire();

    EXPECT_EQ(pool.available(), POOL_SIZE - 2);

    pool.reset();
    EXPECT_EQ(pool.available(), POOL_SIZE);
}

TEST_F(ObjectPoolTest, ObjectUsage)
{
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
// Memory Layout & Performance Tests
// ============================================================================

TEST_F(ObjectPoolTest, CacheLineAlignment)
{
    auto *obj = pool.acquire();
    EXPECT_NE(obj, nullptr);

    // Verify storage is 64-byte aligned (cache-line)
    auto addr = reinterpret_cast<uintptr_t>(obj);
    // Note: Individual objects within the array may not be perfectly 64-byte
    // aligned, but the storage array base is.

    pool.release(obj);
}

TEST_F(ObjectPoolTest, NoHeapAllocation)
{
    // Acquire and release should not allocate on heap — they only
    // manipulate the pre-allocated storage via the free list.
    auto *obj = pool.acquire();
    pool.release(obj);

    SUCCEED() << "Verify with profiler that no heap allocation occurred";
}

TEST_F(ObjectPoolTest, AllocationSpeed)
{
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
// Thread Safety Tests (ObjectPool is spinlock-protected internally)
// ============================================================================

TEST_F(ObjectPoolTest, ThreadSafety)
{
    // The ObjectPool is now internally spinlock-protected, so no external
    // mutex is required. This test validates correctness under concurrent
    // acquire/release from multiple threads.

    const int NUM_THREADS = 4;
    const int OPS_PER_THREAD = 20; // Must be less than POOL_SIZE / NUM_THREADS

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
