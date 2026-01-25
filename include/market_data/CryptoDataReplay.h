#pragma once

#include "core/Order.h"
#include "core/OrderBook.h"
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <functional>
#include <chrono>
#include <thread>
#include <optional>
#include <map>
#include <iomanip>
#include <algorithm>

namespace lob::market_data
{

    /**
     * @brief L2/L3 Market Data Event
     *
     * Represents order book updates from various crypto exchanges.
     */
    struct MarketDataEvent
    {
        enum class Type
        {
            SNAPSHOT,  // Full order book snapshot
            UPDATE,    // Incremental update
            TRADE,     // Trade execution
            L2_UPDATE, // Level 2 (aggregated price level)
            L3_UPDATE  // Level 3 (individual orders)
        };

        Type type;
        std::string symbol;
        uint64_t timestamp_ns; // Nanoseconds since epoch
        core::Side side;       // BUY or SELL
        uint64_t price;        // Price (in smallest units, e.g., satoshis)
        uint64_t quantity;     // Quantity
        std::string order_id;  // Order ID (for L3 data)
        bool is_delete;        // True if this is a deletion
        uint64_t sequence;     // Exchange sequence number

        // Constructor for convenience
        MarketDataEvent() = default;
        MarketDataEvent(Type t, const std::string &sym, uint64_t ts,
                        core::Side s, uint64_t p, uint64_t q)
            : type(t), symbol(sym), timestamp_ns(ts), side(s),
              price(p), quantity(q), is_delete(false), sequence(0) {}
    };

    /**
     * @brief Crypto Data Source Configuration
     */
    struct DataSourceConfig
    {
        enum class Exchange
        {
            COINBASE,
            BINANCE,
            KRAKEN,
            FTX,
            GENERIC_CSV
        };

        Exchange exchange = Exchange::COINBASE;
        std::string file_path;
        std::string symbol = "BTC-USD";
        uint64_t price_multiplier = 100; // Convert to integer cents
        bool replay_trades = true;
        bool replay_orders = true;
        double speed_multiplier = 1.0; // 1.0 = real-time
    };

    /**
     * @brief Coinbase L2/L3 Data Parser
     *
     * Parses Coinbase Exchange order book data formats:
     * - Level 2: Aggregated price levels (bids/asks)
     * - Level 3: Individual orders with order IDs
     *
     * Data Source URLs:
     * - WebSocket: wss://ws-feed.exchange.coinbase.com
     * - REST L2: https://api.exchange.coinbase.com/products/{id}/book?level=2
     * - REST L3: https://api.exchange.coinbase.com/products/{id}/book?level=3
     *
     * File Format (NDJSON - Newline Delimited JSON):
     * {"type":"l2update","product_id":"BTC-USD","time":"2024-01-15T10:30:00.000Z",
     *  "changes":[["buy","42150.50","1.5"],["sell","42151.00","0.8"]]}
     */
    class CoinbaseDataParser
    {
    public:
        /**
         * @brief Parse a single JSON line from Coinbase feed
         * @param line JSON string
         * @param price_multiplier Multiplier to convert to integer price
         * @return Vector of events (may contain multiple for l2update)
         */
        static std::vector<MarketDataEvent> parse_line(
            const std::string &line,
            uint64_t price_multiplier = 100)
        {
            std::vector<MarketDataEvent> events;

            // Simple JSON parsing (production code should use a proper JSON library)
            // Look for "type" field
            auto type_pos = line.find("\"type\"");
            if (type_pos == std::string::npos)
                return events;

            // Parse l2update
            if (line.find("\"l2update\"") != std::string::npos)
            {
                events = parse_l2update(line, price_multiplier);
            }
            // Parse snapshot
            else if (line.find("\"snapshot\"") != std::string::npos)
            {
                events = parse_snapshot(line, price_multiplier);
            }
            // Parse match (trade)
            else if (line.find("\"match\"") != std::string::npos)
            {
                auto event = parse_match(line, price_multiplier);
                if (event.has_value())
                {
                    events.push_back(*event);
                }
            }

            return events;
        }

    private:
        static std::vector<MarketDataEvent> parse_l2update(
            const std::string &line, uint64_t price_multiplier)
        {
            std::vector<MarketDataEvent> events;

            // Find product_id
            std::string symbol = extract_string(line, "product_id");

            // Find time
            uint64_t timestamp = parse_timestamp(extract_string(line, "time"));

            // Find changes array
            auto changes_pos = line.find("\"changes\"");
            if (changes_pos == std::string::npos)
                return events;

            auto array_start = line.find('[', changes_pos);
            auto array_end = line.rfind(']');
            if (array_start == std::string::npos || array_end == std::string::npos)
                return events;

            std::string changes = line.substr(array_start, array_end - array_start + 1);

            // Parse individual changes: [["buy","price","size"],...]
            size_t pos = 0;
            while ((pos = changes.find('[', pos)) != std::string::npos)
            {
                auto end = changes.find(']', pos);
                if (end == std::string::npos)
                    break;

                std::string change = changes.substr(pos + 1, end - pos - 1);

                // Parse "side","price","size"
                std::vector<std::string> parts;
                std::stringstream ss(change);
                std::string part;
                while (std::getline(ss, part, ','))
                {
                    // Remove quotes
                    part.erase(std::remove(part.begin(), part.end(), '"'), part.end());
                    part.erase(std::remove(part.begin(), part.end(), ' '), part.end());
                    parts.push_back(part);
                }

                if (parts.size() >= 3)
                {
                    MarketDataEvent event;
                    event.type = MarketDataEvent::Type::L2_UPDATE;
                    event.symbol = symbol;
                    event.timestamp_ns = timestamp;
                    event.side = (parts[0] == "buy") ? core::Side::BUY : core::Side::SELL;
                    event.price = static_cast<uint64_t>(std::stod(parts[1]) * price_multiplier);
                    event.quantity = static_cast<uint64_t>(std::stod(parts[2]) * 100000000); // BTC to satoshis
                    event.is_delete = (event.quantity == 0);
                    events.push_back(event);
                }

                pos = end + 1;
            }

            return events;
        }

        static std::vector<MarketDataEvent> parse_snapshot(
            const std::string &line, uint64_t price_multiplier)
        {
            std::vector<MarketDataEvent> events;

            std::string symbol = extract_string(line, "product_id");
            uint64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count();

            // Parse bids
            parse_price_levels(line, "bids", symbol, timestamp, core::Side::BUY,
                               price_multiplier, events);

            // Parse asks
            parse_price_levels(line, "asks", symbol, timestamp, core::Side::SELL,
                               price_multiplier, events);

            // Mark first event as snapshot
            if (!events.empty())
            {
                events[0].type = MarketDataEvent::Type::SNAPSHOT;
            }

            return events;
        }

        static std::optional<MarketDataEvent> parse_match(
            const std::string &line, uint64_t price_multiplier)
        {
            MarketDataEvent event;
            event.type = MarketDataEvent::Type::TRADE;
            event.symbol = extract_string(line, "product_id");
            event.timestamp_ns = parse_timestamp(extract_string(line, "time"));

            std::string side_str = extract_string(line, "side");
            event.side = (side_str == "buy") ? core::Side::BUY : core::Side::SELL;

            std::string price_str = extract_string(line, "price");
            std::string size_str = extract_string(line, "size");

            if (price_str.empty() || size_str.empty())
                return std::nullopt;

            event.price = static_cast<uint64_t>(std::stod(price_str) * price_multiplier);
            event.quantity = static_cast<uint64_t>(std::stod(size_str) * 100000000);

            return event;
        }

        static void parse_price_levels(
            const std::string &line,
            const std::string &field,
            const std::string &symbol,
            uint64_t timestamp,
            core::Side side,
            uint64_t price_multiplier,
            std::vector<MarketDataEvent> &events)
        {
            auto field_pos = line.find("\"" + field + "\"");
            if (field_pos == std::string::npos)
                return;

            auto array_start = line.find('[', field_pos);
            if (array_start == std::string::npos)
                return;

            // Find matching closing bracket
            int bracket_count = 1;
            size_t array_end = array_start + 1;
            while (array_end < line.size() && bracket_count > 0)
            {
                if (line[array_end] == '[')
                    bracket_count++;
                else if (line[array_end] == ']')
                    bracket_count--;
                array_end++;
            }

            std::string array_content = line.substr(array_start, array_end - array_start);

            // Parse [["price","size"],...]
            size_t pos = 0;
            while ((pos = array_content.find('[', pos)) != std::string::npos)
            {
                if (pos == 0)
                {
                    pos++;
                    continue;
                }

                auto end = array_content.find(']', pos);
                if (end == std::string::npos)
                    break;

                std::string level = array_content.substr(pos + 1, end - pos - 1);

                // Parse "price","size"
                std::vector<std::string> parts;
                std::stringstream ss(level);
                std::string part;
                while (std::getline(ss, part, ','))
                {
                    part.erase(std::remove(part.begin(), part.end(), '"'), part.end());
                    part.erase(std::remove(part.begin(), part.end(), ' '), part.end());
                    parts.push_back(part);
                }

                if (parts.size() >= 2)
                {
                    MarketDataEvent event;
                    event.type = MarketDataEvent::Type::L2_UPDATE;
                    event.symbol = symbol;
                    event.timestamp_ns = timestamp;
                    event.side = side;
                    event.price = static_cast<uint64_t>(std::stod(parts[0]) * price_multiplier);
                    event.quantity = static_cast<uint64_t>(std::stod(parts[1]) * 100000000);
                    events.push_back(event);
                }

                pos = end + 1;
            }
        }

        static std::string extract_string(const std::string &json, const std::string &key)
        {
            std::string search = "\"" + key + "\"";
            auto pos = json.find(search);
            if (pos == std::string::npos)
                return "";

            auto colon = json.find(':', pos);
            if (colon == std::string::npos)
                return "";

            auto quote_start = json.find('"', colon);
            if (quote_start == std::string::npos)
                return "";

            auto quote_end = json.find('"', quote_start + 1);
            if (quote_end == std::string::npos)
                return "";

            return json.substr(quote_start + 1, quote_end - quote_start - 1);
        }

        static uint64_t parse_timestamp(const std::string &iso_time)
        {
            // Parse ISO 8601: 2024-01-15T10:30:00.000Z
            // For simplicity, use current time if parse fails
            if (iso_time.empty())
            {
                return std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                    .count();
            }

            // Basic parsing (production code should use a proper date library)
            std::tm tm = {};
            std::istringstream ss(iso_time);
            ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");

            auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
            return std::chrono::duration_cast<std::chrono::nanoseconds>(
                       tp.time_since_epoch())
                .count();
        }
    };

    /**
     * @brief Binance Data Parser
     *
     * Parses Binance order book data formats.
     *
     * Data Source URLs:
     * - WebSocket: wss://stream.binance.com:9443/ws/{symbol}@depth
     * - REST: https://api.binance.com/api/v3/depth?symbol={symbol}&limit=5000
     */
    class BinanceDataParser
    {
    public:
        /**
         * @brief Parse a Binance depth update
         * @param line JSON string
         * @param price_multiplier Multiplier for price
         * @return Vector of market data events
         */
        static std::vector<MarketDataEvent> parse_line(
            const std::string &line,
            uint64_t price_multiplier = 100)
        {
            std::vector<MarketDataEvent> events;

            // Parse lastUpdateId
            auto update_id_pos = line.find("\"lastUpdateId\"");
            uint64_t sequence = 0;
            if (update_id_pos != std::string::npos)
            {
                auto colon = line.find(':', update_id_pos);
                auto comma = line.find(',', colon);
                if (colon != std::string::npos && comma != std::string::npos)
                {
                    sequence = std::stoull(line.substr(colon + 1, comma - colon - 1));
                }
            }

            uint64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count();

            // Parse bids: [["price","qty"],...]
            parse_order_book_side(line, "bids", timestamp, sequence,
                                  core::Side::BUY, price_multiplier, events);

            // Parse asks: [["price","qty"],...]
            parse_order_book_side(line, "asks", timestamp, sequence,
                                  core::Side::SELL, price_multiplier, events);

            return events;
        }

    private:
        static void parse_order_book_side(
            const std::string &line,
            const std::string &field,
            uint64_t timestamp,
            uint64_t sequence,
            core::Side side,
            uint64_t price_multiplier,
            std::vector<MarketDataEvent> &events)
        {
            auto field_pos = line.find("\"" + field + "\"");
            if (field_pos == std::string::npos)
                return;

            auto array_start = line.find('[', field_pos);
            if (array_start == std::string::npos)
                return;

            // Find matching closing bracket
            int bracket_count = 1;
            size_t array_end = array_start + 1;
            while (array_end < line.size() && bracket_count > 0)
            {
                if (line[array_end] == '[')
                    bracket_count++;
                else if (line[array_end] == ']')
                    bracket_count--;
                array_end++;
            }

            std::string array_content = line.substr(array_start, array_end - array_start);

            // Parse [["price","qty"],...]
            size_t pos = 0;
            while ((pos = array_content.find('[', pos)) != std::string::npos)
            {
                if (pos == 0)
                {
                    pos++;
                    continue;
                }

                auto end = array_content.find(']', pos);
                if (end == std::string::npos)
                    break;

                std::string level = array_content.substr(pos + 1, end - pos - 1);

                // Parse "price","qty"
                std::vector<std::string> parts;
                std::stringstream ss(level);
                std::string part;
                while (std::getline(ss, part, ','))
                {
                    part.erase(std::remove(part.begin(), part.end(), '"'), part.end());
                    part.erase(std::remove(part.begin(), part.end(), ' '), part.end());
                    parts.push_back(part);
                }

                if (parts.size() >= 2 && !parts[0].empty() && !parts[1].empty())
                {
                    MarketDataEvent event;
                    event.type = MarketDataEvent::Type::L2_UPDATE;
                    event.symbol = "BTCUSDT";
                    event.timestamp_ns = timestamp;
                    event.side = side;
                    event.sequence = sequence;
                    event.price = static_cast<uint64_t>(std::stod(parts[0]) * price_multiplier);
                    event.quantity = static_cast<uint64_t>(std::stod(parts[1]) * 100000000);
                    event.is_delete = (event.quantity == 0);
                    events.push_back(event);
                }

                pos = end + 1;
            }
        }
    };

    /**
     * @brief CSV Data Parser for Generic Historical Data
     *
     * Parses CSV format:
     * timestamp,side,price,quantity
     * 1705312200000,buy,42150.50,1.5
     * 1705312200001,sell,42151.00,0.8
     */
    class CSVDataParser
    {
    public:
        static std::vector<MarketDataEvent> parse_line(
            const std::string &line,
            uint64_t price_multiplier = 100,
            bool has_header = false)
        {
            std::vector<MarketDataEvent> events;

            if (has_header && line.find("timestamp") != std::string::npos)
            {
                return events; // Skip header
            }

            std::vector<std::string> parts;
            std::stringstream ss(line);
            std::string part;
            while (std::getline(ss, part, ','))
            {
                parts.push_back(part);
            }

            if (parts.size() >= 4)
            {
                MarketDataEvent event;
                event.type = MarketDataEvent::Type::L2_UPDATE;
                event.symbol = "GENERIC";
                event.timestamp_ns = std::stoull(parts[0]) * 1000000; // ms to ns
                event.side = (parts[1] == "buy" || parts[1] == "BUY")
                                 ? core::Side::BUY
                                 : core::Side::SELL;
                event.price = static_cast<uint64_t>(std::stod(parts[2]) * price_multiplier);
                event.quantity = static_cast<uint64_t>(std::stod(parts[3]) * 100000000);
                events.push_back(event);
            }

            return events;
        }
    };

    /**
     * @brief Crypto Data Replayer
     *
     * Replays historical order book data from files.
     */
    class CryptoDataReplay
    {
    public:
        using EventCallback = std::function<void(const MarketDataEvent &)>;

        explicit CryptoDataReplay(const DataSourceConfig &config)
            : config_(config) {}

        /**
         * @brief Load data from file
         * @return Number of events loaded
         */
        size_t load_file(const std::string &path)
        {
            std::ifstream file(path);
            if (!file.is_open())
            {
                return 0;
            }

            events_.clear();
            std::string line;

            while (std::getline(file, line))
            {
                if (line.empty())
                    continue;

                std::vector<MarketDataEvent> parsed;

                switch (config_.exchange)
                {
                case DataSourceConfig::Exchange::COINBASE:
                    parsed = CoinbaseDataParser::parse_line(line, config_.price_multiplier);
                    break;
                case DataSourceConfig::Exchange::BINANCE:
                    parsed = BinanceDataParser::parse_line(line, config_.price_multiplier);
                    break;
                case DataSourceConfig::Exchange::GENERIC_CSV:
                    parsed = CSVDataParser::parse_line(line, config_.price_multiplier);
                    break;
                default:
                    parsed = CoinbaseDataParser::parse_line(line, config_.price_multiplier);
                    break;
                }

                for (auto &event : parsed)
                {
                    events_.push_back(std::move(event));
                }
            }

            return events_.size();
        }

        /**
         * @brief Set callback for events during replay
         */
        void set_callback(EventCallback callback)
        {
            callback_ = std::move(callback);
        }

        /**
         * @brief Replay all events (blocking)
         * @param realtime If true, replays at original timestamps
         */
        void replay(bool realtime = false)
        {
            if (events_.empty() || !callback_)
                return;

            uint64_t last_ts = events_[0].timestamp_ns;

            for (const auto &event : events_)
            {
                if (realtime && event.timestamp_ns > last_ts)
                {
                    uint64_t delay_ns = static_cast<uint64_t>(
                        (event.timestamp_ns - last_ts) / config_.speed_multiplier);
                    std::this_thread::sleep_for(std::chrono::nanoseconds(delay_ns));
                }

                callback_(event);
                last_ts = event.timestamp_ns;
            }
        }

        /**
         * @brief Get all loaded events
         */
        const std::vector<MarketDataEvent> &events() const { return events_; }

        /**
         * @brief Get number of events
         */
        size_t size() const { return events_.size(); }

        /**
         * @brief Clear loaded events
         */
        void clear() { events_.clear(); }

    private:
        DataSourceConfig config_;
        std::vector<MarketDataEvent> events_;
        EventCallback callback_;
    };

} // namespace lob::market_data
