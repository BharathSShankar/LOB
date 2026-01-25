#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace lob
{
    namespace profiling
    {

        /**
         * @brief Tracks memory allocations and deallocations on the hot path
         *
         * This profiler is designed to be low-overhead and thread-safe.
         * It tracks allocation counts, sizes, and timing information.
         */
        class MemoryProfiler
        {
        public:
            struct AllocationStats
            {
                uint64_t alloc_count{0};
                uint64_t dealloc_count{0};
                uint64_t total_bytes_allocated{0};
                uint64_t total_bytes_deallocated{0};
                uint64_t peak_memory{0};
                uint64_t current_memory{0};
            };

            struct AllocationEvent
            {
                uint64_t timestamp_ns;
                size_t size;
                void *ptr;
                std::string tag;
                bool is_allocation; // true for alloc, false for dealloc
            };

            static MemoryProfiler &instance()
            {
                static MemoryProfiler profiler;
                return profiler;
            }

            // Record an allocation
            void record_allocation(void *ptr, size_t size, const std::string &tag = "unknown");

            // Record a deallocation
            void record_deallocation(void *ptr, size_t size, const std::string &tag = "unknown");

            // Get current stats
            AllocationStats get_stats() const { return stats_; }

            // Get allocation history (for detailed analysis)
            std::vector<AllocationEvent> get_events() const;

            // Reset all stats
            void reset();

            // Generate report
            std::string generate_report() const;

            // Enable/disable event recording (for low-overhead vs detailed profiling)
            void set_event_recording(bool enabled) { record_events_ = enabled; }

            // Start profiling session
            void start_session(const std::string &name);

            // End profiling session
            void end_session();

        private:
            MemoryProfiler() = default;
            ~MemoryProfiler() = default;

            MemoryProfiler(const MemoryProfiler &) = delete;
            MemoryProfiler &operator=(const MemoryProfiler &) = delete;

            AllocationStats stats_;
            std::atomic<bool> record_events_{false};
            mutable std::mutex events_mutex_;
            std::vector<AllocationEvent> events_;
            std::string session_name_;
            std::chrono::steady_clock::time_point session_start_;

            std::unordered_map<void *, size_t> active_allocations_;
            mutable std::mutex allocations_mutex_;

            uint64_t get_timestamp_ns() const
            {
                return std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                    .count();
            }
        };

        /**
         * @brief RAII-style scope profiler for hot path sections
         */
        class ScopeMemoryProfiler
        {
        public:
            ScopeMemoryProfiler(const std::string &section_name)
                : section_name_(section_name),
                  start_stats_(MemoryProfiler::instance().get_stats())
            {
            }

            ~ScopeMemoryProfiler()
            {
                auto end_stats = MemoryProfiler::instance().get_stats();

                uint64_t allocs = end_stats.alloc_count - start_stats_.alloc_count;
                uint64_t deallocs = end_stats.dealloc_count - start_stats_.dealloc_count;

                (void)allocs;   // Suppress unused warning
                (void)deallocs; // Suppress unused warning

                // This could be logged or stored for reporting
                // For now, we just track it
            }

        private:
            std::string section_name_;
            MemoryProfiler::AllocationStats start_stats_;
        };

// Helper macros for easy profiling
#ifdef ENABLE_MEMORY_PROFILING
#define PROFILE_MEMORY_SCOPE(name) lob::profiling::ScopeMemoryProfiler __profiler_##__LINE__(name)
#define PROFILE_ALLOC(ptr, size, tag) lob::profiling::MemoryProfiler::instance().record_allocation(ptr, size, tag)
#define PROFILE_DEALLOC(ptr, size, tag) lob::profiling::MemoryProfiler::instance().record_deallocation(ptr, size, tag)
#else
#define PROFILE_MEMORY_SCOPE(name)
#define PROFILE_ALLOC(ptr, size, tag)
#define PROFILE_DEALLOC(ptr, size, tag)
#endif

    } // namespace profiling
} // namespace lob
