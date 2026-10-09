#pragma once
#include "ConfigReader.h"
#include "Consts.h"

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

enum class ImageUploadedParamT
{
    All,
    Uploaded,
    NotUploaded
};

class HttpsClient
{
public:
    HttpsClient(
        Config config,
        net::io_context& io);

    net::awaitable<int> create_event(
        std::string filename,
        int event_code);

    net::awaitable<void> upload_image(
        uint32_t event_id,
        std::wstring filepath);

    net::awaitable<std::string> list_events(
        int limit = 20,
        int offset = 0,
        ImageUploadedParamT image_uploaded = ImageUploadedParamT::All);

    net::awaitable<std::string> server_status();

    net::awaitable<void> Stop();

private:
    net::awaitable<void> Connect();

    // Helpers
    template<typename request_body_t>
    http::request<request_body_t> create_basic_request(
        std::string sub_url,
        http::verb verb)
    {
        http::request<request_body_t> request
        {
            verb,
            sub_url,
            11
        };

        request.set(http::field::host, m_host);
        request.set(http::field::accept, "application/json");
        request.set(http::field::user_agent, "MyAgent/1.0");
        request.set("X-API-Key", m_config.m_api_key);
        request.keep_alive(true);

        return request;
    }

    http::request<http::string_body> create_post_request(
        std::string sub_url,
        std::string content_type,
        std::string body);

    template<typename request_body_t>
    net::awaitable<http::response<http::string_body>> send_request(
        http::request<request_body_t> request)
    {
        auto& tcp_stream = beast::get_lowest_layer(m_stream);

        http::response_parser<http::string_body> parser;
        parser.body_limit(MAX_RESPONSE_SIZE);
        try
        {
            tcp_stream.expires_after(REQUEST_TIMEOUT);

            co_await http::async_write(
                m_stream, request, net::use_awaitable);

            tcp_stream.expires_after(REQUEST_TIMEOUT);

            beast::flat_buffer buffer;
            co_await http::async_read(
                m_stream, buffer, parser, net::use_awaitable);

            tcp_stream.expires_never();
        }
        catch (std::exception&)
        {
            m_connected = false;

            beast::error_code ec;
            tcp_stream.socket().close(ec);

            throw;
        }

        auto response = parser.release();
        if (!response.keep_alive())
        {
            m_connected = false;

            beast::error_code ec;
            tcp_stream.socket().close(ec);
        }

        co_return response;
    }

private:
    Config m_config;

    std::string m_host;
    std::string m_port;

    ssl::context m_ssl_context;
    tcp::resolver m_resolver;
    beast::ssl_stream<beast::tcp_stream> m_stream;

    bool m_connected = false;
    bool m_request_in_progress = false;
    bool m_stopping = false;
};

