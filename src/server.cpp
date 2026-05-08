#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <memory>
#include <mutex>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include "server.h"
#include "session.h"
#include "repo.h"

namespace ricochet {

server::server(boost::asio::io_context& io, const config& conf)
    : m_config(conf)
    , m_repo(conf.client_repo)
    , m_io(io)
    , m_ssl(std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23_server))
    , m_server(io)
{
    m_ssl->set_options(
        boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::single_dh_use
    );

    m_ssl->use_certificate_chain_file(m_config.server_cert);
    m_ssl->use_private_key_file(m_config.server_key, boost::asio::ssl::context::pem);
    m_ssl->set_verify_mode(boost::asio::ssl::verify_peer | boost::asio::ssl::verify_fail_if_no_peer_cert | boost::asio::ssl::verify_client_once);

    if (!m_config.ca_cert.empty())
        m_ssl->load_verify_file(m_config.ca_cert);

    m_ssl->set_verify_callback([this](bool preverified, boost::asio::ssl::verify_context& ctx)
    {
        if (preverified)
            return true;

        X509_STORE_CTX* store = ctx.native_handle();
        X509* cert = X509_STORE_CTX_get_current_cert(store);
        return cert && m_repo.is_certificate_allowed(cert);
    });
}

void server::accept()
{
    m_server.open(m_config.server_endpoint.protocol());
    m_server.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
    m_server.bind(m_config.server_endpoint);
    m_server.listen();

    do_accept();
}

void server::stop()
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

void server::do_accept()
{
    auto socket = std::make_shared<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(m_io, *m_ssl);
    m_server.async_accept(socket->lowest_layer(), [this, weak = weak_from_this(), socket](const boost::system::error_code& ec)
    {
        auto self = weak.lock();
        if (!self)
            return;

        if (!ec)
        {
            socket->async_handshake(boost::asio::ssl::stream_base::server, [this, weak, socket](const boost::system::error_code& ec)
            {
                auto self = weak.lock();
                if (!self)
                    return;

                if (!ec)
                {
                    try
                    {
                        X509Ptr cert(SSL_get_peer_certificate(socket->native_handle()));
                        if (!cert)
                            throw std::runtime_error("No peer certificate");

                        std::string hash = m_repo.get_certificate_hash(cert.get());
                        auto relay = std::make_shared<session>(m_io, m_ssl, std::move(*socket), m_config.idle_timeout);

                        std::lock_guard<std::mutex> lock(m_mutex);
                        if (!check_limits(hash))
                        {
                            relay->error(ricochet::failure::limit_reached);
                        }
                        else
                        {
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
                            relay->start();
                        }
                        m_relays[hash].insert(relay);
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

        do_accept();
    });
}

bool server::check_limits(const std::string& client)
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

} // namespace ricochet