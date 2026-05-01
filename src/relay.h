#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <chrono>
#include "ricochet.h"

namespace ricochet {

struct relay
{
    virtual ~relay() {}
    virtual endpoint bind() const = 0;
    virtual void start(const endpoint& one_loc, schema one_role,
               const endpoint& two_loc, schema two_role) = 0;
    virtual void close() = 0;
};

class tcp_relay : public relay, public std::enable_shared_from_this<tcp_relay>
{
    boost::asio::ip::tcp::acceptor m_acceptor;
    boost::asio::ip::tcp::socket m_one;
    boost::asio::ip::tcp::socket m_two;
    boost::asio::deadline_timer m_timer;
    std::chrono::seconds m_idle;

public:

    tcp_relay(bool v6, std::chrono::seconds idle);
    ~tcp_relay() override;
    endpoint bind() const override;
    void start(const endpoint& one_loc, schema one_role,
               const endpoint& two_loc, schema two_role) override;
    void close() override;
};

class udp_relay : public relay, public std::enable_shared_from_this<udp_relay>
{
    boost::asio::ip::udp::socket m_socket;
    boost::asio::ip::udp::endpoint m_one;
    boost::asio::ip::udp::endpoint m_two;
    boost::asio::deadline_timer m_timer;
    std::chrono::seconds m_idle;

public:

    udp_relay(bool v6, std::chrono::seconds idle);
    ~udp_relay() override;
    endpoint bind() const override;
    void start(const endpoint& one_loc, schema one_role,
               const endpoint& two_loc, schema two_role) override;
    void close() override;
};

}
