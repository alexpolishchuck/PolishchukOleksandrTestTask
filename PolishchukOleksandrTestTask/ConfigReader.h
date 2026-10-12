#pragma once
#include <string>
struct Config
{
    std::string m_url;
    std::string m_api_key;
};
Config read_config(const std::string& file_path);