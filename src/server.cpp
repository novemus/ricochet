#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <filesystem>
#include <map>
#include <set>
#include "session.h"

namespace ricochet {

struct config
{
    boost::asio::ip::udp::endpoint server_endpoint;
    std::filesystem::path server_cert;
    std::filesystem::path server_key;
    std::filesystem::path client_repo;
    std::chrono::seconds idle_timeout;
    size_t client_relay_limit;
    size_t total_relay_limit;
};

class server : public std::enable_shared_from_this<server>
{
    config m_config;
    boost::asio::io_context& m_io;
    boost::asio::ssl::context m_ssl;
    boost::asio::ip::tcp::acceptor m_acceptor;
    std::map<std::string, std::set<std::weak_ptr<session>>> m_relays;
    std::mutex m_mutex;

public:

    server(boost::asio::io_context& io, const config& conf)
        : m_config(conf)
        , m_io(io)
        , m_ssl(boost::asio::ssl::context::tlsv12)
        , m_acceptor(io)
    {
    }

    ~server()
    {
    }

    void start()
    {
    }

    void stop()
    {
    }

protected:

    void accept()
    {
    }
};

}

int main(int argc, char** argv)
{
    return 0;
}