#include "profiling/MemoryProfiler.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace lob
{
    namespace profiling
    {

        void MemoryProfiler::record_allocation(void *ptr, size_t size, const std::string &tag)
        {
            // Update stats atomically
            {
                std::lock_guard<std::mutex> lock(allocations_mutex_);
                stats_.alloc_count++;
                stats_.total_bytes_allocated += size;
                stats_.current_memory += size;

                if (stats_.current_memory > stats_.peak_memory)
                {
                    stats_.peak_memory = stats_.current_memory;
                }

                active_allocations_[ptr] = size;
            }

            // Record event if enabled
            if (record_events_)
            {
                std::lock_guard<std::mutex> lock(events_mutex_);
                events_.push_back({get_timestamp_ns(), size, ptr, tag, true});
            }
        }

        void MemoryProfiler::record_deallocation(void *ptr, size_t size, const std::string &tag)
        {
            // Update stats atomically
            {
                std::lock_guard<std::mutex> lock(allocations_mutex_);
                stats_.dealloc_count++;
                stats_.total_bytes_deallocated += size;

                if (stats_.current_memory >= size)
                {
                    stats_.current_memory -= size;
                }

                active_allocations_.erase(ptr);
            }

            // Record event if enabled
            if (record_events_)
            {
                std::lock_guard<std::mutex> lock(events_mutex_);
                events_.push_back({get_timestamp_ns(), size, ptr, tag, false});
            }
        }

        std::vector<MemoryProfiler::AllocationEvent> MemoryProfiler::get_events() const
        {
            std::lock_guard<std::mutex> lock(events_mutex_);
            return events_;
        }

        void MemoryProfiler::reset()
        {
            std::lock_guard<std::mutex> lock1(allocations_mutex_);
            std::lock_guard<std::mutex> lock2(events_mutex_);

            stats_ = AllocationStats{};
            events_.clear();
            active_allocations_.clear();
            session_name_.clear();
        }

        std::string MemoryProfiler::generate_report() const
        {
            std::ostringstream oss;

            oss << "\n=== Memory Profiling Report ===\n\n";

            if (!session_name_.empty())
            {
                oss << "Session: " << session_name_ << "\n";
                auto duration = std::chrono::steady_clock::now() - session_start_;
                auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
                oss << "Duration: " << duration_ms << " ms\n\n";
            }

            // Overall statistics
            oss << "Overall Statistics:\n";
            oss << "  Total Allocations:   " << std::setw(12) << stats_.alloc_count << "\n";
            oss << "  Total Deallocations: " << std::setw(12) << stats_.dealloc_count << "\n";
            oss << "  Net Allocations:     " << std::setw(12)
                << (stats_.alloc_count - stats_.dealloc_count) << "\n\n";

            oss << "Memory Statistics:\n";
            oss << "  Total Allocated:     " << std::setw(12) << stats_.total_bytes_allocated
                << " bytes (" << (stats_.total_bytes_allocated / 1024.0 / 1024.0) << " MB)\n";
            oss << "  Total Deallocated:   " << std::setw(12) << stats_.total_bytes_deallocated
                << " bytes (" << (stats_.total_bytes_deallocated / 1024.0 / 1024.0) << " MB)\n";
            oss << "  Current Memory:      " << std::setw(12) << stats_.current_memory
                << " bytes (" << (stats_.current_memory / 1024.0 / 1024.0) << " MB)\n";
            oss << "  Peak Memory:         " << std::setw(12) << stats_.peak_memory
                << " bytes (" << (stats_.peak_memory / 1024.0 / 1024.0) << " MB)\n\n";

            // Per-allocation statistics
            if (stats_.alloc_count > 0)
            {
                oss << "Memory Efficiency:\n";
                oss << "  Average Allocation Size: "
                    << (stats_.total_bytes_allocated / stats_.alloc_count) << " bytes\n";

                if (stats_.dealloc_count > 0)
                {
                    oss << "  Memory Leak Indicator:   "
                        << (stats_.alloc_count - stats_.dealloc_count) << " unfreed allocations\n";
                }
            }

            // Active allocations
            {
                std::lock_guard<std::mutex> lock(allocations_mutex_);
                if (!active_allocations_.empty())
                {
                    oss << "\nActive Allocations: " << active_allocations_.size() << "\n";
                    size_t total_active = 0;
                    for (const auto &[ptr, size] : active_allocations_)
                    {
                        total_active += size;
                    }
                    oss << "  Total Active Memory: " << total_active << " bytes ("
                        << (total_active / 1024.0 / 1024.0) << " MB)\n";
                }
            }

            // Event summary
            if (record_events_)
            {
                std::lock_guard<std::mutex> lock(events_mutex_);
                if (!events_.empty())
                {
                    oss << "\nRecorded Events: " << events_.size() << "\n";

                    // Calculate allocation rate
                    if (events_.size() > 1)
                    {
                        uint64_t time_span = events_.back().timestamp_ns - events_.front().timestamp_ns;
                        if (time_span > 0)
                        {
                            double rate = (events_.size() * 1e9) / time_span;
                            oss << "  Allocation Rate: " << std::fixed << std::setprecision(2)
                                << rate << " operations/second\n";
                        }
                    }
                }
            }

            oss << "\n===============================\n";

            return oss.str();
        }

        void MemoryProfiler::start_session(const std::string &name)
        {
            session_name_ = name;
            session_start_ = std::chrono::steady_clock::now();
            reset();
        }

        void MemoryProfiler::end_session()
        {
            // Session ends but we keep the data for reporting
        }

    } // namespace profiling
} // namespace lob
