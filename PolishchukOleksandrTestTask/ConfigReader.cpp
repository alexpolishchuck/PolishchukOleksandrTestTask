#include "pch.h"
#include "ConfigReader.h"

Config read_config(const std::string& file_path)
{
    constexpr std::size_t MAX_CONFIG_SIZE = 64 * 1024;

    std::ifstream file(file_path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot open config file: " + file_path);

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
