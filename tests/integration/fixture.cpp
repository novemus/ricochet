/*
 * Copyright (c) 2026 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include <filesystem>
#include <string>
#include "fixture.h"

integration_test_fixture::context::context() : repo(std::filesystem::path(TEST_CONTEXT_DIR) / "repo")
{
    ca_cert = repo.parent_path() / "ca.crt";
    server_cert = repo.parent_path() / "server.crt";
    server_self_cert = repo.parent_path() / "server.pem";
    server_key = repo.parent_path() / "server.key";
    client_cert = repo / "test_client" / "localhost" / "client.crt";
    client_self_cert = repo / "test_client" / "localhost" / "client.pem";
    client_key = repo / "test_client" / "localhost" / "client.key";
}

integration_test_fixture::integration_test_fixture()
    : m_work(boost::asio::make_work_guard(m_io))
{
    for (size_t i = 0; i < 4; ++i)
    {
        boost::asio::post(m_pool, [&]()
        {
            m_io.run();
        });
    }
}

integration_test_fixture::~integration_test_fixture()
{
    m_work.reset();
    m_pool.join();
}

std::shared_ptr<ricochet::server> integration_test_fixture::create_server(bool using_ca, size_t client_limit, size_t total_limit)
{
    auto& ctx = get_test_context();

    ricochet::config config;
    config.server_endpoint = boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 7411);
    config.server_cert = using_ca ? ctx.server_cert : ctx.server_self_cert;
    config.server_key = ctx.server_key;
    config.ca_cert = using_ca ? ctx.ca_cert : std::filesystem::path();
    config.client_repo = ctx.repo;
    config.relay_port_base = 7412;
    config.relay_port_span = 10;
    config.wait_timeout = boost::posix_time::seconds(2);
    config.idle_timeout = boost::posix_time::seconds(2);
    config.client_relay_limit = client_limit;
    config.total_relay_limit = total_limit;

    return std::make_shared<ricochet::server>(m_io, config);
}

std::shared_ptr<ricochet::client> integration_test_fixture::create_client(bool using_ca)
{
    auto& ctx = get_test_context();
    return std::make_shared<ricochet::client>(
        boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 7411),
        using_ca ? ctx.client_cert : ctx.client_self_cert,
        ctx.client_key,
        using_ca ? ctx.ca_cert : ctx.server_self_cert
    );
}

std::shared_ptr<ricochet::agent> integration_test_fixture::create_agent(bool using_ca)
{
    auto& ctx = get_test_context();
    return ricochet::create_agent(
        boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 7411),
        using_ca ? ctx.client_cert : ctx.client_self_cert,
        ctx.client_key,
        using_ca ? ctx.ca_cert : ctx.server_self_cert
    );
}
