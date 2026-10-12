#pragma once
#include "Consts.h"
#include "NetworkUtils.h"
#include "ConfigReader.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <string>

namespace net = boost::asio;
namespace ssl = net::ssl;
namespace beast = boost::beast;
namespace http = boost::beast::http;
using tcp = net::ip::tcp;

class Logger;

enum class ImageUploadedParamT
{
    All = 0,
    Uploaded = 1,
    NotUploaded = 2
};

class HttpsClient
{
public:
    HttpsClient(
        Config config,
        net::io_context* io,
        Logger* logger);

    net::awaitable<int> create_event(
        std::string filename,
        int event_code);

    net::awaitable<void> upload_image(
        uint32_t event_id,
        const std::string& filepath);

    net::awaitable<std::string> list_events(
        int limit = 20,
        int offset = 0,
        ImageUploadedParamT image_uploaded = ImageUploadedParamT::All);

    net::awaitable<std::string> server_status();

    net::awaitable<void> Stop();

private:
    net::awaitable<void> connect();
    void close_connection();

    // Helpers
    http::request<http::string_body> create_post_request(
        std::string sub_url,
        std::string content_type,
        std::string body);

    template<typename request_body_t>
    net::awaitable<http::response<http::string_body>> send_request(
        http::request<request_body_t> request)
    {
        co_await connect();
        auto& tcp_stream = beast::get_lowest_layer(*m_stream);
        tcp_stream.expires_after(REQUEST_TIMEOUT);

        co_await http::async_write(
            *m_stream, request, net::use_awaitable);

        tcp_stream.expires_after(REQUEST_TIMEOUT);

        beast::flat_buffer buffer;
        http::response_parser<http::string_body> parser;
        parser.body_limit(MAX_RESPONSE_SIZE);
        co_await http::async_read(
            *m_stream, buffer, parser, net::use_awaitable);

        tcp_stream.expires_never();
        co_return parser.release();
    }

    template<typename request_body_t>
    net::awaitable<http::response<http::string_body>> send_request_with_attempts(
        http::request<request_body_t> request)
    {
        http::response<http::string_body> response;
        net::steady_timer timer(co_await net::this_coro::executor);
        const int max_attempts = 3;
        for (int attempt = 0; attempt < max_attempts; ++attempt)
        {
            try
            {
                response = co_await send_request(request);
            }
            catch (const boost::system::system_error& ex)
            {
                close_connection();
                if (is_retryable_network_error(ex.code()) && attempt < max_attempts - 1)
                {
                    continue;
                }

                throw;
            }
            catch (const std::exception&)
            {
                close_connection();
                throw;
            }

            if (!response.keep_alive())
            {
                close_connection();
                co_return response;
            }

            if (response.result() != http::status::too_many_requests)
            {
                break;
            }

            co_await wait_rate_limit(response, timer, m_logger);
        }
            
        co_return response;
    }

private:
    Config m_config;
    net::io_context* m_io;
    tcp::resolver m_resolver;
    ssl::context m_ssl_context;
    std::optional<beast::ssl_stream<beast::tcp_stream>> m_stream;
    std::string m_host;
    std::string m_port;
    std::string m_api;
    Logger* m_logger;

    bool m_connected = false;
    bool m_request_in_progress = false;
    bool m_stopping = false;
};