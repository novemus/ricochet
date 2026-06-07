#include <boost/test/unit_test.hpp>
#include <boost/asio/spawn.hpp>
#include "fixture.h"

BOOST_FIXTURE_TEST_SUITE(agent_integration_tests, integration_test_fixture)

BOOST_AUTO_TEST_CASE(udp4_relay)
{
    auto fut = boost::asio::spawn(get_io_context(), [&](boost::asio::yield_context yield)
    {
        auto server = create_server(true);
        auto agent = create_agent(true);

        BOOST_REQUIRE_NO_THROW(server->start());

        try
        {
            ricochet::endpoint ep;
            agent->assign_relay(yield, ricochet::protocol::udp4, ep);

            BOOST_CHECK(!ep.address().is_loopback() && !ep.address().is_unspecified());
            BOOST_CHECK(ep.port() > 0);

            auto address = ricochet::get_outgoing_address(get_io_context(), true);

            ricochet::peer red(address, 0, ricochet::schema::client);
            ricochet::peer blue(address, 7788, ricochet::schema::server);

            BOOST_REQUIRE_NO_THROW(agent->launch_relay(yield, red, blue));
        }
        catch (const ricochet::unavailable_proto&)
        {
            BOOST_CHECK(true);
            BOOST_TEST_MESSAGE("udp4 unavailable");
        }

        BOOST_REQUIRE_NO_THROW(server->stop());
    },
    boost::asio::use_future);

    BOOST_REQUIRE_NO_THROW(fut.get());
}

BOOST_AUTO_TEST_CASE(tcp4_relay)
{
    auto fut = boost::asio::spawn(get_io_context(), [&](boost::asio::yield_context yield)
    {
        auto server = create_server(true);
        auto agent = create_agent(true);

        BOOST_REQUIRE_NO_THROW(server->start());

        try
        {
            ricochet::endpoint ep;
            agent->assign_relay(yield, ricochet::protocol::tcp4, ep);

            BOOST_CHECK(!ep.address().is_loopback() && !ep.address().is_unspecified());
            BOOST_CHECK(ep.port() > 0);

            auto address = ricochet::get_outgoing_address(get_io_context(), true);

            ricochet::peer red(address, 0, ricochet::schema::client);
            ricochet::peer blue(address, 8899, ricochet::schema::server);

            BOOST_REQUIRE_NO_THROW(agent->launch_relay(yield, red, blue));
        }
        catch (const ricochet::unavailable_proto&)
        {
            BOOST_CHECK(true);
            BOOST_TEST_MESSAGE("udp4 unavailable");
        }
        
        BOOST_REQUIRE_NO_THROW(server->stop());
    },
    boost::asio::use_future);

    BOOST_REQUIRE_NO_THROW(fut.get());
}

BOOST_AUTO_TEST_CASE(reuse_agent)
{
    auto fut = boost::asio::spawn(get_io_context(), [&](boost::asio::yield_context yield)
    {
        auto server = create_server(false);
        auto agent = create_agent(false);

        BOOST_REQUIRE_NO_THROW(server->start());

        try
        {
            for (size_t i = 0; i < 2; ++i)
            {
                ricochet::endpoint ep;
                agent->assign_relay(yield, ricochet::protocol::udp4, ep);

                BOOST_CHECK(!ep.address().is_loopback() && !ep.address().is_unspecified());
                BOOST_CHECK(ep.port() > 0);

                auto address = ricochet::get_outgoing_address(get_io_context(), true);

                ricochet::peer red(address, 0, ricochet::schema::client);
                ricochet::peer blue(address, 0, ricochet::schema::client);

                BOOST_REQUIRE_NO_THROW(agent->launch_relay(yield, red, blue));
            }
        }
        catch (const ricochet::unavailable_proto&)
        {
            BOOST_CHECK(true);
            BOOST_TEST_MESSAGE("udp4 unavailable");
        }

        BOOST_REQUIRE_NO_THROW(server->stop());
    },
    boost::asio::use_future);

    BOOST_REQUIRE_NO_THROW(fut.get());
}

BOOST_AUTO_TEST_SUITE_END()
