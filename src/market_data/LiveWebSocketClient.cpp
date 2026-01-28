/**
 * @file LiveWebSocketClient.cpp
 * @brief Live WebSocket Client Implementation using libwebsockets
 *
 * Connects to Coinbase or Binance WebSocket feeds for real-time market data.
 *
 * Dependencies:
 *   brew install libwebsockets
 */

#ifdef ENABLE_LIVE_WEBSOCKET

#include "market_data/WebSocketClient.h"
#include "market_data/CryptoDataReplay.h"
#include <libwebsockets.h>
#include <cstring>
#include <cstdio>
#include <queue>
#include <mutex>
#include <iostream>
#include <atomic>
#include <thread>

namespace lob::market_data
{

    /**
     * @brief libwebsockets-based WebSocket Client
     */
    class LibWebSocketClient : public IWebSocketClient
    {
    public:
        explicit LibWebSocketClient(const WebSocketConfig &config)
            : config_(config), context_(nullptr), wsi_(nullptr),
              connected_(false), running_(false) {}

        ~LibWebSocketClient() override
        {
            disconnect();
        }

        bool connect() override
        {
            if (connected_)
                return true;

            // Parse URL
            std::string url = config_.get_url();
            std::string host, path;
            int port = 443;
            bool use_ssl = true;

            // Parse wss://host:port/path
            size_t proto_end = url.find("://");
            if (proto_end != std::string::npos)
            {
                std::string protocol = url.substr(0, proto_end);
                use_ssl = (protocol == "wss");
                url = url.substr(proto_end + 3);
            }

            size_t path_start = url.find('/');
            if (path_start != std::string::npos)
            {
                host = url.substr(0, path_start);
                path = url.substr(path_start);
            }
            else
            {
                host = url;
                path = "/";
            }

            size_t port_start = host.find(':');
            if (port_start != std::string::npos)
            {
                port = std::stoi(host.substr(port_start + 1));
                host = host.substr(0, port_start);
            }
            else
            {
                port = use_ssl ? 443 : 80;
            }

            host_ = host;
            path_ = path;
            port_ = port;
            use_ssl_ = use_ssl;

            std::cout << "Connecting to: " << host_ << ":" << port_ << path_ << std::endl;

            // Silence verbose libwebsockets logging (only show errors)
            lws_set_log_level(LLL_ERR, nullptr);

            // Create libwebsockets context
            struct lws_context_creation_info info;
            memset(&info, 0, sizeof(info));

            static struct lws_protocols protocols[] = {
                {"lob-protocol",
                 &LibWebSocketClient::callback_static,
                 sizeof(LibWebSocketClient *),
                 65536,
                 0,
                 nullptr,
                 0},
                LWS_PROTOCOL_LIST_TERM};

            info.port = CONTEXT_PORT_NO_LISTEN;
            info.protocols = protocols;
            info.gid = -1;
            info.uid = -1;
            info.user = this;
            info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
            info.timeout_secs = 30;

            // Set CA certificate path for SSL verification
            // Try multiple possible locations on macOS
#ifdef __APPLE__
            // Homebrew OpenSSL locations
            static const char *ca_paths[] = {
                "/opt/homebrew/etc/openssl@3/cert.pem",
                "/opt/homebrew/etc/openssl/cert.pem",
                "/usr/local/etc/openssl@3/cert.pem",
                "/usr/local/etc/openssl/cert.pem",
                "/etc/ssl/cert.pem",
                nullptr};

            for (const char **path = ca_paths; *path != nullptr; ++path)
            {
                FILE *f = fopen(*path, "r");
                if (f)
                {
                    fclose(f);
                    info.client_ssl_ca_filepath = *path;
                    std::cout << "Using CA bundle: " << *path << std::endl;
                    break;
                }
            }
#else
            info.client_ssl_ca_filepath = "/etc/ssl/certs/ca-certificates.crt";
#endif

            context_ = lws_create_context(&info);
            if (!context_)
            {
                std::cerr << "Failed to create libwebsockets context" << std::endl;
                return false;
            }

            // Connect
            struct lws_client_connect_info ccinfo;
            memset(&ccinfo, 0, sizeof(ccinfo));
            ccinfo.context = context_;
            ccinfo.address = host_.c_str();
            ccinfo.port = port_;
            ccinfo.path = path_.c_str();
            ccinfo.host = host_.c_str();
            ccinfo.origin = host_.c_str();
            ccinfo.protocol = "lob-protocol";
            ccinfo.userdata = this;
            ccinfo.pwsi = &wsi_;
            // Not using ALPN - can cause issues with some servers
            ccinfo.alpn = nullptr;

            if (use_ssl_)
            {
                // Use SSL with full verification
                ccinfo.ssl_connection = LCCSCF_USE_SSL |
                                        LCCSCF_ALLOW_SELFSIGNED |
                                        LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
            }

            wsi_ = lws_client_connect_via_info(&ccinfo);
            if (!wsi_)
            {
                std::cerr << "Failed to create WebSocket connection" << std::endl;
                lws_context_destroy(context_);
                context_ = nullptr;
                return false;
            }

            running_ = true;

            // Start service thread
            service_thread_ = std::thread(&LibWebSocketClient::service_loop, this);

            // Wait for connection (with timeout)
            for (int i = 0; i < 50 && !connected_; ++i)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            if (!connected_)
            {
                std::cerr << "Connection timeout" << std::endl;
                disconnect();
                return false;
            }

            return true;
        }

        void disconnect() override
        {
            running_ = false;
            connected_ = false;

            if (service_thread_.joinable())
            {
                service_thread_.join();
            }

            if (context_)
            {
                lws_context_destroy(context_);
                context_ = nullptr;
            }
            wsi_ = nullptr;
        }

        bool is_connected() const override
        {
            return connected_;
        }

        bool send(const std::string &message) override
        {
            if (!connected_ || !wsi_)
                return false;

            std::lock_guard<std::mutex> lock(send_mutex_);
            send_queue_.push(message);
            lws_callback_on_writable(wsi_);
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
        void service_loop()
        {
            while (running_)
            {
                if (context_)
                {
                    lws_service(context_, 50);
                }
                else
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }
        }

        static int callback_static(struct lws *wsi, enum lws_callback_reasons reason,
                                   void *user, void *in, size_t len)
        {
            // Get the client instance from context user data
            struct lws_context *ctx = lws_get_context(wsi);
            LibWebSocketClient *client = nullptr;

            if (ctx)
            {
                client = static_cast<LibWebSocketClient *>(
                    lws_context_user(ctx));
            }

            if (client)
            {
                return client->callback(wsi, reason, user, in, len);
            }

            return 0;
        }

        int callback(struct lws *wsi, enum lws_callback_reasons reason,
                     void * /*user*/, void *in, size_t len)
        {
            switch (reason)
            {
            case LWS_CALLBACK_CLIENT_ESTABLISHED:
                std::cout << "WebSocket connection established" << std::endl;
                connected_ = true;
                wsi_ = wsi;

                // Send subscription message
                {
                    std::string sub_msg = config_.get_subscribe_message();
                    if (!sub_msg.empty())
                    {
                        send(sub_msg);
                    }
                }
                break;

            case LWS_CALLBACK_CLIENT_RECEIVE:
                if (in && len > 0)
                {
                    std::string message(static_cast<char *>(in), len);
                    process_message(message);
                }
                break;

            case LWS_CALLBACK_CLIENT_WRITEABLE:
            {
                std::lock_guard<std::mutex> lock(send_mutex_);
                if (!send_queue_.empty())
                {
                    std::string &msg = send_queue_.front();

                    // Prepare buffer with LWS_PRE padding
                    std::vector<unsigned char> buf(LWS_PRE + msg.size());
                    memcpy(&buf[LWS_PRE], msg.c_str(), msg.size());

                    int written = lws_write(wsi, &buf[LWS_PRE], msg.size(), LWS_WRITE_TEXT);
                    if (written < 0)
                    {
                        std::cerr << "Error writing to WebSocket" << std::endl;
                    }

                    send_queue_.pop();

                    if (!send_queue_.empty())
                    {
                        lws_callback_on_writable(wsi);
                    }
                }
            }
            break;

            case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
                if (in && len > 0)
                {
                    std::string error(static_cast<char *>(in), len);
                    std::cerr << "WebSocket connection error: " << error << std::endl;
                    if (error_callback_)
                    {
                        error_callback_(error);
                    }
                }
                else
                {
                    std::cerr << "WebSocket connection error (no details)" << std::endl;
                }
                connected_ = false;
                break;

            case LWS_CALLBACK_CLIENT_CLOSED:
                std::cout << "WebSocket connection closed" << std::endl;
                connected_ = false;
                break;

            default:
                break;
            }

            return 0;
        }

        void process_message(const std::string &message)
        {
            stats_.messages_received++;
            stats_.bytes_received += message.size();

            if (message_callback_)
            {
                message_callback_(message);
            }

            // Parse the message based on exchange
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
        struct lws_context *context_;
        struct lws *wsi_;

        std::atomic<bool> connected_;
        std::atomic<bool> running_;
        std::thread service_thread_;

        std::string host_;
        std::string path_;
        int port_ = 443;
        bool use_ssl_ = true;

        std::mutex send_mutex_;
        std::queue<std::string> send_queue_;

        MessageCallback message_callback_;
        EventCallback event_callback_;
        ErrorCallback error_callback_;

        Stats stats_;
    };

    // Factory function for live WebSocket client
    std::unique_ptr<IWebSocketClient> create_live_websocket_client(
        const WebSocketConfig &config)
    {
        return std::make_unique<LibWebSocketClient>(config);
    }

} // namespace lob::market_data

#else

// Stub when libwebsockets is not available
#include "market_data/WebSocketClient.h"
#include <iostream>

namespace lob::market_data
{
    std::unique_ptr<IWebSocketClient> create_live_websocket_client(
        const WebSocketConfig &config)
    {
        std::cerr << "Live WebSocket support not enabled. Build with -DENABLE_LIVE_WEBSOCKET=ON" << std::endl;
        return create_websocket_client(config);
    }
}

#endif // ENABLE_LIVE_WEBSOCKET
