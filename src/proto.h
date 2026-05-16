#pragma once

#include <boost/asio.hpp>
#include <limits>
#include <vector>
#include <variant>
#include <cstdint>
#include <ostream>
#include <stdexcept>

namespace ricochet {

class malformed_message : public std::runtime_error
{
public:
    explicit malformed_message(const std::string& msg)
        : std::runtime_error(msg) {}
};

class unavailable_proto : public std::runtime_error
{
public:
    explicit unavailable_proto(const std::string& msg)
        : std::runtime_error(msg) {}
};

class limit_reached : public std::runtime_error
{
public:
    explicit limit_reached(const std::string& msg)
        : std::runtime_error(msg) {}
};

class server_error : public std::runtime_error
{
public:
    explicit server_error(const std::string& msg)
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
    malformed_message = 1,
    unavailable_proto = 2,
    limit_reached = 3
};

struct buffer
{
    buffer() = default;
    explicit buffer(std::size_t size) : m_data(size) {}
    explicit buffer(const std::vector<uint8_t>& data) : m_data(data) {}

    void resize(std::size_t size) { m_data.resize(size); }
    std::size_t size() const { return m_data.size(); }
    uint8_t* data() { return m_data.data(); }
    const uint8_t* data() const { return m_data.data(); }
    std::string dump(size_t pos = 0, size_t size = std::numeric_limits<size_t>::max()) const;

protected:

    std::vector<uint8_t> m_data;
};

struct endpoint : public buffer
{
    boost::asio::ip::address address() const;
    uint16_t port() const;

    endpoint() = default;
    endpoint(const boost::asio::ip::address& addr, uint16_t port);
    explicit endpoint(const std::vector<uint8_t>& data) : buffer(data) {}
};

struct peer : public buffer
{
    endpoint location() const;
    schema role() const;
    
    peer() = default;
    peer(const boost::asio::ip::address& addr, uint16_t port, schema role);
    explicit peer(const std::vector<uint8_t>& data) : buffer(data) {}
};

struct couple : public buffer
{
    peer red() const;
    peer blue() const;

    couple() = default;
    couple(const boost::asio::ip::address& red_addr, uint16_t red_port, schema red_role,
           const boost::asio::ip::address& blue_addr, uint16_t blue_port, schema blue_role);
    explicit couple(const std::vector<uint8_t>& data) : buffer(data) {}
};

struct query : public buffer
{
    static constexpr size_t header_size = 3;

    enum class kind : uint8_t
    {
        provide = 0,
        connect = 1 
    };

    using value = std::variant<protocol, couple>;

    kind type() const;
    uint16_t length() const;
    value payload() const;

    query() = default;
    explicit query(size_t size) : buffer(size) {}
    explicit query(const std::vector<uint8_t>& data) : buffer(data) {}

    static query make_provide_query(protocol proto);
    static query make_connect_query(const peer& red, const peer& blue);
};

struct reply : public buffer
{
    static constexpr size_t header_size = 3;

    enum class kind : uint8_t
    {
        binding = 0,
        confirm = 1,
        mistake = 2 
    };

    using value = std::variant<endpoint, failure, bool>;

    kind type() const;
    uint16_t length() const;
    value payload() const;

    reply() = default;
    explicit reply(size_t size) : buffer(size) {}
    explicit reply(const std::vector<uint8_t>& data) : buffer(data) {}

    static reply make_binding_reply(const endpoint& relay);
    static reply make_mistake_reply(failure err);
    static reply make_confirm_reply();
};

inline std::ostream& operator<<(std::ostream& os, const query::kind& val)
{
    switch (val)
    {
        case query::kind::connect: return os << "connect";
        case query::kind::provide: return os << "provide";
        default: return os << "unknown";
    }
}

inline std::ostream& operator<<(std::ostream& os, const reply::kind& val)
{
    switch (val)
    {
        case reply::kind::binding: return os << "binding";
        case reply::kind::confirm: return os << "confirm";
        case reply::kind::mistake: return os << "mistake";
        default: return os << "unknown";
    }
}

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
        case failure::malformed_message: return os << "malformed_message";
        case failure::unavailable_proto: return os << "unavailable_proto";
        case failure::limit_reached: return os << "limit_reached";
        default: return os << "unknown";
    }
}

inline std::ostream& operator<<(std::ostream& os, const endpoint& val)
{
    return os << (val.address().is_v6() ? "[" : "") << val.address() << (val.address().is_v6() ? "]" : "") << ":" << val.port();
}

inline std::ostream& operator<<(std::ostream& os, const peer& val)
{
    return os << val.location() << "@" << val.role();
}

inline std::ostream& operator<<(std::ostream& os, const couple& val)
{
    return os << val.red() << "&&" << val.blue();
}

}
