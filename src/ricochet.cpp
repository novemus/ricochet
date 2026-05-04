#include <ricochet.h>
#include <cstring>
#include <arpa/inet.h>

namespace ricochet {

endpoint::endpoint(const boost::asio::ip::address& addr, uint16_t port)
{
    if (addr.is_v4())
    {
        m_data.push_back(4); // IPv4 type
        auto v4_bytes = addr.to_v4().to_bytes();
        m_data.insert(m_data.end(), v4_bytes.begin(), v4_bytes.end());
    }
    else if (addr.is_v6())
    {
        m_data.push_back(6); // IPv6 type
        auto v6_bytes = addr.to_v6().to_bytes();
        m_data.insert(m_data.end(), v6_bytes.begin(), v6_bytes.end());
    }
    
    m_data.push_back(static_cast<uint8_t>((port >> 8) & 0xFF));
    m_data.push_back(static_cast<uint8_t>(port & 0xFF));
}

boost::asio::ip::address endpoint::address() const
{
    if (m_data.empty())
        throw malformed_query("Empty endpoint data");

    size_t pos = 0;
    uint8_t type = m_data[pos++];

    if (type == 4) // IPv4
    {
        if (m_data.size() < 7)
            throw malformed_query("Invalid IPv4 endpoint data");

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
            throw malformed_query("Invalid IPv6 endpoint data");

        std::array<uint8_t, 16> bytes;
        for (int i = 0; i < 16; ++i)
        {
            bytes[i] = m_data[pos++];
        }
        return boost::asio::ip::address_v6(bytes);
    }
    throw malformed_query("Invalid address type");
}

uint16_t endpoint::port() const
{
    if (m_data.empty())
        throw malformed_query("Empty endpoint data");
    
    size_t pos = (m_data[0] == 4) ? 5 : 17;
    
    if (m_data.size() < pos + 2)
        throw malformed_query("Invalid port data");

    return ntohs(*reinterpret_cast<const uint16_t*>(&m_data[pos]));
}

endpoint peer::location() const
{
    if (m_data.empty())
        throw malformed_query("Empty peer data");

    size_t len = (m_data[0] == 4) ? 7 : 19;

    if (m_data.size() < len + 1)
        throw malformed_query("Invalid endpoint data");

    std::vector<uint8_t> buffer(m_data.begin(), m_data.begin() + len);
    return endpoint { buffer };
}

peer::peer(const boost::asio::ip::address& addr, uint16_t port, schema role)
{
    if (addr.is_v4())
    {
        m_data.push_back(4);
        auto v4_bytes = addr.to_v4().to_bytes();
        m_data.insert(m_data.end(), v4_bytes.begin(), v4_bytes.end());
    }
    else if (addr.is_v6())
    {
        m_data.push_back(6);
        auto v6_bytes = addr.to_v6().to_bytes();
        m_data.insert(m_data.end(), v6_bytes.begin(), v6_bytes.end());
    }
    
    m_data.push_back(static_cast<uint8_t>((port >> 8) & 0xFF));
    m_data.push_back(static_cast<uint8_t>(port & 0xFF));
    m_data.push_back(static_cast<uint8_t>(role));
}

schema peer::role() const
{
    if (m_data.empty())
        throw malformed_query("Empty peer data");

    size_t len = (m_data[0] == 4) ? 7 : 19;

    if (m_data.size() < len + 1)
        throw malformed_query("Invalid role data");

    return static_cast<schema>(m_data[len]);
}

couple::couple(const boost::asio::ip::address& red_addr, uint16_t red_port, schema red_role,
               const boost::asio::ip::address& blue_addr, uint16_t blue_port, schema blue_role)
{
    if (red_addr.is_v4())
    {
        m_data.push_back(4);
        auto v4_bytes = red_addr.to_v4().to_bytes();
        m_data.insert(m_data.end(), v4_bytes.begin(), v4_bytes.end());
    }
    else if (red_addr.is_v6())
    {
        m_data.push_back(6);
        auto v6_bytes = red_addr.to_v6().to_bytes();
        m_data.insert(m_data.end(), v6_bytes.begin(), v6_bytes.end());
    }
    
    m_data.push_back(static_cast<uint8_t>((red_port >> 8) & 0xFF));
    m_data.push_back(static_cast<uint8_t>(red_port & 0xFF));
    m_data.push_back(static_cast<uint8_t>(red_role));

    if (blue_addr.is_v4())
    {
        m_data.push_back(4);
        auto v4_bytes = blue_addr.to_v4().to_bytes();
        m_data.insert(m_data.end(), v4_bytes.begin(), v4_bytes.end());
    }
    else if (blue_addr.is_v6())
    {
        m_data.push_back(6);
        auto v6_bytes = blue_addr.to_v6().to_bytes();
        m_data.insert(m_data.end(), v6_bytes.begin(), v6_bytes.end());
    }
    
    m_data.push_back(static_cast<uint8_t>((blue_port >> 8) & 0xFF));
    m_data.push_back(static_cast<uint8_t>(blue_port & 0xFF));
    m_data.push_back(static_cast<uint8_t>(blue_role));
}

peer couple::red() const
{
    if (m_data.empty())
        throw malformed_query("Empty couple data");

    size_t len = m_data[0] == 4 ? 8 : 20;

    if (m_data.size() < len)
        throw malformed_query("Invalid first peer data");

    std::vector<uint8_t> buffer(m_data.begin(), m_data.begin() + len);
    return peer { buffer };
}

peer couple::blue() const
{
    if (m_data.empty())
        throw malformed_query("Empty couple data");

    size_t len = m_data[0] == 4 ? 8 : 20;

    if (m_data.size() < len * 2 || m_data[0] != m_data[len])
        throw malformed_query("Invalid second peer data");

    std::vector<uint8_t> buffer(m_data.begin() + len, m_data.begin() + len * 2);
    return peer { buffer };
}

query::kind query::type() const
{
    if (m_data.empty())
        throw malformed_query("Invalid query data");

    return static_cast<kind>(m_data[0]); // Tag is first byte
}

query::value query::payload() const
{
    kind which = type();

    if (m_data.size() < 6) // tag (1) + length (4) + payload (1) minimum
         throw malformed_query("Invalid protocol payload");

    if (which == kind::provide)
    {
        return static_cast<protocol>(m_data[5]); // Skip tag + length
    }
    else if (which == kind::connect)
    {
        size_t len = m_data[5] == 4 ? 16 : 40; // Check address type after tag + length
        if (m_data.size() < 1 + 4 + len) // tag + length + payload
            throw malformed_query("Invalid couple payload");

        std::vector<uint8_t> buffer(m_data.begin() + 5, m_data.begin() + 5 + len); // Skip tag + length
        return couple { buffer };
    }

    throw malformed_query("Invalid query kind");
}

query::query(const std::vector<uint8_t>& data)
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

query query::make_connect_query(const peer& red, const peer& blue)
{
    query result;
    
    result.m_data.push_back(static_cast<uint8_t>(kind::connect));
    result.m_data.insert(result.m_data.end(), {0, 0, 0, 0});
    result.m_data.insert(result.m_data.end(), red.data(), red.data() + red.size());
    result.m_data.insert(result.m_data.end(), blue.data(), blue.data() + blue.size());
    
    uint32_t* ptr = reinterpret_cast<uint32_t*>(&result.m_data[1]);
    *ptr = htonl(static_cast<uint32_t>(result.m_data.size() - 5));

    return result;
}

reply::kind reply::type() const
{
    if (m_data.empty())
        throw malformed_query("Invalid reply data");

    return static_cast<kind>(m_data[0]); // Tag is first byte
}

reply::value reply::payload() const
{
    kind which = type();
    if (which == kind::binding)
    {
        if (m_data.size() < 6) // tag (1) + length (4) + address type (1) minimum
            throw malformed_query("Invalid binding payload");

        size_t len = (m_data[5] == 4) ? 7 : 19; // Check address type after tag + length

        if (m_data.size() < 1 + 4 + len) // tag + length + payload
            throw malformed_query("Invalid binding payload");

        std::vector<uint8_t> buffer(m_data.begin() + 5, m_data.begin() + 5 + len); // Skip tag + length
        return endpoint { buffer };
    }
    else if (which == kind::mistake)
    {
        if (m_data.size() < 6) // tag (1) + length (4) + error code (1)
            throw malformed_query("Invalid mistake payload");

        return static_cast<failure>(m_data[5]); // Skip tag + length
    }
    else if (which == kind::confirm)
    {
        return true;
    }

    throw malformed_query("Invalid reply kind");
}

reply::reply(const std::vector<uint8_t>& data)
    : buffer(data)
{
}

uint32_t reply::length() const
{
    if (m_data.size() < 5) // tag (1) + length (4) minimum
        return 0;
    
    return ntohl(*reinterpret_cast<const uint32_t*>(m_data.data() + 1));
}

reply reply::make_binding_reply(const endpoint& relay)
{
    reply result;
    
    result.m_data.push_back(static_cast<uint8_t>(kind::binding));
    result.m_data.insert(result.m_data.end(), {0, 0, 0, 0});
    result.m_data.insert(result.m_data.end(), relay.data(), relay.data() + relay.size());

    uint32_t* ptr = reinterpret_cast<uint32_t*>(&result.m_data[1]);
    *ptr = htonl(static_cast<uint32_t>(result.m_data.size() - 5));

    return result;
}

reply reply::make_mistake_reply(failure err)
{
    reply result;
    
    result.m_data.push_back(static_cast<uint8_t>(kind::mistake));
    result.m_data.insert(result.m_data.end(), {0, 0, 0, 0});
    result.m_data.push_back(static_cast<uint8_t>(err));

    uint32_t* ptr = reinterpret_cast<uint32_t*>(&result.m_data[1]);
    *ptr = htonl(static_cast<uint32_t>(result.m_data.size() - 5));
    
    return result;
}

reply reply::make_confirm_reply()
{
    reply result;

    result.m_data.push_back(static_cast<uint8_t>(kind::confirm));
    result.m_data.insert(result.m_data.end(), {0, 0, 0, 0});

    uint32_t* ptr = reinterpret_cast<uint32_t*>(&result.m_data[1]);
    *ptr = htonl(static_cast<uint32_t>(result.m_data.size() - 5));
    
    return result;
}

}
