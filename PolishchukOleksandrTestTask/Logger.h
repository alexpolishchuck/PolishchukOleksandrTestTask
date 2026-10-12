#pragma once

#include <mutex>
#include <string_view>

class Logger
{
public:
    void log_plain_message(std::string_view message);
    void log_info(std::string_view message);
    void log_warning(std::string_view message);
    void log_error(std::string_view message);

private:
    std::mutex m_mutex;
};
