#include "profiling/HotPathProfiler.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

namespace lob
{
    namespace profiling
    {

        void HotPathProfiler::record_execution(const std::string &path_name, uint64_t duration_ns)
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);

            auto &stats = path_stats_[path_name];
            stats.call_count++;
            stats.total_time_ns += duration_ns;
            stats.min_time_ns = std::min(stats.min_time_ns, duration_ns);
            stats.max_time_ns = std::max(stats.max_time_ns, duration_ns);

            // Collect sample if enabled
            if (collect_samples_ && stats.samples.size() < max_samples_)
            {
                stats.samples.push_back(duration_ns);
            }
        }

        HotPathProfiler::PathStats HotPathProfiler::get_path_stats(const std::string &path_name) const
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);

            auto it = path_stats_.find(path_name);
            if (it != path_stats_.end())
            {
                return it->second;
            }
            return PathStats{};
        }

        std::unordered_map<std::string, HotPathProfiler::PathStats> HotPathProfiler::get_all_stats() const
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            return path_stats_;
        }

        void HotPathProfiler::reset()
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            path_stats_.clear();
        }

        uint64_t HotPathProfiler::calculate_percentile(const std::vector<uint64_t> &samples, double percentile) const
        {
            if (samples.empty())
                return 0;

            std::vector<uint64_t> sorted = samples;
            std::sort(sorted.begin(), sorted.end());

            size_t index = static_cast<size_t>(std::ceil(percentile * sorted.size() / 100.0)) - 1;
            index = std::min(index, sorted.size() - 1);

            return sorted[index];
        }

        std::string HotPathProfiler::generate_report() const
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);

            std::ostringstream oss;

            oss << "\n=== Hot Path Profiling Report ===\n\n";

            if (path_stats_.empty())
            {
                oss << "No profiling data collected.\n";
                return oss.str();
            }

            // Create sorted list of paths by total time
            std::vector<std::pair<std::string, PathStats>> sorted_paths;
            for (const auto &[name, stats] : path_stats_)
            {
                sorted_paths.emplace_back(name, stats);
            }

            std::sort(sorted_paths.begin(), sorted_paths.end(),
                      [](const auto &a, const auto &b)
                      {
                          return a.second.total_time_ns > b.second.total_time_ns;
                      });

            // Print summary table
            oss << std::left << std::setw(30) << "Path"
                << std::right << std::setw(12) << "Calls"
                << std::setw(15) << "Total (ms)"
                << std::setw(15) << "Avg (ns)"
                << std::setw(15) << "Min (ns)"
                << std::setw(15) << "Max (ns)"
                << std::setw(15) << "P50 (ns)"
                << std::setw(15) << "P95 (ns)"
                << std::setw(15) << "P99 (ns)"
                << "\n";
            oss << std::string(157, '-') << "\n";

            for (const auto &[name, stats] : sorted_paths)
            {
                uint64_t avg_ns = stats.call_count > 0 ? stats.total_time_ns / stats.call_count : 0;
                double total_ms = stats.total_time_ns / 1e6;

                uint64_t p50 = 0, p95 = 0, p99 = 0;
                if (!stats.samples.empty())
                {
                    p50 = calculate_percentile(stats.samples, 50.0);
                    p95 = calculate_percentile(stats.samples, 95.0);
                    p99 = calculate_percentile(stats.samples, 99.0);
                }

                oss << std::left << std::setw(30) << (name.length() > 29 ? name.substr(0, 26) + "..." : name)
                    << std::right << std::setw(12) << stats.call_count
                    << std::setw(15) << std::fixed << std::setprecision(3) << total_ms
                    << std::setw(15) << avg_ns
                    << std::setw(15) << (stats.min_time_ns == UINT64_MAX ? 0 : stats.min_time_ns)
                    << std::setw(15) << stats.max_time_ns
                    << std::setw(15) << p50
                    << std::setw(15) << p95
                    << std::setw(15) << p99
                    << "\n";
            }

            oss << "\n";

            // Additional insights
            oss << "Insights:\n";

            // Find paths with highest variance
            for (const auto &[name, stats] : sorted_paths)
            {
                if (stats.call_count > 0)
                {
                    uint64_t avg_ns = stats.total_time_ns / stats.call_count;
                    uint64_t range = stats.max_time_ns - (stats.min_time_ns == UINT64_MAX ? 0 : stats.min_time_ns);

                    if (range > avg_ns * 10) // High variance
                    {
                        oss << "  ⚠️  '" << name << "' shows high variance (range: "
                            << range << " ns, avg: " << avg_ns << " ns)\n";
                    }
                }
            }

            // Find potential bottlenecks (>100μs average)
            for (const auto &[name, stats] : sorted_paths)
            {
                if (stats.call_count > 0)
                {
                    uint64_t avg_ns = stats.total_time_ns / stats.call_count;
                    if (avg_ns > 100000) // > 100 microseconds
                    {
                        oss << "  🐢 '" << name << "' averaging " << (avg_ns / 1000.0)
                            << " μs (potential bottleneck)\n";
                    }
                }
            }

            // Find frequently called paths
            for (const auto &[name, stats] : sorted_paths)
            {
                if (stats.call_count > 100000)
                {
                    oss << "  🔥 '" << name << "' called " << stats.call_count
                        << " times (hot path)\n";
                }
            }

            oss << "\n=================================\n";

            return oss.str();
        }

    } // namespace profiling
} // namespace lob
