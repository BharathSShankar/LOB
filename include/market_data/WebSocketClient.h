#pragma once

/**
 * @file WebSocketClient.h
 * @brief WebSocket Client for Live Crypto Market Data Streaming
 *
 * This module provides real-time market data streaming from:
 * - Coinbase Exchange: wss://ws-feed.exchange.coinbase.com
 * - Binance: wss://stream.binance.com:9443/ws/{symbol}@depth
 *
 * Dependencies (choose one):
 * - libwebsockets (recommended for low-latency)
 * - Boost.Beast (if already using Boost)
 * - websocketpp
 *
 * Install on macOS (Apple Silicon M1/M2/M3):
 *   brew install libwebsockets
 *   brew install boost
 */

#include "core/Order.h"
#include "market_data/CryptoDataReplay.h"
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace lob::market_data
{

    /**
     * @brief WebSocket Connection Configuration
     */
    struct WebSocketConfig
    {
        enum class Exchange
        {
            COINBASE,
            BINANCE
        };

        Exchange exchange = Exchange::COINBASE;
        std::string symbol = "BTC-USD"; // Coinbase format
        bool subscribe_l2 = true;       // Level 2 order book
        bool subscribe_trades = true;   // Trade executions
        bool subscribe_ticker = false;  // Ticker updates

        // Price conversion
        uint64_t price_multiplier = 100; // Convert to cents

        // Connection settings
        int reconnect_delay_ms = 1000;
        int ping_interval_ms = 30000;
        int read_timeout_ms = 60000;

        // Get WebSocket URL
        std::string get_url() const
        {
            if (exchange == Exchange::COINBASE)
            {
                return "wss://ws-feed.exchange.coinbase.com";
            }
            else
            {
                // Binance: Convert BTC-USD -> btcusdt
                std::string binance_symbol = symbol;
                // Remove dash and convert to lowercase
                size_t dash = binance_symbol.find('-');
                if (dash != std::string::npos)
                {
                    binance_symbol.erase(dash, 1);
                }
                for (auto &c : binance_symbol)
                    c = static_cast<char>(std::tolower(c));

                return "wss://stream.binance.com:9443/ws/" + binance_symbol + "@depth@100ms";
            }
        }

        // Get subscription message
        std::string get_subscribe_message() const
        {
            if (exchange == Exchange::COINBASE)
            {
                std::string channels = "[";
                bool first = true;

                if (subscribe_l2)
                {
                    channels += "{\"name\":\"level2\",\"product_ids\":[\"" + symbol + "\"]}";
                    first = false;
                }
                if (subscribe_trades)
                {
                    if (!first)
                        channels += ",";
                    channels += "{\"name\":\"matches\",\"product_ids\":[\"" + symbol + "\"]}";
                    first = false;
                }
                if (subscribe_ticker)
                {
                    if (!first)
                        channels += ",";
                    channels += "{\"name\":\"ticker\",\"product_ids\":[\"" + symbol + "\"]}";
                }

                channels += "]";

                return "{\"type\":\"subscribe\",\"channels\":" + channels + "}";
            }
            else
            {
                // Binance uses URL-based subscription, return empty
                return "";
            }
        }
    };

    /**
     * @brief Abstract WebSocket Client Interface
     *
     * This interface allows different WebSocket implementations:
     * - LibWebSocketsClient (recommended)
     * - BoostBeastClient
     * - MockClient (for testing)
     */
    class IWebSocketClient
    {
    public:
        using MessageCallback = std::function<void(const std::string &)>;
        using EventCallback = std::function<void(const MarketDataEvent &)>;
        using ErrorCallback = std::function<void(const std::string &)>;

        virtual ~IWebSocketClient() = default;

        /**
         * @brief Connect to WebSocket server
         * @return true if connection successful
         */
        virtual bool connect() = 0;

        /**
         * @brief Disconnect from server
         */
        virtual void disconnect() = 0;

        /**
         * @brief Check if connected
         */
        virtual bool is_connected() const = 0;

        /**
         * @brief Send a message
         */
        virtual bool send(const std::string &message) = 0;

        /**
         * @brief Set callback for raw messages
         */
        virtual void on_message(MessageCallback callback) = 0;

        /**
         * @brief Set callback for parsed events
         */
        virtual void on_event(EventCallback callback) = 0;

        /**
         * @brief Set callback for errors
         */
        virtual void on_error(ErrorCallback callback) = 0;

        /**
         * @brief Get connection statistics
         */
        struct Stats
        {
            uint64_t messages_received = 0;
            uint64_t bytes_received = 0;
            uint64_t events_parsed = 0;
            uint64_t parse_errors = 0;
            uint64_t reconnect_count = 0;
            double avg_latency_us = 0.0;
        };

        virtual Stats get_stats() const = 0;
    };

    /**
     * @brief Simple WebSocket Client Implementation
     *
     * This is a basic implementation that can be extended
     * with actual WebSocket library integration.
     *
     * For production use, implement with libwebsockets:
     *
     * Install: brew install libwebsockets
     * CMake:   find_package(Libwebsockets REQUIRED)
     *          target_link_libraries(... websockets)
     */
    class SimpleWebSocketClient : public IWebSocketClient
    {
    public:
        explicit SimpleWebSocketClient(const WebSocketConfig &config)
            : config_(config), connected_(false), running_(false) {}

        ~SimpleWebSocketClient() override
        {
            disconnect();
        }

        bool connect() override
        {
            if (connected_)
                return true;

            // NOTE: This is a placeholder implementation.
            // In production, replace with actual WebSocket library:
            //
            // Example with libwebsockets:
            // struct lws_context_creation_info info;
            // memset(&info, 0, sizeof(info));
            // info.port = CONTEXT_PORT_NO_LISTEN;
            // info.protocols = protocols_;
            // context_ = lws_create_context(&info);
            //
            // Example with Boost.Beast:
            // boost::asio::io_context ioc;
            // boost::beast::websocket::stream<tcp::socket> ws(ioc);
            // ws.connect(resolve(config_.get_url()));

            running_ = true;
            connected_ = true;

            // Start receive thread
            receive_thread_ = std::thread(&SimpleWebSocketClient::receive_loop, this);

            // Send subscription message
            if (!config_.get_subscribe_message().empty())
            {
                send(config_.get_subscribe_message());
            }

            return true;
        }

        void disconnect() override
        {
            running_ = false;
            connected_ = false;

            if (receive_thread_.joinable())
            {
                receive_thread_.join();
            }
        }

        bool is_connected() const override
        {
            return connected_;
        }

        bool send(const std::string &message) override
        {
            if (!connected_)
                return false;

            // NOTE: Implement actual send with WebSocket library
            (void)message;
            return true;
        }

        void on_message(MessageCallback callback) override
        {
            message_callback_ = std::move(callback);
        }

        void on_event(EventCallback callback) override
        {
            event_callback_ = std::move(callback);
        }

        void on_error(ErrorCallback callback) override
        {
            error_callback_ = std::move(callback);
        }

        Stats get_stats() const override
        {
            return stats_;
        }

    private:
        void receive_loop()
        {
            while (running_)
            {
                // NOTE: In production, this would read from the WebSocket.
                // For now, we simulate with a sleep.
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                // When a message is received:
                // process_message(raw_message);
            }
        }

        void process_message(const std::string &message)
        {
            stats_.messages_received++;
            stats_.bytes_received += message.size();

            if (message_callback_)
            {
                message_callback_(message);
            }

            // Parse the message
            std::vector<MarketDataEvent> events;
            if (config_.exchange == WebSocketConfig::Exchange::COINBASE)
            {
                events = CoinbaseDataParser::parse_line(message, config_.price_multiplier);
            }
            else
            {
                events = BinanceDataParser::parse_line(message, config_.price_multiplier);
            }

            for (const auto &event : events)
            {
                stats_.events_parsed++;
                if (event_callback_)
                {
                    event_callback_(event);
                }
            }
        }

        WebSocketConfig config_;
        std::atomic<bool> connected_;
        std::atomic<bool> running_;
        std::thread receive_thread_;

        MessageCallback message_callback_;
        EventCallback event_callback_;
        ErrorCallback error_callback_;

        Stats stats_;
    };

    /**
     * @brief Factory function to create WebSocket client
     */
    inline std::unique_ptr<IWebSocketClient> create_websocket_client(
        const WebSocketConfig &config)
    {
        return std::make_unique<SimpleWebSocketClient>(config);
    }

    /**
     * @brief Live Market Data Streamer
     *
     * High-level class that combines WebSocket client with order book updates.
     */
    class LiveMarketDataStreamer
    {
    public:
        using EventCallback = std::function<void(const MarketDataEvent &)>;

        explicit LiveMarketDataStreamer(const WebSocketConfig &config)
            : config_(config), client_(create_websocket_client(config)) {}

        /**
         * @brief Start streaming
         */
        bool start()
        {
            client_->on_event([this](const MarketDataEvent &event)
                              {
                if (event_callback_) {
                    event_callback_(event);
                } });

            client_->on_error([](const std::string &error)
                              { std::cerr << "WebSocket error: " << error << std::endl; });

            return client_->connect();
        }

        /**
         * @brief Stop streaming
         */
        void stop()
        {
            client_->disconnect();
        }

        /**
         * @brief Set event callback
         */
        void set_callback(EventCallback callback)
        {
            event_callback_ = std::move(callback);
        }

        /**
         * @brief Check if streaming
         */
        bool is_streaming() const
        {
            return client_->is_connected();
        }

        /**
         * @brief Get statistics
         */
        IWebSocketClient::Stats get_stats() const
        {
            return client_->get_stats();
        }

    private:
        WebSocketConfig config_;
        std::unique_ptr<IWebSocketClient> client_;
        EventCallback event_callback_;
    };

} // namespace lob::market_data
