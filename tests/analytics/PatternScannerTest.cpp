#include <gtest/gtest.h>
#include "analytics/PatternScanner.h"

using namespace lob::analytics;

// ============================================================================
// Candle builder helpers
// ============================================================================

static Candle make_candle(double open, double high, double low, double close,
                          uint64_t volume = 1000, uint32_t trades = 10)
{
    Candle c;
    c.open = open;
    c.high = high;
    c.low = low;
    c.close = close;
    c.volume = volume;
    c.trade_count = trades;
    c.timestamp = 0;
    return c;
}

/// Build an ascending price series (uptrend): each candle +step
static std::vector<Candle> make_uptrend(int n, double start = 100.0, double step = 1.0)
{
    std::vector<Candle> cs;
    for (int i = 0; i < n; ++i)
    {
        double o = start + i * step;
        cs.push_back(make_candle(o, o + step * 0.8, o - step * 0.2, o + step * 0.5));
    }
    return cs;
}

/// Build a descending price series (downtrend): each candle -step
static std::vector<Candle> make_downtrend(int n, double start = 100.0, double step = 1.0)
{
    std::vector<Candle> cs;
    for (int i = 0; i < n; ++i)
    {
        double o = start - i * step;
        cs.push_back(make_candle(o, o + step * 0.2, o - step * 0.8, o - step * 0.5));
    }
    return cs;
}

/// Build a flat (sideways) series
static std::vector<Candle> make_sideways(int n, double price = 100.0, double noise = 0.1)
{
    std::vector<Candle> cs;
    for (int i = 0; i < n; ++i)
        cs.push_back(make_candle(price, price + noise, price - noise, price));
    return cs;
}

// ============================================================================
// to_string tests
// ============================================================================

TEST(PatternTypeTest, ToStringReturnsNonEmpty)
{
    EXPECT_EQ(to_string(PatternType::UPTREND), "Uptrend");
    EXPECT_EQ(to_string(PatternType::DOWNTREND), "Downtrend");
    EXPECT_EQ(to_string(PatternType::SIDEWAYS), "Sideways");
    EXPECT_EQ(to_string(PatternType::FLASH_CRASH), "Flash Crash");
    EXPECT_EQ(to_string(PatternType::DOJI), "Doji");
    EXPECT_EQ(to_string(PatternType::BULL_FLAG), "Bull Flag");
    EXPECT_EQ(to_string(PatternType::DOUBLE_TOP), "Double Top");
    EXPECT_EQ(to_string(PatternType::ENGULFING_BULLISH), "Bullish Engulfing");
}

// ============================================================================
// Trend detectors
// ============================================================================

class TrendDetectorTest : public ::testing::Test
{
protected:
    PatternScanner scanner;
};

TEST_F(TrendDetectorTest, DetectsUptrend)
{
    auto candles = make_uptrend(8);
    EXPECT_TRUE(scanner.detect_uptrend(candles));
    EXPECT_FALSE(scanner.detect_downtrend(candles));
}

TEST_F(TrendDetectorTest, DetectsDowntrend)
{
    auto candles = make_downtrend(8);
    EXPECT_TRUE(scanner.detect_downtrend(candles));
    EXPECT_FALSE(scanner.detect_uptrend(candles));
}

TEST_F(TrendDetectorTest, DetectsSideways)
{
    auto candles = make_sideways(8);
    EXPECT_TRUE(scanner.detect_sideways(candles));
    EXPECT_FALSE(scanner.detect_uptrend(candles));
    EXPECT_FALSE(scanner.detect_downtrend(candles));
}

TEST_F(TrendDetectorTest, NoTrend_TooFewCandles)
{
    auto short_up = make_uptrend(3);
    EXPECT_FALSE(scanner.detect_uptrend(short_up));
}

TEST_F(TrendDetectorTest, ScanReturnsUptrendMatch)
{
    auto candles = make_uptrend(10);
    auto matches = scanner.scan(candles);
    bool found = false;
    for (auto &m : matches)
        if (m.pattern == PatternType::UPTREND)
        {
            found = true;
            break;
        }
    EXPECT_TRUE(found);
}

TEST_F(TrendDetectorTest, ScanReturnsDowntrendMatch)
{
    auto candles = make_downtrend(10);
    auto matches = scanner.scan(candles);
    bool found = false;
    for (auto &m : matches)
        if (m.pattern == PatternType::DOWNTREND)
        {
            found = true;
            break;
        }
    EXPECT_TRUE(found);
}

// ============================================================================
// Candlestick pattern detectors
// ============================================================================

class CandlestickPatternTest : public ::testing::Test
{
protected:
    PatternScanner scanner;
};

TEST_F(CandlestickPatternTest, DetectsDoji)
{
    // body = 0.05, range = 4.0 → ratio ≈ 0.0125 < 0.10
    Candle doji = make_candle(100.0, 102.0, 98.0, 100.05);
    EXPECT_TRUE(scanner.detect_doji(doji));
}

TEST_F(CandlestickPatternTest, DoesNotMismatchDoji_LargeBody)
{
    Candle big = make_candle(100.0, 105.0, 98.0, 104.0);
    EXPECT_FALSE(scanner.detect_doji(big));
}

TEST_F(CandlestickPatternTest, DetectsHammer)
{
    // open=close=100, high=100.3 (upper wick=0.3), low=95 (lower wick=5)
    // range=5.3, body=0, lower/range=0.94 (long), upper/range=0.056 < 0.15 ✓
    Candle hammer = make_candle(100.0, 100.3, 95.0, 100.0);
    EXPECT_TRUE(scanner.detect_hammer(hammer));
    EXPECT_FALSE(scanner.detect_shooting_star(hammer));
}

TEST_F(CandlestickPatternTest, DetectsShootingStar)
{
    // open=close=100, low=99.7 (lower wick=0.3), high=105 (upper wick=5)
    // range=5.3, body=0, upper/range=0.94 (long), lower/range=0.056 < 0.15 ✓
    Candle star = make_candle(100.0, 105.0, 99.7, 100.0);
    EXPECT_TRUE(scanner.detect_shooting_star(star));
    EXPECT_FALSE(scanner.detect_hammer(star));
}

TEST_F(CandlestickPatternTest, DetectsBullishEngulfing)
{
    Candle prev = make_candle(105.0, 106.0, 99.0, 100.0); // bearish
    Candle curr = make_candle(99.0, 108.0, 98.0, 107.0);  // bullish, engulfs prev
    EXPECT_TRUE(scanner.detect_bullish_engulfing(prev, curr));
    EXPECT_FALSE(scanner.detect_bearish_engulfing(prev, curr));
}

TEST_F(CandlestickPatternTest, DetectsBearishEngulfing)
{
    Candle prev = make_candle(100.0, 107.0, 99.0, 106.0); // bullish
    Candle curr = make_candle(107.0, 108.0, 98.0, 99.0);  // bearish, engulfs prev
    EXPECT_TRUE(scanner.detect_bearish_engulfing(prev, curr));
    EXPECT_FALSE(scanner.detect_bullish_engulfing(prev, curr));
}

TEST_F(CandlestickPatternTest, BullishEngulfing_RequiresBearishPrev)
{
    Candle prev = make_candle(100.0, 106.0, 99.0, 105.0); // bullish
    Candle curr = make_candle(99.0, 108.0, 98.0, 107.0);  // also bullish
    EXPECT_FALSE(scanner.detect_bullish_engulfing(prev, curr));
}

TEST_F(CandlestickPatternTest, DetectsMorningStar)
{
    Candle c1 = make_candle(110.0, 111.0, 99.0, 100.0);  // strong bearish
    Candle c2 = make_candle(99.5, 101.0, 98.0, 100.0);   // tiny body
    Candle c3 = make_candle(101.0, 112.0, 100.0, 111.0); // strong bullish
    EXPECT_TRUE(scanner.detect_morning_star(c1, c2, c3));
    EXPECT_FALSE(scanner.detect_evening_star(c1, c2, c3));
}

TEST_F(CandlestickPatternTest, DetectsEveningStar)
{
    Candle c1 = make_candle(100.0, 111.0, 99.0, 110.0);  // strong bullish
    Candle c2 = make_candle(110.5, 112.0, 109.0, 110.5); // tiny body
    Candle c3 = make_candle(110.0, 111.0, 98.0, 99.0);   // strong bearish
    EXPECT_TRUE(scanner.detect_evening_star(c1, c2, c3));
    EXPECT_FALSE(scanner.detect_morning_star(c1, c2, c3));
}

// ============================================================================
// Gap detectors
// ============================================================================

class GapPatternTest : public ::testing::Test
{
protected:
    PatternScanner scanner;
};

TEST_F(GapPatternTest, DetectsGapUp)
{
    Candle prev = make_candle(100.0, 102.0, 98.0, 101.0);
    Candle curr = make_candle(103.5, 105.0, 103.0, 104.0); // open > prev.high
    EXPECT_TRUE(scanner.detect_gap_up(prev, curr));
    EXPECT_FALSE(scanner.detect_gap_down(prev, curr));
}

TEST_F(GapPatternTest, DetectsGapDown)
{
    Candle prev = make_candle(100.0, 102.0, 98.0, 99.0);
    Candle curr = make_candle(96.0, 97.0, 95.0, 95.5); // open < prev.low
    EXPECT_TRUE(scanner.detect_gap_down(prev, curr));
    EXPECT_FALSE(scanner.detect_gap_up(prev, curr));
}

TEST_F(GapPatternTest, NoGap_WhenPriceOverlaps)
{
    Candle prev = make_candle(100.0, 102.0, 98.0, 101.0);
    Candle curr = make_candle(101.5, 103.0, 100.5, 102.0);
    EXPECT_FALSE(scanner.detect_gap_up(prev, curr));
    EXPECT_FALSE(scanner.detect_gap_down(prev, curr));
}

// ============================================================================
// Flash Crash detector
// ============================================================================

class FlashCrashTest : public ::testing::Test
{
protected:
    PatternScanner scanner;
};

TEST_F(FlashCrashTest, DetectsFlashCrash_SuddenDrop)
{
    std::vector<Candle> candles;
    for (int i = 0; i < 5; ++i)
        candles.push_back(make_candle(100.0, 101.0, 99.0, 100.0));

    // Sudden crash: high=100 → low=85 in 2 candles (15 %)
    candles.push_back(make_candle(100.0, 100.0, 85.0, 86.0));
    candles.push_back(make_candle(86.0, 87.0, 84.0, 85.0));

    uint32_t s = 0, e = 0;
    EXPECT_TRUE(scanner.detect_flash_crash(candles, s, e));
}

TEST_F(FlashCrashTest, NoFlashCrash_GradualDecline)
{
    auto candles = make_downtrend(10, 100.0, 0.5);
    uint32_t s = 0, e = 0;
    EXPECT_FALSE(scanner.detect_flash_crash(candles, s, e));
}

TEST_F(FlashCrashTest, ScanDetectsFlashCrashPattern)
{
    std::vector<Candle> candles;
    for (int i = 0; i < 5; ++i)
        candles.push_back(make_candle(100.0, 101.0, 99.0, 100.0));
    candles.push_back(make_candle(100.0, 100.0, 85.0, 86.0));

    auto matches = scanner.scan(candles);
    bool found = false;
    for (auto &m : matches)
        if (m.pattern == PatternType::FLASH_CRASH)
        {
            found = true;
            break;
        }
    EXPECT_TRUE(found);
}

// ============================================================================
// Reversal patterns
// ============================================================================

class ReversalPatternTest : public ::testing::Test
{
protected:
    PatternScanner scanner;
};

TEST_F(ReversalPatternTest, DetectsDoubleTop)
{
    // Two clear peaks at ~112, separated by a valley.
    // Need: peak idx ≥ window(1) and peak idx < n-window(1)
    // Series: valley – PEAK1 – valley – PEAK2 – valley  (5 candles, window=1 → i in [1,3])
    std::vector<Candle> cs;
    cs.push_back(make_candle(100.0, 105.0, 99.0, 104.0));  // 0 – pre-peak valley
    cs.push_back(make_candle(104.0, 113.0, 103.0, 112.0)); // 1 – PEAK 1 (high=113)
    cs.push_back(make_candle(112.0, 112.0, 95.0, 96.0));   // 2 – valley
    cs.push_back(make_candle(96.0, 113.5, 95.0, 112.0));   // 3 – PEAK 2 (high=113.5, ~similar)
    cs.push_back(make_candle(112.0, 112.0, 105.0, 106.0)); // 4 – post-peak

    uint32_t s = 0, e = 0;
    EXPECT_TRUE(scanner.detect_double_top(cs, s, e));
}

TEST_F(ReversalPatternTest, DetectsDoubleBottom)
{
    // Two clear troughs at ~88, separated by a peak.
    std::vector<Candle> cs;
    cs.push_back(make_candle(100.0, 101.0, 90.0, 91.0)); // 0 – pre-trough
    cs.push_back(make_candle(91.0, 92.0, 87.5, 89.0));   // 1 – TROUGH 1 (low=87.5)
    cs.push_back(make_candle(89.0, 105.0, 88.0, 104.0)); // 2 – peak
    cs.push_back(make_candle(104.0, 105.0, 87.0, 88.0)); // 3 – TROUGH 2 (low=87.0, ~similar)
    cs.push_back(make_candle(88.0, 96.0, 87.5, 95.0));   // 4 – recovery

    uint32_t s = 0, e = 0;
    EXPECT_TRUE(scanner.detect_double_bottom(cs, s, e));
}

// ============================================================================
// Continuation patterns
// ============================================================================

class ContinuationPatternTest : public ::testing::Test
{
protected:
    PatternScanner scanner;
};

TEST_F(ContinuationPatternTest, DetectsBullFlag)
{
    // Flagpole: 4 candles, each +$2 (total gain ~8 %)
    std::vector<Candle> cs;
    for (int i = 0; i < 4; ++i)
    {
        double base = 100.0 + i * 2.0;
        cs.push_back(make_candle(base, base + 2.2, base - 0.3, base + 2.0));
    }
    // Flag: 4 tight candles (range ~0.3 %)
    for (int i = 0; i < 4; ++i)
        cs.push_back(make_candle(108.0, 108.2, 107.9, 108.0));

    uint32_t s = 0, e = 0;
    EXPECT_TRUE(scanner.detect_bull_flag(cs, s, e));
    EXPECT_EQ(s, 0u);
}

TEST_F(ContinuationPatternTest, DetectsBearFlag)
{
    // Pole: 4 candles, each -$2 (total drop ~8 %)
    std::vector<Candle> cs;
    for (int i = 0; i < 4; ++i)
    {
        double base = 100.0 - i * 2.0;
        cs.push_back(make_candle(base, base + 0.3, base - 2.2, base - 2.0));
    }
    // Flag: 4 tight candles
    for (int i = 0; i < 4; ++i)
        cs.push_back(make_candle(92.0, 92.2, 91.9, 92.0));

    uint32_t s = 0, e = 0;
    EXPECT_TRUE(scanner.detect_bear_flag(cs, s, e));
}

// ============================================================================
// Local extrema helpers
// ============================================================================

class LocalExtremaTest : public ::testing::Test
{
protected:
    PatternScanner scanner;
};

TEST_F(LocalExtremaTest, FindLocalMaxima_SinglePeak)
{
    // Index:     0         1(PEAK)    2          3          4
    //   high:  101         115       109         103        101
    // window=1 → i iterates [1, 3]; peak at i=1: checks j=0(h=101<115) and j=2(h=109<115) ✓
    std::vector<Candle> cs;
    cs.push_back(make_candle(100, 101, 99, 100));  // 0 high=101
    cs.push_back(make_candle(101, 115, 100, 109)); // 1 high=115 – peak
    cs.push_back(make_candle(109, 109, 99, 100));  // 2 high=109
    cs.push_back(make_candle(100, 103, 98, 99));   // 3 high=103
    cs.push_back(make_candle(99, 101, 97, 98));    // 4 high=101

    auto peaks = scanner.find_local_maxima(cs, 1);
    ASSERT_GE(peaks.size(), 1u) << "Expected at least one peak";
    EXPECT_EQ(peaks[0], 1u);
}

TEST_F(LocalExtremaTest, FindLocalMinima_SingleTrough)
{
    // Index:     0          1(TROUGH)    2          3          4
    //   low:    99           85          88          93         97
    // window=1 → i in [1,3]; trough at i=1: j=0(l=99>85) and j=2(l=88>85) ✓
    std::vector<Candle> cs;
    cs.push_back(make_candle(100, 102, 99, 101)); // 0 low=99
    cs.push_back(make_candle(101, 102, 85, 89));  // 1 low=85 – trough
    cs.push_back(make_candle(89, 95, 88, 94));    // 2 low=88
    cs.push_back(make_candle(94, 99, 93, 98));    // 3 low=93
    cs.push_back(make_candle(98, 102, 97, 101));  // 4 low=97

    auto troughs = scanner.find_local_minima(cs, 1);
    ASSERT_GE(troughs.size(), 1u) << "Expected at least one trough";
    EXPECT_EQ(troughs[0], 1u);
}

// ============================================================================
// SMA utility
// ============================================================================

TEST(SMATest, SimpleMovingAverage_Correctness)
{
    PatternScanner scanner;
    std::vector<Candle> cs;
    for (int i = 1; i <= 5; ++i)
        cs.push_back(make_candle(i * 10.0, i * 10.0 + 1, i * 10.0 - 1, i * 10.0));

    auto sma = scanner.calculate_sma(cs, 3);
    ASSERT_EQ(sma.size(), 5u);

    EXPECT_NEAR(sma[2], 20.0, 1e-9); // (10+20+30)/3
    EXPECT_NEAR(sma[3], 30.0, 1e-9); // (20+30+40)/3
    EXPECT_NEAR(sma[4], 40.0, 1e-9); // (30+40+50)/3
}

// ============================================================================
// Edge cases
// ============================================================================

TEST(PatternScannerEdge, EmptyInput_ReturnsEmpty)
{
    PatternScanner scanner;
    EXPECT_TRUE(scanner.scan({}).empty());
}

TEST(PatternScannerEdge, SingleCandle_NoFatalFailure)
{
    PatternScanner scanner;
    EXPECT_NO_FATAL_FAILURE(scanner.scan({make_candle(100, 101, 99, 100)}));
}

TEST(PatternScannerEdge, ConfidenceAlwaysInRange)
{
    PatternScanner scanner;
    auto candles = make_uptrend(15);
    for (auto &m : scanner.scan(candles))
    {
        EXPECT_GE(m.confidence, 0.0);
        EXPECT_LE(m.confidence, 1.0);
    }
}
