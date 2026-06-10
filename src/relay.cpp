#include <boost/asio.hpp>
#if defined(__APPLE__) || defined(__linux__)
#include <sys/socket.h>
#endif
#include <memory>
#include <chrono>
#include <cstdlib>
#include <algorithm>
#include <random>
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

heap::heap(uint16_t base, uint16_t span)
{
    m_heap.resize(span);

    for (size_t i = 0; i < span; ++i)
        m_heap[i] = i + base;

    std::random_device rd;
    std::shuffle(m_heap.begin(), m_heap.end(), std::mt19937(rd()));
}

boost::asio::ip::tcp::acceptor heap::make_tcp_relay(boost::asio::io_context& io, bool ip4)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    boost::system::error_code ec;
    for (size_t i = 0; i < m_heap.size(); ++i)
    {
        if (m_next == m_heap.size())
            m_next = 0;

        boost::asio::ip::tcp::endpoint ep(get_outgoing_address(io, ip4), m_heap[m_next++]);
        boost::asio::ip::tcp::socket socket(io);
        if (!socket.open(ep.protocol(), ec) && !socket.bind(ep, ec) && !socket.close(ec))
        {
            boost::asio::ip::tcp::acceptor relay(io);
            relay.open(ep.protocol());
            relay.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
#if defined(__APPLE__) || defined(__linux__)
            int optval = 1;
            ::setsockopt(relay.native_handle(), SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
#endif
            relay.bind(ep);
            relay.listen();
            return std::move(relay);
        }
    }

    throw std::runtime_error("No free port");
}

boost::asio::ip::udp::socket heap::make_udp_relay(boost::asio::io_context& io, bool ip4)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    boost::system::error_code ec;
    for (size_t i = 0; i < m_heap.size(); ++i)
    {
        if (m_next == m_heap.size())
            m_next = 0;

        boost::asio::ip::udp::endpoint ep(get_outgoing_address(io, ip4), m_heap[m_next++]);
        boost::asio::ip::udp::socket socket(io);
        if (!socket.open(ep.protocol(), ec) && !socket.bind(ep, ec) && !socket.close(ec))
        {
            boost::asio::ip::udp::socket relay(io);
            relay.open(ep.protocol());
            relay.set_option(boost::asio::socket_base::reuse_address(true));
            relay.bind(ep);
            return std::move(relay);
        }
    }

    throw std::runtime_error("No free port");
}

tcp_relay::tcp_relay(boost::asio::io_context& io, boost::asio::ip::tcp::acceptor server, boost::posix_time::seconds wait, boost::posix_time::seconds idle)
    : m_io(io)
    , m_strand(io)
    , m_server(std::move(server))
    , m_one(io)
    , m_two(io)
    , m_timer(io)
    , m_defer(io)
    , m_wait(wait)
    , m_idle(idle)
    , m_timestamp(std::chrono::steady_clock::now() - std::chrono::seconds(5))
    , m_reconnects(0)
{
    _inf_ << "TCP relay " << this << " created on " << m_server.local_endpoint();
}

tcp_relay::~tcp_relay()
{
    break_relay();
    _trc_("TCP relay " << this << " destroyed");
}

protocol tcp_relay::get_protocol() const
{
    return m_server.is_open() && m_server.local_endpoint().address().is_v6() ? protocol::tcp6 : protocol::tcp4;
}

endpoint tcp_relay::get_endpoint() const
{
    return endpoint(get_outgoing_address(m_io, m_server.local_endpoint().address()), m_server.local_endpoint().port());
}

void tcp_relay::start(const peer& red, const peer& blue, final_callback&& final)
{
    _inf_ << "TCP relay " << this << " starting, red=" << red << ", blue=" << blue;

    m_final = std::move(final);

    start_relay(red, blue);
    watch_activity(m_wait);
}

void tcp_relay::close()
{
    _inf_ << "TCP relay " << this << " closing...";
    
    boost::asio::post(m_strand, [weak = weak_from_this()]()
    {
        if (auto self = weak.lock())
        {
            self->break_relay();
        }
    });
}

void tcp_relay::watch_activity(boost::posix_time::seconds timeout)
{
    m_timer.expires_from_now(timeout);
    m_timer.async_wait(m_strand.wrap([this, weak = weak_from_this(), timeout](const boost::system::error_code& ec)
    {
        auto self = weak.lock();
        if (!self)
            return;

        if (!ec)
        {
            auto expired = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - m_timestamp
            );

            if (expired.count() >= timeout.total_seconds())
            {
                _inf_ << "TCP relay " << this << " timeout";
                break_relay();
            }
            else
            {
                watch_activity(m_idle);
            }
        }
    }));
}

void tcp_relay::start_relay(const peer& one, const peer& two)
{
    if (m_one.is_open() && m_two.is_open())
    {
        _inf_ << "TCP relay " << this << " is active";

        boost::system::error_code ec;
        m_server.close(ec);

        m_timestamp = std::chrono::steady_clock::now();

        transmit_data(m_one, m_two);
        transmit_data(m_two, m_one);
    }
    else if ((!m_one.is_open() && one.role() == schema::client) || (!m_two.is_open() && two.role() == schema::client))
    {
        auto peer = std::make_shared<boost::asio::ip::tcp::socket>(m_io); 
        m_server.async_accept(*peer, m_strand.wrap([this, weak = weak_from_this(), peer, one, two](const boost::system::error_code& ec)
        {
            auto self = weak.lock();
            if (!self)
                return;

            if (!ec)
            {
                auto ep = peer->remote_endpoint();
                auto ep1 = one.location();
                auto ep2 = two.location();

                bool one_matches = !m_one.is_open() && one.role() == schema::client
                                && (ep1.address().is_unspecified() || ep.address() == ep1.address())
                                && (ep1.port() == 0 || ep.port() == ep1.port());
                bool two_matches = !m_two.is_open() && two.role() == schema::client
                                && (ep2.address().is_unspecified() || ep.address() == ep2.address())
                                && (ep2.port() == 0 || ep.port() == ep2.port());

                if (one_matches)
                {
                    _dbg_ << "TCP relay " << this << " accepted peer " << ep;
                    m_one = std::move(*peer);
                }
                else if (two_matches)
                {
                    _dbg_ << "TCP relay " << this << " accepted peer " << ep;
                    m_two = std::move(*peer);
                }
                else
                {
                    _wrn_ << "TCP relay " << this << " rejected wrong peer " << ep;

                    boost::system::error_code ec;
                    peer->shutdown(boost::asio::socket_base::shutdown_type::shutdown_both, ec);
                }

                start_relay(one, two);
            }
            else
            {
                _err_ << "TCP relay " << this << " failed to accept: " << ec.message();
                break_relay();
            }
        }));
    }
    else if ((!m_one.is_open() && one.role() == schema::server) || (!m_two.is_open() && two.role() == schema::server))
    {
        auto local = m_server.local_endpoint();
        auto peer = std::make_shared<boost::asio::ip::tcp::socket>(m_io);

        try
        {
            peer->open(local.protocol());
            peer->set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
#if defined(__APPLE__) || defined(__linux__)
            int optval = 1;
            ::setsockopt(peer->native_handle(), SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
#endif
            peer->bind(local);
        }
        catch (const std::exception& e)
        {
            _err_ << "TCP relay " << this << " failed to bind: " << e.what();

            break_relay();
            return;
        }

        boost::asio::ip::tcp::endpoint ep = m_one.is_open() 
                                          ? boost::asio::ip::tcp::endpoint(two.location().address(), two.location().port())
                                          : boost::asio::ip::tcp::endpoint(one.location().address(), one.location().port());

        peer->async_connect(ep, m_strand.wrap([this, weak = weak_from_this(), peer, ep, one, two](const boost::system::error_code& ec)
        {
            auto self = weak.lock();
            if (!self)
                return;

            if (!ec)
            {
                _dbg_ << "TCP relay " << this << " connected to peer " << ep;

                if (ep.address() == one.location().address() && ep.port() == one.location().port())
                    m_one = std::move(*peer);
                else
                    m_two = std::move(*peer);

                m_reconnects = 0;
                start_relay(one, two);
            }
            else if (ec == boost::asio::error::connection_refused && m_reconnects < 4)
            {
                m_reconnects++;
                _wrn_ << "TCP relay " << this << " connection refused to peer " << ep << ", retrying in " << m_reconnects * 2 << " seconds";

                m_defer.expires_from_now(boost::posix_time::seconds(m_reconnects * 2));
                m_defer.async_wait(m_strand.wrap([this, weak = weak_from_this(), one, two](const boost::system::error_code& ec)
                {
                    auto self = weak.lock();
                    if (!self)
                        return;

                    if (!ec)
                        start_relay(one, two);
                }));
            }
            else
            {
                _err_ << "TCP relay " << this << " failed to connect to peer " << ep << ": " << ec.message();
                break_relay();
            }
        }));
    }
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
                            _dbg_ << "TCP relay " << this << " failed to write data: " << ec.message();
                            break_relay();
                        }
                    });
            }
            else
            {
                _dbg_ << "TCP relay " << this << " failed to read data: " << ec.message();
                break_relay();
            }
        }));
}

void tcp_relay::break_relay()
{
    boost::system::error_code ec;
    m_server.close(ec);

    m_one.close(ec);
    m_two.close(ec);

    m_timer.cancel(ec);
    m_defer.cancel(ec);

    if (m_final)
    {
        _inf_ << "TCP relay " << this << " closed";

        m_final();
        m_final = nullptr;
    }
}

udp_relay::udp_relay(boost::asio::io_context& io, boost::asio::ip::udp::socket socket, boost::posix_time::seconds wait, boost::posix_time::seconds idle)
    : m_io(io)
    , m_strand(io)
    , m_socket(std::move(socket))
    , m_timer(io)
    , m_wait(wait)
    , m_idle(idle)
    , m_timestamp(std::chrono::steady_clock::now() - std::chrono::seconds(5))
{
    _inf_ << "UDP relay " << this << " created on " << m_socket.local_endpoint();
}

udp_relay::~udp_relay()
{
    break_relay();
    _trc_("UDP relay " << this << " destroyed");
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

void udp_relay::start(const peer& red, const peer& blue, final_callback&& final)
{
    _inf_ << "UDP relay " << this << " starting, red=" << red << ", blue=" << blue;

    m_one = boost::asio::ip::udp::endpoint(red.location().address(), red.location().port());
    m_two = boost::asio::ip::udp::endpoint(blue.location().address(), blue.location().port());
    m_final = std::move(final);

    read_socket();
    watch_activity(m_wait);
}

void udp_relay::close()
{
    _inf_ << "UDP relay " << this << " closing...";
    
    boost::asio::post(m_strand, [weak = weak_from_this()]()
    {
        if (auto self = weak.lock())
        {
            self->break_relay();
        }
    });
}

void udp_relay::watch_activity(boost::posix_time::seconds timeout)
{
    m_timer.expires_from_now(timeout);
    m_timer.async_wait(m_strand.wrap([this, weak = weak_from_this(), timeout](const boost::system::error_code& ec)
    {
        auto self = weak.lock();
        if (!self)
            return;

        if (!ec)
        {
            auto expired = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - m_timestamp
            );

            if (expired.count() >= timeout.total_seconds())
            {
                _inf_ << "UDP relay " << this << " timeout";
                break_relay();
            }
            else
            {
                watch_activity(m_idle);
            }
        }
    }));
}

bool udp_relay::can_transmit() const
{
    return !m_one.address().is_unspecified() && m_one.port() != 0 && !m_two.address().is_unspecified() && m_two.port() != 0;
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
                    bool one_address_matches = m_one.address().is_unspecified() || (m_one.port() == 0 && peer->address() == m_one.address());
                    bool one_port_matches = m_one.port() == 0 || (m_one.address().is_unspecified() && peer->port() == m_one.port());
                    bool two_address_matches = m_two.address().is_unspecified() || (m_two.port() == 0 && peer->address() == m_two.address());
                    bool two_port_matches = m_two.port() == 0 || (m_two.address().is_unspecified() && peer->port() == m_two.port());

                    if (one_address_matches && one_port_matches)
                    {
                        _dbg_ << "UDP relay " << this << " connected peer " << *peer;
                        m_one = *peer;
                    }
                    else if (two_address_matches && two_port_matches)
                    {
                        _dbg_ << "UDP relay " << this << " connected peer " << *peer;
                        m_two = *peer;
                    }
                    else
                    {
                        _wrn_ << "UDP relay " << this << " rejected wrong peer " << *peer;
                    }

                    if (can_transmit())
                    {
                        m_timestamp = std::chrono::steady_clock::now();
                        _inf_ << "UDP relay " << this << " is active";
                    }
                }

                if (can_transmit() && (*peer == m_one || *peer == m_two))
                {
                    m_timestamp = std::chrono::steady_clock::now();
                    m_socket.async_send_to(boost::asio::buffer(buffer->data(), size), *peer == m_one ? m_two : m_one, 
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

    if (m_final)
    {
        _inf_ << "UDP relay " << this << " closed";

        m_final();
        m_final = nullptr;
    }
}

}