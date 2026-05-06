#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ssl.hpp>
#include <ricochet.h>

namespace ricochet {

bool is_ip4_available(boost::asio::io_context& io);
bool is_ip6_available(boost::asio::io_context& io);

using cleanup_function = std::function<void()>;

struct relay
{
    virtual ~relay() {}
    virtual protocol get_protocol() const = 0;
    virtual endpoint get_endpoint() const = 0;
    virtual void start(const peer& red, const peer& blue) = 0;
    virtual void close() = 0;
};

class tcp_relay : public relay, public std::enable_shared_from_this<tcp_relay>
{
    using socket_ptr = std::shared_ptr<boost::asio::ip::tcp::socket>;

    boost::asio::io_context& m_io;
    boost::asio::io_context::strand m_strand;
    boost::asio::ip::tcp::acceptor m_server;
    boost::asio::deadline_timer m_timer;
    boost::posix_time::seconds m_idle;
    cleanup_function m_clean;
    socket_ptr m_near;
    socket_ptr m_away;
    std::chrono::steady_clock::time_point m_timestamp;

public:

    tcp_relay(boost::asio::io_context& io, protocol proto, boost::posix_time::seconds idle, cleanup_function clean);
    ~tcp_relay() override;
    protocol get_protocol() const override;
    endpoint get_endpoint() const override;
    void start(const peer& red, const peer& blue) override;
    void close() override;

private:

    void connect_peer(const endpoint& which);
    void accept_peer(const endpoint& which);
    void transmit_data(socket_ptr from, socket_ptr to);
    void start_relay();
    void break_relay();
    void watch_activity();
};

class udp_relay : public relay, public std::enable_shared_from_this<udp_relay>
{
    using socket_ptr = std::shared_ptr<boost::asio::ip::udp::socket>;

    boost::asio::io_context& m_io;
    boost::asio::io_context::strand m_strand;
    boost::asio::ip::udp::socket m_socket;
    boost::asio::ip::udp::endpoint m_near;
    boost::asio::ip::udp::endpoint m_away;
    boost::asio::deadline_timer m_timer;
    boost::posix_time::seconds m_idle;
    cleanup_function m_clean;
    std::chrono::steady_clock::time_point m_timestamp;

public:

    udp_relay(boost::asio::io_context& io, protocol proto, boost::posix_time::seconds idle, cleanup_function clean);
    ~udp_relay() override;
    protocol get_protocol() const override;
    endpoint get_endpoint() const override;
    void start(const peer& red, const peer& blue) override;
    void close() override;

private:

    bool can_transmit() const;
    void read_socket();
    void break_relay();
    void watch_activity();
};

}
