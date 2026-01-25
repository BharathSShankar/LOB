#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include <cassert>

namespace lob::memory
{

    /**
     * @brief Lock-free Object Pool for pre-allocated objects
     *
     * Zero heap allocation during runtime - all objects allocated at construction.
     * Uses a free list for O(1) allocation/deallocation.
     *
     * Week 3-4 Focus: This is critical for achieving sub-microsecond latency.
     * No new/delete/malloc on the hot path!
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
         * @brief Rent an object from the pool
         * @return Pointer to object, nullptr if pool exhausted
         */
        T *acquire() noexcept;

        /**
         * @brief Return an object to the pool
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
         * @brief Reset pool to initial state
         */
        void reset() noexcept;

    private:
        // TODO (Week 3-4): Implement efficient memory layout
        // Consider alignment and cache line optimization

        // Storage for all objects (allocated at construction)
        alignas(64) std::array<T, Capacity> storage_;

        // Free list - indices of available objects
        std::array<size_t, Capacity> free_list_;

        // Index of next free slot
        size_t free_index_;

        // TODO (Week 5): Make this lock-free using atomics
        // For multi-threaded access, need atomic operations
        // std::atomic<size_t> free_index_;
    };

    // ============================================================================
    // Template Implementation (must be in header)
    // ============================================================================

    template <typename T, size_t Capacity>
    ObjectPool<T, Capacity>::ObjectPool() noexcept
        : free_index_(Capacity)
    {
        // TODO (Week 3-4): Initialize free list
        // All indices point to objects in storage_
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
        // TODO (Week 3-4): Implement O(1) allocation
        // 1. Check if pool has available objects
        // 2. Get next free index from free list
        // 3. Return pointer to object

        if (free_index_ == 0)
        {
            return nullptr; // Pool exhausted
        }

        --free_index_;
        size_t obj_index = free_list_[free_index_];
        return &storage_[obj_index];
    }

    template <typename T, size_t Capacity>
    void ObjectPool<T, Capacity>::release(T *obj) noexcept
    {
        // TODO (Week 3-4): Implement O(1) deallocation
        // 1. Validate pointer is from this pool
        // 2. Add index back to free list

        if (!obj)
            return;

        // Calculate index of object in storage
        size_t obj_index = obj - &storage_[0];
        assert(obj_index < Capacity && "Object not from this pool");

        if (free_index_ >= Capacity)
        {
            return; // Pool already full
        }

        free_list_[free_index_] = obj_index;
        ++free_index_;
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
        // TODO (Week 3-4): Reset pool to initial state
        free_index_ = Capacity;
        for (size_t i = 0; i < Capacity; ++i)
        {
            free_list_[i] = i;
        }
    }

} // namespace lob::memory
