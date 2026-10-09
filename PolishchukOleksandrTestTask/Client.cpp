#include "pch.h"
#include "Client.h"

namespace
{
    std::string GenerateUUID()
    {
        return boost::uuids::to_string(
            boost::uuids::random_generator()());
    }

    std::string GetTimestamp()
    {
        const auto now = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now());

        std::tm utc{};
        if (gmtime_s(&utc, &now) != 0)
            throw std::runtime_error("Cannot get UTC time");

        char buffer[21];
        if (std::strftime(
            buffer, sizeof(buffer),
            "%Y-%m-%dT%H:%M:%SZ", &utc) == 0)
        {
            throw std::runtime_error("Cannot format UTC time");
        }

        return buffer;
    }

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

    std::string image_uploaded_param_to_str(ImageUploadedParamT param)
    {
        switch (param)
        {
        case ImageUploadedParamT::All:
            return "all";
        case ImageUploadedParamT::NotUploaded:
            return "false";
        case ImageUploadedParamT::Uploaded:
            return "true;";
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

    std::string create_multipart_file_body(std::wstring filepath, std::string boundary)
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

        // Read actual binary image bytes directly into the body
        if (!file.read(
            body.data() + offset,
            static_cast<std::streamsize>(size)))
        {
            throw std::runtime_error("Cannot read image file");
        }

        body += suffix;

        return body;
    }

}

HttpsClient::HttpsClient(
    Config config,
    net::io_context& io)
    : m_config(std::move(config))
    , m_resolver(io)
    , m_ssl_context(ssl::context::tlsv12_client)
    , m_stream(io, m_ssl_context)
{
    const auto url = boost::urls::parse_uri(m_config.m_url).value();
    if (url.scheme_id() != boost::urls::scheme::https ||
        !url.has_authority() ||
        url.has_userinfo() ||
        url.host().empty())
    {
        throw std::invalid_argument("Invalid HTTPS URL");
    }

    const auto port = url.port();
    m_port = url.has_port()
        ? std::string(port.data(), port.size())
        : "443";

    const std::string host = url.host();
    m_host = m_port == "443"
        ? host
        : host + ":" + m_port;

    if (m_port.empty())
        throw std::invalid_argument("Invalid HTTPS port");

    m_ssl_context.set_default_verify_paths();
    m_stream.set_verify_mode(ssl::verify_peer);
    m_stream.set_verify_callback(ssl::host_name_verification(m_host));
}

net::awaitable<int> HttpsClient::create_event(
    std::string filename,
    int event_code)
{
    if (m_stopping)
        throw std::runtime_error("Client is stopping");

    if (m_request_in_progress)
        throw std::logic_error("Request already in progress");

    int retry_attempts = 3;
    
    FlagGuard guard{ m_request_in_progress };
    boost::json::object body
    {
        {"event_uid", GenerateUUID()},
        {"event_code", event_code},
        {"file_name", filename},
        {"timestamp", GetTimestamp()}
    };

    while (retry_attempts > 0)
    {
        co_await Connect();

        http::request<http::string_body> request =
            create_post_request("/events", "application/json", boost::json::serialize(body));

        http::response<http::string_body> response = co_await send_request(std::move(request));

        if (response.result() == http::status::conflict)
        {
            retry_attempts--;
            continue;
        }

        if (response.result() != http::status::ok &&
            response.result() != http::status::created)
        {
            throw std::runtime_error(
                "HttpsClient::create_event: HTTP " +
                std::to_string(response.result_int()));
        }

        const auto json = boost::json::parse(response.body());
        const auto& obj = json.as_object();
        const auto& id = obj.at("event_id").as_int64();

        co_return id;
    }
    std::stringstream ss;
    ss << "HttpsClient::create_event. Couldn't generate unique id. Last generated guid: ";
    ss << body.at("event_uid").as_string();
    throw std::runtime_error(ss.str());
}

net::awaitable<void> HttpsClient::upload_image(uint32_t event_id, std::wstring filepath)
{
    if (m_stopping || !m_connected)
        throw std::runtime_error("HttpsClient::upload_image. Client is not connected");

    if (m_request_in_progress)
        throw std::logic_error("HttpsClient::upload_image. Request already in progress");

    FlagGuard guard{ m_request_in_progress };

    const std::string boundary = "BOUNDARY";
    std::string body = create_multipart_file_body(filepath, boundary);
    std::string url = "/events/" + std::to_string(event_id) + "/image";
    http::request<http::string_body> request = create_post_request(
        std::move(url),
        "multipart/form-data; boundary=" + boundary,
        std::move(body));

    http::response<http::string_body> response = co_await send_request(std::move(request));

    if (response.result_int() != 200)
    {
        throw std::runtime_error(
            "HttpsClient::upload_image. Image upload failed: HTTP " +
            std::to_string(response.result_int()));
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
    url.set_path("/events");

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
        create_basic_request<http::empty_body>(std::string(target.data(), target.size()), http::verb::get);

    http::response<http::string_body> response = co_await send_request(std::move(request));
    if (response.result() != http::status::ok)
    {
        throw std::runtime_error(
            "HttpsClient::list_events: HTTP " +
            std::to_string(response.result_int()));
    }

    const auto json = boost::json::parse(response.body());
    const auto& obj = json.as_object();
    const auto& items = obj.at("items").as_array();

    std::stringstream result_ss;
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        result_ss << boost::json::serialize(items[i]);
        result_ss << (i + 1 < items.size()) ? ",\r\n" : "\r\n";
    }

    co_return result_ss.str();
}

net::awaitable<std::string> HttpsClient::server_status()
{
    if (m_stopping)
        throw std::runtime_error("Client is stopping");

    if (m_request_in_progress)
        throw std::logic_error("Request already in progress");

    FlagGuard guard{ m_request_in_progress };

    http::request<http::empty_body> request =
        create_basic_request<http::empty_body>("/status", http::verb::get);

    http::response<http::string_body> response = co_await send_request(std::move(request));
    if (response.result() != http::status::ok)
    {
        throw std::runtime_error(
            "HttpsClient::is_server_online: HTTP " +
            std::to_string(response.result_int()));
    }

    const auto json = boost::json::parse(response.body());
    const auto& obj = json.as_object();
    const auto& status = obj.at("status").as_string();

    co_return status;
}

net::awaitable<void> HttpsClient::Stop()
{
    m_stopping = true;
    if (m_connected)
    {
        beast::error_code ec;
        beast::get_lowest_layer(m_stream).socket().close(ec);
        m_connected = false;
    }
    co_return;
}

net::awaitable<void> HttpsClient::Connect()
{
    if (m_connected)
        co_return;

    if (!SSL_set_tlsext_host_name(
        m_stream.native_handle(), m_host.c_str()))
    {
        throw std::runtime_error("Failed to set TLS hostname");
    }

    auto endpoints = co_await m_resolver.async_resolve(
        m_host, 
        m_port, 
        net::use_awaitable);

    beast::tcp_stream& tcp_stream = beast::get_lowest_layer(m_stream);
    tcp_stream.expires_after(std::chrono::seconds(10));

    co_await tcp_stream.async_connect(
        endpoints, 
        net::use_awaitable);

    tcp_stream.expires_after(std::chrono::seconds(10));
    co_await m_stream.async_handshake(
        ssl::stream_base::client, 
        net::use_awaitable);

    tcp_stream.expires_never();
    m_connected = true;
}

http::request<http::string_body> HttpsClient::create_post_request(
    std::string sub_url,
    std::string content_type,
    std::string body)
{
    http::request<http::string_body> request = 
        create_basic_request<http::string_body>(sub_url, http::verb::post);

    request.set(http::field::content_type, content_type);
    request.body() = body;
    request.prepare_payload();

    return request;
}
