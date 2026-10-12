#include "pch.h"
#include "Client.h"
#include "HttpError.h"
#include "NetworkUtils.h"
#include "ConfigReader.h"
#include "Logger.h"

#include <windows.h>
#include <wincrypt.h>
#include <openssl/err.h>
#include <openssl/x509.h>

#pragma comment(lib, "Crypt32.lib")

namespace
{
    struct FlagGuard
    {
        FlagGuard(bool& busy)
            : m_busy(busy)
        {
            if (busy == true)
                throw std::invalid_argument("FlagGuard: busy flag is already set to true");

            busy = true;
        }

        ~FlagGuard()
        {
            m_busy = false;
        }

        bool& m_busy;
    };

    std::string generate_uuid()
    {
        return boost::uuids::to_string(
            boost::uuids::random_generator()());
    }

    std::string get_timestamp()
    {
        const auto now = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now());

        std::tm utc;
        if (gmtime_s(&utc, &now) != 0)
            throw std::runtime_error("Cannot get UTC time");

        char buffer[21];
        if (std::strftime(
            buffer, 
            sizeof(buffer),
            "%Y-%m-%dT%H:%M:%SZ", 
            &utc) == 0)
        {
            throw std::runtime_error("Cannot format UTC time");
        }

        return buffer;
    }

    std::string image_uploaded_param_to_str(ImageUploadedParamT param)
    {
        switch (param)
        {
        case ImageUploadedParamT::All:
            return "all";
        case ImageUploadedParamT::NotUploaded:
            return "false";
        case ImageUploadedParamT::Uploaded:
            return "true";
        }

        std::stringstream ss;
        ss << "image_uploaded_param_to_str. Unknown value of param: ";
        ss << static_cast<int>(param);
        throw std::invalid_argument(ss.str());
    }

    std::string get_image_mime_type(const std::filesystem::path& path)
    {
        const auto ext_u8 = path.extension().u8string();
        std::string ext (ext_u8.begin(), ext_u8.end());
        if (ext == ".jpg" || ext == ".jpeg")
            return "image/jpeg";
        if (ext == ".png")
            return "image/png";

        throw std::runtime_error("HttpsClient::upload_image. Unsupported image format: " + ext);
    }

    std::string create_multipart_file_body(const std::string& filepath, std::string boundary)
    {
        const std::filesystem::path path(filepath);
        const auto size = std::filesystem::file_size(path);
        if (size > MAX_IMAGE_SIZE)
            throw std::runtime_error("create_multipart_file_body. Image exceeds maximum size");

        std::ifstream file(path, std::ios::binary);
        if (!file)
            throw std::runtime_error("create_multipart_file_body. Cannot open image file");

        const auto name_u8 = path.filename().u8string();
        std::string filename(name_u8.begin(), name_u8.end());
        std::string mime_type = get_image_mime_type(path);

        const std::string prefix =
            "--" + boundary + "\r\n"
            "Content-Disposition: form-data; name=\"file\"; filename=\"" +
            filename + "\"\r\n"
            "Content-Type: " + mime_type + "\r\n\r\n";

        const std::string suffix =
            "\r\n--" + boundary + "--\r\n";

        std::string body;
        body.reserve(prefix.size() +
            static_cast<std::size_t>(size) +
            suffix.size());

        body = prefix;

        const auto offset = body.size();
        body.resize(offset + static_cast<std::size_t>(size));

        if (!file.read(
            body.data() + offset,
            static_cast<std::streamsize>(size)))
        {
            throw std::runtime_error("Cannot read image file");
        }

        body += suffix;

        return body;
    }

    bool is_url_valid(boost::urls::url_view url)
    {
        if (url.scheme_id() != boost::urls::scheme::https ||
            !url.has_authority() ||
            url.has_userinfo() ||
            url.host().empty())
        {
            return false;
        }

        return true;
    }

    std::string make_port_from_url(boost::urls::url_view url)
    {
        const auto port = url.port();
        std::string port_str = url.has_port()
            ? std::string(port.data(), port.size())
            : "443";

        return port_str;
    }

    std::string make_host_param(const std::string& host, const std::string& port)
    {
        return port == "443"
            ? host
            : host + ":" + port;
    }

    template<typename request_body_t>
    http::request<request_body_t> create_basic_request(
        std::string sub_url,
        http::verb verb,
        const std::string& host,
        const std::string& api_key)
    {
        http::request<request_body_t> request
        {
            verb,
            sub_url,
            11
        };

        request.set(http::field::host, host);
        request.set(http::field::accept, "application/json");
        request.set(http::field::user_agent, "MyAgent/1.0");
        request.set("x-api-key", api_key);
        request.keep_alive(true);

        return request;
    }

    std::string extract_api_from_url(boost::urls::url_view url)
    {
        std::string api = url.encoded_path().empty() ? "/" : std::string(url.encoded_path());
        if (api.back() != '/')
            api += '/';
        return api;
    }
}

HttpsClient::HttpsClient(
    Config config,
    net::io_context* io,
    Logger* logger)
    : m_config(std::move(config))
    , m_io(io)
    , m_resolver(*io)
    , m_ssl_context(ssl::context::tlsv12_client)
    , m_logger(logger)
{
    const auto url = boost::urls::parse_uri(m_config.m_url).value();
    if (!is_url_valid(url))
    {
        throw std::invalid_argument("Invalid HTTPS URL");
    }

    m_port = make_port_from_url(url);
    if (m_port.empty())
        throw std::invalid_argument("Invalid HTTPS port");

    m_host = url.host();
    if (SSL_CTX_load_verify_store(
        m_ssl_context.native_handle(),
        "org.openssl.winstore:") != 1)
    {
        throw std::runtime_error(
            "Failed to load Windows certificate store");
    }

    m_api = extract_api_from_url(url);
}

net::awaitable<int> HttpsClient::create_event(
    std::string filename,
    int event_code)
{
    if (m_stopping)
        throw std::runtime_error("HttpsClient::create_event. Client is stopping");

    if (m_request_in_progress)
        throw std::logic_error("HttpsClient::create_event. Another request already in progress");
    
    FlagGuard guard{ m_request_in_progress };
    
    int guid_retry_attempts = 3;
    bool regenerate_guid = true;
    std::string guid;
    while (guid_retry_attempts > 0)
    {
        if (regenerate_guid)
        {
            guid = generate_uuid();
            regenerate_guid = false;
        }

        boost::json::object body
        {
            {"event_uid", guid},
            {"event_code", event_code},
            {"file_name", filename},
            {"timestamp", get_timestamp()}
        };

        http::request<http::string_body> request =
            create_post_request(m_api + "events", "application/json", boost::json::serialize(body));

        http::response<http::string_body> response
            = co_await send_request_with_attempts(std::move(request));

        if (response.result() == http::status::conflict)
        {
            guid_retry_attempts--;
            regenerate_guid = true;
            continue;
        }

        if (response.result() != http::status::ok &&
            response.result() != http::status::created)
        {
            throw HttpError(response.result_int(), response.body(), "HttpsClient::create_event");
        }

        const auto json = boost::json::parse(response.body());
        const auto& obj = json.as_object();
        const auto& id = obj.at("event_id").as_int64();

        co_return id;
    }

    std::stringstream ss;
    ss << "HttpsClient::create_event. Couldn't generate unique id. Last generated guid: ";
    ss << guid;
    throw std::runtime_error(ss.str());
}

net::awaitable<void> HttpsClient::upload_image(uint32_t event_id, const std::string& filepath)
{
    if (m_stopping)
        throw std::runtime_error("HttpsClient::upload_image. Client is not connected");

    if (m_request_in_progress)
        throw std::logic_error("HttpsClient::upload_image. Request already in progress");

    FlagGuard guard{ m_request_in_progress };

    const std::string boundary = "BOUNDARY";
    std::string body = create_multipart_file_body(filepath, boundary);

    std::string url = m_api + "events/" + std::to_string(event_id) + "/image";
    std::string content_type = "multipart/form-data; boundary=" + boundary;
    http::request<http::string_body> request = create_post_request(
        std::move(url),
        std::move(content_type),
        std::move(body));

    http::response<http::string_body> response = co_await send_request_with_attempts(std::move(request));
    if (response.result() != http::status::ok)
    {
        throw HttpError(response.result_int(), response.body(), "HttpsClient::upload_image.");
    }

    co_return;
}

net::awaitable<std::string> HttpsClient::list_events(
    int limit, 
    int offset, 
    ImageUploadedParamT image_uploaded)
{
    if (m_stopping)
        throw std::runtime_error("Client is stopping");

    if (m_request_in_progress)
        throw std::logic_error("Request already in progress");

    FlagGuard guard{ m_request_in_progress };

    boost::urls::url url;
    url.set_path(m_api + "events");

    url.params().append(
        {
            {"limit", std::to_string(limit)},
            {"offset", std::to_string(offset)}
        });

    if (image_uploaded != ImageUploadedParamT::All)
        url.params().append(
            {
                {"image_uploaded", image_uploaded_param_to_str(image_uploaded)}
            });

    const auto target = url.encoded_target();

    http::request<http::empty_body> request =
        create_basic_request<http::empty_body>(
            std::string(target.data(), target.size()), 
            http::verb::get, 
            make_host_param(m_host, m_port),
            m_config.m_api_key);

    http::response<http::string_body> response = co_await send_request_with_attempts(std::move(request));
    if (response.result() != http::status::ok)
    {
        throw HttpError(response.result_int(), response.body(), "HttpsClient::list_events");
    }

    const auto json = boost::json::parse(response.body());
    const auto& obj = json.as_object();
    const auto& items = obj.at("items").as_array();

    std::string res = boost::json::serialize(items);
    co_return res;
}

net::awaitable<std::string> HttpsClient::server_status()
{
    if (m_stopping)
        throw std::runtime_error("Client is stopping");

    if (m_request_in_progress)
        throw std::logic_error("Request already in progress");

    FlagGuard guard{ m_request_in_progress };

    http::request<http::empty_body> request =
        create_basic_request<http::empty_body>(
            m_api + "status", 
            http::verb::get, 
            make_host_param(m_host, m_port),
            m_config.m_api_key);

    http::response<http::string_body> response = co_await send_request_with_attempts(std::move(request));
    if (response.result() != http::status::ok)
    {
        throw HttpError(response.result_int(), response.body(), "HttpsClient::server_status");
    }

    const auto json = boost::json::parse(response.body());
    const auto& obj = json.as_object();
    const auto& status = obj.at("status").as_string();

    co_return status;
}

net::awaitable<void> HttpsClient::Stop()
{
    m_stopping = true;
    close_connection();
    co_return;
}

net::awaitable<void> HttpsClient::connect()
{
    if (m_connected)
        co_return;

    m_stream.emplace(*m_io, m_ssl_context);
    m_stream->set_verify_mode(ssl::verify_peer);
    m_stream->set_verify_callback(ssl::host_name_verification(m_host));
    if (!SSL_set_tlsext_host_name(
            m_stream->native_handle(), m_host.c_str()))
    {
        throw std::runtime_error("HttpsClient::connect. Failed to set TLS hostname");
    }

    auto endpoints = co_await m_resolver.async_resolve(
        m_host,
        m_port, 
        net::use_awaitable);

    beast::tcp_stream& tcp_stream = beast::get_lowest_layer(*m_stream);
    tcp_stream.expires_after(std::chrono::seconds(10));

    co_await tcp_stream.async_connect(
        endpoints, 
        net::use_awaitable);

    tcp_stream.expires_after(std::chrono::seconds(10));
    co_await m_stream->async_handshake(
        ssl::stream_base::client, 
        net::use_awaitable);

    tcp_stream.expires_never();
    m_connected = true;
}

void HttpsClient::close_connection()
{
    m_connected = false;
    if (m_stream)
    {
        beast::error_code ec;
        beast::get_lowest_layer(*m_stream)
            .socket().close(ec);
    }
}

http::request<http::string_body> HttpsClient::create_post_request(
    std::string sub_url,
    std::string content_type,
    std::string body)
{
    http::request<http::string_body> request = 
        create_basic_request<http::string_body>(
            sub_url, 
            http::verb::post, 
            make_host_param(m_host, m_port),
            m_config.m_api_key);

    request.set(http::field::content_type, content_type);
    request.body() = body;
    request.prepare_payload();

    return request;
}
