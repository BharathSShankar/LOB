#pragma once

#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <map>
#include <sstream>
#include <iomanip>

namespace lob::benchmarks
{

    /**
     * @brief Latency Statistics Calculator
     *
     * Computes latency histograms and percentiles (p50, p99, p99.9)
     * for Tick-to-Trade latency measurements.
     *
     * Key Concepts:
     * - Histogram: Distribution of latency values across buckets
     * - Percentiles: Value below which X% of observations fall
     * - p50 (median): 50% of requests are faster than this
     * - p99: 99% of requests are faster than this (tail latency)
     * - p99.9: 99.9% threshold (critical for SLA compliance)
     */
    class LatencyStats
    {
    public:
        LatencyStats() = default;

        /**
         * @brief Record a latency sample (in nanoseconds)
         */
        void record(uint64_t latency_ns) noexcept
        {
            samples_.push_back(latency_ns);
            is_sorted_ = false;
        }

        /**
         * @brief Clear all recorded samples
         */
        void clear() noexcept
        {
            samples_.clear();
            is_sorted_ = false;
        }

        /**
         * @brief Get number of samples
         */
        size_t count() const noexcept
        {
            return samples_.size();
        }

        /**
         * @brief Get minimum latency
         */
        uint64_t min() const noexcept
        {
            if (samples_.empty())
                return 0;
            ensure_sorted();
            return samples_.front();
        }

        /**
         * @brief Get maximum latency
         */
        uint64_t max() const noexcept
        {
            if (samples_.empty())
                return 0;
            ensure_sorted();
            return samples_.back();
        }

        /**
         * @brief Get mean latency
         */
        double mean() const noexcept
        {
            if (samples_.empty())
                return 0.0;
            double sum = std::accumulate(samples_.begin(), samples_.end(), 0.0);
            return sum / static_cast<double>(samples_.size());
        }

        /**
         * @brief Get standard deviation
         */
        double stddev() const noexcept
        {
            if (samples_.size() < 2)
                return 0.0;
            double m = mean();
            double sq_sum = 0.0;
            for (auto s : samples_)
            {
                double diff = static_cast<double>(s) - m;
                sq_sum += diff * diff;
            }
            return std::sqrt(sq_sum / static_cast<double>(samples_.size() - 1));
        }

        /**
         * @brief Get percentile value
         * @param percentile Percentile (0.0 to 100.0)
         */
        uint64_t percentile(double p) const noexcept
        {
            if (samples_.empty())
                return 0;
            ensure_sorted();
            size_t index = static_cast<size_t>(p / 100.0 * static_cast<double>(samples_.size() - 1));
            return samples_[index];
        }

        /**
         * @brief Get p50 (median)
         */
        uint64_t p50() const noexcept { return percentile(50.0); }

        /**
         * @brief Get p95
         */
        uint64_t p95() const noexcept { return percentile(95.0); }

        /**
         * @brief Get p99 (tail latency)
         */
        uint64_t p99() const noexcept { return percentile(99.0); }

        /**
         * @brief Get p99.9 (extreme tail latency)
         */
        uint64_t p999() const noexcept { return percentile(99.9); }

        /**
         * @brief Get p99.99
         */
        uint64_t p9999() const noexcept { return percentile(99.99); }

        /**
         * @brief Build histogram with specified bucket size
         * @param bucket_size_ns Size of each bucket in nanoseconds
         * @return Map of bucket start -> count
         */
        std::map<uint64_t, size_t> histogram(uint64_t bucket_size_ns = 100) const noexcept
        {
            std::map<uint64_t, size_t> hist;
            for (auto s : samples_)
            {
                uint64_t bucket = (s / bucket_size_ns) * bucket_size_ns;
                hist[bucket]++;
            }
            return hist;
        }

        /**
         * @brief Build logarithmic histogram (powers of 2)
         * @return Map of bucket start -> count
         */
        std::map<uint64_t, size_t> log_histogram() const noexcept
        {
            std::map<uint64_t, size_t> hist;
            for (auto s : samples_)
            {
                if (s == 0)
                {
                    hist[0]++;
                }
                else
                {
                    // Find bucket: 2^floor(log2(s))
                    uint64_t bucket = 1ULL << static_cast<uint64_t>(std::log2(static_cast<double>(s)));
                    hist[bucket]++;
                }
            }
            return hist;
        }

        /**
         * @brief Get ASCII histogram representation
         */
        std::string ascii_histogram(size_t num_buckets = 20, size_t bar_width = 50) const noexcept
        {
            if (samples_.empty())
                return "No data";

            ensure_sorted();
            uint64_t min_val = samples_.front();
            uint64_t max_val = samples_.back();
            uint64_t range = max_val - min_val;

            if (range == 0)
            {
                return "All samples have the same value: " + std::to_string(min_val) + " ns";
            }

            uint64_t bucket_size = (range + num_buckets - 1) / num_buckets;

            // Count samples in each bucket
            std::vector<size_t> counts(num_buckets, 0);
            for (auto s : samples_)
            {
                size_t bucket_idx = static_cast<size_t>((s - min_val) / bucket_size);
                if (bucket_idx >= num_buckets)
                    bucket_idx = num_buckets - 1;
                counts[bucket_idx]++;
            }

            size_t max_count = *std::max_element(counts.begin(), counts.end());

            std::ostringstream oss;
            oss << "\nLatency Distribution Histogram\n";
            oss << "==============================\n\n";

            for (size_t i = 0; i < num_buckets; ++i)
            {
                uint64_t bucket_start = min_val + i * bucket_size;
                uint64_t bucket_end = bucket_start + bucket_size - 1;

                // Scale to microseconds for readability
                double start_us = static_cast<double>(bucket_start) / 1000.0;
                double end_us = static_cast<double>(bucket_end) / 1000.0;

                oss << std::fixed << std::setprecision(2);
                oss << std::setw(8) << start_us << "-" << std::setw(8) << end_us << " us |";

                size_t bar_len = max_count > 0 ? (counts[i] * bar_width / max_count) : 0;
                oss << std::string(bar_len, '#');
                oss << " (" << counts[i] << ")\n";
            }

            return oss.str();
        }

        /**
         * @brief Get summary report
         */
        std::string summary() const noexcept
        {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2);

            oss << "\n=== Latency Statistics Summary ===\n";
            oss << "Samples:  " << count() << "\n";
            oss << "Min:      " << min() / 1000.0 << " us\n";
            oss << "Max:      " << max() / 1000.0 << " us\n";
            oss << "Mean:     " << mean() / 1000.0 << " us\n";
            oss << "StdDev:   " << stddev() / 1000.0 << " us\n";
            oss << "\n=== Percentiles ===\n";
            oss << "p50:      " << p50() / 1000.0 << " us\n";
            oss << "p95:      " << p95() / 1000.0 << " us\n";
            oss << "p99:      " << p99() / 1000.0 << " us\n";
            oss << "p99.9:    " << p999() / 1000.0 << " us\n";
            oss << "p99.99:   " << p9999() / 1000.0 << " us\n";
            oss << "=================================\n";

            return oss.str();
        }

        /**
         * @brief Get raw samples (for external analysis)
         */
        const std::vector<uint64_t> &samples() const noexcept
        {
            return samples_;
        }

    private:
        void ensure_sorted() const noexcept
        {
            if (!is_sorted_)
            {
                std::sort(samples_.begin(), samples_.end());
                is_sorted_ = true;
            }
        }

        mutable std::vector<uint64_t> samples_;
        mutable bool is_sorted_ = false;
    };

} // namespace lob::benchmarks
