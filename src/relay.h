#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ssl.hpp>
#include "proto.h"

namespace ricochet {

bool is_ip4_available(boost::asio::io_context& io);
bool is_ip6_available(boost::asio::io_context& io);
boost::asio::ip::address get_outgoing_address(boost::asio::io_context& io, bool ip4);
boost::asio::ip::address get_outgoing_address(boost::asio::io_context& io, const boost::asio::ip::address& address);

class heap
{
    std::vector<uint16_t> m_heap;
    size_t m_next = 0;
    std::mutex m_mutex;

public:

    heap(uint16_t base, uint16_t span);
    boost::asio::ip::tcp::acceptor make_tcp_relay(boost::asio::io_context& io, bool ip4);
    boost::asio::ip::udp::socket make_udp_relay(boost::asio::io_context& io, bool ip4);
};

using final_callback = std::function<void()>;

struct relay
{
    virtual ~relay() {}
    virtual protocol get_protocol() const = 0;
    virtual endpoint get_endpoint() const = 0;
    virtual void start(const peer& red, const peer& blue, final_callback&& final) = 0;
    virtual void close() = 0;
};

class tcp_relay : public relay, public std::enable_shared_from_this<tcp_relay>
{
    boost::asio::io_context& m_io;
    boost::asio::io_context::strand m_strand;
    boost::asio::ip::tcp::acceptor m_server;
    boost::asio::ip::tcp::socket m_one;
    boost::asio::ip::tcp::socket m_two;
    boost::asio::deadline_timer m_timer;
    boost::asio::deadline_timer m_defer;
    boost::posix_time::seconds m_wait;
    boost::posix_time::seconds m_idle;
    final_callback m_final;
    std::chrono::steady_clock::time_point m_timestamp;
    int m_reconnects;

public:

    tcp_relay(boost::asio::io_context& io, boost::asio::ip::tcp::acceptor server, boost::posix_time::seconds wait, boost::posix_time::seconds idle);
    ~tcp_relay() override;
    protocol get_protocol() const override;
    endpoint get_endpoint() const override;
    void start(const peer& red, const peer& blue, final_callback&& final) override;
    void close() override;

private:

    void start_relay(const peer& one, const peer& two);
    void transmit_data(boost::asio::ip::tcp::socket& from, boost::asio::ip::tcp::socket& to);
    void break_relay();
    void watch_activity(boost::posix_time::seconds timeout);
};

class udp_relay : public relay, public std::enable_shared_from_this<udp_relay>
{
    using socket_ptr = std::shared_ptr<boost::asio::ip::udp::socket>;

    boost::asio::io_context& m_io;
    boost::asio::io_context::strand m_strand;
    boost::asio::ip::udp::socket m_socket;
    boost::asio::ip::udp::endpoint m_one;
    boost::asio::ip::udp::endpoint m_two;
    boost::asio::deadline_timer m_timer;
    boost::posix_time::seconds m_wait;
    boost::posix_time::seconds m_idle;
    final_callback m_final;
    std::chrono::steady_clock::time_point m_timestamp;

public:

    udp_relay(boost::asio::io_context& io, boost::asio::ip::udp::socket socket, boost::posix_time::seconds wait, boost::posix_time::seconds idle);
    ~udp_relay() override;
    protocol get_protocol() const override;
    endpoint get_endpoint() const override;
    void start(const peer& red, const peer& blue, final_callback&& final) override;
    void close() override;

private:

    bool can_transmit() const;
    void read_socket();
    void break_relay();
    void watch_activity(boost::posix_time::seconds timeout);
};

}
