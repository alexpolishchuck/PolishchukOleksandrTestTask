#include "pch.h"
#include "HttpError.h"

HttpError::HttpError(
    int code, 
    const std::string& response_body, 
    const std::string& message)
    : std::runtime_error(message)
    , m_response_body(response_body)
    , m_error_message(message + ". HTTP " + std::to_string(code) + ".")
{
    boost::system::error_code ec;
    const auto json = boost::json::parse(m_response_body, ec);

    if (ec || !json.is_object())
        return;

    const auto* error = json.as_object().if_contains("error");
    if (!error || !error->is_object())
        return;

    const auto& obj = error->as_object();
    const auto* error_code = obj.if_contains("code");
    const auto* error_message = obj.if_contains("message");
    if (!error_code || !error_message ||
        !error_code->is_string() || !error_message->is_string())
        return;

    const auto& code_str = error_code->as_string();
    const auto& message_str = error_message->as_string();

    m_error_message += " " +
        std::string(code_str.data(), code_str.size()) + ": " +
        std::string(message_str.data(), message_str.size());
}

const char* HttpError::what() const noexcept

{
    return m_error_message.c_str();
}
