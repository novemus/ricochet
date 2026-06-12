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
#include <boost/asio/ssl.hpp>
#include <boost/asio/spawn.hpp>
#include <filesystem>
#include "proto.h"

namespace ricochet {

class client
{
    using socket_ptr = std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>;

public:
    client(const boost::asio::ip::tcp::endpoint& server,
           const std::filesystem::path& cert,
           const std::filesystem::path key,
           const std::filesystem::path ca);
    ~client();

    void connect(boost::asio::yield_context yield);
    void shutdown(boost::asio::yield_context yield);
    void write_query(boost::asio::yield_context yield, const query& req);
    void read_reply(boost::asio::yield_context yield, reply& res);

private:

    static void execute(boost::asio::yield_context yield, const std::function<void()>& function, socket_ptr socket, int timeout = 10000);

    boost::asio::ssl::context m_ssl;
    boost::asio::ip::tcp::endpoint m_server;
    socket_ptr m_socket;
};

} // namespace ricochet
