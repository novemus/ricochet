#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <chrono>
#include "ricochet.h"
#include "relay.h"

namespace ricochet {

class session : public std::enable_shared_from_this<session>
{
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> m_socket;
    boost::asio::deadline_timer m_timer;
    std::weak_ptr<relay> m_relay;

public:

    session(boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket,
            std::chrono::seconds idle);

    ~session();
    void start();
    void close();
};

}
