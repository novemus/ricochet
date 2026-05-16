#include <boost/asio.hpp>
#ifdef __APPLE__
#include <sys/socket.h>
#endif
#include <memory>
#include <chrono>
#include <cstdlib>
#include "proto.h"
#include "relay.h"
#include "logging.h"

namespace ricochet {

boost::asio::ip::address get_outgoing_address(boost::asio::io_context& io, bool ip4)
{
    try
    {
        boost::asio::ip::udp::socket socket(io, ip4 ? boost::asio::ip::udp::v4() : boost::asio::ip::udp::v6());
        socket.set_option(boost::asio::socket_base::reuse_address(true));
        socket.bind(boost::asio::ip::udp::endpoint(ip4 ? boost::asio::ip::udp::v4() : boost::asio::ip::udp::v6(), 0));

        const char* remote_endpoint_env = std::getenv(ip4 ? "RICOCHET_OUTGOING_TEST_IPV4" : "RICOCHET_OUTGOING_TEST_IPV6");
        std::string remote_endpoint_str = remote_endpoint_env ? remote_endpoint_env : (ip4 ? "8.8.8.8:53" : "[2001:4860:4860::8888]:53");

        boost::asio::ip::udp::endpoint remote;

        try
        {
            size_t colon_pos = remote_endpoint_str.find_last_of(':');
            if (colon_pos == std::string::npos)
                throw std::runtime_error("No port specified in endpoint");

            std::string remote_addr = remote_endpoint_str.substr(0, colon_pos);
            std::string port_str = remote_endpoint_str.substr(colon_pos + 1);

            if (remote_addr.front() == '[' && remote_addr.back() == ']')
                remote_addr = remote_addr.substr(1, remote_addr.length() - 2);

            uint16_t remote_port = static_cast<uint16_t>(std::stoi(port_str));

            remote = boost::asio::ip::udp::endpoint(boost::asio::ip::make_address(remote_addr), remote_port);
        }
        catch (const std::exception& e)
        {
            _wrn_ << "Can't parse outgoing test address: " << e.what();
            remote = boost::asio::ip::udp::endpoint(boost::asio::ip::make_address(ip4 ? "8.8.8.8" : "2001:4860:4860::8888"), 53);
        }

        socket.connect(remote);
        auto local = socket.local_endpoint();

        return local.address();
    }
    catch (const boost::system::system_error& ex)
    {
        throw unavailable_proto(ex.code().message());
    }
}

bool is_ip4_available(boost::asio::io_context& io)
{
    try
    {
        return get_outgoing_address(io, true).is_v4();
    }
    catch (const unavailable_proto&) {}
    return false;
}

bool is_ip6_available(boost::asio::io_context& io)
{
    try
    {
        return get_outgoing_address(io, false).is_v6();
    }
    catch (const unavailable_proto&) {}
    return false;
}

boost::asio::ip::address get_outgoing_address(boost::asio::io_context& io, const boost::asio::ip::address& address)
{
    return address.is_unspecified() ? get_outgoing_address(io, address.is_v4()) : address;
}

tcp_relay::tcp_relay(boost::asio::io_context& io, protocol proto, boost::posix_time::seconds idle, cleanup_function clean)
    : m_io(io)
    , m_strand(io)
    , m_server(io)
    , m_near(io)
    , m_away(io)
    , m_idle_timer(io)
    , m_retry_timer(io)
    , m_idle(idle)
    , m_clean(clean)
    , m_timestamp(std::chrono::steady_clock::now())
    , m_reconnects(0)
{
    auto protocol = proto == protocol::tcp6 ? boost::asio::ip::tcp::v6() : boost::asio::ip::tcp::v4();

    m_server.open(protocol);
    m_server.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
#ifdef __APPLE__
    int optval = 1;
    ::setsockopt(m_server.native_handle(), SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
#endif
    m_server.bind(boost::asio::ip::tcp::endpoint(protocol, 0));
    m_server.listen();

    _trc_("TCP relay " << this << " created");
}

tcp_relay::~tcp_relay()
{
    _trc_("TCP relay " << this << " destroyed");
    break_relay();
}

protocol tcp_relay::get_protocol() const
{
    return m_server.is_open() && m_server.local_endpoint().address().is_v6() ? protocol::tcp6 : protocol::tcp4;
}

endpoint tcp_relay::get_endpoint() const
{
    return endpoint(get_outgoing_address(m_io, m_server.local_endpoint().address()), m_server.local_endpoint().port());
}

void tcp_relay::start(const peer& red, const peer& blue)
{
    _inf_ << "TCP relay " << this << " starting, red=" << red << ", blue=" << blue;

    if (red.role() == schema::client)
        accept_peer(red.location());
    else
        connect_peer(red.location());
    
    if (blue.role() == schema::client)
        accept_peer(blue.location());
    else
        connect_peer(blue.location());

    watch_activity();
}

void tcp_relay::close()
{
    _inf_ << "TCP relay " << this << " closing...";
    
    m_strand.post([weak = weak_from_this()]()
    {
        if (auto self = weak.lock())
        {
            self->break_relay();
        }
    });
}

void tcp_relay::start_relay()
{
    if (m_near.is_open() && m_away.is_open())
    {
        _inf_ << "TCP relay " << this << " is active";

        boost::system::error_code ec;
        m_server.close(ec);

        transmit_data(m_near, m_away);
        transmit_data(m_away, m_near);
    }
}

void tcp_relay::watch_activity()
{
    m_idle_timer.expires_from_now(m_idle);
    m_idle_timer.async_wait(m_strand.wrap([this, weak = weak_from_this()](const boost::system::error_code& ec)
    {
        auto self = weak.lock();
        if (!self)
            return;
        
        if (!ec)
        {
            auto expired = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - m_timestamp
            );

            if (expired.count() >= m_idle.total_seconds())
            {
                _inf_ << "TCP relay " << this << " idle timeout";
                break_relay();
            }
            else
            {
                watch_activity();
            }
        }
    }));
}

void tcp_relay::connect_peer(const endpoint& which)
{
    auto endpoint = m_server.local_endpoint();

    auto peer = std::make_shared<boost::asio::ip::tcp::socket>(m_io);

    try
    {
        peer->open(endpoint.protocol());
        peer->set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
#ifdef __APPLE__
        int optval = 1;
        ::setsockopt(peer->native_handle(), SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
#endif
        peer->bind(endpoint);
    }
    catch (const std::exception& e)
    {
        _err_ << "TCP relay " << this << " failed to bind: " << e.what();

        break_relay();
        return;
    }

    peer->async_connect(boost::asio::ip::tcp::endpoint(which.address(), which.port()),
        m_strand.wrap([this, weak = weak_from_this(), peer, which](const boost::system::error_code& ec)
        {
            auto self = weak.lock();
            if (!self)
                return;

            if (!ec)
            {
                _dbg_ << "TCP relay " << this << " connected to peer " << which;
                
                if (m_near.is_open())
                    m_away = std::move(*peer);
                else
                    m_near = std::move(*peer);

                start_relay();
            }
            else if (ec == boost::asio::error::connection_refused && m_reconnects < 3)
            {
                m_reconnects++;
                _wrn_ << "TCP relay " << this << " connection refused to peer " << which << ", retrying in 2 seconds";

                m_retry_timer.expires_from_now(boost::posix_time::seconds(2));
                m_retry_timer.async_wait(m_strand.wrap([this, weak = weak_from_this(), which](const boost::system::error_code& ec)
                {
                    auto self = weak.lock();
                    if (!self)
                        return;

                    if (!ec)
                        connect_peer(which);
                }));
            }
            else
            {
                _err_ << "TCP relay " << this << " failed to connect to peer " << which << ": " << ec.message();
                break_relay();
            }
        }));
}

void tcp_relay::accept_peer(const endpoint& which)
{
    auto peer = std::make_shared<boost::asio::ip::tcp::socket>(m_io); 
    m_server.async_accept(*peer, m_strand.wrap([this, weak = weak_from_this(), peer, which](const boost::system::error_code& ec)
    {
        auto self = weak.lock();
        if (!self)
            return;

        if (!ec)
        {
            auto endpoint = peer->remote_endpoint();

            bool address_matches = which.address().is_unspecified() || endpoint.address() == which.address();
            bool port_matches = which.port() == 0 || endpoint.port() == which.port();

            if (address_matches && port_matches)
            {
                _dbg_ << "TCP relay " << this << " accepted peer " << endpoint;

                if (m_near.is_open())
                    m_away = std::move(*peer);
                else
                    m_near = std::move(*peer);

                start_relay();
            }
            else
            {
                _wrn_ << "TCP relay " << this << " rejected wrong peer " << endpoint;

                boost::system::error_code ec;
                peer->shutdown(boost::asio::socket_base::shutdown_type::shutdown_both, ec);
                accept_peer(which);
            }
        }
        else
        {
            _err_ << "TCP relay " << this << " failed to accept: " << ec.message();
            break_relay();
        }
    }));
}

void tcp_relay::transmit_data(boost::asio::ip::tcp::socket& from, boost::asio::ip::tcp::socket& to)
{
    auto buffer = std::make_shared<std::array<uint8_t, 8192>>();
    from.async_read_some(boost::asio::buffer(buffer.get(), buffer->size()), 
        m_strand.wrap([this, weak = weak_from_this(), buffer, &from, &to](const boost::system::error_code& ec, std::size_t size)
        {
            auto self = weak.lock();
            if (!self)
                return;

            if (!ec)
            {
                m_timestamp = std::chrono::steady_clock::now();
                
                boost::asio::async_write(to, boost::asio::buffer(buffer.get(), size),
                    [this, weak, &from, &to](const boost::system::error_code& ec, std::size_t)
                    {
                        auto self = weak.lock();
                        if (!self)
                            return;

                        if (!ec)
                        {
                            m_timestamp = std::chrono::steady_clock::now();
                            transmit_data(from, to);
                        }
                        else
                        {
                            _dbg_ << "TCP relay " << this << " failed to write to " << to.remote_endpoint() << ": " << ec.message();
                            break_relay();
                        }
                    });
            }
            else
            {
                _dbg_ << "TCP relay " << this << " failed to read from " << from.remote_endpoint() << ": " << ec.message();
                break_relay();
            }
        }));
}

void tcp_relay::break_relay()
{
    boost::system::error_code ec;
    m_server.close(ec);

    m_near.close(ec);
    m_away.close(ec);

    m_idle_timer.cancel(ec);
    m_retry_timer.cancel(ec);

    if (m_clean)
    {
        _inf_ << "TCP relay " << this << " closed";

        m_clean();
        m_clean = nullptr;
    }
}

udp_relay::udp_relay(boost::asio::io_context& io, protocol proto, boost::posix_time::seconds idle, cleanup_function clean)
    : m_io(io)
    , m_strand(io)
    , m_socket(io, proto == protocol::udp6 ? boost::asio::ip::udp::v6() : boost::asio::ip::udp::v4())
    , m_timer(io)
    , m_idle(idle)
    , m_clean(clean)
{
    m_socket.set_option(boost::asio::socket_base::reuse_address(true));
    m_socket.bind(boost::asio::ip::udp::endpoint(proto == protocol::udp6 ? boost::asio::ip::udp::v6() : boost::asio::ip::udp::v4(), 0));
    
    _trc_("UDP relay " << this << " created (" << proto << ")");
}

udp_relay::~udp_relay()
{
    _trc_("UDP relay " << this << " destroyed");
    break_relay();
}

protocol udp_relay::get_protocol() const
{
    return m_socket.is_open() && m_socket.local_endpoint().address().is_v6() 
           ? protocol::udp6 : protocol::udp4;
}

endpoint udp_relay::get_endpoint() const
{
    return endpoint(get_outgoing_address(m_io, m_socket.local_endpoint().address()), m_socket.local_endpoint().port());
}

void udp_relay::start(const peer& red, const peer& blue)
{
    _inf_ << "UDP relay " << this << " starting, red=" << red << ", blue=" << blue;

    if (red.role() == schema::server)
    {
        if (m_near.address().is_unspecified() || m_near.port() == 0)
            m_near = boost::asio::ip::udp::endpoint(red.location().address(), red.location().port());
        else
            m_away = boost::asio::ip::udp::endpoint(red.location().address(), red.location().port());
    }

    if (blue.role() == schema::server)
    {
        if (m_near.address().is_unspecified() || m_near.port() == 0)
            m_near = boost::asio::ip::udp::endpoint(blue.location().address(), blue.location().port());
        else
            m_away = boost::asio::ip::udp::endpoint(blue.location().address(), blue.location().port());
    }

    read_socket();
    watch_activity();
}

void udp_relay::close()
{
    _inf_ << "UDP relay " << this << " closing...";
    
    m_strand.post([weak = weak_from_this()]()
    {
        if (auto self = weak.lock())
        {
            self->break_relay();
        }
    });
}

void udp_relay::watch_activity()
{
    m_timer.expires_from_now(m_idle);
    m_timer.async_wait(m_strand.wrap([this, weak = weak_from_this()](const boost::system::error_code& ec)
    {
        auto self = weak.lock();
        if (!self)
            return;
        
        if (!ec)
        {
            auto expired = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - m_timestamp
            );

            if (expired.count() >= m_idle.total_seconds())
            {
                _inf_ << "UDP relay " << this << " idle timeout";
                break_relay();
            }
            else
            {
                watch_activity();
            }
        }
    }));
}

bool udp_relay::can_transmit() const
{
    return !m_near.address().is_unspecified() && m_near.port() != 0 && !m_away.address().is_unspecified() && m_away.port() != 0;
}

void udp_relay::read_socket()
{
    auto buffer = std::make_shared<std::array<uint8_t, 8192>>();
    auto peer = std::make_shared<boost::asio::ip::udp::endpoint>();

    m_socket.async_receive_from(boost::asio::buffer(*buffer), *peer,
        m_strand.wrap([this, weak = weak_from_this(), buffer, peer](const boost::system::error_code& ec, std::size_t size)
        {
            auto self = weak.lock();
            if (!self)
                return;

            if (!ec)
            {
                if (!can_transmit())
                {
                    bool near_address_matches = m_near.address().is_unspecified() || peer->address() == m_near.address();
                    bool near_port_matches = m_near.port() == 0 || peer->port() == m_near.port();
                    bool away_address_matches = m_away.address().is_unspecified() || peer->address() == m_away.address();
                    bool away_port_matches = m_away.port() == 0 || peer->port() == m_away.port();

                    if (near_address_matches && near_port_matches)
                    {
                        _dbg_ << "UDP relay " << this << " connected near peer " << *peer;
                        m_timestamp = std::chrono::steady_clock::now();
                        m_near = *peer;
                    }
                    else if (away_address_matches && away_port_matches)
                    {
                        _dbg_ << "UDP relay " << this << " connected away peer " << *peer;
                        m_timestamp = std::chrono::steady_clock::now();
                        m_away = *peer;
                    }
                    else
                    {
                        _wrn_ << "UDP relay " << this << " rejected wrong peer " << *peer;
                    }

                    if (can_transmit())
                    {
                        _inf_ << "UDP relay " << this << " is active";
                    }
                }

                if (can_transmit() && (*peer == m_near || *peer == m_away))
                {
                    m_timestamp = std::chrono::steady_clock::now();
                    m_socket.async_send_to(boost::asio::buffer(buffer->data(), size), *peer == m_near ? m_away : m_near, 
                        m_strand.wrap([this, weak, peer](const boost::system::error_code& ec, std::size_t)
                    {
                        auto self = weak.lock();
                        if (!self)
                            return;

                        if (ec)
                        {
                            _dbg_ << "TCP relay " << this << " failed to send to "  << *peer << ": " << ec.message();
                            break_relay();
                        }
                    }));
                }

                read_socket();
            }
            else
            {
                _dbg_ << "UDP relay " << this << " failed to receive: " << ec.message();
                break_relay();
            }
        }));
}

void udp_relay::break_relay()
{
    boost::system::error_code ec;
    m_socket.close(ec);
    m_timer.cancel(ec);

    if (m_clean)
    {
        _inf_ << "UDP relay " << this << " closed";

        m_clean();
        m_clean = nullptr;
    }
}

}