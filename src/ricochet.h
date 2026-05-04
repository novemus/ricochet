#pragma once

#include <boost/asio.hpp>
#include <vector>
#include <variant>
#include <cstdint>
#include <variant>
#include <ostream>
#include <stdexcept>

namespace ricochet {

class protocol_unavailable : public std::runtime_error
{
public:
    explicit protocol_unavailable(const std::string& msg)
        : std::runtime_error(msg) {}
};

class malformed_query : public std::runtime_error
{
public:
    explicit malformed_query(const std::string& msg)
        : std::runtime_error(msg) {}
};

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
    malformed_query = 1,
    unavailable_proto = 2,
    limit_reached = 3
};

// Stream operators for enum types
inline std::ostream& operator<<(std::ostream& os, const protocol& proto)
{
    switch (proto)
    {
        case protocol::udp4: return os << "udp4";
        case protocol::tcp4: return os << "tcp4";
        case protocol::udp6: return os << "udp6";
        case protocol::tcp6: return os << "tcp6";
        default: return os << "unknown";
    }
}

inline std::ostream& operator<<(std::ostream& os, const schema& sch)
{
    switch (sch)
    {
        case schema::client: return os << "client";
        case schema::server: return os << "server";
        default: return os << "unknown";
    }
}

inline std::ostream& operator<<(std::ostream& os, const failure& fail)
{
    switch (fail)
    {
        case failure::server_error: return os << "server_error";
        case failure::malformed_query: return os << "malformed_query";
        case failure::unavailable_proto: return os << "unavailable_proto";
        case failure::limit_reached: return os << "limit_reached";
        default: return os << "unknown";
    }
}

struct buffer
{
    buffer() = default;
    explicit buffer(std::size_t size) : m_data(size) {}
    explicit buffer(const std::vector<uint8_t>& data) : m_data(data) {}

    void resize(std::size_t size) { m_data.resize(size); }
    std::size_t size() const { return m_data.size(); }
    uint8_t* data() { return m_data.data(); }
    const uint8_t* data() const { return m_data.data(); }

protected:

    std::vector<uint8_t> m_data;
};

struct endpoint : public buffer
{
    boost::asio::ip::address address() const;
    uint16_t port() const;

    endpoint(const boost::asio::ip::address& addr, uint16_t port);
    explicit endpoint(const std::vector<uint8_t>& data) : buffer(data) {}
};

struct peer : public buffer
{
    endpoint location() const;
    schema role() const;
    
    peer(const boost::asio::ip::address& addr, uint16_t port, schema role);
    explicit peer(const std::vector<uint8_t>& data) : buffer(data) {}
};

struct couple : public buffer
{
    peer red() const;
    peer blue() const;

    couple(const boost::asio::ip::address& red_addr, uint16_t red_port, schema red_role,
           const boost::asio::ip::address& blue_addr, uint16_t blue_port, schema blue_role);
    explicit couple(const std::vector<uint8_t>& data) : buffer(data) {}
};

struct query : public buffer
{
    enum class kind : uint8_t
    {
        provide = 0,
        connect = 1 
    };

    using value = std::variant<protocol, couple>;

    kind type() const;
    uint32_t length() const;
    value payload() const;

    query() = default;
    explicit query(const std::vector<uint8_t>& data);

    static query make_provide_query(protocol proto);
    static query make_connect_query(const peer& red, const peer& blue);
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

    kind type() const;
    uint32_t length() const;
    value payload() const;

    reply() = default;
    explicit reply(const std::vector<uint8_t>& data);

    static reply make_binding_reply(const endpoint& relay);
    static reply make_mistake_reply(failure err);
    static reply make_confirm_reply();
};

}
