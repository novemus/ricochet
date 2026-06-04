#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <filesystem>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include "repo.h"
#include "session.h"

namespace ricochet {

struct config
{
    boost::asio::ip::tcp::endpoint server_endpoint;
    std::filesystem::path server_cert;
    std::filesystem::path server_key;
    std::filesystem::path ca_cert;
    std::filesystem::path client_repo;
    boost::posix_time::seconds wait_timeout;
    boost::posix_time::seconds idle_timeout;
    size_t client_relay_limit;
    size_t total_relay_limit;

    config()
        : server_endpoint()
        , server_cert("")
        , server_key("")
        , ca_cert("")
        , client_repo("")
        , wait_timeout(boost::posix_time::seconds(30))
        , idle_timeout(boost::posix_time::seconds(180))
        , client_relay_limit(10)
        , total_relay_limit(100)
    {}
};

class server : public std::enable_shared_from_this<server>
{
    config m_config;
    repository m_repo;
    boost::asio::io_context& m_io;
    std::shared_ptr<boost::asio::ssl::context> m_ssl;
    boost::asio::ip::tcp::acceptor m_server;
    std::map<std::string, std::set<std::shared_ptr<session>>> m_relays;
    std::mutex m_mutex;

public:

    server(boost::asio::io_context& io, const config& cfg);

    void start();
    void stop();

private:

    void accept();
    bool check_limits(const std::string& client) const;
};

} // namespace ricochet
