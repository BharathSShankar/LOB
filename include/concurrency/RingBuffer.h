#pragma once

#include <atomic>
#include <array>
#include <cstdint>

namespace lob::concurrency
{

    /**
     * @brief Lock-free Single-Producer Single-Consumer (SPSC) Ring Buffer
     *
     * Week 5 Focus: This implements the Disruptor pattern for ultra-low latency
     * thread communication without mutexes.
     *
     * Key Concepts:
     * - Lock-free: Uses atomic operations instead of locks
     * - Wait-free for producer and consumer (in most cases)
     * - Cache-line padding to prevent false sharing
     * - Memory ordering (acquire/release semantics)
     *
     * @tparam T Type of elements in the buffer
     * @tparam Size Size of the ring buffer (must be power of 2 for optimization)
     */
    template <typename T, size_t Size>
    class RingBuffer
    {
        static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");

    public:
        RingBuffer() noexcept;
        ~RingBuffer() noexcept = default;

        // Non-copyable, non-movable
        RingBuffer(const RingBuffer &) = delete;
        RingBuffer &operator=(const RingBuffer &) = delete;

        /**
         * @brief Push element to buffer (Producer side)
         * @param item Item to push
         * @return true if successful, false if buffer is full
         */
        bool push(const T &item) noexcept;

        /**
         * @brief Push element using move semantics
         */
        bool push(T &&item) noexcept;

        /**
         * @brief Pop element from buffer (Consumer side)
         * @param item Reference to store popped item
         * @return true if successful, false if buffer is empty
         */
        bool pop(T &item) noexcept;

        /**
         * @brief Check if buffer is empty
         */
        bool empty() const noexcept;

        /**
         * @brief Check if buffer is full
         */
        bool full() const noexcept;

        /**
         * @brief Get current size (approximate, may be stale)
         */
        size_t size() const noexcept;

        /**
         * @brief Get capacity
         */
        constexpr size_t capacity() const noexcept { return Size; }

    private:
        // Cache line size varies by architecture:
        // x86-64: 64 bytes
        // Apple M1/M2/M3 (ARM64): 128 bytes
#ifdef ARM_CACHE_LINE_SIZE
        static constexpr size_t CACHE_LINE_SIZE = ARM_CACHE_LINE_SIZE;
#else
        static constexpr size_t CACHE_LINE_SIZE = 64;
#endif

        // The actual data storage
        std::array<T, Size> buffer_;

        // TODO (Week 5): Add proper memory ordering
        // std::memory_order_acquire, std::memory_order_release

        // Producer-side index (written by producer, read by consumer)
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> write_index_;

        // Consumer-side index (written by consumer, read by producer)
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> read_index_;

        // Cached indices to reduce atomic operations
        size_t cached_read_index_;
        size_t cached_write_index_;
    };

    // ============================================================================
    // Template Implementation
    // ============================================================================

    template <typename T, size_t Size>
    RingBuffer<T, Size>::RingBuffer() noexcept
        : write_index_(0), read_index_(0), cached_read_index_(0), cached_write_index_(0)
    {
        // TODO (Week 5): Initialize ring buffer
    }

    template <typename T, size_t Size>
    bool RingBuffer<T, Size>::push(const T &item) noexcept
    {
        // TODO (Week 5): Implement lock-free push
        // 1. Get current write position
        // 2. Check if buffer is full (write would catch up to read)
        // 3. Write item at write position
        // 4. Increment write index with proper memory ordering

        const size_t write_pos = write_index_.load(std::memory_order_relaxed);
        const size_t next_write = (write_pos + 1) & (Size - 1);

        // Check if full (would we catch up to read index?)
        if (next_write == cached_read_index_)
        {
            // Refresh cached read index
            cached_read_index_ = read_index_.load(std::memory_order_acquire);
            if (next_write == cached_read_index_)
            {
                return false; // Buffer is full
            }
        }

        // Write data
        buffer_[write_pos] = item;

        // Publish write (release semantics ensures write completes before index update)
        write_index_.store(next_write, std::memory_order_release);

        return true;
    }

    template <typename T, size_t Size>
    bool RingBuffer<T, Size>::push(T &&item) noexcept
    {
        // TODO (Week 5): Implement move version
        const size_t write_pos = write_index_.load(std::memory_order_relaxed);
        const size_t next_write = (write_pos + 1) & (Size - 1);

        if (next_write == cached_read_index_)
        {
            cached_read_index_ = read_index_.load(std::memory_order_acquire);
            if (next_write == cached_read_index_)
            {
                return false;
            }
        }

        buffer_[write_pos] = std::move(item);
        write_index_.store(next_write, std::memory_order_release);

        return true;
    }

    template <typename T, size_t Size>
    bool RingBuffer<T, Size>::pop(T &item) noexcept
    {
        // TODO (Week 5): Implement lock-free pop
        // 1. Get current read position
        // 2. Check if buffer is empty (read caught up to write)
        // 3. Read item from read position
        // 4. Increment read index with proper memory ordering

        const size_t read_pos = read_index_.load(std::memory_order_relaxed);

        // Check if empty
        if (read_pos == cached_write_index_)
        {
            // Refresh cached write index
            cached_write_index_ = write_index_.load(std::memory_order_acquire);
            if (read_pos == cached_write_index_)
            {
                return false; // Buffer is empty
            }
        }

        // Read data
        item = buffer_[read_pos];

        const size_t next_read = (read_pos + 1) & (Size - 1);

        // Publish read (release semantics)
        read_index_.store(next_read, std::memory_order_release);

        return true;
    }

    template <typename T, size_t Size>
    bool RingBuffer<T, Size>::empty() const noexcept
    {
        return read_index_.load(std::memory_order_acquire) ==
               write_index_.load(std::memory_order_acquire);
    }

    template <typename T, size_t Size>
    bool RingBuffer<T, Size>::full() const noexcept
    {
        const size_t write_pos = write_index_.load(std::memory_order_acquire);
        const size_t read_pos = read_index_.load(std::memory_order_acquire);
        const size_t next_write = (write_pos + 1) & (Size - 1);
        return next_write == read_pos;
    }

    template <typename T, size_t Size>
    size_t RingBuffer<T, Size>::size() const noexcept
    {
        const size_t write_pos = write_index_.load(std::memory_order_acquire);
        const size_t read_pos = read_index_.load(std::memory_order_acquire);
        return (write_pos - read_pos) & (Size - 1);
    }

} // namespace lob::concurrency
