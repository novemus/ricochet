#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/asio/spawn.hpp>
#include "fixture.h"

BOOST_FIXTURE_TEST_SUITE(client_integration_tests, integration_test_fixture)

BOOST_AUTO_TEST_CASE(client_connect_with_ca)
{
    auto fut = boost::asio::spawn(get_io_context(), [&](boost::asio::yield_context yield)
    {
        auto server = create_server(true);
        auto client = create_client(true);

        BOOST_REQUIRE_NO_THROW(server->start());
        BOOST_REQUIRE_NO_THROW(client->connect(yield));

        BOOST_REQUIRE_NO_THROW(client->shutdown(yield));
        BOOST_REQUIRE_NO_THROW(server->stop());
    },
    boost::asio::use_future);

    BOOST_REQUIRE_NO_THROW(fut.get());
}

BOOST_AUTO_TEST_CASE(client_connect_without_ca)
{
    auto fut = boost::asio::spawn(get_io_context(), [&](boost::asio::yield_context yield)
    {
        auto server = create_server(false);
        auto client = create_client(false);

        BOOST_REQUIRE_NO_THROW(server->start());
        BOOST_REQUIRE_NO_THROW(client->connect(yield));

        BOOST_REQUIRE_NO_THROW(client->shutdown(yield));
        BOOST_REQUIRE_NO_THROW(server->stop());
    },
    boost::asio::use_future);

    BOOST_REQUIRE_NO_THROW(fut.get());
}

BOOST_AUTO_TEST_CASE(udp4_relay_session)
{
    auto fut = boost::asio::spawn(get_io_context(), [&](boost::asio::yield_context yield)
    {
        auto server = create_server(true);
        auto client = create_client(true);

        BOOST_REQUIRE_NO_THROW(server->start());
        BOOST_REQUIRE_NO_THROW(client->connect(yield));

        BOOST_REQUIRE_NO_THROW(client->write_query(yield, ricochet::query::make_provide_query(ricochet::protocol::udp4)));

        ricochet::reply reply(4096);
        BOOST_REQUIRE_NO_THROW(client->read_reply(yield, reply));

        if (reply.type() == ricochet::reply::kind::mistake)
        {
            auto failure = std::get<ricochet::failure>(reply.payload());
            BOOST_CHECK(failure == ricochet::failure::unavailable_proto);
            BOOST_TEST_MESSAGE("UDP4 unavailable");
        }
        else
        {
            BOOST_CHECK_EQUAL(reply.type(), ricochet::reply::kind::binding);

            auto endpoint = std::get<ricochet::endpoint>(reply.payload());

            BOOST_CHECK(!endpoint.address().is_loopback() && !endpoint.address().is_unspecified());
            BOOST_CHECK(endpoint.port() > 0);

            auto address = ricochet::get_outgoing_address(get_io_context(), true);

            ricochet::peer red(address, 0, ricochet::schema::client);
            ricochet::peer blue(address, 5000, ricochet::schema::server);

            BOOST_REQUIRE_NO_THROW(client->write_query(yield, ricochet::query::make_connect_query(red, blue)));

            ricochet::reply reply(4096);
            BOOST_REQUIRE_NO_THROW(client->read_reply(yield, reply));
            BOOST_CHECK_EQUAL(reply.type(), ricochet::reply::kind::confirm);
        }

        BOOST_REQUIRE_NO_THROW(client->shutdown(yield));
        BOOST_REQUIRE_NO_THROW(server->stop());
    },
    boost::asio::use_future);

    BOOST_REQUIRE_NO_THROW(fut.get());
}

BOOST_AUTO_TEST_CASE(udp6_relay_session)
{
    auto fut = boost::asio::spawn(get_io_context(), [&](boost::asio::yield_context yield)
    {
        auto server = create_server(true);
        auto client = create_client(true);

        BOOST_REQUIRE_NO_THROW(server->start());
        BOOST_REQUIRE_NO_THROW(client->connect(yield));

        BOOST_REQUIRE_NO_THROW(client->write_query(yield, ricochet::query::make_provide_query(ricochet::protocol::udp6)));

        ricochet::reply reply(4096);
        BOOST_REQUIRE_NO_THROW(client->read_reply(yield, reply));

        if (reply.type() == ricochet::reply::kind::mistake)
        {
            auto failure = std::get<ricochet::failure>(reply.payload());
            BOOST_CHECK(failure == ricochet::failure::unavailable_proto);
            BOOST_TEST_MESSAGE("udp6 unavailable");
        }
        else
        {
            BOOST_CHECK_EQUAL(reply.type(), ricochet::reply::kind::binding);

            auto endpoint = std::get<ricochet::endpoint>(reply.payload());

            BOOST_CHECK(!endpoint.address().is_loopback() && !endpoint.address().is_unspecified());
            BOOST_CHECK(endpoint.port() > 0);

            auto address = ricochet::get_outgoing_address(get_io_context(), false);

            ricochet::peer red(address, 0, ricochet::schema::client);
            ricochet::peer blue(address, 6000, ricochet::schema::server);

            BOOST_REQUIRE_NO_THROW(client->write_query(yield, ricochet::query::make_connect_query(red, blue)));

            ricochet::reply reply(4096);
            BOOST_REQUIRE_NO_THROW(client->read_reply(yield, reply));
            BOOST_CHECK_EQUAL(reply.type(), ricochet::reply::kind::confirm);
        }

        BOOST_REQUIRE_NO_THROW(client->shutdown(yield));
        BOOST_REQUIRE_NO_THROW(server->stop());
    },
    boost::asio::use_future);

    BOOST_REQUIRE_NO_THROW(fut.get());
}

BOOST_AUTO_TEST_CASE(multiple_client_sessions)
{
    auto fut = boost::asio::spawn(get_io_context(), [&](boost::asio::yield_context yield)
    {
        auto server = create_server(false, 4, 8);
        BOOST_REQUIRE_NO_THROW(server->start());
        
        std::vector<std::pair<std::shared_ptr<ricochet::client>, ricochet::protocol>> pool = {
            { create_client(false), ricochet::protocol::udp4 },
            { create_client(false), ricochet::protocol::tcp4 }, 
            { create_client(false), ricochet::protocol::udp6 },
            { create_client(false), ricochet::protocol::tcp6 } 
        };

        for (auto [client, proto] : pool) 
        {
            client->connect(yield);

            auto query = ricochet::query::make_provide_query(proto);
            client->write_query(yield, query);

            ricochet::reply reply(4096);
            client->read_reply(yield, reply);

            if (reply.type() == ricochet::reply::kind::mistake)
            {
                auto failure = std::get<ricochet::failure>(reply.payload());
                BOOST_CHECK(failure == ricochet::failure::unavailable_proto);
                BOOST_TEST_MESSAGE(proto << " unavailable");
            }
            else
            {
                BOOST_CHECK_EQUAL(reply.type(), ricochet::reply::kind::binding);
                
                auto endpoint = std::get<ricochet::endpoint>(reply.payload());

                BOOST_CHECK(!endpoint.address().is_loopback() && !endpoint.address().is_unspecified());
                BOOST_CHECK(endpoint.port() > 0);

                auto address = ricochet::get_outgoing_address(get_io_context(), proto == ricochet::protocol::udp4 || proto == ricochet::protocol::tcp4);

                ricochet::peer red(address, 0, ricochet::schema::client);
                ricochet::peer blue(address, 0, ricochet::schema::client);

                BOOST_REQUIRE_NO_THROW(client->write_query(yield, ricochet::query::make_connect_query(red, blue)));

                ricochet::reply reply(4096);
                BOOST_REQUIRE_NO_THROW(client->read_reply(yield, reply));
                BOOST_CHECK_EQUAL(reply.type(), ricochet::reply::kind::confirm);
            }
        }

        for (auto [client, proto] : pool) 
        {
            BOOST_REQUIRE_NO_THROW(client->shutdown(yield));
        }

        BOOST_REQUIRE_NO_THROW(server->stop());
    },
    boost::asio::use_future);

    BOOST_REQUIRE_NO_THROW(fut.get());
}

BOOST_AUTO_TEST_CASE(client_relay_limit)
{
    auto fut = boost::asio::spawn(get_io_context(), [&](boost::asio::yield_context yield)
    {
        auto server = create_server(true);
        BOOST_REQUIRE_NO_THROW(server->start());

        std::vector<std::shared_ptr<ricochet::client>> pool = {
            create_client(true),
            create_client(true),
            create_client(true),
            create_client(true)
        };

        for (size_t i = 0; i < pool.size(); ++i) 
        {
            BOOST_REQUIRE_NO_THROW(pool[i]->connect(yield));
            BOOST_REQUIRE_NO_THROW(pool[i]->write_query(yield, ricochet::query::make_provide_query(ricochet::protocol::tcp4)));

            ricochet::reply reply(4096);
            BOOST_REQUIRE_NO_THROW(pool[i]->read_reply(yield, reply));
            
            if (reply.type() == ricochet::reply::kind::binding)
            {
                BOOST_CHECK_LE(i, 1);
            }
            else if (reply.type() == ricochet::reply::kind::mistake)
            {
                auto failure = std::get<ricochet::failure>(reply.payload());
                if (failure == ricochet::failure::limit_reached)
                {
                    BOOST_CHECK_GE(i, 2);
                }
                else if (failure == ricochet::failure::unavailable_proto)
                {
                    BOOST_TEST_MESSAGE("tcp4 unavailable");
                    break;
                }
            }
        }

        for (auto client : pool) 
        {
            BOOST_REQUIRE_NO_THROW(client->shutdown(yield));
        }

        BOOST_REQUIRE_NO_THROW(server->stop());
    },
    boost::asio::use_future);

    BOOST_REQUIRE_NO_THROW(fut.get());
}

BOOST_AUTO_TEST_CASE(total_relay_limit)
{
    auto fut = boost::asio::spawn(get_io_context(), [&](boost::asio::yield_context yield)
    {
        auto server = create_server(false, 4, 2);
        BOOST_REQUIRE_NO_THROW(server->start());
        
        std::vector<std::shared_ptr<ricochet::client>> pool = {
            create_client(false),
            create_client(false),
            create_client(false),
            create_client(false)
        };

        for (size_t i = 0; i < pool.size(); ++i) 
        {
            BOOST_REQUIRE_NO_THROW(pool[i]->connect(yield));
            BOOST_REQUIRE_NO_THROW(pool[i]->write_query(yield, ricochet::query::make_provide_query(ricochet::protocol::tcp4)));

            ricochet::reply reply(4096);
            BOOST_REQUIRE_NO_THROW(pool[i]->read_reply(yield, reply));
            
            if (reply.type() == ricochet::reply::kind::binding)
            {
                BOOST_CHECK_LE(i, 1);
            }
            else if (reply.type() == ricochet::reply::kind::mistake)
            {
                auto failure = std::get<ricochet::failure>(reply.payload());
                if (failure == ricochet::failure::limit_reached)
                {
                    BOOST_CHECK_GE(i, 2);
                }
                else if (failure == ricochet::failure::unavailable_proto)
                {
                    BOOST_TEST_MESSAGE("tcp4 unavailable");
                    break;
                }
            }
        }

        for (auto client : pool) 
        {
            BOOST_REQUIRE_NO_THROW(client->shutdown(yield));
        }

        BOOST_REQUIRE_NO_THROW(server->stop());
    },
    boost::asio::use_future);

    BOOST_REQUIRE_NO_THROW(fut.get());
}

BOOST_AUTO_TEST_CASE(client_malformed_message)
{
    auto fut = boost::asio::spawn(get_io_context(), [&](boost::asio::yield_context yield)
    {
        auto server = create_server(true);
        auto client = create_client(true);

        BOOST_REQUIRE_NO_THROW(server->start());
        BOOST_REQUIRE_NO_THROW(client->connect(yield));

        BOOST_REQUIRE_NO_THROW(client->write_query(yield, ricochet::query::make_provide_query(ricochet::protocol::udp4)));

        ricochet::reply reply(4096);
        BOOST_REQUIRE_NO_THROW(client->read_reply(yield, reply));

        if (reply.type() == ricochet::reply::kind::mistake)
        {
            auto failure = std::get<ricochet::failure>(reply.payload());
            BOOST_CHECK(failure == ricochet::failure::unavailable_proto);
            BOOST_TEST_MESSAGE("udp4 unavailable");
        }
        else
        {
            BOOST_CHECK_EQUAL(reply.type(), ricochet::reply::kind::binding);

            BOOST_REQUIRE_NO_THROW(client->write_query(yield, ricochet::query::make_provide_query(ricochet::protocol::udp4)));

            ricochet::reply reply(4096);
            BOOST_REQUIRE_NO_THROW(client->read_reply(yield, reply));

            BOOST_CHECK_EQUAL(reply.type(), ricochet::reply::kind::mistake);
            auto failure = std::get<ricochet::failure>(reply.payload());
            BOOST_CHECK_EQUAL(failure, ricochet::failure::malformed_message);
        }

        BOOST_REQUIRE_NO_THROW(client->shutdown(yield));
        BOOST_REQUIRE_NO_THROW(server->stop());
    },
    boost::asio::use_future);

    BOOST_REQUIRE_NO_THROW(fut.get());
}

BOOST_AUTO_TEST_CASE(undefined_server_port)
{
    auto fut = boost::asio::spawn(get_io_context(), [&](boost::asio::yield_context yield)
    {
        auto server = create_server(true);
        auto client = create_client(true);

        BOOST_REQUIRE_NO_THROW(server->start());
        BOOST_REQUIRE_NO_THROW(client->connect(yield));

        BOOST_REQUIRE_NO_THROW(client->write_query(yield, ricochet::query::make_provide_query(ricochet::protocol::udp4)));

        ricochet::reply reply(4096);
        BOOST_REQUIRE_NO_THROW(client->read_reply(yield, reply));

        if (reply.type() == ricochet::reply::kind::mistake)
        {
            auto failure = std::get<ricochet::failure>(reply.payload());
            BOOST_CHECK(failure == ricochet::failure::unavailable_proto);
            BOOST_TEST_MESSAGE("udp4 unavailable");
        }
        else
        {
            BOOST_CHECK_EQUAL(reply.type(), ricochet::reply::kind::binding);

            auto endpoint = std::get<ricochet::endpoint>(reply.payload());

            BOOST_CHECK(!endpoint.address().is_loopback() && !endpoint.address().is_unspecified());
            BOOST_CHECK(endpoint.port() > 0);

            auto ip4addr = ricochet::get_outgoing_address(get_io_context(), true);

            ricochet::peer red(ip4addr, 0, ricochet::schema::client);
            ricochet::peer blue(ip4addr, 0, ricochet::schema::server);

            BOOST_REQUIRE_NO_THROW(client->write_query(yield, ricochet::query::make_connect_query(red, blue)));

            ricochet::reply reply(4096);
            BOOST_REQUIRE_NO_THROW(client->read_reply(yield, reply));
            BOOST_CHECK_EQUAL(reply.type(), ricochet::reply::kind::mistake);
            auto failure = std::get<ricochet::failure>(reply.payload());
            BOOST_CHECK_EQUAL(failure, ricochet::failure::malformed_message);
        }

        BOOST_REQUIRE_NO_THROW(client->shutdown(yield));
        BOOST_REQUIRE_NO_THROW(server->stop());
    },
    boost::asio::use_future);

    BOOST_REQUIRE_NO_THROW(fut.get());
}

BOOST_AUTO_TEST_CASE(undefined_server_address)
{
    auto fut = boost::asio::spawn(get_io_context(), [&](boost::asio::yield_context yield)
    {
        auto server = create_server(true);
        auto client = create_client(true);

        BOOST_REQUIRE_NO_THROW(server->start());
        BOOST_REQUIRE_NO_THROW(client->connect(yield));

        BOOST_REQUIRE_NO_THROW(client->write_query(yield, ricochet::query::make_provide_query(ricochet::protocol::udp4)));

        ricochet::reply reply(4096);
        BOOST_REQUIRE_NO_THROW(client->read_reply(yield, reply));

        if (reply.type() == ricochet::reply::kind::mistake)
        {
            auto failure = std::get<ricochet::failure>(reply.payload());
            BOOST_CHECK(failure == ricochet::failure::unavailable_proto);
            BOOST_TEST_MESSAGE("udp4 unavailable");
        }
        else
        {
            BOOST_CHECK_EQUAL(reply.type(), ricochet::reply::kind::binding);

            auto endpoint = std::get<ricochet::endpoint>(reply.payload());

            BOOST_CHECK(!endpoint.address().is_loopback() && !endpoint.address().is_unspecified());
            BOOST_CHECK(endpoint.port() > 0);

            ricochet::peer red(boost::asio::ip::make_address("0.0.0.0"), 0, ricochet::schema::client);
            ricochet::peer blue(boost::asio::ip::make_address("0.0.0.0"), 5555, ricochet::schema::server);

            BOOST_REQUIRE_NO_THROW(client->write_query(yield, ricochet::query::make_connect_query(red, blue)));

            ricochet::reply reply(4096);
            BOOST_REQUIRE_NO_THROW(client->read_reply(yield, reply));
            BOOST_CHECK_EQUAL(reply.type(), ricochet::reply::kind::mistake);
            auto failure = std::get<ricochet::failure>(reply.payload());
            BOOST_CHECK_EQUAL(failure, ricochet::failure::malformed_message);
        }

        BOOST_REQUIRE_NO_THROW(client->shutdown(yield));
        BOOST_REQUIRE_NO_THROW(server->stop());
    },
    boost::asio::use_future);

    BOOST_REQUIRE_NO_THROW(fut.get());
}

BOOST_AUTO_TEST_CASE(wrong_connect_query)
{
    auto fut = boost::asio::spawn(get_io_context(), [&](boost::asio::yield_context yield)
    {
        auto server = create_server(true);
        auto client = create_client(true);

        BOOST_REQUIRE_NO_THROW(server->start());
        BOOST_REQUIRE_NO_THROW(client->connect(yield));

        BOOST_REQUIRE_NO_THROW(client->write_query(yield, ricochet::query::make_provide_query(ricochet::protocol::tcp4)));

        ricochet::reply reply(4096);
        BOOST_REQUIRE_NO_THROW(client->read_reply(yield, reply));

        if (reply.type() == ricochet::reply::kind::mistake)
        {
            auto failure = std::get<ricochet::failure>(reply.payload());
            BOOST_CHECK(failure == ricochet::failure::unavailable_proto);
            BOOST_TEST_MESSAGE("udp4 unavailable");
        }
        else
        {
            BOOST_CHECK_EQUAL(reply.type(), ricochet::reply::kind::binding);

            auto endpoint = std::get<ricochet::endpoint>(reply.payload());

            BOOST_CHECK(!endpoint.address().is_loopback() && !endpoint.address().is_unspecified());
            BOOST_CHECK(endpoint.port() > 0);

            ricochet::peer red(boost::asio::ip::make_address("::"), 0, ricochet::schema::client);
            ricochet::peer blue(boost::asio::ip::make_address("::"), 0, ricochet::schema::client);

            BOOST_REQUIRE_NO_THROW(client->write_query(yield, ricochet::query::make_connect_query(red, blue)));

            ricochet::reply reply(4096);
            BOOST_REQUIRE_NO_THROW(client->read_reply(yield, reply));
            BOOST_CHECK_EQUAL(reply.type(), ricochet::reply::kind::mistake);
            auto failure = std::get<ricochet::failure>(reply.payload());
            BOOST_CHECK_EQUAL(failure, ricochet::failure::malformed_message);
        }

        BOOST_REQUIRE_NO_THROW(client->shutdown(yield));
        BOOST_REQUIRE_NO_THROW(server->stop());
    },
    boost::asio::use_future);

    BOOST_REQUIRE_NO_THROW(fut.get());
}

BOOST_AUTO_TEST_CASE(server_idle_timeout)
{
    auto fut = boost::asio::spawn(get_io_context(), [&](boost::asio::yield_context yield)
    {
        auto server = create_server(true);
        auto client = create_client(true);

        BOOST_REQUIRE_NO_THROW(server->start());
        BOOST_REQUIRE_NO_THROW(client->connect(yield));

        BOOST_REQUIRE_NO_THROW(client->write_query(yield, ricochet::query::make_provide_query(ricochet::protocol::udp4)));

        ricochet::reply reply(4096);
        BOOST_REQUIRE_NO_THROW(client->read_reply(yield, reply));

        boost::asio::steady_timer timer(get_io_context());
        timer.expires_after(std::chrono::seconds(3));
        timer.async_wait(yield);

        try
        {
            ricochet::peer red(boost::asio::ip::make_address("0.0.0.0"), 0, ricochet::schema::client);
            ricochet::peer blue(boost::asio::ip::make_address("0.0.0.0"), 0, ricochet::schema::client);

            client->write_query(yield, ricochet::query::make_connect_query(red, blue));

            ricochet::reply reply(4096);
            client->read_reply(yield, reply);
        }
        catch (const boost::system::system_error&)
        {
            BOOST_CHECK(true);
        }

        BOOST_REQUIRE_NO_THROW(client->shutdown(yield));
        BOOST_REQUIRE_NO_THROW(server->stop());
    },
    boost::asio::use_future);

    BOOST_REQUIRE_NO_THROW(fut.get());
}

BOOST_AUTO_TEST_SUITE_END()