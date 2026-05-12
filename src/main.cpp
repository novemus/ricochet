#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>
#include <iostream>
#include <filesystem>
#include <vector>
#include <thread>
#include "server.h"
#include "logging.h"

namespace po = boost::program_options;

int main(int argc, char* argv[])
{
    try
    {
        po::options_description desc("Ricochet relay server options");
        desc.add_options()
            ("help", "show help message")
            ("host", po::value<std::string>()->default_value("0.0.0.0"), "listen address")
            ("port", po::value<uint16_t>()->default_value(4433), "listen port")
            ("cert", po::value<std::filesystem::path>()->default_value("server.crt"), "SSL certificate file")
            ("key", po::value<std::filesystem::path>()->default_value("server.key"), "SSL private key file")
            ("ca", po::value<std::filesystem::path>()->default_value("ca.crt"), "SSL CA certificate file")
            ("repo", po::value<std::filesystem::path>()->required(), "path to client certificate repository")
            ("idle", po::value<int>()->default_value(300), "idle session timeout in seconds")
            ("client-limit", po::value<size_t>()->default_value(10), "maximum sessions per client")
            ("total-limit", po::value<size_t>()->default_value(1000), "maximum total sessions")
            ("log-level", po::value<std::string>()->default_value("info"), "logging level (debug, info, warning, error, fatal)")
            ("log-file", po::value<std::filesystem::path>(), "log file path (optional)")
        ;

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (vm.count("help"))
        {
            std::cout << desc << "\n";
            return 0;
        }

        if (vm["idle"].as<int>() <= 0)
        {
            _err_ << "Error: idle timeout must be positive";
            return 1;
        }

        if (vm["client-limit"].as<size_t>() == 0)
        {
            _err_ << "Error: client limit must be positive";
            return 1;
        }

        if (vm["total-limit"].as<size_t>() == 0)
        {
            _err_ << "Error: total limit must be positive";
            return 1;
        }

        ricochet::config config;
        config.server_endpoint = boost::asio::ip::tcp::endpoint(
            boost::asio::ip::make_address(vm["host"].as<std::string>()),
            vm["port"].as<uint16_t>()
        );
        config.server_cert = vm["cert"].as<std::filesystem::path>();
        config.server_key = vm["key"].as<std::filesystem::path>();
        config.client_repo = vm["repo"].as<std::filesystem::path>();
        config.idle_timeout = boost::posix_time::seconds(vm["idle"].as<int>());
        config.client_relay_limit = vm["client-limit"].as<size_t>();
        config.total_relay_limit = vm["total-limit"].as<size_t>();
        config.log_level = vm["log-level"].as<std::string>();
        
        if (vm.count("log-file"))
        {
            config.log_file = vm["log-file"].as<std::filesystem::path>();
        }

        boost::asio::io_context io;
        ricochet::server server(io, config);

        unsigned int thread_count = std::max(4u, std::thread::hardware_concurrency());

        // Initialize logging
        boost::log::trivial::severity_level log_level = ricochet::logging::parse_log_level(vm["log-level"].as<std::string>());
        
        // Initialize console logging
        ricochet::logging::init_console_logging(log_level);
        
        // Initialize file logging if specified
        if (vm.count("log-file"))
        {
            ricochet::logging::init_file_logging(vm["log-file"].as<std::filesystem::path>().string(), log_level);
        }

        _inf_ << "Starting Ricochet relay server on " 
              << config.server_endpoint.address() << ":" << config.server_endpoint.port();
        _inf_ << "SSL certificate: " << config.server_cert;
        _inf_ << "SSL private key: " << config.server_key;
        _inf_ << "Client repository: " << config.client_repo;
        _inf_ << "Idle timeout: " << config.idle_timeout.total_seconds() << " seconds";
        _inf_ << "Max sessions per client: " << config.client_relay_limit;
        _inf_ << "Max total sessions: " << config.total_relay_limit;
        _inf_ << "Using " << thread_count << " worker threads";

        server.accept();

        std::vector<std::thread> workers;
        for (unsigned int i = 1; i < thread_count; ++i)
        {
            workers.emplace_back([&io]()
            {
                try
                {
                    io.run();
                }
                catch (const std::exception& e)
                {
                    _err_ << "Worker thread error: " << e.what();
                }
            });
        }

        try
        {
            io.run();
        }
        catch (const std::exception& e)
        {
            _err_ << "Main thread error: " << e.what();
        }

        for (auto& worker : workers)
        {
            if (worker.joinable())
                worker.join();
        }
    }
    catch (const std::exception& e)
    {
        _err_ << "Error: " << e.what();
        return 1;
    }

    return 0;
}
