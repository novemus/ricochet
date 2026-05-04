#include <boost/test/unit_test.hpp>
#include <ricochet.h>
#include <vector>

using namespace ricochet;

BOOST_AUTO_TEST_CASE(couple_vector_constructor)
{
    // Two peers: each has 7 bytes for IPv4 endpoint + 1 byte for role
    std::vector<uint8_t> data = {
        4, 192, 168, 1, 1, 0x1F, 0x90, 0x00,  // peer 1: client
        4, 192, 168, 1, 2, 0x1F, 0x91, 0x01   // peer 2: server
    };
    couple c(data);
    
    BOOST_CHECK_EQUAL(c.size(), data.size());
    BOOST_CHECK(c.data() != nullptr);
}

BOOST_AUTO_TEST_CASE(couple_inheritance_from_buffer)
{
    std::vector<uint8_t> data = {
        4, 127, 0, 0, 1, 0x04, 0xD2, 0x01,  // peer 1: server
        4, 127, 0, 0, 1, 0x04, 0xD3, 0x00   // peer 2: client
    };
    couple c(data);
    
    BOOST_CHECK_EQUAL(c.size(), 16);
    BOOST_CHECK(c.data() != nullptr);
    
    // Test buffer functionality
    c.resize(18);
    BOOST_CHECK_EQUAL(c.size(), 18);
}

BOOST_AUTO_TEST_CASE(couple_peer_extraction)
{
    std::vector<uint8_t> data = {
        4, 192, 168, 1, 100, 0x1F, 0x90, 0x00,  // peer 1: client
        4, 192, 168, 1, 101, 0x1F, 0x91, 0x01   // peer 2: server
    };
    couple c(data);
    
    peer first = c.one();
    peer second = c.two();
    
    BOOST_CHECK_EQUAL(first.size(), 8);
    BOOST_CHECK_EQUAL(second.size(), 8);
    BOOST_CHECK(first.data() != nullptr);
    BOOST_CHECK(second.data() != nullptr);
}

BOOST_AUTO_TEST_CASE(couple_empty_data)
{
    std::vector<uint8_t> empty_data;
    couple c(empty_data);
    
    BOOST_CHECK_EQUAL(c.size(), 0);
}

BOOST_AUTO_TEST_CASE(couple_minimal_data)
{
    std::vector<uint8_t> min_data = {
        4, 127, 0, 0, 1, 0x00, 0x50, 0x00,  // peer 1: client, port 80
        4, 127, 0, 0, 1, 0x00, 0x51, 0x01   // peer 2: server, port 81
    };
    couple c(min_data);
    
    BOOST_CHECK_EQUAL(c.size(), 16);
    
    peer p1 = c.one();
    peer p2 = c.two();
    BOOST_CHECK_EQUAL(p1.size(), 8);
    BOOST_CHECK_EQUAL(p2.size(), 8);
}

BOOST_AUTO_TEST_CASE(couple_ipv6_vector_constructor)
{
    // Two IPv6 peers: each has 19 bytes for IPv6 endpoint + 1 byte for role
    std::vector<uint8_t> data = {
        6, // IPv6 type for peer 1
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // ::1
        0x1F, 0x90, // Port 8080
        0x00, // Client role
        
        6, // IPv6 type for peer 2
        0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // 2001:db8::1
        0x04, 0xD2, // Port 1234
        0x01  // Server role
    };
    couple c(data);
    
    BOOST_CHECK_EQUAL(c.size(), data.size());
    BOOST_CHECK(c.data() != nullptr);
}

BOOST_AUTO_TEST_CASE(couple_ipv6_peer_extraction)
{
    std::vector<uint8_t> data = {
        6, // IPv6 type for peer 1
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // ::1
        0x1F, 0x90, // Port 8080
        0x00, // Client role
        
        6, // IPv6 type for peer 2
        0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // 2001:db8::1
        0x04, 0xD2, // Port 1234
        0x01  // Server role
    };
    couple c(data);
    
    peer first = c.one();
    peer second = c.two();
    
    BOOST_CHECK_EQUAL(first.size(), 20); // 1 byte type + 16 bytes IPv6 + 2 bytes port + 1 byte role
    BOOST_CHECK_EQUAL(second.size(), 20);
    BOOST_CHECK(first.data() != nullptr);
    BOOST_CHECK(second.data() != nullptr);
    
    // Test first peer
    endpoint first_loc = first.location();
    BOOST_CHECK(first_loc.address().is_v6());
    BOOST_CHECK_EQUAL(first_loc.address().to_string(), "::1");
    BOOST_CHECK_EQUAL(first_loc.port(), 8080);
    BOOST_CHECK_EQUAL(first.role(), schema::client);
    
    // Test second peer
    endpoint second_loc = second.location();
    BOOST_CHECK(second_loc.address().is_v6());
    BOOST_CHECK_EQUAL(second_loc.address().to_string(), "2001:db8::1");
    BOOST_CHECK_EQUAL(second_loc.port(), 1234);
    BOOST_CHECK_EQUAL(second.role(), schema::server);
}

BOOST_AUTO_TEST_CASE(couple_mixed_ipv4_ipv6_should_throw)
{
    std::vector<uint8_t> data = {
        // IPv4 peer 1
        4, 192, 168, 1, 100, 0x1F, 0x90, 0x00,  // IPv4 client
        
        // IPv6 peer 2
        6, // IPv6 type
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // ::1
        0x1F, 0x90, // Port 8080
        0x01  // Server role
    };
    couple c(data);
    
    // First peer (IPv4) should work fine
    peer ipv4_peer = c.one();
    endpoint ipv4_loc = ipv4_peer.location();
    
    BOOST_CHECK(ipv4_loc.address().is_v4());
    BOOST_CHECK_EQUAL(ipv4_loc.address().to_string(), "192.168.1.100");
    BOOST_CHECK_EQUAL(ipv4_loc.port(), 8080);
    BOOST_CHECK_EQUAL(ipv4_peer.role(), schema::client);
    
    // Second peer (IPv6) should throw exception due to mixed address types
    BOOST_CHECK_THROW(c.two(), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(couple_mixed_ipv6_ipv4_should_throw)
{
    std::vector<uint8_t> data = {
        // IPv6 peer 1
        6, // IPv6 type
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // ::1
        0x1F, 0x90, // Port 8080
        0x00, // Client role
        
        // IPv4 peer 2
        4, 192, 168, 1, 100, 0x1F, 0x91, 0x01  // IPv4 server
    };
    couple c(data);
    
    // First peer (IPv6) should work fine
    peer ipv6_peer = c.one();
    endpoint ipv6_loc = ipv6_peer.location();
    
    BOOST_CHECK(ipv6_loc.address().is_v6());
    BOOST_CHECK_EQUAL(ipv6_loc.address().to_string(), "::1");
    BOOST_CHECK_EQUAL(ipv6_loc.port(), 8080);
    BOOST_CHECK_EQUAL(ipv6_peer.role(), schema::client);
    
    // Second peer (IPv4) should throw exception due to mixed address types
    BOOST_CHECK_THROW(c.two(), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(couple_both_ipv6_localhost)
{
    std::vector<uint8_t> data = {
        // IPv6 peer 1 (localhost)
        6, // IPv6 type
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // ::1
        0x00, 0x50, // Port 80
        0x00, // Client role
        
        // IPv6 peer 2 (localhost)
        6, // IPv6 type
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // ::1
        0x00, 0x51, // Port 81
        0x01  // Server role
    };
    couple c(data);
    
    BOOST_CHECK_EQUAL(c.size(), 40); // 2 * (1 + 16 + 2 + 1)
    
    peer p1 = c.one();
    peer p2 = c.two();
    
    BOOST_CHECK_EQUAL(p1.size(), 20);
    BOOST_CHECK_EQUAL(p2.size(), 20);
    
    endpoint loc1 = p1.location();
    endpoint loc2 = p2.location();
    
    BOOST_CHECK_EQUAL(loc1.address().to_string(), "::1");
    BOOST_CHECK_EQUAL(loc1.port(), 80);
    BOOST_CHECK_EQUAL(p1.role(), schema::client);
    
    BOOST_CHECK_EQUAL(loc2.address().to_string(), "::1");
    BOOST_CHECK_EQUAL(loc2.port(), 81);
    BOOST_CHECK_EQUAL(p2.role(), schema::server);
}
