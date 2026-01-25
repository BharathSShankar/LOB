#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <algorithm>

namespace lob
{
    namespace profiling
    {

        /**
         * @brief Profiles hot path execution times with minimal overhead
         *
         * Tracks min, max, average, and percentile latencies for critical paths
         */
        class HotPathProfiler
        {
        public:
            struct PathStats
            {
                uint64_t call_count{0};
                uint64_t total_time_ns{0};
                uint64_t min_time_ns{UINT64_MAX};
                uint64_t max_time_ns{0};
                std::vector<uint64_t> samples; // For percentile calculations
            };

            static HotPathProfiler &instance()
            {
                static HotPathProfiler profiler;
                return profiler;
            }

            // Record a hot path execution
            void record_execution(const std::string &path_name, uint64_t duration_ns);

            // Get statistics for a specific path
            PathStats get_path_stats(const std::string &path_name) const;

            // Get all path statistics
            std::unordered_map<std::string, PathStats> get_all_stats() const;

            // Reset all statistics
            void reset();

            // Generate detailed report
            std::string generate_report() const;

            // Enable/disable sample collection (for percentiles)
            void set_sample_collection(bool enabled) { collect_samples_ = enabled; }

            // Set maximum number of samples to keep per path
            void set_max_samples(size_t max) { max_samples_ = max; }

        private:
            HotPathProfiler() = default;
            ~HotPathProfiler() = default;

            HotPathProfiler(const HotPathProfiler &) = delete;
            HotPathProfiler &operator=(const HotPathProfiler &) = delete;

            mutable std::mutex stats_mutex_;
            std::unordered_map<std::string, PathStats> path_stats_;
            bool collect_samples_{true};
            size_t max_samples_{10000};

            // Calculate percentile from sorted samples
            uint64_t calculate_percentile(const std::vector<uint64_t> &samples, double percentile) const;
        };

        /**
         * @brief RAII timer for automatic hot path profiling
         */
        class ScopeTimer
        {
        public:
            explicit ScopeTimer(const std::string &path_name)
                : path_name_(path_name),
                  start_(std::chrono::high_resolution_clock::now())
            {
            }

            ~ScopeTimer()
            {
                auto end = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_).count();
                HotPathProfiler::instance().record_execution(path_name_, duration);
            }

        private:
            std::string path_name_;
            std::chrono::high_resolution_clock::time_point start_;
        };

// Helper macros for easy profiling
#ifdef ENABLE_HOTPATH_PROFILING
#define PROFILE_HOTPATH(name) lob::profiling::ScopeTimer __timer_##__LINE__(name)
#else
#define PROFILE_HOTPATH(name)
#endif

    } // namespace profiling
} // namespace lob
