/*
 * Copyright (c) 2026 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#pragma once

#include <boost/asio.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <filesystem>
#include "server.h"
#include "client.h"
#include "agent.h"

class integration_test_fixture
{
    struct context
    {
        std::filesystem::path repo;
        std::filesystem::path ca_cert;
        std::filesystem::path client_cert;
        std::filesystem::path client_self_cert;
        std::filesystem::path client_key;
        std::filesystem::path server_cert;
        std::filesystem::path server_self_cert;
        std::filesystem::path server_key;

        context();
        ~context();
    };

    context& get_test_context() { static context s_context; return s_context; }

public:

    integration_test_fixture();
    ~integration_test_fixture();

    std::shared_ptr<ricochet::server> create_server(bool using_ca = true, size_t client_limit = 2, size_t total_limit = 4);
    std::shared_ptr<ricochet::client> create_client(bool using_ca = true);
    std::shared_ptr<ricochet::agent> create_agent(bool using_ca = true);
    boost::asio::io_context& get_io_context() { return m_io; }

private:

    boost::asio::io_context m_io;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> m_work;
    boost::asio::thread_pool m_pool;
};
