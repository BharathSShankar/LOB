#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include <atomic>
#include <cassert>

namespace lob::memory
{

    /**
     * @brief Spinlock-protected Object Pool for pre-allocated objects
     *
     * All objects are heap-allocated at construction time. During runtime,
     * acquire() and release() operate in O(1) with no heap allocation.
     * Thread safety is provided via a lightweight spinlock (std::atomic_flag),
     * making the pool safe for concurrent access from producer and consumer
     * threads. The ring buffer between threads remains fully lock-free.
     *
     * @tparam T Type of object to pool
     * @tparam Capacity Maximum number of objects in pool
     */
    template <typename T, size_t Capacity>
    class ObjectPool
    {
    public:
        /**
         * @brief Construct object pool with pre-allocated memory
         */
        ObjectPool() noexcept;

        /**
         * @brief Destructor - cleanup all objects
         */
        ~ObjectPool() noexcept;

        // Non-copyable, non-movable
        ObjectPool(const ObjectPool &) = delete;
        ObjectPool &operator=(const ObjectPool &) = delete;

        /**
         * @brief Acquire an object from the pool (spinlock-protected)
         * @return Pointer to object, nullptr if pool exhausted
         */
        T *acquire() noexcept;

        /**
         * @brief Return an object to the pool (spinlock-protected)
         * @param obj Pointer to object to return
         */
        void release(T *obj) noexcept;

        /**
         * @brief Get number of available objects
         */
        size_t available() const noexcept;

        /**
         * @brief Get total capacity
         */
        constexpr size_t capacity() const noexcept { return Capacity; }

        /**
         * @brief Check if pool is empty
         */
        bool is_empty() const noexcept;

        /**
         * @brief Check if pool is full
         */
        bool is_full() const noexcept;

        /**
         * @brief Reset pool to initial state (NOT thread-safe — call only when idle)
         */
        void reset() noexcept;

    private:
        // Storage for all objects (allocated at construction, cache-line aligned)
        alignas(64) std::array<T, Capacity> storage_;

        // Free list — indices of available objects
        std::array<size_t, Capacity> free_list_;

        // Number of free slots (also acts as stack top index)
        size_t free_index_;

        // Spinlock protecting free_list_ and free_index_
        mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;

        void spin_lock() const noexcept
        {
            while (lock_.test_and_set(std::memory_order_acquire))
            {
                // Busy-wait (spin). On x86 this compiles to a tight loop;
                // on ARM the acquire fence provides the necessary barrier.
            }
        }

        void spin_unlock() const noexcept
        {
            lock_.clear(std::memory_order_release);
        }
    };

    // ============================================================================
    // Template Implementation (must be in header)
    // ============================================================================

    template <typename T, size_t Capacity>
    ObjectPool<T, Capacity>::ObjectPool() noexcept
        : free_index_(Capacity)
    {
        for (size_t i = 0; i < Capacity; ++i)
        {
            free_list_[i] = i;
        }
    }

    template <typename T, size_t Capacity>
    ObjectPool<T, Capacity>::~ObjectPool() noexcept
    {
        // Objects are automatically destroyed as they're in std::array
    }

    template <typename T, size_t Capacity>
    T *ObjectPool<T, Capacity>::acquire() noexcept
    {
        spin_lock();

        if (free_index_ == 0)
        {
            spin_unlock();
            return nullptr; // Pool exhausted
        }

        --free_index_;
        size_t obj_index = free_list_[free_index_];

        spin_unlock();
        return &storage_[obj_index];
    }

    template <typename T, size_t Capacity>
    void ObjectPool<T, Capacity>::release(T *obj) noexcept
    {
        if (!obj)
            return;

        // Calculate index of object in storage
        size_t obj_index = obj - &storage_[0];
        assert(obj_index < Capacity && "Object not from this pool");

        spin_lock();

        if (free_index_ >= Capacity)
        {
            spin_unlock();
            return; // Pool already full
        }

        free_list_[free_index_] = obj_index;
        ++free_index_;

        spin_unlock();
    }

    template <typename T, size_t Capacity>
    size_t ObjectPool<T, Capacity>::available() const noexcept
    {
        return free_index_;
    }

    template <typename T, size_t Capacity>
    bool ObjectPool<T, Capacity>::is_empty() const noexcept
    {
        return free_index_ == 0;
    }

    template <typename T, size_t Capacity>
    bool ObjectPool<T, Capacity>::is_full() const noexcept
    {
        return free_index_ == Capacity;
    }

    template <typename T, size_t Capacity>
    void ObjectPool<T, Capacity>::reset() noexcept
    {
        free_index_ = Capacity;
        for (size_t i = 0; i < Capacity; ++i)
        {
            free_list_[i] = i;
        }
    }

} // namespace lob::memory
