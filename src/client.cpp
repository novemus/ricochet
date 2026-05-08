#include <boost/system/system_error.hpp>
#include "proto.h"
#include "client.h"

namespace ricochet {

client::client(boost::asio::io_context& io,
           const boost::asio::ip::tcp::endpoint& server,
           const std::filesystem::path& cert,
           const std::filesystem::path key,
           const std::filesystem::path ca)
    : m_io(io)
    , m_ssl(boost::asio::ssl::context::sslv23_client)
    , m_server(server)
{
    m_ssl.set_options(
        boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::single_dh_use
    );

    if (!cert.empty() && !key.empty())
    {
        m_ssl.use_certificate_file(cert, boost::asio::ssl::context::pem);
        m_ssl.use_private_key_file(key, boost::asio::ssl::context::pem);
    }

    if (!ca.empty())
    {
        m_ssl.load_verify_file(ca);
        m_ssl.set_verify_mode(boost::asio::ssl::verify_peer | boost::asio::ssl::verify_fail_if_no_peer_cert);
    }
}

client::~client()
{
    if (m_socket->lowest_layer().is_open())
    {
        boost::system::error_code ec;
        m_socket->lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        m_socket->lowest_layer().close(ec);
    }
}

void client::connect(boost::asio::yield_context yield)
{
    m_socket = std::make_unique<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(m_io, m_ssl);

    execute([this, yield]()
    {
        m_socket->lowest_layer().open(m_server.protocol());
        m_socket->lowest_layer().async_connect(m_server, yield);
        m_socket->async_handshake(boost::asio::ssl::stream_base::client, yield);
    });
}

void client::shutdown(boost::asio::yield_context yield)
{
    if (!m_socket || !m_socket->lowest_layer().is_open())
        return;

    execute([this, yield]()
    {
        boost::system::error_code ec;
        m_socket->async_shutdown(yield[ec]);
        m_socket->lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        m_socket->lowest_layer().close(ec);
    },
    2000);
}

void client::write_query(boost::asio::yield_context yield, const query& req)
{
    if (!m_socket || !m_socket->lowest_layer().is_open())
        throw boost::system::system_error(boost::asio::error::not_connected, "not connected to server");

    execute([this, req, yield]() mutable
    {
        auto size = boost::asio::async_write(*m_socket, boost::asio::buffer(req.data(), req.size()), yield);

        if (size != req.size())
            throw boost::system::system_error(boost::asio::error::no_recovery, "can't write query message");
    });
}

void client::read_reply(boost::asio::yield_context yield, reply& res)
{
    if (!m_socket || !m_socket->lowest_layer().is_open())
        throw boost::system::system_error(boost::asio::error::not_connected, "not connected to server");

    execute([this, &res, yield]() mutable
    {
        try
        {
            auto size = boost::asio::async_read(*m_socket, boost::asio::buffer(res.data(), reply::header_size), yield);

            if (size != reply::header_size)
                throw boost::system::system_error(boost::asio::error::no_recovery, "can't read reply header");

            if (res.length() > res.size() - reply::header_size)
                throw malformed_message("too big reply payload size");

            size = boost::asio::async_read(*m_socket, boost::asio::buffer(res.data() + reply::header_size, res.length()), yield);
            if (size != res.length())
                throw boost::system::system_error(boost::asio::error::no_recovery, "can't read reply payload");

            res.resize(res.length() + reply::header_size);
        }
        catch (const boost::system::system_error& ex)
        {
            if (ex.code() == boost::asio::error::eof)
                shutdown(yield);
            throw;
        }
    });
}

void client::execute(const std::function<void()>& function, int timeout)
{
    boost::asio::deadline_timer timer(m_io);

    timer.expires_from_now(boost::posix_time::milliseconds(timeout));
    timer.async_wait([&](const boost::system::error_code& ec)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        if (m_socket->lowest_layer().is_open())
        {
            boost::system::error_code err;
            m_socket->lowest_layer().cancel(err);
        }
    });

    function();

    boost::system::error_code ec;
    timer.cancel(ec);
}

} // namespace ricochet
