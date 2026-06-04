#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/bind/bind.hpp>
#include <memory>
#include "proto.h"
#include "relay.h"

namespace ricochet {

class session : public std::enable_shared_from_this<session>
{
    boost::asio::io_context& m_io;
    std::shared_ptr<boost::asio::ssl::context> m_ssl;
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> m_socket;
    boost::asio::deadline_timer m_timer;
    boost::posix_time::seconds m_wait;
    boost::posix_time::seconds m_idle;
    std::shared_ptr<ricochet::relay> m_relay;
    ricochet::query m_query;
    cleanup_function m_clean;
    bool m_break;
    std::mutex m_mutex;

public:

    session(boost::asio::io_context& io,
            std::shared_ptr<boost::asio::ssl::context> ssl,
            boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket,
            boost::posix_time::seconds wait,
            boost::posix_time::seconds idle);
    ~session();

    void start(bool reject, cleanup_function clean);
    void close();

private:

    void do_close();
    void do_shutdown();
    void do_read_header();
    void do_read_payload();
    void do_write(const ricochet::reply& msg);
    void handle_query();
    void handle_provide_query();
    void handle_connect_query();
    void send_error_reply(ricochet::failure err);
    void start_timer();
};

}
