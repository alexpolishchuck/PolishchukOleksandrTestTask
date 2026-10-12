#pragma once
#include <boost/beast.hpp>
#include <boost/asio.hpp>
namespace net = boost::asio;
namespace http = boost::beast::http;

class Logger;

bool is_retryable_network_error(const boost::system::error_code& ec);
net::awaitable<void> wait_rate_limit(
    const http::response<http::string_body>& response,
    net::steady_timer& timer,
    Logger* logger);