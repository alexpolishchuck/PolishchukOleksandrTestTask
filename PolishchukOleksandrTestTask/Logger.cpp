#include "pch.h"
#include "Logger.h"

#include <iostream>

void Logger::log_plain_message(std::string_view message)
{
    std::lock_guard lock(m_mutex);
    std::cout << message << '\n';
}

void Logger::log_info(std::string_view message)
{
    std::lock_guard lock(m_mutex);
    std::cout << "[INFO] " << message << '\n';
}

void Logger::log_warning(std::string_view message)
{
    std::lock_guard lock(m_mutex);
    std::cout << "[WARNING] " << message << '\n';
}

void Logger::log_error(std::string_view message)
{
    std::lock_guard lock(m_mutex);
    std::cout << "[ERROR] " << message << '\n';
}
