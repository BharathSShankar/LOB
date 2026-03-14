#include <gtest/gtest.h>
#include <cstdio>
#include <string>
#include "analytics/OHLCVAggregator.h"

using namespace lob::analytics;
using lob::core::Trade;

// ============================================================================
// Helpers
// ============================================================================

/// Build a Trade from raw (integer-scaled) price, quantity and nanosecond timestamp.
static Trade make_trade(uint64_t price, uint64_t qty, uint64_t timestamp_ns)
{
    Trade t{};
    t.buy_order_id = 1;
    t.sell_order_id = 2;
    t.price = price;
    t.quantity = qty;
    t.timestamp = timestamp_ns;
    return t;
}

// price_scale = 100 → raw price 10000 == $100.00
static constexpr double SCALE = 100.0;
static constexpr uint64_t ONE_SEC_NS = 1'000'000'000ULL;

// ============================================================================
// Candle struct tests
// ============================================================================

class CandleTest : public ::testing::Test
{
};

TEST_F(CandleTest, DefaultCandle_IsZero)
{
    Candle c;
    EXPECT_EQ(c.timestamp, 0u);
    EXPECT_DOUBLE_EQ(c.open, 0.0);
    EXPECT_DOUBLE_EQ(c.high, 0.0);
    EXPECT_DOUBLE_EQ(c.low, 0.0);
    EXPECT_DOUBLE_EQ(c.close, 0.0);
    EXPECT_EQ(c.volume, 0u);
    EXPECT_EQ(c.trade_count, 0u);
}

TEST_F(CandleTest, BullishBearish)
{
    Candle bull;
    bull.open = 100.0;
    bull.close = 105.0;
    EXPECT_TRUE(bull.is_bullish());
    EXPECT_FALSE(bull.is_bearish());

    Candle bear;
    bear.open = 105.0;
    bear.close = 100.0;
    EXPECT_FALSE(bear.is_bullish());
    EXPECT_TRUE(bear.is_bearish());
}

TEST_F(CandleTest, BodySizeAndWicks)
{
    Candle c;
    c.open = 98.0;
    c.close = 102.0;
    c.high = 104.0;
    c.low = 95.0;

    EXPECT_DOUBLE_EQ(c.body_size(), 4.0);
    EXPECT_DOUBLE_EQ(c.upper_wick(), 2.0);
    EXPECT_DOUBLE_EQ(c.lower_wick(), 3.0);
    EXPECT_DOUBLE_EQ(c.range(), 9.0);
}

TEST_F(CandleTest, Doji_SmallBodyRatio)
{
    Candle c;
    c.open = 100.0;
    c.close = 100.05; // tiny body
    c.high = 102.0;
    c.low = 98.0; // range = 4.0 → body/range ≈ 0.0125 < 0.10
    EXPECT_TRUE(c.is_doji());
}

TEST_F(CandleTest, Doji_LargeBodyNotDoji)
{
    Candle c;
    c.open = 100.0;
    c.close = 101.0; // body=1, range=4 → ratio=0.25 > 0.10
    c.high = 102.0;
    c.low = 98.0;
    EXPECT_FALSE(c.is_doji());
}

// ============================================================================
// OHLCVAggregator core behaviour
// ============================================================================

class OHLCVAggregatorTest : public ::testing::Test
{
protected:
    // 1-second candles, price scale = 100
    OHLCVAggregator agg{1000, SCALE};
};

TEST_F(OHLCVAggregatorTest, InitialState_NoData)
{
    EXPECT_EQ(agg.candle_count(), 0u);
    EXPECT_EQ(agg.total_trade_count(), 0u);
    EXPECT_EQ(agg.get_candles().size(), 0u);
}

TEST_F(OHLCVAggregatorTest, SingleTrade_UpdatesCurrentCandle)
{
    agg.add_trade(make_trade(10000, 50, 0)); // $100.00, qty=50

    auto cur = agg.get_current_candle();
    EXPECT_DOUBLE_EQ(cur.open, 100.0);
    EXPECT_DOUBLE_EQ(cur.high, 100.0);
    EXPECT_DOUBLE_EQ(cur.low, 100.0);
    EXPECT_DOUBLE_EQ(cur.close, 100.0);
    EXPECT_EQ(cur.volume, 50u);
    EXPECT_EQ(cur.trade_count, 1u);
    EXPECT_EQ(agg.candle_count(), 0u); // still forming
    EXPECT_EQ(agg.total_trade_count(), 1u);
}

TEST_F(OHLCVAggregatorTest, MultipleTradesInOnePeriod_OHLCV)
{
    agg.add_trade(make_trade(10000, 100, 0));           // $100
    agg.add_trade(make_trade(10500, 200, 100'000'000)); // $105 (+0.1s)
    agg.add_trade(make_trade(9800, 150, 500'000'000));  // $98  (+0.5s)
    agg.add_trade(make_trade(10200, 50, 900'000'000));  // $102 (+0.9s)

    auto c = agg.get_current_candle();
    EXPECT_DOUBLE_EQ(c.open, 100.0);
    EXPECT_DOUBLE_EQ(c.high, 105.0);
    EXPECT_DOUBLE_EQ(c.low, 98.0);
    EXPECT_DOUBLE_EQ(c.close, 102.0);
    EXPECT_EQ(c.volume, 500u);
    EXPECT_EQ(c.trade_count, 4u);
}

TEST_F(OHLCVAggregatorTest, NewPeriod_FinalizesCurrentAndOpensNew)
{
    agg.add_trade(make_trade(10000, 100, 0));          // candle 0
    agg.add_trade(make_trade(10500, 100, ONE_SEC_NS)); // candle 1 (new period)

    EXPECT_EQ(agg.candle_count(), 1u);

    auto prev = agg.get_candles(1).front();
    EXPECT_DOUBLE_EQ(prev.open, 100.0);
    EXPECT_DOUBLE_EQ(prev.close, 100.0);

    auto cur = agg.get_current_candle();
    EXPECT_DOUBLE_EQ(cur.open, 105.0);
    EXPECT_DOUBLE_EQ(cur.close, 105.0);
}

TEST_F(OHLCVAggregatorTest, MultipleCandles_GetCandlesCountRespected)
{
    // Feed trades across 5 one-second periods
    for (uint32_t i = 0; i < 5; ++i)
        agg.add_trade(make_trade(10000 + i * 100, 100,
                                 static_cast<uint64_t>(i) * ONE_SEC_NS));

    EXPECT_EQ(agg.candle_count(), 4u); // 5th trade is in forming candle

    auto all = agg.get_candles(100);
    auto last2 = agg.get_candles(2);
    EXPECT_EQ(all.size(), 4u);
    EXPECT_EQ(last2.size(), 2u);
    EXPECT_DOUBLE_EQ(last2[0].open, all[2].open);
    EXPECT_DOUBLE_EQ(last2[1].open, all[3].open);
}

TEST_F(OHLCVAggregatorTest, TimestampAlignedToPeriod)
{
    // Trade at 1.5 s should belong to candle starting at 1 s
    agg.add_trade(make_trade(10000, 10, 0));
    agg.add_trade(make_trade(10100, 10, ONE_SEC_NS + ONE_SEC_NS / 2));

    EXPECT_EQ(agg.candle_count(), 1u);
    auto cur = agg.get_current_candle();
    EXPECT_EQ(cur.timestamp, ONE_SEC_NS); // aligned to 1 s boundary
}

TEST_F(OHLCVAggregatorTest, Flush_FinalizesCurrent)
{
    agg.add_trade(make_trade(10000, 100, 0));
    EXPECT_EQ(agg.candle_count(), 0u);

    agg.flush();
    EXPECT_EQ(agg.candle_count(), 1u);
    EXPECT_EQ(agg.get_current_candle().trade_count, 0u);
}

TEST_F(OHLCVAggregatorTest, Reset_ClearsAllData)
{
    agg.add_trade(make_trade(10000, 100, 0));
    agg.add_trade(make_trade(10100, 100, ONE_SEC_NS));
    agg.reset();

    EXPECT_EQ(agg.candle_count(), 0u);
    EXPECT_EQ(agg.total_trade_count(), 0u);
    EXPECT_EQ(agg.get_current_candle().trade_count, 0u);
}

TEST_F(OHLCVAggregatorTest, TotalTradeCount_Cumulative)
{
    for (int i = 0; i < 7; ++i)
        agg.add_trade(make_trade(10000, 1,
                                 static_cast<uint64_t>(i) * 100'000'000));
    EXPECT_EQ(agg.total_trade_count(), 7u);
}

TEST_F(OHLCVAggregatorTest, ExportToCSV_CreatesFile)
{
    agg.add_trade(make_trade(10000, 100, 0));
    agg.add_trade(make_trade(10500, 100, ONE_SEC_NS));
    agg.flush();

    const char *path = "/tmp/test_ohlcv_export.csv";
    EXPECT_TRUE(agg.export_to_csv(path));

    // Verify file exists and has expected header using C stdio
    FILE *f = std::fopen(path, "r");
    ASSERT_NE(f, nullptr);

    char buf[256] = {};
    char *first = std::fgets(buf, static_cast<int>(sizeof(buf)), f);
    ASSERT_NE(first, nullptr);

    // Strip trailing newline
    std::string header(buf);
    while (!header.empty() && (header.back() == '\n' || header.back() == '\r'))
        header.pop_back();
    EXPECT_EQ(header, "timestamp,open,high,low,close,volume,trade_count");

    // Count data rows
    int rows = 0;
    while (std::fgets(buf, static_cast<int>(sizeof(buf)), f))
        ++rows;
    EXPECT_GE(rows, 1);

    std::fclose(f);
}

TEST_F(OHLCVAggregatorTest, ExportToCSV_BadPath_ReturnsFalse)
{
    EXPECT_FALSE(agg.export_to_csv("/nonexistent_directory/test.csv"));
}
