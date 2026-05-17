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
            ("bind-addr", po::value<std::string>()->default_value("0.0.0.0"), "listen address")
            ("bind-port", po::value<uint16_t>()->default_value(4433), "listen port")
            ("cert-file", po::value<std::filesystem::path>()->default_value("server.pem"), "SSL certificate file")
            ("key-file", po::value<std::filesystem::path>()->default_value("server.key"), "SSL private key file")
            ("ca-file", po::value<std::filesystem::path>()->default_value("ca.pem"), "SSL CA certificate file")
            ("repo-path", po::value<std::filesystem::path>()->default_value(std::filesystem::current_path()), "path to client SSL certificate repository")
            ("idle-time", po::value<int>()->default_value(300), "idle session timeout in seconds")
            ("client-limit", po::value<size_t>()->default_value(10), "maximum sessions per client")
            ("total-limit", po::value<size_t>()->default_value(100), "maximum sessions count")
            ("log-level", po::value<std::string>()->default_value("info"), "logging level (trace, debug, info, warning, error, fatal)")
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

        boost::log::trivial::severity_level logging = ricochet::logging::parse_log_level(vm["log-level"].as<std::string>());
        ricochet::logging::init_console_logging(logging);

        if (vm.count("log-file"))
        {
            ricochet::logging::init_file_logging(vm["log-file"].as<std::filesystem::path>().string(), logging);
        }

        if (vm["idle-time"].as<int>() <= 0)
        {
            _ftl_ << "Idle timeout must be positive";
            return 1;
        }

        if (vm["client-limit"].as<size_t>() == 0)
        {
            _ftl_ << "Client limit must be positive";
            return 1;
        }

        if (vm["total-limit"].as<size_t>() == 0)
        {
            _ftl_ << "Total limit must be positive";
            return 1;
        }

        ricochet::config config;
        config.server_endpoint = boost::asio::ip::tcp::endpoint(
            boost::asio::ip::make_address(vm["bind-addr"].as<std::string>()),
            vm["bind-port"].as<uint16_t>()
        );
        config.server_cert = vm["cert-file"].as<std::filesystem::path>();
        config.server_key = vm["key-file"].as<std::filesystem::path>();
        config.ca_cert = vm["ca-file"].as<std::filesystem::path>();
        config.client_repo = vm["repo-path"].as<std::filesystem::path>();
        config.idle_timeout = boost::posix_time::seconds(vm["idle-time"].as<int>());
        config.client_relay_limit = vm["client-limit"].as<size_t>();
        config.total_relay_limit = vm["total-limit"].as<size_t>();

        boost::asio::io_context io;
        ricochet::server server(io, config);

        unsigned int thread_count = std::max(4u, std::thread::hardware_concurrency());

        _inf_ << "Starting Ricochet relay server on " << config.server_endpoint.address() << ":" << config.server_endpoint.port();
        _inf_ << "SSL certificate: " << config.server_cert;
        _inf_ << "SSL private key: " << config.server_key;
        _inf_ << "CA certificate: " << config.ca_cert;
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
        _ftl_ << e.what();
        return 1;
    }

    return 0;
}
