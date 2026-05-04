#include <boost/test/unit_test.hpp>
#include <ricochet.h>
#include <vector>
#include <boost/asio.hpp>

using namespace ricochet;

BOOST_AUTO_TEST_CASE(reply_default_constructor)
{
    reply r;
    BOOST_CHECK_EQUAL(r.size(), 0);
}

BOOST_AUTO_TEST_CASE(reply_buffer_constructor)
{
    std::vector<uint8_t> data = {0x00, 0x01, 0x00, 0x00, 0x06, 0xC0, 0xA8, 0x01, 0x01, 0x1F, 0x90}; // binding reply
    buffer buf(data);
    reply r(buf);
    
    BOOST_CHECK_EQUAL(r.size(), data.size());
    BOOST_CHECK(r.data() != nullptr);
}

BOOST_AUTO_TEST_CASE(reply_binding_type)
{
    boost::asio::ip::address addr = boost::asio::ip::make_address("192.168.1.100");
    reply r = reply::make_binding_reply(addr, 8080);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(r.type()), 0); // binding kind
}

BOOST_AUTO_TEST_CASE(reply_confirm_type)
{
    reply r = reply::make_confirm_reply();
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(r.type()), 1); // confirm kind
}

BOOST_AUTO_TEST_CASE(reply_mistake_type)
{
    reply r = reply::make_mistake_reply(failure::malformed_query);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(r.type()), 2); // mistake kind
}

BOOST_AUTO_TEST_CASE(reply_binding_payload)
{
    boost::asio::ip::address addr = boost::asio::ip::make_address("127.0.0.1");
    reply r = reply::make_binding_reply(addr, 1234);
    reply::value payload = r.payload();
    
    BOOST_CHECK(std::holds_alternative<endpoint>(payload));
}

BOOST_AUTO_TEST_CASE(reply_confirm_payload)
{
    reply r = reply::make_confirm_reply();
    reply::value payload = r.payload();
    
    BOOST_CHECK(std::holds_alternative<bool>(payload));
    BOOST_CHECK_EQUAL(std::get<bool>(payload), true);
}

BOOST_AUTO_TEST_CASE(reply_mistake_payload)
{
    reply r = reply::make_mistake_reply(failure::limit_reached);
    reply::value payload = r.payload();
    
    BOOST_CHECK(std::holds_alternative<failure>(payload));
    BOOST_CHECK_EQUAL(std::get<failure>(payload), failure::limit_reached);
}

BOOST_AUTO_TEST_CASE(reply_length)
{
    boost::asio::ip::address addr = boost::asio::ip::make_address("10.0.0.1");
    reply r = reply::make_binding_reply(addr, 80);
    uint32_t length = r.length();
    BOOST_CHECK(length > 0);
}

BOOST_AUTO_TEST_CASE(reply_inheritance_from_buffer)
{
    std::vector<uint8_t> data = {0x02, 0x01, 0x00, 0x00, 0x01, 0x03}; // mistake reply
    reply r(data);
    
    // Test buffer functionality
    r.resize(10);
    BOOST_CHECK_EQUAL(r.size(), 10);
}

BOOST_AUTO_TEST_CASE(reply_make_mistake_all_failures)
{
    reply r_server_error = reply::make_mistake_reply(failure::server_error);
    reply r_malformed = reply::make_mistake_reply(failure::malformed_query);
    reply r_unavailable = reply::make_mistake_reply(failure::unavailable_proto);
    reply r_limit = reply::make_mistake_reply(failure::limit_reached);
    
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(r_server_error.type()), 2);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(r_malformed.type()), 2);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(r_unavailable.type()), 2);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(r_limit.type()), 2);
    
    reply::value payload_server = r_server_error.payload();
    reply::value payload_malformed = r_malformed.payload();
    reply::value payload_unavailable = r_unavailable.payload();
    reply::value payload_limit = r_limit.payload();
    
    BOOST_CHECK(std::get<failure>(payload_server) == failure::server_error);
    BOOST_CHECK(std::get<failure>(payload_malformed) == failure::malformed_query);
    BOOST_CHECK(std::get<failure>(payload_unavailable) == failure::unavailable_proto);
    BOOST_CHECK(std::get<failure>(payload_limit) == failure::limit_reached);
}

BOOST_AUTO_TEST_CASE(reply_make_binding_different_addresses)
{
    boost::asio::ip::address ipv4 = boost::asio::ip::make_address("192.168.1.1");
    boost::asio::ip::address ipv6 = boost::asio::ip::make_address("::1");
    
    reply r_ipv4 = reply::make_binding_reply(ipv4, 8080);
    reply r_ipv6 = reply::make_binding_reply(ipv6, 8080);
    
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(r_ipv4.type()), 0);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(r_ipv6.type()), 0);
    
    reply::value payload_ipv4 = r_ipv4.payload();
    reply::value payload_ipv6 = r_ipv6.payload();
    
    BOOST_CHECK(std::holds_alternative<endpoint>(payload_ipv4));
    BOOST_CHECK(std::holds_alternative<endpoint>(payload_ipv6));
}
