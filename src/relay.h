#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <ricochet.h>
#include <session.h>

namespace ricochet {

struct relay
{
    virtual ~relay() {}
    virtual endpoint bind() const = 0;
    virtual void start(const endpoint& one_loc, schema one_role, const endpoint& two_loc, schema two_role) = 0;
    virtual void close() = 0;
    virtual protocol get_protocol() const = 0;
    virtual void set_cleaner(std::function<void()> clean) = 0;
};

class tcp_relay : public relay, public std::enable_shared_from_this<tcp_relay>
{
    boost::asio::ip::tcp::acceptor m_server;
    boost::asio::ip::tcp::socket m_one;
    boost::asio::ip::tcp::socket m_two;
    boost::asio::deadline_timer m_timer;
    boost::posix_time::seconds m_idle;
    std::function<void()> m_clean;

public:

    tcp_relay(bool v6, boost::posix_time::seconds idle);
    ~tcp_relay() override;
    endpoint bind() const override;
    void start(const endpoint& one_loc, schema one_role, const endpoint& two_loc, schema two_role) override;
    void close() override;
    protocol get_protocol() const override;
    void set_cleaner(std::function<void()> clean) override;
};

class udp_relay : public relay, public std::enable_shared_from_this<udp_relay>
{
    boost::asio::ip::udp::socket m_socket;
    boost::asio::ip::udp::endpoint m_one;
    boost::asio::ip::udp::endpoint m_two;
    boost::asio::deadline_timer m_timer;
    boost::posix_time::seconds m_idle;
    std::function<void()> m_clean;

public:

    udp_relay(bool v6, boost::posix_time::seconds idle);
    ~udp_relay() override;
    endpoint bind() const override;
    protocol get_protocol() const override;
    void start(const endpoint& one_loc, schema one_role, const endpoint& two_loc, schema two_role) override;
    void close() override;
    void set_cleaner(std::function<void()> finalizer) override;
};

}
