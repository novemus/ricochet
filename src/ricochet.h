#pragma once

#include <boost/asio.hpp>
#include <vector>
#include <variant>
#include <cstdint>
#include <variant>
#include <memory>

namespace ricochet {

enum class protocol : uint8_t
{
    udp4 = 0,
    tcp4 = 1,
    udp6 = 2,
    tcp6 = 3
};

enum class schema : uint8_t
{ 
    client = 0, 
    server = 1 
};

enum class failure : uint8_t
{
    server_error = 0,
    unavalable_proto = 1,
    malformed_couple = 2,
    limit_reached = 3
};

struct buffer
{
    buffer() = default;
    buffer(const std::vector<uint8_t>& data) : m_data(data) {}
    virtual ~buffer() = default;

    uint8_t* data() { return m_data.data(); }
    const uint8_t* data() const { return m_data.data(); }
    size_t size() const { return m_data.size(); }

protected:

    std::vector<uint8_t> m_data;
};

struct endpoint : public buffer
{
    boost::asio::ip::address address();
    uint16_t port();

    endpoint(const std::vector<uint8_t>& data) : buffer(data) {}
};

struct peer : public buffer
{
    endpoint location();
    schema role();

    peer(const std::vector<uint8_t>& data) : buffer(data) {}
};

struct couple : public buffer
{
    peer one();
    peer two();

    couple(const std::vector<uint8_t>& data) : buffer(data) {}
};

struct query : public buffer
{
    enum class kind : uint8_t
    {
        provide = 0,
        connect = 1 
    };

    using value = std::variant<protocol, couple>;

    kind type();
    value payload();

    static query make_provide_query(protocol proto);
    static query make_connect_query(const boost::asio::ip::address& addr_one, uint16_t port_one, schema role_one,
                                    const boost::asio::ip::address& addr_two, uint16_t port_two, schema role_two);
};

struct reply : public buffer
{
    enum class kind : uint8_t
    {
        binding = 0,
        confirm = 1,
        mistake = 2 
    };

    using value = std::variant<endpoint, failure, bool>;

    kind type();
    value payload();

    static reply make_binding_reply(const boost::asio::ip::address& addr, uint16_t port);
    static reply make_mistake_reply(failure err);
    static reply make_confirm_reply();
};

}
