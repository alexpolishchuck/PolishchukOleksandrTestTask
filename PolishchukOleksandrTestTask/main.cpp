#include "pch.h"
#include "Client.h"
#include "HttpError.h"
#include "Logger.h"
#include "ConfigReader.h"

#include <future>
#include <iostream>
#include <optional>
#include <thread>

namespace
{
    std::optional<std::string> read_text(const std::string& prompt, Logger* logger)
    {
        logger->log_plain_message(prompt);
        std::string input(MAX_CIN_INPUT_SIZE, '\0');
        if (!std::cin.getline(input.data(), input.size()))
        {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            logger->log_warning(
                "Input is too long. Only " + std::to_string(MAX_CIN_INPUT_SIZE) + " characters are allowed.");
            return std::nullopt;
        }
        
        input.resize(std::strlen(input.c_str()));
        return input;
    }

    std::optional<int> read_integer(const std::string& prompt, Logger* logger)
    {
        try
        {
            std::optional<std::string> input_opt = read_text(prompt, logger);
            if (!input_opt)
                return std::nullopt;

            std::size_t parsed = 0;
            const int value = std::stoi(*input_opt, &parsed);
            if (parsed != input_opt->size())
                throw std::invalid_argument("trailing characters");
            return value;
        }
        catch (const std::exception& ex)
        {
            logger->log_error("Invalid number: " + std::string(ex.what()));
            return std::nullopt;
        }
    }

    class ConsoleUi
    {
    public:
        ConsoleUi (HttpsClient* client, net::io_context* io, Logger* logger)
            : m_client(client)
            , m_io(io)
            , m_logger(logger)
        {}

        void run()
        {
            m_logger->log_plain_message("Client API console");
            m_logger->log_plain_message("Requests run on a dedicated coroutine thread.");

            while (true)
            {
                print_menu();
                const auto command = read_integer("Select an operation: ", m_logger);
                if (!command)
                    continue;

                try
                {
                    switch (*command)
                    {
                    case 1:
                        show_server_status();
                        break;
                    case 2:
                        list_events();
                        break;
                    case 3:
                        create_event();
                        break;
                    case 4:
                        upload_image();
                        break;
                    case 0:
                        return;
                    default:
                        m_logger->log_plain_message("Unknown operation.");
                        break;
                    }
                }
                catch (const std::exception& ex)
                {
                    m_logger->log_error("Request failed: " + std::string(ex.what()));
                }
            }
        }

    private:
        void print_menu() const
        {
            m_logger->log_plain_message("\n1. Server status\n"
                "2. List events\n"
                "3. Create event\n"
                "4. Upload image\n"
                "0. Exit\n");
        }

        void show_server_status()
        {
            auto result = net::co_spawn(
                *m_io,
                m_client->server_status(),
                net::use_future);

            m_logger->log_plain_message("Server status: " + result.get());
        }

        void list_events()
        {
            const auto limit = read_integer("Limit: ", m_logger);
            if (!limit)
                return;

            const auto offset = read_integer("Offset: ", m_logger);
            if (!offset)
                return;

            const auto filter = read_integer(
                "Image filter (0 = all, 1 = uploaded, 2 = not uploaded): ", m_logger);
            if (!filter || *filter < 0 || *filter > 2)
            {
                if (filter)
                    m_logger->log_error("Invalid image filter.");
                return;
            }

            const auto image_filter = static_cast<ImageUploadedParamT>(*filter);
            auto result = net::co_spawn(
                *m_io,
                m_client->list_events(*limit, *offset, image_filter),
                net::use_future);
            m_logger->log_plain_message("Events:\n" + result.get());
        }

        void create_event()
        {
            const auto filename = read_text("Filename: ", m_logger);
            if (!filename)
                return;
            const auto event_code = read_integer("Event code: ", m_logger);
            if (!event_code)
                return;

            auto result = net::co_spawn(
                *m_io,
                m_client->create_event(*filename, *event_code),
                net::use_future);
            m_logger->log_plain_message("Created event with ID: " + std::to_string(result.get()));
        }

        void upload_image()
        {
            const auto event_id = read_integer("Event ID: ", m_logger);
            if (!event_id || *event_id < 0)
            {
                if (event_id)
                    m_logger->log_error("Event ID must not be negative.");
                return;
            }
            const auto filepath = read_text("Image path (.jpg, .jpeg, or .png): ", m_logger);
            if (!filepath)
                return;

            auto result = net::co_spawn(
                *m_io,
                m_client->upload_image(
                    static_cast<uint32_t>(*event_id), *filepath),
                net::use_future);
            result.get();
            m_logger->log_plain_message("Image uploaded.");
        }

        HttpsClient* m_client;
        net::io_context* m_io;
        Logger* m_logger;
    };
}

class App
{
public:
    App(Config config)
        : m_client(std::move(config), &m_io, &m_logger)
        , m_ui(&m_client, &m_io, &m_logger)
        , m_work_guard(net::make_work_guard(m_io))
    {
    }

    void run()
    {
        try
        {
            m_coroutine_thread = std::thread([this]
                {
                    m_io.run();
                });

            m_ui.run();
        }
        catch (const std::exception& ex)
        {
            std::cerr << "Fatal error: " << ex.what() << "\n";
        }
    }

    ~App()
    {
        try
        {
            auto stop_result = net::co_spawn(m_io, m_client.Stop(), net::use_future);
            stop_result.get();
        }
        catch (const std::exception& ex)
        {
            std::cerr << "Error during shutdown of HttpsClient: " << ex.what() << "\n";
        }
        
        m_work_guard.reset();
        if (m_coroutine_thread.joinable())
            m_coroutine_thread.join();
    }

private:
    net::io_context m_io;
    net::executor_work_guard<net::io_context::executor_type> m_work_guard;
    HttpsClient m_client;
    std::thread m_coroutine_thread;
    ConsoleUi m_ui;
    Logger m_logger;
};

int main(int argc, char* argv[])
{
    try
    {
        const std::string config_path = argc > 1 ? argv[1] : "config.json";
        Config config = read_config(config_path);
        App app(std::move(config));
        app.run();
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}