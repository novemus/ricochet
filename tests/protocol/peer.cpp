#include <boost/test/unit_test.hpp>
#include <vector>
#include "proto.h"

using namespace ricochet;

BOOST_AUTO_TEST_CASE(peer_vector_constructor)
{
    std::vector<uint8_t> data = {4, 192, 168, 1, 1, 0x1F, 0x90, 0x01}; // IPv4 endpoint + server role
    peer p(data);
    
    BOOST_CHECK_EQUAL(p.size(), data.size());
    BOOST_CHECK(p.data() != nullptr);
}

BOOST_AUTO_TEST_CASE(peer_inheritance_from_buffer)
{
    std::vector<uint8_t> data = {4, 127, 0, 0, 1, 0x04, 0xD2, 0x00}; // IPv4 endpoint + client role
    peer p(data);
    
    BOOST_CHECK_EQUAL(p.size(), 8);
    BOOST_CHECK(p.data() != nullptr);
    
    // Test buffer functionality
    p.resize(10);
    BOOST_CHECK_EQUAL(p.size(), 10);
}

BOOST_AUTO_TEST_CASE(peer_location_extraction)
{
    std::vector<uint8_t> data = {4, 192, 168, 1, 100, 0x1F, 0x90, 0x01}; // IPv4 endpoint + server role
    peer p(data);
    
    endpoint loc = p.location();
    BOOST_CHECK_EQUAL(loc.size(), 7);
    BOOST_CHECK(loc.data() != nullptr);
}

BOOST_AUTO_TEST_CASE(peer_role_extraction)
{
    std::vector<uint8_t> client_data = {4, 192, 168, 1, 1, 0x1F, 0x90, 0x00}; // IPv4 client role
    peer client_peer(client_data);
    
    std::vector<uint8_t> server_data = {4, 192, 168, 1, 1, 0x1F, 0x90, 0x01}; // IPv4 server role
    peer server_peer(server_data);
    
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(client_peer.role()), 0);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(server_peer.role()), 1);
}

BOOST_AUTO_TEST_CASE(peer_empty_data)
{
    std::vector<uint8_t> empty_data;
    peer p(empty_data);
    
    BOOST_CHECK_EQUAL(p.size(), 0);
}

BOOST_AUTO_TEST_CASE(peer_ipv6_vector_constructor)
{
    std::vector<uint8_t> data = {
        6, // IPv6 type
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // ::1
        0x1F, 0x90, // Port 8080
        0x01 // Server role
    };
    peer p(data);
    
    BOOST_CHECK_EQUAL(p.size(), data.size());
    BOOST_CHECK(p.data() != nullptr);
}

BOOST_AUTO_TEST_CASE(peer_ipv6_location_extraction)
{
    std::vector<uint8_t> data = {
        6, // IPv6 type
        0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // 2001:db8::1
        0x04, 0xD2, // Port 1234
        0x00 // Client role
    };
    peer p(data);
    
    endpoint loc = p.location();
    BOOST_CHECK_EQUAL(loc.size(), 19); // 1 byte type + 16 bytes IPv6 + 2 bytes port
    BOOST_CHECK(loc.data() != nullptr);
    
    boost::asio::ip::address addr = loc.address();
    BOOST_CHECK(addr.is_v6());
    BOOST_CHECK_EQUAL(addr.to_string(), "2001:db8::1");
    BOOST_CHECK_EQUAL(loc.port(), 1234);
}

BOOST_AUTO_TEST_CASE(peer_ipv6_role_extraction)
{
    std::vector<uint8_t> client_data = {
        6, // IPv6 type
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // ::1
        0x1F, 0x90, // Port 8080
        0x00 // Client role
    };
    peer client_peer(client_data);
    
    std::vector<uint8_t> server_data = {
        6, // IPv6 type
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // ::1
        0x1F, 0x90, // Port 8080
        0x01 // Server role
    };
    peer server_peer(server_data);
    
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(client_peer.role()), 0);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(server_peer.role()), 1);
}

BOOST_AUTO_TEST_CASE(peer_mixed_ipv4_ipv6_invalid)
{
    // This test demonstrates that mixed IPv4/IPv6 scenarios should be handled at higher level
    // Individual peers can have either IPv4 or IPv6 addresses
    std::vector<uint8_t> ipv4_data = {4, 192, 168, 1, 1, 0x1F, 0x90, 0x00}; // IPv4 client
    std::vector<uint8_t> ipv6_data = {
        6, // IPv6 type
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // ::1
        0x1F, 0x90, // Port 8080
        0x01 // Server role
    };
    
    peer ipv4_peer(ipv4_data);
    peer ipv6_peer(ipv6_data);
    
    endpoint ipv4_loc = ipv4_peer.location();
    endpoint ipv6_loc = ipv6_peer.location();
    
    BOOST_CHECK(ipv4_loc.address().is_v4());
    BOOST_CHECK(ipv6_loc.address().is_v6());
    BOOST_CHECK_EQUAL(ipv4_peer.role(), schema::client);
    BOOST_CHECK_EQUAL(ipv6_peer.role(), schema::server);
}
