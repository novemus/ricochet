#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/bind/bind.hpp>
#include <memory>
#include <ricochet.h>
#include <relay.h>

namespace ricochet {

class session : public std::enable_shared_from_this<session>
{
    boost::asio::io_context& m_io;
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> m_socket;
    boost::asio::io_context::strand m_strand;
    boost::asio::deadline_timer m_timer;
    boost::posix_time::seconds m_idle;
    std::shared_ptr<ricochet::relay> m_relay;
    ricochet::query m_query;
    cleanup_function m_clean;

public:

    session(boost::asio::io_context& io,
            boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket,
            boost::posix_time::seconds idle);

    ~session();
    void start();
    void close();
    void error(ricochet::failure err);
    void set_cleaner(cleanup_function clean);

private:

    void do_close();
    void do_read();
    void do_read_length();
    void do_read_payload(uint32_t len);
    void do_write(const ricochet::reply& msg);
    void handle_query();
    void handle_provide_query();
    void handle_connect_query();
    void send_error_reply(ricochet::failure err);
    void start_timer();
};

}
