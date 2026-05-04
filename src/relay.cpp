#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include "relay.h"

namespace ricochet {

boost::asio::ip::address get_outgoing_address(boost::asio::io_context& io, boost::asio::ip::address address)
{
    if (!address.is_unspecified())
        return address;

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

tcp_relay::tcp_relay(boost::asio::io_context& io, bool v6, boost::posix_time::seconds idle)
    : m_io(io)
    , m_server(io)
    , m_one(io, v6 ? boost::asio::ip::tcp::v6() : boost::asio::ip::tcp::v4())
    , m_two(io, v6 ? boost::asio::ip::tcp::v6() : boost::asio::ip::tcp::v4())
    , m_timer(io)
    , m_idle(idle)
{
    auto protocol = v6 ? boost::asio::ip::tcp::v6() : boost::asio::ip::tcp::v4();
    
    m_server.open(protocol);
    m_server.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
    m_server.bind(boost::asio::ip::tcp::endpoint(protocol, 0));
    m_server.listen();
}

void tcp_relay::set_cleaner(std::function<void()> clean)
{
    m_clean = clean;
}

boost::asio::ip::address tcp_relay::get_address() const
{
    return get_outgoing_address(m_io, m_server.local_endpoint().address());
}

uint16_t tcp_relay::get_port() const
{
    return m_server.local_endpoint().port();
}

void tcp_relay::start(const endpoint& one_loc, schema one_role, const endpoint& two_loc, schema two_role)
{
    // TODO: Implement TCP relay start logic
}

void tcp_relay::close()
{
    boost::system::error_code ec;
    m_server.close(ec);
    m_one.close(ec);
    m_two.close(ec);
    m_timer.cancel(ec);
}

udp_relay::udp_relay(boost::asio::io_context& io, bool v6, boost::posix_time::seconds idle)
    : m_io(io)
    , m_socket(io, v6 ? boost::asio::ip::udp::v6() : boost::asio::ip::udp::v4())
    , m_timer(io)
    , m_idle(idle)
{
    m_socket.set_option(boost::asio::socket_base::reuse_address(true));
    m_socket.bind(boost::asio::ip::udp::endpoint(v6 ? boost::asio::ip::udp::v6() : boost::asio::ip::udp::v4(), 0));
}

void udp_relay::set_cleaner(std::function<void()> clean)
{
    m_clean = clean;
}

boost::asio::ip::address udp_relay::get_address() const
{
    return get_outgoing_address(m_io, m_socket.local_endpoint().address());
}

uint16_t udp_relay::get_port() const
{
    return m_socket.local_endpoint().port();
}

void udp_relay::start(const endpoint& one_loc, schema one_role, const endpoint& two_loc, schema two_role)
{
    // TODO: Implement UDP relay start logic
}

void udp_relay::close()
{
    boost::system::error_code ec;
    m_socket.close(ec);
    m_timer.cancel(ec);
}

protocol tcp_relay::get_protocol() const
{
    return m_server.is_open() && m_server.local_endpoint().address().is_v6() 
           ? protocol::tcp6 : protocol::tcp4;
}

protocol udp_relay::get_protocol() const
{
    return m_socket.is_open() && m_socket.local_endpoint().address().is_v6() 
           ? protocol::udp6 : protocol::udp4;
}

tcp_relay::~tcp_relay()
{
    close();
    if (m_clean)
        m_clean();
}

udp_relay::~udp_relay()
{
    close();
    if (m_clean)
        m_clean();
}

}