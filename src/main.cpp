/*
 * Copyright (c) 2026 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>
#include <iostream>
#include <fstream>
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
        po::options_description desc("Ricochet relay server options", 200, 100);
        desc.add_options()
            ("help", "show help message")
            ("address", po::value<std::string>()->default_value("0.0.0.0"), "listen address")
            ("port", po::value<uint16_t>()->default_value(7411), "listen port")
            ("span", po::value<uint16_t>()->default_value(100), "span of relay ports (next to the listen port)")
            ("cert", po::value<std::filesystem::path>()->default_value("ricochet.pem"), "SSL certificate file")
            ("key", po::value<std::filesystem::path>()->default_value("ricochet.key"), "SSL private key file")
            ("ca", po::value<std::filesystem::path>(), "CA certificate file (not needed for repository clients)")
            ("repo", po::value<std::filesystem::path>(), "client certificate repository (optional)")
            ("wait", po::value<int>()->default_value(30), "wait for relay connection (seconds)")
            ("idle", po::value<int>()->default_value(180), "idle relay timeout (seconds)")
            ("quota", po::value<uint32_t>()->default_value(10), "maximum relays per client")
            ("limit", po::value<uint32_t>()->default_value(100), "maximum count of relays")
            ("report", po::value<std::string>()->default_value("info"), "report detailing (trace, debug, info, warning, error, fatal)")
            ("journal", po::value<std::filesystem::path>(), "journal file (console output by default)")
            ("config", po::value<std::filesystem::path>(), "ini-like configuration file (optional)");

        po::positional_options_description conf;
        conf.add("config", 1);

        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv).options(desc).positional(conf).run(), vm);

        if (vm.count("help"))
        {
            std::cout << desc << std::endl;
            return 0;
        }

        if (vm.count("config"))
        {
            std::ifstream config(vm["config"].as<std::filesystem::path>());
            if (!config)
                throw std::runtime_error("Cannot open configuration file: " + vm["config"].as<std::filesystem::path>().string());

            po::store(po::parse_config_file(config, desc, true), vm);
        }

        po::notify(vm);

        boost::log::trivial::severity_level report = ricochet::logging::parse_log_level(vm["report"].as<std::string>());
        if (vm.count("journal"))
        {
            ricochet::logging::init_file_logging(std::filesystem::canonical(vm["journal"].as<std::filesystem::path>()).string(), report);
        }
        else 
        {
            ricochet::logging::init_console_logging(report);
        }

        if (vm["wait"].as<int>() <= 0)
        {
            _ftl_ << "Wait timeout must be positive";
            return 1;
        }

        if (vm["idle"].as<int>() <= 0)
        {
            _ftl_ << "Idle timeout must be positive";
            return 1;
        }

        if (vm["quota"].as<uint32_t>() == 0)
        {
            _ftl_ << "Client limit must be positive";
            return 1;
        }

        if (vm["limit"].as<uint32_t>() == 0)
        {
            _ftl_ << "Total limit must be positive";
            return 1;
        }

        ricochet::config config;
        config.server_endpoint = boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address(vm["address"].as<std::string>()), vm["port"].as<uint16_t>());
        config.relay_port_base = vm["port"].as<uint16_t>() + 1;
        config.relay_port_span = vm["span"].as<uint16_t>();
        config.server_cert = std::filesystem::canonical(vm["cert"].as<std::filesystem::path>());
        config.server_key = std::filesystem::canonical(vm["key"].as<std::filesystem::path>());
        config.ca_cert = vm.count("ca") ? std::filesystem::canonical(vm["ca"].as<std::filesystem::path>()) : std::filesystem::path();
        config.client_repo = vm.count("repo") ? std::filesystem::canonical(vm["repo"].as<std::filesystem::path>()) : std::filesystem::path();
        config.wait_timeout = boost::posix_time::seconds(vm["wait"].as<int>());
        config.idle_timeout = boost::posix_time::seconds(vm["idle"].as<int>());
        config.client_relay_limit = vm["quota"].as<uint32_t>();
        config.total_relay_limit = vm["limit"].as<uint32_t>();

        _inf_ << "Ricochet server: " << config.server_endpoint;
        _inf_ << "Relay port base: " << config.relay_port_base;
        _inf_ << "Relay port span: " << config.relay_port_span;
        _inf_ << "SSL certificate: " << config.server_cert;
        _inf_ << "SSL private key: " << config.server_key;
        _inf_ << "CA certificate: " << config.ca_cert;
        _inf_ << "Client repository: " << config.client_repo;
        _inf_ << "Wait timeout: " << config.wait_timeout.total_seconds() << " seconds";
        _inf_ << "Idle timeout: " << config.idle_timeout.total_seconds() << " seconds";
        _inf_ << "Max relays per client: " << config.client_relay_limit;
        _inf_ << "Max total relays: " << config.total_relay_limit;

        boost::asio::io_context io;
        auto server = std::make_shared<ricochet::server>(io, config);
        server->start();

        std::vector<std::thread> workers;
        for (unsigned int i = 1; i < std::max(4u, std::thread::hardware_concurrency()); ++i)
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
