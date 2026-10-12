#include "pch.h"
#include "NetworkUtils.h"
#include "Logger.h"

bool is_retryable_network_error(const boost::system::error_code& ec)
{
    namespace net = boost::asio;
    namespace beast = boost::beast;

    return
        ec == net::error::connection_reset ||
        ec == net::error::connection_aborted ||
        ec == net::error::connection_refused ||
        ec == net::error::broken_pipe ||
        ec == net::error::timed_out ||
        ec == net::error::network_unreachable ||
        ec == net::error::host_unreachable ||
        ec == net::error::network_reset ||
        ec == net::error::network_down ||
        ec == net::error::eof ||
        ec == net::error::host_not_found_try_again ||
        ec == beast::error::timeout ||
        ec == beast::http::error::end_of_stream ||
        ec == beast::http::error::partial_message;
}

net::awaitable<void> wait_rate_limit(
    const http::response<http::string_body>& response, 
    net::steady_timer& timer,
    Logger* logger)
{
    if (response.result() != http::status::too_many_requests)
        co_return;

    auto it = response.find(http::field::retry_after);
    if (it == response.end())
        co_return;

    const auto value = it->value();
    int seconds = std::stoi(std::string(value.data(), value.size()));
    logger->log_plain_message("Rate limited. Waiting for " + std::to_string(seconds) + " seconds.");
    timer.expires_after(std::chrono::seconds(seconds));
    co_await timer.async_wait(net::use_awaitable);
}
