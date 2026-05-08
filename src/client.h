#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/spawn.hpp>
#include <filesystem>
#include "proto.h"

namespace ricochet {

class client
{
public:
    client(boost::asio::io_context& io,
           const boost::asio::ip::tcp::endpoint& server,
           const std::filesystem::path& cert,
           const std::filesystem::path key,
           const std::filesystem::path ca);
    ~client();

    void connect(boost::asio::yield_context yield);
    void shutdown(boost::asio::yield_context yield);
    void write_query(boost::asio::yield_context yield, const query& req);
    void read_reply(boost::asio::yield_context yield, reply& res);

private:

    void execute(const std::function<void()>& function, int timeout = 10000);

    boost::asio::io_context& m_io;
    boost::asio::ssl::context m_ssl;
    boost::asio::ip::tcp::endpoint m_server;
    std::unique_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> m_socket;
};

} // namespace ricochet
