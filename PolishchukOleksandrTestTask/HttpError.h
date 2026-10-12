#pragma once
#include <boost/json.hpp>
#include <stdexcept>
#include <string>

struct HttpError : public std::runtime_error
{
    HttpError(int code, const std::string& response_body, const std::string& message);

    const char* what() const noexcept override;

private:
    std::string m_response_body;
    std::string m_error_message;
};