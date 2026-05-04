#include <boost/bind.hpp>
#include <session.h>

namespace ricochet {

session::session(boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket,
                 boost::posix_time::seconds idle)
    : m_socket(std::move(socket))
    , m_timer(m_socket.get_executor())
    , m_idle(idle)
{
    m_query.resize(4096); 
}

session::~session()
{
    close();
    if (m_clean) 
        m_clean();
}

void session::set_cleaner(std::function<void()> clean)
{
    m_clean = clean;
}

void session::start()
{
    start_timer();
    do_read();
}

void session::close()
{
    boost::system::error_code ec;

    m_timer.cancel(ec);

    if (m_socket.lowest_layer().is_open())
    {
        m_socket.shutdown(ec);
        m_socket.lowest_layer().close(ec);
    }

    if (m_relay)
    {
        m_relay->close();
    }
}

void session::do_read()
{
    start_timer();
    m_socket.async_read_some(boost::asio::buffer(m_query.data(), 1),
        [this, self = shared_from_this()](const boost::system::error_code& ec, std::size_t size)
        {
            if (!ec)
            {
                if (size == 1)
                {
                    if (m_relay && m_query.type() != ricochet::query::kind::connect)
                    {
                        send_error_reply(ricochet::failure::malformed_query);
                        return;
                    }
                    do_read_length();
                }
                else
                {
                    close();
                }
            }
            else
            {
                close();
            }
        });
}

void session::do_read_length()
{
    start_timer();
    m_socket.async_read_some(boost::asio::buffer(m_query.data() + 1, 4),
        [this, self = shared_from_this()](const boost::system::error_code& ec, std::size_t size)
        {
            if (!ec)
            {
                if (size == 4)
                {
                    uint32_t length = ntohl(*reinterpret_cast<const uint32_t*>(m_query.data() + 1));
                    if (length > m_query.size() - 5)
                    {
                        send_error_reply(ricochet::failure::malformed_query);
                        return;
                    }

                    do_read_payload(length);
                }
                else
                {
                    close();
                }
            }
            else
            {
                close();
            }
        });
}

void session::do_read_payload(uint32_t length)
{
    start_timer();
    m_socket.async_read_some(boost::asio::buffer(m_query.data() + 5, length),
        [this, self = shared_from_this(), length](const boost::system::error_code& ec, std::size_t size)
        {
            if (!ec)
            {
                if (size == length)
                {
                    handle_message();
                }
                else
                {
                    close();
                }
            }
            else
            {
                close();
            }
        });
}

void session::handle_message()
{
    try
    {
        switch (m_query.type())
        {
            case ricochet::query::kind::provide:
                handle_provide_query(m_query);
                break;
            case ricochet::query::kind::connect:
                handle_connect_query(m_query);
                break;
            default:
                send_error_reply(ricochet::failure::malformed_query);
                break;
        }
    }
    catch (const malformed_query& e)
    {
        send_error_reply(ricochet::failure::malformed_query);
    }
    catch (const protocol_unavailable& e)
    {
        send_error_reply(ricochet::failure::unavailable_proto);
    }
    catch (const std::exception& e)
    {
        send_error_reply(ricochet::failure::server_error);
    }
}

void session::do_write(const ricochet::reply& reply)
{
    start_timer();
    boost::asio::async_write(m_socket, boost::asio::buffer(reply.data(), reply.size()),
        [this, self = shared_from_this()](const boost::system::error_code& ec, std::size_t)
        {
            if (ec)
            {
                close();
            }
        });
}

void session::handle_provide_query(const ricochet::query& msg)
{
    auto proto = std::get<ricochet::protocol>(msg.payload());
    switch (proto)
    {
        case ricochet::protocol::tcp4:
        case ricochet::protocol::tcp6:
            m_relay = std::make_shared<ricochet::tcp_relay>(proto == ricochet::protocol::tcp6, m_idle);
            break;
        case ricochet::protocol::udp4:
        case ricochet::protocol::udp6:
            m_relay = std::make_shared<ricochet::udp_relay>(proto == ricochet::protocol::udp6, m_idle);
            break;
        default:
            throw malformed_query("Unsupported protocol");
    }

    auto endpoint = m_relay->bind();
    do_write(ricochet::reply::make_binding_reply(endpoint.address(), endpoint.port()));

    if (m_clean) 
    {
        m_relay->set_cleaner(m_clean);
        m_clean = nullptr;
    }
}

void session::handle_connect_query(const ricochet::query& msg)
{
    auto payload = std::get<ricochet::couple>(msg.payload());

    if (m_relay)
    {
        auto peer_one = payload.one();
        auto peer_two = payload.two();
        
        bool peer_one_is_ipv6 = peer_one.location().address().is_v6();
        bool peer_two_is_ipv6 = peer_two.location().address().is_v6();
        
        bool protocol_matches = false;
        protocol relay_protocol = m_relay->get_protocol();
        if (relay_protocol == protocol::tcp4 || relay_protocol == protocol::udp4)
            protocol_matches = !peer_one_is_ipv6 && !peer_two_is_ipv6;
        else if (relay_protocol == protocol::tcp6 || relay_protocol == protocol::udp6)
            protocol_matches = peer_one_is_ipv6 && peer_two_is_ipv6;
        
        if (!protocol_matches)
        {
            send_error_reply(ricochet::failure::malformed_query);
            return;
        }
        
        m_relay->start(
            peer_one.location(), peer_one.role(),
            peer_two.location(), peer_two.role());
        
        do_write(ricochet::reply::make_confirm_reply());
        do_read();
    }
    else
    {
        send_error_reply(ricochet::failure::server_error);
    }
}

void session::send_error_reply(ricochet::failure error)
{
    do_write(ricochet::reply::make_mistake_reply(error));
}

void session::error(ricochet::failure error)
{
    send_error_reply(error);
    close();
}

void session::start_timer()
{
    m_timer.expires_from_now(m_idle);
    m_timer.async_wait([this, self = shared_from_this()](const boost::system::error_code& ec)
    {
        if (!ec)
        {
            close();
        }
    });
}

}