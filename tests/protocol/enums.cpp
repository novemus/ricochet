#include <boost/test/unit_test.hpp>
#include <ricochet.h>

using namespace ricochet;

BOOST_AUTO_TEST_CASE(protocol_values)
{
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(protocol::udp4), 0);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(protocol::tcp4), 1);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(protocol::udp6), 2);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(protocol::tcp6), 3);
}

BOOST_AUTO_TEST_CASE(schema_values)
{
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(schema::client), 0);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(schema::server), 1);
}

BOOST_AUTO_TEST_CASE(failure_values)
{
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(failure::server_error), 0);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(failure::malformed_query), 1);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(failure::unavailable_proto), 2);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(failure::limit_reached), 3);
}

BOOST_AUTO_TEST_CASE(protocol_size)
{
    BOOST_CHECK_EQUAL(sizeof(protocol), 1);
}

BOOST_AUTO_TEST_CASE(schema_size)
{
    BOOST_CHECK_EQUAL(sizeof(schema), 1);
}

BOOST_AUTO_TEST_CASE(failure_size)
{
    BOOST_CHECK_EQUAL(sizeof(failure), 1);
}
