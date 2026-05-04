#include <boost/asio/ip/tcp.hpp>
#include <memory>
#include <chrono>
#include <iostream>
#include <ricochet.h>
#include <relay.h>

namespace ricochet {

boost::asio::ip::address get_outgoing_address(boost::asio::io_context& io, boost::asio::ip::address address)
{
    if (!address.is_unspecified())
        return address;

    try 
    {
        boost::asio::ip::udp::socket socket(io, address.is_v4() ? boost::asio::ip::udp::v4() : boost::asio::ip::udp::v6());
        socket.set_option(boost::asio::socket_base::reuse_address(true));
        socket.bind(boost::asio::ip::udp::endpoint(address, 0));

        auto remote = boost::asio::ip::udp::endpoint(
            address.is_v4() ? boost::asio::ip::make_address("8.8.8.8") : boost::asio::ip::make_address("2001:4860:4860::8888"),
            53);

        socket.connect(remote);
        auto local = socket.local_endpoint();

        return local.address();
    } 
    catch (const boost::system::system_error& ex) 
    {
        std::cerr << "Error: " << ex.what() << std::endl;
    }

    throw protocol_unavailable("Failed to determine outgoing address");
}

tcp_relay::tcp_relay(boost::asio::io_context& io, protocol proto, boost::posix_time::seconds idle, cleanup_function clean)
    : m_io(io)
    , m_strand(io)
    , m_server(io)
    , m_timer(io)
    , m_idle(idle)
    , m_clean(clean)
    , m_timestamp(std::chrono::steady_clock::now())
{
    auto protocol = proto == protocol::tcp6 ? boost::asio::ip::tcp::v6() : boost::asio::ip::tcp::v4();

    m_server.open(protocol);
    m_server.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
    m_server.bind(boost::asio::ip::tcp::endpoint(protocol, 0));
    m_server.listen();
}

tcp_relay::~tcp_relay()
{
    break_relay();
}

protocol tcp_relay::get_protocol() const
{
    return m_server.is_open() && m_server.local_endpoint().address().is_v6() 
           ? protocol::tcp6 : protocol::tcp4;
}

endpoint tcp_relay::get_endpoint() const
{
    return endpoint(get_outgoing_address(m_io, m_server.local_endpoint().address()), m_server.local_endpoint().port());
}

void tcp_relay::start(const peer& red, const peer& blue)
{
    if (red.role() == schema::client)
    {
        accept_peer(red.location());
    }
    else
    {
        connect_peer(red.location());
    }
    
    if (blue.role() == schema::client)
    {
        accept_peer(blue.location());
    }
    else
    {
        connect_peer(blue.location());
    }

    watch_activity();
}

void tcp_relay::close()
{
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
    if (m_near && m_away)
    {
        boost::system::error_code ec;
        m_server.close(ec);

        transmit_data(m_near, m_away);
        transmit_data(m_away, m_near);
    }
}

void tcp_relay::watch_activity()
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
    auto peer = std::make_shared<boost::asio::ip::tcp::socket>(m_io);
    peer->async_connect(boost::asio::ip::tcp::endpoint(which.address(), which.port()), 
        m_strand.wrap([this, weak = weak_from_this(), peer](const boost::system::error_code& ec)
        {
            auto self = weak.lock();
            if (!self)
                return;

            if (!ec)
            {
                if (m_near)
                    m_away = peer;
                else
                    m_near = peer;

                start_relay();
            }
            else
            {
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
            auto location = peer->remote_endpoint();

            bool address_matches = which.address().is_unspecified() || location.address() == which.address();
            bool port_matches = which.port() == 0 || location.port() == which.port();

            if (address_matches && port_matches)
            {
                if (m_near)
                    m_away = peer;
                else
                    m_near = peer;

                start_relay();
            }
            else
            {
                accept_peer(which);
            }
        }
        else
        {
            accept_peer(which);
        }
    }));
}

void tcp_relay::transmit_data(socket_ptr from, socket_ptr to)
{
    auto buffer = std::make_shared<std::array<uint8_t, 8192>>();
    from->async_read_some(boost::asio::buffer(buffer.get(), buffer->size()), 
        m_strand.wrap([this, weak = weak_from_this(), buffer, from, to](const boost::system::error_code& ec, std::size_t size)
        {
            auto self = weak.lock();
            if (!self)
                return;

            if (!ec)
            {
                m_timestamp = std::chrono::steady_clock::now();
                
                boost::asio::async_write(*to, boost::asio::buffer(buffer.get(), size),
                    [this, weak, from, to](const boost::system::error_code& ec, std::size_t)
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
                            break_relay();
                        }
                    });
            }
            else
            {
                break_relay();
            }
        }));
}

void tcp_relay::break_relay()
{
    boost::system::error_code ec;
    m_server.close(ec);

    if (m_near)
        m_near->close(ec);

    if (m_away)
        m_away->close(ec);

    m_timer.cancel(ec);

    if (m_clean)
        m_clean();
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
}

udp_relay::~udp_relay()
{
    close();
    if (m_clean)
        m_clean();
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
    auto from = std::shared_ptr<boost::asio::ip::udp::endpoint>();

    m_socket.async_receive_from(boost::asio::buffer(*buffer), *from,
        m_strand.wrap([this, weak = weak_from_this(), buffer, from](const boost::system::error_code& ec, std::size_t size)
        {
            auto self = weak.lock();
            if (!self)
                return;

            if (!ec)
            {
                if (!can_transmit())
                {
                    bool near_address_matches = m_near.address().is_unspecified() || from->address() == m_near.address();
                    bool near_port_matches = m_near.port() == 0 || from->port() == m_near.port();
                    bool away_address_matches = m_away.address().is_unspecified() || from->address() == m_away.address();
                    bool away_port_matches = m_away.port() == 0 || from->port() == m_away.port();

                    if (near_address_matches && near_port_matches)
                    {
                        m_timestamp = std::chrono::steady_clock::now();
                        m_near = *from;
                    }
                    else if (away_address_matches && away_port_matches)
                    {
                        m_timestamp = std::chrono::steady_clock::now();
                        m_away = *from;
                    }
                }

                if (can_transmit())
                {
                    m_timestamp = std::chrono::steady_clock::now();
                    m_socket.async_send_to(boost::asio::buffer(buffer->data(), size), *from == m_near ? m_away : m_near, 
                        m_strand.wrap([this, weak](const boost::system::error_code& ec, std::size_t)
                    {
                        auto self = weak.lock();
                        if (!self)
                            return;

                        if (ec)
                            break_relay();
                    }));
                }

                read_socket();
            }
            else
            {
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
        m_clean();
}

}