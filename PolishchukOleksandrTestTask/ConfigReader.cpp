#include "pch.h"
#include "ConfigReader.h"

ConfigReader::ConfigReader(std::string file_path)
    : m_file_path(std::move(file_path))
{}

Config ConfigReader::Read() const
{
    constexpr std::size_t MAX_CONFIG_SIZE = 64 * 1024;

    std::ifstream file(m_file_path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot open config file: " + m_file_path);

    std::string content(MAX_CONFIG_SIZE, '\0');
    file.read(content.data(), content.size());
    if (file.bad())
        throw std::runtime_error("Cannot read config file");

    content.resize(static_cast<std::size_t>(file.gcount()));

    const auto root = boost::json::parse(content);
    const auto& obj = root.as_object();

    const auto& url = obj.at("url").as_string();
    const auto& api_key = obj.at("api_key").as_string();

    return {
        std::string(url.data(), url.size()),
        std::string(api_key.data(), api_key.size())
    };
}