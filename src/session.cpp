#include <boost/bind.hpp>
#include "session.h"

namespace ricochet {

session::session(boost::asio::io_context& io,
                std::shared_ptr<boost::asio::ssl::context> ssl,
                 boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket,
                 boost::posix_time::seconds idle)
    : m_io(io)
    , m_ssl(ssl)
    , m_socket(std::move(socket))
    , m_strand(m_io)
    , m_timer(m_io)
    , m_idle(idle)
    , m_query(4096)
{
}

session::~session()
{
    do_close();
}

void session::set_cleaner(cleanup_function clean)
{
    m_strand.post([this, weak = weak_from_this(), clean]()
    {
        if (auto self = weak.lock())
        {
            m_clean = clean;
        }
    });
}

void session::start()
{
    do_read_header();
}

void session::close()
{
    m_strand.post([weak = weak_from_this()]()
    {
        if (auto self = weak.lock())
        {
            self->do_close();
        }
    });
}

void session::error(ricochet::failure error)
{
    m_strand.post([weak = weak_from_this(), error]()
    {
        if (auto self = weak.lock())
        {
            self->send_error_reply(error);
        }
    });
}

void session::do_close()
{
    boost::system::error_code ec;
    m_timer.cancel(ec);

    if (m_relay)
        m_relay->close();

    if (m_socket.lowest_layer().is_open())
    {
        boost::system::error_code ec;
        m_socket.lowest_layer().shutdown(boost::asio::socket_base::shutdown_type::shutdown_both, ec);
        m_socket.lowest_layer().close(ec);
    }

    if (m_clean)
    {
        m_clean();
        m_clean = nullptr;
    }
}

void session::do_shutdown()
{
    boost::system::error_code ec;
    m_timer.cancel(ec);

    if (m_socket.lowest_layer().is_open())
    {
        auto finalize = [this, weak = weak_from_this()](const boost::system::error_code&)
        {
            auto self = weak.lock();
            if (!self)
                return;

            boost::system::error_code ec;
            m_socket.lowest_layer().shutdown(boost::asio::socket_base::shutdown_type::shutdown_both, ec);
            m_socket.lowest_layer().close(ec);

            if (m_relay)
                m_relay->close();

            if (m_clean)
            {
                m_clean();
                m_clean = nullptr;
            }
        };

        m_timer.expires_from_now(boost::posix_time::seconds(2));
        m_timer.async_wait(m_strand.wrap(finalize));
        m_socket.async_shutdown(m_strand.wrap(finalize));
    }
    else if (m_clean)
    {
        m_clean();
        m_clean = nullptr;
    }
}

void session::do_read_header()
{
    start_timer();
    m_socket.async_read_some(boost::asio::buffer(m_query.data(), query::header_size),
        m_strand.wrap([this, weak = weak_from_this()](const boost::system::error_code& ec, std::size_t size)
        {
            auto self = weak.lock();
            if (!self)
                return;

            if (!ec)
            {
                if (size == query::header_size)
                {
                    if (m_relay && m_query.type() != ricochet::query::kind::connect)
                    {
                        send_error_reply(ricochet::failure::malformed_message);
                        return;
                    }
                    if (m_query.length() > m_query.size() - query::header_size)
                    {
                        send_error_reply(ricochet::failure::malformed_message);
                        return;
                    }
                    do_read_payload();
                }
                else
                {
                    send_error_reply(ricochet::failure::server_error);
                }
            }
            else if (ec == boost::asio::error::eof)
            {
                do_shutdown();
            }
            else
            {
                do_close();
            }
        }));
}

void session::do_read_payload()
{
    start_timer();
    m_socket.async_read_some(boost::asio::buffer(m_query.data() + query::header_size, m_query.length()),
        m_strand.wrap([this, weak = weak_from_this()](const boost::system::error_code& ec, std::size_t size)
        {
            auto self = weak.lock();
            if (!self)
                return;

            if (!ec)
            {
                if (size == m_query.length())
                {
                    handle_query();
                }
                else
                {
                    send_error_reply(ricochet::failure::server_error);
                }
            }
            else if (ec == boost::asio::error::eof)
            {
                do_shutdown();
            }
            else
            {
                do_close();
            }
        }));
}

void session::handle_query()
{
    try
    {
        switch (m_query.type())
        {
            case ricochet::query::kind::provide:
                handle_provide_query();
                break;
            case ricochet::query::kind::connect:
                handle_connect_query();
                break;
            default:
                send_error_reply(ricochet::failure::malformed_message);
                break;
        }
    }
    catch (const malformed_message& e)
    {
        send_error_reply(ricochet::failure::malformed_message);
    }
    catch (const unavailable_proto& e)
    {
        send_error_reply(ricochet::failure::unavailable_proto);
    }
    catch (const std::exception& e)
    {
        send_error_reply(ricochet::failure::server_error);
    }
}

void session::do_write(const ricochet::reply& msg)
{
    start_timer();
    boost::asio::async_write(m_socket, boost::asio::buffer(msg.data(), msg.size()),
        m_strand.wrap([this, weak = weak_from_this(), msg](const boost::system::error_code& ec, std::size_t)
        {
            auto self = weak.lock();
            if (!self)
                return;

            if (ec)
            {
                do_close();
            }
            else if (msg.type() == ricochet::reply::kind::binding)
            {
                do_read_header();
            }
            else if (msg.type() == ricochet::reply::kind::confirm)
            {
                do_shutdown();
            }
            else if (msg.type() == ricochet::reply::kind::mistake)
            {
                do_shutdown();
            }
        }));
}

void session::handle_provide_query()
{
    auto proto = std::get<ricochet::protocol>(m_query.payload());
    switch (proto)
    {
        case ricochet::protocol::tcp4:
        case ricochet::protocol::tcp6:
            m_relay = std::make_shared<ricochet::tcp_relay>(m_io, proto, m_idle, m_clean);
            break;
        case ricochet::protocol::udp4:
        case ricochet::protocol::udp6:
            m_relay = std::make_shared<ricochet::udp_relay>(m_io, proto, m_idle, m_clean);
            break;
        default:
            throw malformed_message("Unsupported protocol");
    }

    m_clean = nullptr;
    do_write(ricochet::reply::make_binding_reply(m_relay->get_endpoint()));
}

void session::handle_connect_query()
{
    auto payload = std::get<ricochet::couple>(m_query.payload());

    if (m_relay)
    {
        auto red = payload.red();
        auto blue = payload.blue();
        
        if (red.role() == ricochet::schema::server && (red.location().address().is_unspecified() || red.location().port() == 0))
        {
            send_error_reply(ricochet::failure::malformed_message);
            return;
        }
        
        if (blue.role() == ricochet::schema::server && (blue.location().address().is_unspecified() || blue.location().port() == 0))
        {
            send_error_reply(ricochet::failure::malformed_message);
            return;
        }
        
        if (red.role() == ricochet::schema::server && blue.role() == ricochet::schema::server)
        {
            send_error_reply(ricochet::failure::malformed_message);
            return;
        }
        
        bool red_is_ipv6 = red.location().address().is_v6();
        bool blue_is_ipv6 = blue.location().address().is_v6();
        
        bool protocol_matches = false;
        protocol relay_protocol = m_relay->get_protocol();
        if (relay_protocol == protocol::tcp4 || relay_protocol == protocol::udp4)
            protocol_matches = !red_is_ipv6 && !blue_is_ipv6;
        else if (relay_protocol == protocol::tcp6 || relay_protocol == protocol::udp6)
            protocol_matches = red_is_ipv6 && blue_is_ipv6;
        
        if (!protocol_matches)
        {
            send_error_reply(ricochet::failure::malformed_message);
            return;
        }
        
        m_relay->start(red, blue);
        
        do_write(ricochet::reply::make_confirm_reply());
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

void session::start_timer()
{
    m_timer.expires_from_now(m_idle);
    m_timer.async_wait(m_strand.wrap([this, weak = weak_from_this()](const boost::system::error_code& ec)
    {
        auto self = weak.lock();
        if (!self)
            return;

        if (!ec)
        {
            do_close();
        }
    }));
}

}