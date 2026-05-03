#include "ricochet.h"
#include <stdexcept>
#include <cstring>
#include <arpa/inet.h>

namespace ricochet {


boost::asio::ip::address endpoint::address()
{
    if (m_data.empty())
        throw std::runtime_error("Empty endpoint data");

    size_t pos = 0;
    uint8_t type = m_data[pos++];

    if (type == 4) // IPv4
    {
        if (m_data.size() < 7)
            throw std::runtime_error("Invalid IPv4 endpoint data");

        std::array<uint8_t, 4> bytes;
        for (int i = 0; i < 4; ++i)
        {
            bytes[i] = m_data[pos++];
        }
        return boost::asio::ip::address_v4(bytes);
    }
    else if (type == 6) // IPv6
    {
        if (m_data.size() < 19)
            throw std::runtime_error("Invalid IPv6 endpoint data");

        std::array<uint8_t, 16> bytes;
        for (int i = 0; i < 16; ++i)
        {
            bytes[i] = m_data[pos++];
        }
        return boost::asio::ip::address_v6(bytes);
    }
    throw std::runtime_error("Invalid address type");
}

uint16_t endpoint::port()
{
    if (m_data.empty())
        throw std::runtime_error("Empty endpoint data");

    size_t pos = (m_data[0] == 4) ? 4 : 16;

    if (m_data.size() < pos + 3)
        throw std::runtime_error("Invalid port data");

    uint16_t port = static_cast<uint16_t>(m_data[pos] << 8) | m_data[pos + 1];
    return port;
}

endpoint peer::location()
{
    if (m_data.empty())
        throw std::runtime_error("Empty peer data");

    size_t len = (m_data[0] == 4) ? 7 : 19;

    if (m_data.size() < len + 2)
        throw std::runtime_error("Invalid endpoint data");

    std::vector<uint8_t> buffer(m_data.begin(), m_data.begin() + len);
    return endpoint { buffer };
}

schema peer::role()
{
    if (m_data.empty())
        throw std::runtime_error("Empty peer data");

    size_t len = (m_data[0] == 4) ? 7 : 19;

    if (m_data.size() < len + 1)
        throw std::runtime_error("Invalid role data");

    return static_cast<schema>(m_data[len]);
}

peer couple::one()
{
    if (m_data.empty())
        throw std::runtime_error("Empty couple data");

    size_t len = m_data[0] == 4 ? 8 : 20;

    if (m_data.size() < len)
        throw std::runtime_error("Invalid first peer data");

    std::vector<uint8_t> buffer(m_data.begin(), m_data.begin() + len);
    return peer { buffer };
}

peer couple::two()
{
    if (m_data.empty())
        throw std::runtime_error("Empty couple data");

    size_t len = m_data[0] == 4 ? 8 : 20;

    if (m_data.size() < len * 2)
        throw std::runtime_error("Invalid second peer data");

    std::vector<uint8_t> buffer(m_data.begin() + len, m_data.begin() + len * 2);
    return peer { buffer };
}

query::kind query::type() const
{
    if (m_data.empty())
        throw std::runtime_error("Invalid query data");

    return static_cast<kind>(m_data[0]); // Tag is first byte
}

query::value query::payload() const
{
    kind which = type();

    if (m_data.size() < 6) // tag (1) + length (4) + payload (1) minimum
         std::runtime_error("Invalid protocol payload");

    if (which == kind::provide)
    {
        return static_cast<protocol>(m_data[5]); // Skip tag + length
    }
    else if (which == kind::connect)
    {
        size_t len = m_data[5] == 4 ? 16 : 40; // Check address type after tag + length
        if (m_data.size() < 1 + 4 + len) // tag + length + payload
            throw std::runtime_error("Invalid couple payload");

        std::vector<uint8_t> buffer(m_data.begin() + 5, m_data.begin() + 5 + len); // Skip tag + length
        return couple { buffer };
    }

    throw std::runtime_error("Invalid query kind");
}

query::query(const buffer& data)
    : buffer(data)
{
}

uint32_t query::length() const
{
    if (m_data.size() < 5) // tag (1) + length (4) minimum
        return 0;

    return ntohl(*reinterpret_cast<const uint32_t*>(m_data.data() + 1));
}

query query::make_provide_query(protocol proto)
{
    query result;
    
    result.m_data.push_back(static_cast<uint8_t>(kind::provide));
    result.m_data.insert(result.m_data.end(), {0, 0, 0, 0});
    result.m_data.push_back(static_cast<uint8_t>(proto));

    uint32_t* ptr = reinterpret_cast<uint32_t*>(&result.m_data[1]);
    *ptr = htonl(static_cast<uint32_t>(result.m_data.size() - 5));

    return result;
}

query query::make_connect_query(const boost::asio::ip::address& addr_one, uint16_t port_one, schema role_one,
                                const boost::asio::ip::address& addr_two, uint16_t port_two, schema role_two)
{
    query result;
    
    // Add tag
    result.m_data.push_back(static_cast<uint8_t>(kind::connect));
    
    // Reserve space for length field (4 bytes)
    result.m_data.insert(result.m_data.end(), {0, 0, 0, 0});
    
    // Add payload data
    if (addr_one.is_v4())
    {
        result.m_data.push_back(4); // IPv4 type
        auto v4_bytes = addr_one.to_v4().to_bytes();
        result.m_data.insert(result.m_data.end(), v4_bytes.begin(), v4_bytes.end());
    }
    else if (addr_one.is_v6())
    {
        result.m_data.push_back(6); // IPv6 type
        auto v6_bytes = addr_one.to_v6().to_bytes();
        result.m_data.insert(result.m_data.end(), v6_bytes.begin(), v6_bytes.end());
    }
    
    result.m_data.push_back(static_cast<uint8_t>((port_one >> 8) & 0xFF));
    result.m_data.push_back(static_cast<uint8_t>(port_one & 0xFF));
    result.m_data.push_back(static_cast<uint8_t>(role_one));

    if (addr_two.is_v4())
    {
        result.m_data.push_back(4); // IPv4 type
        auto v4_bytes = addr_two.to_v4().to_bytes();
        result.m_data.insert(result.m_data.end(), v4_bytes.begin(), v4_bytes.end());
    }
    else if (addr_two.is_v6())
    {
        result.m_data.push_back(6); // IPv6 type
        auto v6_bytes = addr_two.to_v6().to_bytes();
        result.m_data.insert(result.m_data.end(), v6_bytes.begin(), v6_bytes.end());
    }
    
    result.m_data.push_back(static_cast<uint8_t>((port_two >> 8) & 0xFF));
    result.m_data.push_back(static_cast<uint8_t>(port_two & 0xFF));
    result.m_data.push_back(static_cast<uint8_t>(role_two));
    
    // Fill length field using reinterpret_cast for direct memory access
    uint32_t* ptr = reinterpret_cast<uint32_t*>(&result.m_data[1]);
    *ptr = htonl(static_cast<uint32_t>(result.m_data.size() - 5));
    
    return result;
}

reply::kind reply::type() const
{
    if (m_data.empty())
        throw std::runtime_error("Invalid reply data");

    return static_cast<kind>(m_data[0]); // Tag is first byte
}

reply::value reply::payload() const
{
    kind which = type();
    if (which == kind::binding)
    {
        if (m_data.size() < 6) // tag (1) + length (4) + address type (1) minimum
            throw std::runtime_error("Invalid binding payload");

        size_t len = (m_data[5] == 4) ? 7 : 19; // Check address type after tag + length

        if (m_data.size() < 1 + 4 + len) // tag + length + payload
            throw std::runtime_error("Invalid binding payload");

        std::vector<uint8_t> buffer(m_data.begin() + 5, m_data.begin() + 5 + len); // Skip tag + length
        return endpoint { buffer };
    }
    else if (which == kind::mistake)
    {
        if (m_data.size() < 6) // tag (1) + length (4) + error code (1)
            throw std::runtime_error("Invalid mistake payload");

        return static_cast<failure>(m_data[5]); // Skip tag + length
    }
    else if (which == kind::confirm)
    {
        return true;
    }

    throw std::runtime_error("Invalid reply kind");
}

reply::reply(const buffer& data)
    : buffer(data)
{
}

uint32_t reply::length() const
{
    if (m_data.size() < 5) // tag (1) + length (4) minimum
        return 0;
    
    return ntohl(*reinterpret_cast<const uint32_t*>(m_data.data() + 1));
}

reply reply::make_binding_reply(const boost::asio::ip::address& addr, uint16_t port)
{
    reply result;
    
    // Add tag
    result.m_data.push_back(static_cast<uint8_t>(kind::binding));
    
    // Reserve space for length field (4 bytes)
    result.m_data.insert(result.m_data.end(), {0, 0, 0, 0});

    // Add payload data
    if (addr.is_v4())
    {
        result.m_data.push_back(4); // IPv4 type
        auto v4_bytes = addr.to_v4().to_bytes();
        result.m_data.insert(result.m_data.end(), v4_bytes.begin(), v4_bytes.end());
    }
    else if (addr.is_v6())
    {
        result.m_data.push_back(6); // IPv6 type
        auto v6_bytes = addr.to_v6().to_bytes();
        result.m_data.insert(result.m_data.end(), v6_bytes.begin(), v6_bytes.end());
    }
    
    result.m_data.push_back(static_cast<uint8_t>((port >> 8) & 0xFF));
    result.m_data.push_back(static_cast<uint8_t>(port & 0xFF));
    
    // Fill length field using reinterpret_cast for direct memory access
    uint32_t* ptr = reinterpret_cast<uint32_t*>(&result.m_data[1]);
    *ptr = htonl(static_cast<uint32_t>(result.m_data.size() - 5));
    
    return result;
}

reply reply::make_mistake_reply(failure err)
{
    reply result;
    
    // Add tag
    result.m_data.push_back(static_cast<uint8_t>(kind::mistake));
    
    // Reserve space for length field (4 bytes)
    result.m_data.insert(result.m_data.end(), {0, 0, 0, 0});
    
    // Add payload
    result.m_data.push_back(static_cast<uint8_t>(err));
    
    // Fill length field using reinterpret_cast for direct memory access
    uint32_t* ptr = reinterpret_cast<uint32_t*>(&result.m_data[1]);
    *ptr = htonl(static_cast<uint32_t>(result.m_data.size() - 5));
    
    return result;
}

reply reply::make_confirm_reply()
{
    reply result;

    // Add tag
    result.m_data.push_back(static_cast<uint8_t>(kind::confirm));
    
    // Reserve space for length field (4 bytes)
    result.m_data.insert(result.m_data.end(), {0, 0, 0, 0});
    
    // Fill length field using reinterpret_cast for direct memory access
    uint32_t* ptr = reinterpret_cast<uint32_t*>(&result.m_data[1]);
    *ptr = htonl(static_cast<uint32_t>(result.m_data.size() - 5));
    
    return result;
}

}
