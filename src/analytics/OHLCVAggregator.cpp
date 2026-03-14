#include "analytics/OHLCVAggregator.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>

namespace lob::analytics
{

    // ============================================================================
    // Candle Implementation
    // ============================================================================

    Candle::Candle() noexcept
        : timestamp(0), open(0.0), high(0.0), low(0.0), close(0.0),
          volume(0), trade_count(0)
    {
    }

    bool Candle::is_doji(double threshold) const noexcept
    {
        double r = range();
        if (r <= 0.0)
            return false;
        return (body_size() / r) < threshold;
    }

    // ============================================================================
    // OHLCVAggregator Implementation
    // ============================================================================

    OHLCVAggregator::OHLCVAggregator(uint64_t candle_period_ms,
                                     double price_scale) noexcept
        : candle_period_ns_(candle_period_ms * 1'000'000ULL),
          price_scale_(price_scale),
          total_trades_(0)
    {
    }

    double OHLCVAggregator::to_price(uint64_t raw_price) const noexcept
    {
        return static_cast<double>(raw_price) / price_scale_;
    }

    uint64_t OHLCVAggregator::align_timestamp(uint64_t ts) const noexcept
    {
        return (ts / candle_period_ns_) * candle_period_ns_;
    }

    void OHLCVAggregator::start_new_candle(uint64_t aligned_timestamp) noexcept
    {
        current_candle_ = Candle{};
        current_candle_.timestamp = aligned_timestamp;
    }

    void OHLCVAggregator::finalize_current_candle()
    {
        if (current_candle_.trade_count == 0)
            return;

        completed_candles_.push_back(current_candle_);

        // Rolling buffer – discard oldest if over limit
        if (completed_candles_.size() > MAX_CANDLES)
        {
            completed_candles_.pop_front();
        }
    }

    void OHLCVAggregator::add_trade(const lob::core::Trade &trade)
    {
        double price = to_price(trade.price);

        // ── First trade ever (or first after reset) ───────────────────────────
        // We use total_trades_ == 0 as the "not yet initialised" sentinel because
        // the first candle's aligned timestamp can legitimately be 0.
        if (total_trades_ == 0)
        {
            start_new_candle(align_timestamp(trade.timestamp));
        }
        // ── Candle period elapsed ─────────────────────────────────────────────
        else if (trade.timestamp >= current_candle_.timestamp + candle_period_ns_)
        {
            finalize_current_candle();
            start_new_candle(align_timestamp(trade.timestamp));
        }

        // ── Update OHLCV ──────────────────────────────────────────────────────
        if (current_candle_.trade_count == 0)
        {
            current_candle_.open = price;
            current_candle_.high = price;
            current_candle_.low = price;
        }
        else
        {
            if (price > current_candle_.high)
                current_candle_.high = price;
            if (price < current_candle_.low)
                current_candle_.low = price;
        }

        current_candle_.close = price;
        current_candle_.volume += trade.quantity;
        ++current_candle_.trade_count;
        ++total_trades_;
    }

    std::vector<Candle> OHLCVAggregator::get_candles(uint32_t max_count) const
    {
        size_t count = std::min(static_cast<size_t>(max_count),
                                completed_candles_.size());
        if (count == 0)
            return {};

        auto begin = completed_candles_.end() - static_cast<std::ptrdiff_t>(count);
        return std::vector<Candle>(begin, completed_candles_.end());
    }

    void OHLCVAggregator::flush()
    {
        finalize_current_candle();
        current_candle_ = Candle{};
    }

    void OHLCVAggregator::reset() noexcept
    {
        current_candle_ = Candle{};
        completed_candles_.clear();
        total_trades_ = 0;
    }

    bool OHLCVAggregator::export_to_csv(const std::string &filename) const
    {
        std::ofstream file(filename);
        if (!file.is_open())
            return false;

        file << "timestamp,open,high,low,close,volume,trade_count\n";
        file << std::fixed << std::setprecision(4);

        for (const auto &c : completed_candles_)
        {
            file << c.timestamp << ','
                 << c.open << ','
                 << c.high << ','
                 << c.low << ','
                 << c.close << ','
                 << c.volume << ','
                 << c.trade_count << '\n';
        }

        // Include partially-formed current candle at the end
        if (current_candle_.trade_count > 0)
        {
            const auto &c = current_candle_;
            file << c.timestamp << ','
                 << c.open << ','
                 << c.high << ','
                 << c.low << ','
                 << c.close << ','
                 << c.volume << ','
                 << c.trade_count << '\n';
        }

        return true;
    }

} // namespace lob::analytics
