#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <filesystem>
#include <map>
#include <set>
#include <memory>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include "session.h"
#include "repo.h"

namespace ricochet {

struct config
{
    boost::asio::ip::udp::endpoint server_endpoint;
    std::filesystem::path server_cert;
    std::filesystem::path server_key;
    std::filesystem::path client_repo;
    boost::posix_time::seconds idle_timeout;
    size_t client_relay_limit;
    size_t total_relay_limit;
};

class server : public std::enable_shared_from_this<server>
{
    config m_config;
    repository m_repo;
    boost::asio::io_context& m_io;
    boost::asio::ssl::context m_ssl;
    boost::asio::ip::tcp::acceptor m_server;
    std::map<std::string, std::set<std::shared_ptr<session>>> m_relays;
    std::mutex m_mutex;

public:

    server(boost::asio::io_context& io, const config& conf)
        : m_config(conf)
        , m_repo(conf.client_repo)
        , m_io(io)
        , m_ssl(boost::asio::ssl::context::tlsv12)
        , m_server(io)
    {
        setup_ssl_context();
    }

    ~server()
    {
    }

    void start()
    {
        boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), m_config.server_endpoint.port());
        m_server.open(endpoint.protocol());
        m_server.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        m_server.bind(endpoint);
        m_server.listen();
        
        accept();
    }

    void stop()
    {
        boost::system::error_code ec;
        m_server.close(ec);

        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [hash, sessions] : m_relays)
        {
            for (auto& session : sessions)
            {
                session->close();
            }
        }
        m_relays.clear();
    }

private:

    bool check_limits(const std::string& client)
    {
        size_t client_sessions = 0;
        size_t total_sessions = 0;

        for (auto& [hash, sessions] : m_relays)
        {
            for (auto it = sessions.begin(); it != sessions.end();)
            {
                if (client == hash)
                    ++client_sessions;

                ++total_sessions;
                ++it;
            }
        }
        
        return total_sessions < m_config.total_relay_limit && client_sessions < m_config.client_relay_limit;
    }

protected:

    void setup_ssl_context()
    {
        m_ssl.set_options(
            boost::asio::ssl::context::default_workarounds |
            boost::asio::ssl::context::no_sslv2 |
            boost::asio::ssl::context::no_sslv3 |
            boost::asio::ssl::context::single_dh_use
        );
        
        m_ssl.use_certificate_chain_file(m_config.server_cert.string());
        m_ssl.use_private_key_file(m_config.server_key.string(), boost::asio::ssl::context::pem);
        
        m_ssl.set_verify_mode(boost::asio::ssl::verify_peer | boost::asio::ssl::verify_fail_if_no_peer_cert);
        m_ssl.set_verify_callback(
            [this](bool, boost::asio::ssl::verify_context& ctx)
            {
                // Get peer certificate from verify context
                X509_STORE_CTX* store = ctx.native_handle();
                X509* cert = X509_STORE_CTX_get_current_cert(store);
                return cert && m_repo.is_certificate_allowed(cert);
            }
        );
    }

    void accept()
    {
        auto socket = std::make_shared<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(m_io, m_ssl);
        m_server.async_accept(socket->lowest_layer(), [this, socket](const boost::system::error_code& ec)
        {
            if (!ec)
            {
                socket->async_handshake(boost::asio::ssl::stream_base::server, [this, socket](const boost::system::error_code& ec)
                {
                    if (!ec)
                    {
                        try
                        {
                            X509Ptr cert(SSL_get_peer_certificate(socket->native_handle()));
                            if (!cert)
                                throw std::runtime_error("No peer certificate");

                            std::string hash = m_repo.get_certificate_hash(cert.get());

                            auto relay = std::make_shared<session>(std::move(*socket), m_config.idle_timeout);
                            if (!check_limits(hash))
                            {
                                relay->error(ricochet::failure::limit_reached);
                                return;
                            }
                            else
                            {
                                std::lock_guard<std::mutex> lock(m_mutex);
                                relay->set_cleaner([this, weak = weak_from_this(), hash, relay]() 
                                {
                                    if (auto self = weak.lock())
                                    {
                                        std::lock_guard<std::mutex> lock(m_mutex);
                                        m_relays[hash].erase(relay);
                                        if (m_relays[hash].empty())
                                            m_relays.erase(hash);
                                    }
                                });
                                m_relays[hash].insert(relay);
                            }

                            relay->start();
                        }
                        catch (const std::exception& e)
                        {
                            boost::system::error_code close_ec;
                            socket->lowest_layer().close(close_ec);
                        }
                    }
                    else
                    {
                        boost::system::error_code close_ec;
                        socket->lowest_layer().close(close_ec);
                    }
                });
            }
            
            accept();
        });
    }
};

}

int main(int argc, char** argv)
{
    return 0;
}