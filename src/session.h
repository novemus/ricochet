#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/bind/bind.hpp>
#include <memory>
#include "ricochet.h"
#include "relay.h"

namespace ricochet {

class session : public std::enable_shared_from_this<session>
{
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> m_socket;
    boost::asio::deadline_timer m_timer;
    std::weak_ptr<relay> m_relay;
    boost::posix_time::seconds m_idle;
    ricochet::query m_query;

public:

    session(boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket,
            boost::posix_time::seconds idle);

    ~session();
    void start();
    void close();
    void error(ricochet::failure error);

private:

    void do_read();
    void do_read_length();
    void do_read_payload(uint32_t length);
    void handle_message();
    void do_write(const ricochet::reply& msg);
    void handle_provide_query(const ricochet::query& msg);
    void handle_connect_query(const ricochet::query& msg);
    void send_error_reply(ricochet::failure error);
    void start_timer();
    void handle_timeout();
    std::shared_ptr<ricochet::relay> create_relay(ricochet::protocol proto);
};

}
