#pragma once
#include <string>
struct Config
{
    std::string m_url;
    std::string m_api_key;
};

class ConfigReader
{
public:
    explicit ConfigReader(std::string file_path);

    Config Read() const;

private:
    std::string m_file_path;
};