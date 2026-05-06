#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>
#include <iostream>
#include <filesystem>
#include <vector>
#include <thread>
#include <ricochet.h>
#include <server.h>
#include <repo.h>

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
            ("repo", po::value<std::filesystem::path>()->required(), "path to client certificate repository")
            ("idle", po::value<int>()->default_value(300), "idle session timeout in seconds")
            ("client-limit", po::value<size_t>()->default_value(10), "maximum sessions per client")
            ("total-limit", po::value<size_t>()->default_value(1000), "maximum total sessions")
            ("verbose", "enable verbose logging")
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
            std::cerr << "Error: idle timeout must be positive\n";
            return 1;
        }

        if (vm["client-limit"].as<size_t>() == 0)
        {
            std::cerr << "Error: client limit must be positive\n";
            return 1;
        }

        if (vm["total-limit"].as<size_t>() == 0)
        {
            std::cerr << "Error: total limit must be positive\n";
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

        boost::asio::io_context io;
        ricochet::server server(io, config);

        unsigned int thread_count = std::max(4u, std::thread::hardware_concurrency());

        if (vm.count("verbose"))
        {
            std::cout << "Starting Ricochet relay server on " 
                      << config.server_endpoint.address() << ":" << config.server_endpoint.port() << "\n";
            std::cout << "SSL certificate: " << config.server_cert << "\n";
            std::cout << "SSL private key: " << config.server_key << "\n";
            std::cout << "Client repository: " << config.client_repo << "\n";
            std::cout << "Idle timeout: " << config.idle_timeout.total_seconds() << " seconds\n";
            std::cout << "Max sessions per client: " << config.client_relay_limit << "\n";
            std::cout << "Max total sessions: " << config.total_relay_limit << "\n";
            std::cout << "Using " << thread_count << " worker threads\n";
        }

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
                    std::cerr << "Worker thread error: " << e.what() << std::endl;
                }
            });
        }

        try
        {
            io.run();
        }
        catch (const std::exception& e)
        {
            std::cerr << "Main thread error: " << e.what() << std::endl;
        }

        for (auto& worker : workers)
        {
            if (worker.joinable())
                worker.join();
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
