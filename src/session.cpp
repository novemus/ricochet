/*
 * Copyright (c) 2026 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include <boost/bind.hpp>
#include "session.h"
#include "logging.h"

namespace ricochet {

session::session(boost::asio::io_context& io,
                 std::shared_ptr<boost::asio::ssl::context> ssl,
                 std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> socket,
                 std::shared_ptr<heap> heap,
                 boost::posix_time::seconds wait,
                 boost::posix_time::seconds idle)
    : m_io(io)
    , m_ssl(ssl)
    , m_socket(socket)
    , m_heap(heap)
    , m_timer(m_io)
    , m_wait(wait)
    , m_idle(idle)
    , m_query(4096)
    , m_break(false)
{
    _trc_("Session " << this << " created");
}

session::~session()
{
    do_close();
    _trc_("Session " << this << " destroyed");
}

void session::start(bool reject, final_callback clean)
{
    _inf_ << "Session " << this << (reject ? " reject..." : " start...");

    boost::asio::post(m_io, [this, weak = weak_from_this(), reject, clean]()
    {
        auto self = weak.lock();
        if (!self)
            return;

        std::lock_guard<std::mutex> lock(m_mutex);

        m_final = clean;

        if (reject)
            send_error_reply(ricochet::failure::limit_reached);
        else
            do_read_header();
    });
}

void session::close()
{
    _inf_ << "Session " << this << " close...";

    boost::asio::post(m_io, [this, weak = weak_from_this()]()
    {
        auto self = weak.lock();
        if (!self)
            return;

        std::lock_guard<std::mutex> lock(m_mutex);
        m_break = true;

        if (m_relay)
        {
            m_relay->close();
            m_relay.reset();
        }

        if (m_final)
        {
            m_final();
            m_final = nullptr;
        }

        boost::system::error_code ec;
        m_socket->lowest_layer().cancel(ec);
    });
}

void session::do_close()
{
    boost::system::error_code ec;
    m_timer.cancel(ec);

    if (m_socket->lowest_layer().is_open())
    {
        boost::system::error_code ec;
        m_socket->lowest_layer().shutdown(boost::asio::socket_base::shutdown_type::shutdown_both, ec);
        m_socket->lowest_layer().close(ec);

        _inf_ << "Session " << this << " channel closed";
    }

    if (m_final)
    {
        m_final();
        m_final = nullptr;
    }
}

void session::do_shutdown()
{
    _trc_("Session " << this << " channel shutdown...");

    boost::system::error_code ec;
    m_timer.cancel(ec);

    auto finalize = [this, weak = weak_from_this(), ssl = m_ssl, socket = m_socket](const boost::system::error_code& err)
    {
        auto self = weak.lock();
        if (!self)
            return;

        _trc_("Session " << this << " channel shutdown" << (err ? ": "  + err.message() : ""));

        std::lock_guard<std::mutex> lock(m_mutex);
        do_close();
    };

    m_timer.expires_from_now(boost::posix_time::seconds(2));
    m_timer.async_wait(finalize);
    m_socket->async_shutdown(finalize);
}

void session::do_read_header()
{
    start_timer();
    m_socket->async_read_some(boost::asio::buffer(m_query.data(), query::header_size),
        [this, weak = weak_from_this(), ssl = m_ssl, socket = m_socket](const boost::system::error_code& ec, std::size_t size)
        {
            auto self = weak.lock();
            if (!self)
                return;

            std::lock_guard<std::mutex> lock(m_mutex);

            if (!ec)
            {
                _trc_("Session " << this << " read header: " << m_query.dump(0, size));

                if (m_break)
                {
                    do_shutdown();
                }
                else if (size == query::header_size)
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
                _wrn_ << "Session " << this << " failed to read header: " << ec.message();
                do_close();
            }
        });
}

void session::do_read_payload()
{
    start_timer();
    m_socket->async_read_some(boost::asio::buffer(m_query.data() + query::header_size, m_query.length()),
        [this, weak = weak_from_this(), ssl = m_ssl, socket = m_socket](const boost::system::error_code& ec, std::size_t size)
        {
            auto self = weak.lock();
            if (!self)
                return;

            std::lock_guard<std::mutex> lock(m_mutex);

            if (!ec)
            {
                _trc_("Session " << this << " read payload: " << m_query.dump(query::header_size, size));

                if (m_break)
                {
                    do_shutdown();
                }
                else if (size == m_query.length())
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
                _wrn_ << "Session " << this << " failed to read payload: " << ec.message();
                do_close();
            }
        });
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
                _wrn_ << "Session " << this << " unknown query " << static_cast<int>(m_query.type());
                send_error_reply(ricochet::failure::malformed_message);
                break;
        }
    }
    catch (const malformed_message& e)
    {
        _wrn_ << "Session " << this << " malformed message";
        send_error_reply(ricochet::failure::malformed_message);
    }
    catch (const unavailable_proto& e)
    {
        _wrn_ << "Session " << this << " unavailable protocol";
        send_error_reply(ricochet::failure::unavailable_proto);
    }
    catch (const std::exception& e)
    {
        _err_ << "Session " << this << " internal error: " << e.what();
        send_error_reply(ricochet::failure::server_error);
    }
}

void session::do_write(const ricochet::reply& msg)
{
    start_timer();
    boost::asio::async_write(*m_socket, boost::asio::buffer(msg.data(), msg.size()),
        [this, weak = weak_from_this(), msg, ssl = m_ssl, socket = m_socket](const boost::system::error_code& ec, std::size_t)
        {
            auto self = weak.lock();
            if (!self)
                return;

            std::lock_guard<std::mutex> lock(m_mutex);

            if (ec)
            {
                _err_ << "Session " << this << " failed to write reply: " << ec.message();
                do_close();
            }
            else if (m_break)
            {
                do_shutdown();
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
        });
}

void session::handle_provide_query()
{
    auto proto = std::get<ricochet::protocol>(m_query.payload());
    _inf_ << "Session " << this << " handling provide query for " << proto;

    switch (proto)
    {
        case ricochet::protocol::tcp4:
        case ricochet::protocol::tcp6:
            m_relay = std::make_shared<ricochet::tcp_relay>(m_io, m_heap->make_tcp_relay(m_io, proto == ricochet::protocol::tcp4), m_wait, m_idle);
            break;
        case ricochet::protocol::udp4:
        case ricochet::protocol::udp6:
            m_relay = std::make_shared<ricochet::udp_relay>(m_io, m_heap->make_udp_relay(m_io, proto == ricochet::protocol::udp4), m_wait, m_idle);
            break;
        default:
            throw malformed_message("Unsupported protocol");
    }

    do_write(ricochet::reply::make_binding_reply(m_relay->get_endpoint()));
}

void session::handle_connect_query()
{
    auto payload = std::get<ricochet::couple>(m_query.payload());
    _inf_ << "Session " << this << " handling connect query";

    if (m_relay)
    {
        auto red = payload.red();
        auto blue = payload.blue();

        if (red.role() == ricochet::schema::server && (red.location().address().is_unspecified() || red.location().port() == 0))
        {
            _wrn_ << "Session " << this << " invalid red server location";
            send_error_reply(ricochet::failure::malformed_message);
            return;
        }

        if (blue.role() == ricochet::schema::server && (blue.location().address().is_unspecified() || blue.location().port() == 0))
        {
            _wrn_ << "Session " << this << " invalid blue server location";
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
            _wrn_ << "Session " << this << " endpoint protocol mismatch";
            send_error_reply(ricochet::failure::malformed_message);
            return;
        }

        m_relay->start(red, blue, std::move(m_final));

        do_write(ricochet::reply::make_confirm_reply());
    }
    else
    {
        _wrn_ << "Session " << this << " no relay available";
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
    m_timer.async_wait([this, weak = weak_from_this()](const boost::system::error_code& ec)
    {
        auto self = weak.lock();
        if (!self)
            return;

        if (!ec)
        {
            _inf_ << "Session " << this << " channel timeout";

            std::lock_guard<std::mutex> lock(m_mutex);
            m_break = true;

            boost::system::error_code ec;
            m_socket->lowest_layer().cancel(ec);
        }
    });
}

}