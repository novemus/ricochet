#include "session.h"
#include <boost/bind.hpp>

namespace ricochet {

session::session(boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket,
                 boost::posix_time::seconds idle)
    : m_socket(std::move(socket))
    , m_timer(m_socket.get_executor())
    , m_idle(idle)
{
}

session::~session()
{
    close();
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
    
    auto ptr = m_relay.lock();
    if (ptr)
    {
        ptr->close();
    }
}

void session::do_read()
{
    start_timer();
    auto self = shared_from_this();
    
    m_query.resize(4096); 

    // Read tag first (1 byte)
    m_socket.async_read_some(boost::asio::buffer(m_query.data(), 1),
        [this, self](const boost::system::error_code& ec, std::size_t size)
        {
            if (!ec)
            {
                if (size == 1)
                {
                    // Read length field (4 bytes)
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
    auto self = shared_from_this();
    
    // Read length field (4 bytes) after tag
    m_socket.async_read_some(boost::asio::buffer(m_query.data() + 1, 4), [this, self](const boost::system::error_code& ec, std::size_t size)
    {
        if (!ec)
        {
            if (size == 4)
            {
                // Parse length from network byte order (big-endian) - this is the payload length
                uint32_t length = ntohl(*reinterpret_cast<const uint32_t*>(m_query.data() + 1));

                // Check for reasonable size limits
                if (length > 4091)
                {
                    send_error_reply(ricochet::failure::malformed_query);
                    return;
                }

                // Read the payload
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
    auto self = shared_from_this();
    
    // Ensure buffer is large enough for tag + length + payload
    if (m_query.size() < 5 + length)
        m_query.resize(5 + length);
    
    m_socket.async_read_some(boost::asio::buffer(m_query.data() + 5, length), [this, self, length](const boost::system::error_code& ec, std::size_t size)
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
        // Use query buffer directly (no copying!)
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
    catch (const std::exception& e)
    {
        send_error_reply(ricochet::failure::malformed_query);
    }
    
    do_read();
}

void session::do_write(const ricochet::reply& msg)
{
    start_timer();
    auto self = shared_from_this();
    boost::asio::async_write(m_socket, boost::asio::buffer(msg.data(), msg.size()),
        [this, self](const boost::system::error_code& ec, std::size_t)
        {
            if (ec)
            {
                close();
            }
        });
}

void session::handle_provide_query(const ricochet::query& msg)
{
    try
    {
        auto relay = create_relay(std::get<ricochet::protocol>(msg.payload()));
        auto endpoint = relay->bind();
        auto msg = ricochet::reply::make_binding_reply(endpoint.address(), endpoint.port());
        do_write(msg);
        m_relay = relay;
    }
    catch (const protocol_unavailable& e)
    {
        send_error_reply(ricochet::failure::unavalable_proto);
    }
    catch (const std::exception& e)
    {
        send_error_reply(ricochet::failure::malformed_query);
    }
}

void session::handle_connect_query(const ricochet::query& msg)
{
    try
    {
        auto couple_payload = std::get<ricochet::couple>(msg.payload());
        
        auto relay = m_relay.lock();
        if (relay)
        {
            auto peer_one = couple_payload.one();
            auto peer_two = couple_payload.two();
            
            // Validate that peer endpoints match the protocol from provide query
            bool peer_one_is_ipv6 = peer_one.location().address().is_v6();
            bool peer_two_is_ipv6 = peer_two.location().address().is_v6();
            
            // Check if peers match the expected protocol
            bool protocol_matches = false;
            protocol relay_protocol = relay->get_protocol();
            if (relay_protocol == protocol::tcp4 || relay_protocol == protocol::udp4)
                protocol_matches = !peer_one_is_ipv6 && !peer_two_is_ipv6; // Both should be IPv4
            else if (relay_protocol == protocol::tcp6 || relay_protocol == protocol::udp6)
                protocol_matches = peer_one_is_ipv6 && peer_two_is_ipv6; // Both should be IPv6
            
            if (!protocol_matches)
            {
                send_error_reply(ricochet::failure::malformed_query);
                return;
            }
            
            // Start relay connections asynchronously
            relay->start(
                peer_one.location(), peer_one.role(),
                peer_two.location(), peer_two.role());
            
            // Send immediate confirm reply
            auto msg = ricochet::reply::make_confirm_reply();
            do_write(msg);
        }
        else
        {
            send_error_reply(ricochet::failure::server_error);
        }
    }
    catch (const std::exception& e)
    {
        send_error_reply(ricochet::failure::malformed_query);
    }
}

void session::send_error_reply(ricochet::failure error)
{
    auto reply_msg = ricochet::reply::make_mistake_reply(error);
    do_write(reply_msg);
}

void session::error(ricochet::failure error)
{
    send_error_reply(error);
    close();
}

std::shared_ptr<ricochet::relay> session::create_relay(ricochet::protocol proto)
{
    std::shared_ptr<ricochet::relay> relay;
    
    switch (proto)
    {
        case ricochet::protocol::tcp4:
        case ricochet::protocol::tcp6:
            relay = std::make_shared<ricochet::tcp_relay>(proto == ricochet::protocol::tcp6, m_idle);
            break;
            
        case ricochet::protocol::udp4:
        case ricochet::protocol::udp6:
            relay = std::make_shared<ricochet::udp_relay>(proto == ricochet::protocol::udp6, m_idle);
            break;
            
        default:
            throw std::runtime_error("Unsupported protocol");
    }
    
    return relay;
}

void session::start_timer()
{
    m_timer.expires_from_now(m_idle);
    auto self = shared_from_this();
    m_timer.async_wait(
        [this, self](const boost::system::error_code& ec)
        {
            if (!ec)
            {
                handle_timeout();
            }
        });
}

void session::handle_timeout()
{
    close();
}

}