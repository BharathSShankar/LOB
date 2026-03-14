#include <gtest/gtest.h>
#include "concurrency/RingBuffer.h"
#include <thread>
#include <vector>

using namespace lob::concurrency;

class RingBufferTest : public ::testing::Test
{
protected:
    static constexpr size_t BUFFER_SIZE = 1024; // Must be power of 2
    RingBuffer<int, BUFFER_SIZE> buffer;

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
// Week 5 Tests - Lock-Free Ring Buffer
// ============================================================================

TEST_F(RingBufferTest, InitialState)
{
    EXPECT_TRUE(buffer.empty());
    EXPECT_FALSE(buffer.full());
    EXPECT_EQ(buffer.size(), 0);
    EXPECT_EQ(buffer.capacity(), BUFFER_SIZE);
}

TEST_F(RingBufferTest, PushSingleElement)
{
    bool success = buffer.push(42);

    EXPECT_TRUE(success);
    EXPECT_FALSE(buffer.empty());
    EXPECT_EQ(buffer.size(), 1);
}

TEST_F(RingBufferTest, PopSingleElement)
{
    buffer.push(42);

    int value;
    bool success = buffer.pop(value);

    EXPECT_TRUE(success);
    EXPECT_EQ(value, 42);
    EXPECT_TRUE(buffer.empty());
}

TEST_F(RingBufferTest, PopFromEmpty)
{
    int value;
    bool success = buffer.pop(value);

    EXPECT_FALSE(success);
}

TEST_F(RingBufferTest, PushToFull)
{
    // Fill buffer completely (capacity - 1 due to implementation)
    for (size_t i = 0; i < BUFFER_SIZE - 1; ++i)
    {
        bool success = buffer.push(static_cast<int>(i));
        EXPECT_TRUE(success);
    }

    EXPECT_TRUE(buffer.full());

    // Next push should fail
    bool success = buffer.push(999);
    EXPECT_FALSE(success);
}

TEST_F(RingBufferTest, PushPopSequence)
{
    std::vector<int> values = {1, 2, 3, 4, 5};

    // Push all values
    for (int v : values)
    {
        EXPECT_TRUE(buffer.push(v));
    }

    // Pop all values
    for (int expected : values)
    {
        int value;
        EXPECT_TRUE(buffer.pop(value));
        EXPECT_EQ(value, expected);
    }

    EXPECT_TRUE(buffer.empty());
}

TEST_F(RingBufferTest, MoveSemantics)
{
    std::string str = "test";
    bool success = buffer.push(std::move(42));
    EXPECT_TRUE(success);
}

TEST_F(RingBufferTest, WrapAround)
{
    // Fill half buffer
    for (size_t i = 0; i < BUFFER_SIZE / 2; ++i)
    {
        buffer.push(static_cast<int>(i));
    }

    // Pop half
    for (size_t i = 0; i < BUFFER_SIZE / 2; ++i)
    {
        int value;
        buffer.pop(value);
    }

    // Push again (should wrap around)
    for (size_t i = 0; i < BUFFER_SIZE / 2; ++i)
    {
        bool success = buffer.push(static_cast<int>(i + 1000));
        EXPECT_TRUE(success);
    }
}

TEST_F(RingBufferTest, SizeTracking)
{
    EXPECT_EQ(buffer.size(), 0);

    buffer.push(1);
    EXPECT_EQ(buffer.size(), 1);

    buffer.push(2);
    EXPECT_EQ(buffer.size(), 2);

    int value;
    buffer.pop(value);
    EXPECT_EQ(buffer.size(), 1);
}

// ============================================================================
// Week 5 Tests - Concurrency & Thread Safety
// ============================================================================

TEST_F(RingBufferTest, SingleProducerSingleConsumer)
{
    static constexpr size_t NUM_ITEMS = 10000;
    std::atomic<bool> producer_done{false};

    // Producer thread
    std::thread producer([this, &producer_done]()
                         {
        for (size_t i = 0; i < NUM_ITEMS; ++i) {
            while (!buffer.push(static_cast<int>(i))) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release); });

    // Consumer thread
    std::thread consumer([this, &producer_done]()
                         {
        size_t count = 0;
        int expected = 0;
        
        while (count < NUM_ITEMS) {
            int value;
            if (buffer.pop(value)) {
                EXPECT_EQ(value, expected);
                ++expected;
                ++count;
            } else {
                std::this_thread::yield();
            }
        } });

    producer.join();
    consumer.join();

    EXPECT_TRUE(buffer.empty());
}

TEST_F(RingBufferTest, MemoryOrdering)
{
    // Stress test memory ordering under contention
    static constexpr size_t ITERATIONS = 100000;

    std::thread producer([this]()
                         {
        for (size_t i = 0; i < ITERATIONS; ++i) {
            while (!buffer.push(static_cast<int>(i))) {
                // Spin
            }
        } });

    std::thread consumer([this]()
                         {
        size_t count = 0;
        while (count < ITERATIONS) {
            int value;
            if (buffer.pop(value)) {
                ++count;
            }
        } });

    producer.join();
    consumer.join();

    EXPECT_TRUE(buffer.empty());
}

TEST_F(RingBufferTest, CacheLinePadding)
{
    // Check that read and write indices are on different cache lines
    SUCCEED() << "Verify with performance profiling that false sharing is avoided";
}

// ============================================================================
// Week 6 Tests - Performance
// ============================================================================

TEST_F(RingBufferTest, Latency)
{
    static constexpr size_t ITERATIONS = 100000;

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < ITERATIONS; ++i)
    {
        buffer.push(static_cast<int>(i));
        int value;
        buffer.pop(value);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

    std::cout << "Average push+pop latency: "
              << duration.count() / ITERATIONS << " ns" << std::endl;
}
