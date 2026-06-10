#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <memory>
#include <vector>
#include <chrono>
#include "proto.h"
#include "relay.h"

struct relay_test_fixture
{
    boost::asio::io_context io;
    boost::asio::thread_pool pool;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work;
    std::shared_ptr<ricochet::heap> heap;

    relay_test_fixture() 
        : work(boost::asio::make_work_guard(io))
        , heap(std::make_shared<ricochet::heap>(7412, 10))
    {
        for (size_t i = 0; i < 4; ++i)
        {
            boost::asio::post(pool, [&]()
            {
                io.run();
            });
        }
    }

    ~relay_test_fixture()
    {
        io.stop();
        pool.join();
    }
};

class mock_tcp_client_peer
{
public:
    mock_tcp_client_peer(boost::asio::io_context& io, const boost::asio::ip::tcp::endpoint& peer)
        : m_socket(io)
        , m_peer(peer)
        , m_timer(io)
    {
    }

    virtual void connect()
    {
        start_timer();
        
        auto fut = boost::asio::spawn(m_socket.get_executor(), [&](boost::asio::yield_context yield)
        {
            m_socket.async_connect(m_peer, yield);
        }, 
        boost::asio::use_future);

        stop_timer();
        fut.get();
    }

    virtual void close()
    {
        if (m_socket.is_open())
            m_socket.close();
    }

    void send(const std::vector<uint8_t>& data)
    {
        start_timer();

        auto fut = boost::asio::spawn(m_socket.get_executor(), [&](boost::asio::yield_context yield)
        {
            boost::asio::async_write(m_socket, boost::asio::buffer(data), yield);
        }, 
        boost::asio::use_future);

        stop_timer();
        fut.get();
    }

    std::vector<uint8_t> receive(size_t size)
    {
        start_timer();

        std::vector<uint8_t> buffer(size);
        auto fut = boost::asio::spawn(m_socket.get_executor(), [&](boost::asio::yield_context yield)
        {
            size_t read = boost::asio::async_read(m_socket, boost::asio::buffer(buffer), yield);
            buffer.resize(read);
        }, 
        boost::asio::use_future);

        stop_timer();
        fut.get();

        return buffer;
    }

protected:

    void start_timer()
    {
        m_timer.expires_from_now(boost::posix_time::seconds(2));
        m_timer.async_wait([this](const boost::system::error_code& ec)
        {
            if (ec != boost::asio::error::operation_aborted)
                close();
        });
    }

    void stop_timer()
    {
        m_timer.cancel();
    }

    boost::asio::ip::tcp::socket m_socket;
    boost::asio::ip::tcp::endpoint m_peer;
    boost::asio::deadline_timer m_timer;
};

class mock_tcp_server_peer : public mock_tcp_client_peer
{
public:
    mock_tcp_server_peer(boost::asio::io_context& io, const boost::asio::ip::tcp::endpoint& bind, const boost::asio::ip::tcp::endpoint& peer)
        : mock_tcp_client_peer(io, peer)
        , m_acceptor(io)
    {
        m_acceptor.open(bind.protocol());
        m_acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        m_acceptor.bind(bind);
        m_acceptor.listen();
    }

    void connect() override
    {
        do
        {
            if (m_socket.is_open())
                m_socket.close();

            start_timer();

            auto fut = boost::asio::spawn(m_acceptor.get_executor(), [&](boost::asio::yield_context yield)
            {
                m_acceptor.async_accept(m_socket, yield);
            }, 
            boost::asio::use_future);

            stop_timer();

            fut.get();
        } 
        while (m_socket.remote_endpoint() != m_peer);

        if (m_acceptor.is_open())
            m_acceptor.close();
    }

    void close() override
    {
        if (m_acceptor.is_open())
            m_acceptor.close();

        mock_tcp_client_peer::close();
    }

private:

    boost::asio::ip::tcp::acceptor m_acceptor;
};

struct udp_helper
{
    static void swap(boost::asio::ip::udp::socket& left, boost::asio::ip::udp::socket& right, const std::vector<uint8_t>& data)
    {
        boost::asio::deadline_timer resend_timer(left.get_executor());
        resend_timer.expires_from_now(boost::posix_time::milliseconds(100));
        resend_timer.async_wait([&](const boost::system::error_code& ec)
        {
            if (ec.value() != boost::asio::error::operation_aborted)
            {
                left.send(boost::asio::buffer(data));
                right.send(boost::asio::buffer(data));
            }
        });

        boost::asio::deadline_timer receive_timer(right.get_executor());
        receive_timer.expires_from_now(boost::posix_time::milliseconds(500));
        receive_timer.async_wait([&](const boost::system::error_code& ec)
        {
            if (ec.value() != boost::asio::error::operation_aborted)
            {
                left.close();
                right.close();
            }
        });

        left.send(boost::asio::buffer(data));
        right.send(boost::asio::buffer(data));

        std::vector<uint8_t> recv(data.size());
        boost::asio::spawn(right.get_executor(), [&](boost::asio::yield_context yield)
        {
            auto read = right.async_receive(boost::asio::buffer(recv), yield);
            recv.resize(read);
        }, 
        boost::asio::use_future).get();

        if (recv != data)
            throw boost::system::system_error(boost::asio::error::invalid_argument);

        boost::asio::spawn(left.get_executor(), [&](boost::asio::yield_context yield)
        {
            auto read = left.async_receive(boost::asio::buffer(recv), yield);
            recv.resize(read);
        }, 
        boost::asio::use_future).get();
    
        if (recv != data)
            throw boost::system::system_error(boost::asio::error::invalid_argument);

        resend_timer.cancel();
        receive_timer.cancel();
    }

    static void push(boost::asio::ip::udp::socket& from, boost::asio::ip::udp::socket& to, const std::vector<uint8_t>& data)
    {
        boost::asio::deadline_timer resend_timer(from.get_executor());
        resend_timer.expires_from_now(boost::posix_time::milliseconds(100));
        resend_timer.async_wait([&](const boost::system::error_code& ec)
        {
            if (ec.value() != boost::asio::error::operation_aborted)
            {
                from.send(boost::asio::buffer(data));
            }
        });

        boost::asio::deadline_timer receive_timer(to.get_executor());
        receive_timer.expires_from_now(boost::posix_time::milliseconds(500));
        receive_timer.async_wait([&](const boost::system::error_code& ec)
        {
            if (ec.value() != boost::asio::error::operation_aborted)
            {
                to.close();
            }
        });

        from.send(boost::asio::buffer(data));

        std::vector<uint8_t> recv(data.size());
        do
        {
            boost::asio::spawn(to.get_executor(), [&](boost::asio::yield_context yield)
            {
                auto read = to.async_receive(boost::asio::buffer(recv), yield);
                recv.resize(read);
            }, 
            boost::asio::use_future).get();
        } 
        while (recv != data);

        resend_timer.cancel();
        receive_timer.cancel();
    }
};

BOOST_FIXTURE_TEST_SUITE(relay_tests, relay_test_fixture)

BOOST_AUTO_TEST_CASE(is_ip4_available_success)
{
    BOOST_REQUIRE_NO_THROW(ricochet::is_ip4_available(io));
}

BOOST_AUTO_TEST_CASE(is_ip6_available_success)
{
    BOOST_REQUIRE_NO_THROW(ricochet::is_ip6_available(io));
}

BOOST_AUTO_TEST_CASE(get_outgoing_address)
{
    try
    {
        auto ipv4_address = ricochet::get_outgoing_address(io, true);
        BOOST_TEST_MESSAGE("Got IPv4 address: " + ipv4_address.to_string());

        BOOST_CHECK(!ipv4_address.is_unspecified());
        BOOST_CHECK(ipv4_address.is_v4());
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("IPv4 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }

    try
    {
        auto ipv6_address = ricochet::get_outgoing_address(io, false);
        BOOST_TEST_MESSAGE("Got IPv6 address: " + ipv6_address.to_string());

        BOOST_CHECK(!ipv6_address.is_unspecified());
        BOOST_CHECK(ipv6_address.is_v6());
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("IPv6 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }
}

BOOST_AUTO_TEST_CASE(get_outgoing_address_with_specified_address)
{
    try
    {
        boost::asio::ip::address specified_address = boost::asio::ip::make_address("192.168.1.100");
        auto result = ricochet::get_outgoing_address(io, specified_address);

        BOOST_TEST(result == specified_address);
        BOOST_TEST_MESSAGE("Specified address returned correctly: " + result.to_string());
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("IPv4 unavailable: " + std::string(e.what()));
        BOOST_TEST(false);
    }

    try
    {
        boost::asio::ip::address specified_address = boost::asio::ip::make_address("::1");
        auto result = ricochet::get_outgoing_address(io, specified_address);

        BOOST_TEST(result == specified_address);
        BOOST_TEST_MESSAGE("Specified address returned correctly: " + result.to_string());
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("IPv6 unavailable: " + std::string(e.what()));
        BOOST_TEST(false);
    }
}

BOOST_AUTO_TEST_CASE(get_outgoing_address_with_unspecified_address)
{
    try
    {
        boost::asio::ip::address unspecified_address = boost::asio::ip::make_address("0.0.0.0");
        auto result = ricochet::get_outgoing_address(io, unspecified_address);

        BOOST_TEST(result != unspecified_address);
        BOOST_TEST_MESSAGE("Detected outgoing address: " + result.to_string());
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("IPv4 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }

    try
    {
        boost::asio::ip::address unspecified_address = boost::asio::ip::make_address("::");
        auto result = ricochet::get_outgoing_address(io, unspecified_address);

        BOOST_TEST(result != unspecified_address);
        BOOST_TEST_MESSAGE("Detected outgoing address: " + result.to_string());
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("IPv6 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }
}

BOOST_AUTO_TEST_CASE(environment_variable_parsing_ipv4)
{
    const char* old_env = std::getenv("RICOCHET_OUTGOING_TEST_IPV4");

#ifdef _WIN32
    _putenv("RICOCHET_OUTGOING_TEST_IPV4=1.2.3.4:8080");
#else
    setenv("RICOCHET_OUTGOING_TEST_IPV4", "1.2.3.4:8080", 1);
#endif

    try
    {
        auto result = ricochet::get_outgoing_address(io, true);

        BOOST_TEST(result.is_v4());
        BOOST_TEST_MESSAGE("Environment variable parsing test completed");
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("IPv4 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }

    if (old_env)
    {
#ifdef _WIN32
        std::string env_var = "RICOCHET_OUTGOING_TEST_IPV4=" + std::string(old_env);
        _putenv(env_var.c_str());
#else
        setenv("RICOCHET_OUTGOING_TEST_IPV4", old_env, 1);
#endif
    }
    else
    {
#ifdef _WIN32
        _putenv("RICOCHET_OUTGOING_TEST_IPV4=");
#else
        unsetenv("RICOCHET_OUTGOING_TEST_IPV4");
#endif
    }
}

BOOST_AUTO_TEST_CASE(environment_variable_parsing_ipv6)
{
    const char* old_env = std::getenv("RICOCHET_OUTGOING_TEST_IPV6");

#ifdef _WIN32
    _putenv("RICOCHET_OUTGOING_TEST_IPV6=[2001:db8::1]:8080");
#else
    setenv("RICOCHET_OUTGOING_TEST_IPV6", "[2001:db8::1]:8080", 1);
#endif

    try
    {
        auto result = ricochet::get_outgoing_address(io, false);

        BOOST_TEST(result.is_v6());
        BOOST_TEST_MESSAGE("Environment variable parsing test completed");
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("IPv6 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }

    if (old_env)
    {
#ifdef _WIN32
        std::string env_var = "RICOCHET_OUTGOING_TEST_IPV6=" + std::string(old_env);
        _putenv(env_var.c_str());
#else
        setenv("RICOCHET_OUTGOING_TEST_IPV6", old_env, 1);
#endif
    }
    else
    {
#ifdef _WIN32
        _putenv("RICOCHET_OUTGOING_TEST_IPV6=");
#else
        unsetenv("RICOCHET_OUTGOING_TEST_IPV6");
#endif
    }
}

BOOST_AUTO_TEST_CASE(invalid_environment_variables)
{
    const char* old_ipv4_env = std::getenv("RICOCHET_OUTGOING_TEST_IPV4");
    const char* old_ipv6_env = std::getenv("RICOCHET_OUTGOING_TEST_IPV6");

#ifdef _WIN32
    _putenv("RICOCHET_OUTGOING_TEST_IPV4=invalid_format");
    _putenv("RICOCHET_OUTGOING_TEST_IPV6=invalid_format");
#else
    setenv("RICOCHET_OUTGOING_TEST_IPV4", "invalid_format", 1);
    setenv("RICOCHET_OUTGOING_TEST_IPV6", "invalid_format", 1);
#endif

    try
    {
        auto result = ricochet::get_outgoing_address(io, true);

        BOOST_TEST(result.is_v4());
        BOOST_TEST_MESSAGE("Invalid IPv4 env var handled gracefully");
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("IPv4 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }

    try
    {
        auto result = ricochet::get_outgoing_address(io, false);

        BOOST_TEST(result.is_v6());
        BOOST_TEST_MESSAGE("Invalid IPv4 env var handled gracefully");
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("IPv6 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }

    if (old_ipv4_env)
    {
#ifdef _WIN32
        std::string env_var = "RICOCHET_OUTGOING_TEST_IPV4=" + std::string(old_ipv4_env);
        _putenv(env_var.c_str());
#else
        setenv("RICOCHET_OUTGOING_TEST_IPV4", old_ipv4_env, 1);
#endif
    }
    else
    {
#ifdef _WIN32
        _putenv("RICOCHET_OUTGOING_TEST_IPV4=");
#else
        unsetenv("RICOCHET_OUTGOING_TEST_IPV4");
#endif
    }

    if (old_ipv6_env)
    {
#ifdef _WIN32
        std::string env_var = "RICOCHET_OUTGOING_TEST_IPV6=" + std::string(old_ipv6_env);
        _putenv(env_var.c_str());
#else
        setenv("RICOCHET_OUTGOING_TEST_IPV6", old_ipv6_env, 1);
#endif
    }
    else
    {
#ifdef _WIN32
        _putenv("RICOCHET_OUTGOING_TEST_IPV6=");
#else
        unsetenv("RICOCHET_OUTGOING_TEST_IPV6");
#endif
    }
}

BOOST_AUTO_TEST_CASE(tcp4_relay_base)
{
    try
    {
        auto tcp_relay = std::make_shared<ricochet::tcp_relay>(
            io,
            heap->make_tcp_relay(io, true),
            boost::posix_time::seconds(30),
            boost::posix_time::seconds(300)
        );

        BOOST_TEST(tcp_relay->get_protocol() == ricochet::protocol::tcp4);

        auto endpoint = tcp_relay->get_endpoint();
        auto addr = endpoint.address();
        auto port = endpoint.port();

        BOOST_TEST(!addr.is_unspecified());
        BOOST_TEST(addr.is_v4());
        BOOST_TEST(port != 0);

        tcp_relay->close();

        BOOST_TEST_MESSAGE("TCP4 relay base test passed");
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("TCP4 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }
    catch (const std::exception& e)
    {
        BOOST_TEST_MESSAGE("TCP4 relay base test failed: " + std::string(e.what()));
        BOOST_TEST(false);
    }
}

BOOST_AUTO_TEST_CASE(tcp6_relay_base)
{
    try
    {
        auto tcp_relay = std::make_shared<ricochet::tcp_relay>(
            io,
            heap->make_tcp_relay(io, false),
            boost::posix_time::seconds(30),
            boost::posix_time::seconds(300)
        );

        BOOST_TEST(tcp_relay->get_protocol() == ricochet::protocol::tcp6);

        auto endpoint = tcp_relay->get_endpoint();
        auto addr = endpoint.address();
        auto port = endpoint.port();

        BOOST_TEST(!addr.is_unspecified());
        BOOST_TEST(addr.is_v6());
        BOOST_TEST(port != 0);

        tcp_relay->close();

        BOOST_TEST_MESSAGE("TCP6 relay base test passed");
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("TCP6 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }
    catch (const std::exception& e)
    {
        BOOST_TEST_MESSAGE("TCP6 relay base test failed: " + std::string(e.what()));
        BOOST_TEST(false);
    }
}

BOOST_AUTO_TEST_CASE(udp4_relay_base)
{
    try
    {
        auto udp_relay = std::make_shared<ricochet::udp_relay>(
            io,
            heap->make_udp_relay(io, true),
            boost::posix_time::seconds(30),
            boost::posix_time::seconds(300)
        );

        auto proto = udp_relay->get_protocol();
        BOOST_TEST(proto == ricochet::protocol::udp4);

        auto endpoint = udp_relay->get_endpoint();
        auto addr = endpoint.address();
        auto port = endpoint.port();

        BOOST_TEST(!addr.is_unspecified());
        BOOST_TEST(addr.is_v4());
        BOOST_TEST(port != 0);

        udp_relay->close();

        BOOST_TEST_MESSAGE("UDP4 relay base test passed");
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("UDP4 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }
    catch (const std::exception& e)
    {
        BOOST_TEST_MESSAGE("UDP4 relay base test failed: " + std::string(e.what()));
        BOOST_TEST(false);
    }
}

BOOST_AUTO_TEST_CASE(udp6_relay_base)
{
    try
    {
        auto udp_relay = std::make_shared<ricochet::udp_relay>(
            io,
            heap->make_udp_relay(io, false),
            boost::posix_time::seconds(30),
            boost::posix_time::seconds(300)
        );

        auto proto = udp_relay->get_protocol();
        BOOST_TEST(proto == ricochet::protocol::udp6);

        auto endpoint = udp_relay->get_endpoint();
        auto addr = endpoint.address();
        auto port = endpoint.port();

        BOOST_TEST(!addr.is_unspecified());
        BOOST_TEST(addr.is_v6());
        BOOST_TEST(port != 0);

        udp_relay->close();

        BOOST_TEST_MESSAGE("UDP6 relay base test passed");
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("UDP6 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }
    catch (const std::exception& e)
    {
        BOOST_TEST_MESSAGE("UDP6 relay base test failed: " + std::string(e.what()));
        BOOST_TEST(false);
    }
}

BOOST_AUTO_TEST_CASE(tcp4_client_client_relay)
{
    try
    {
        auto relay = std::make_shared<ricochet::tcp_relay>(
            io,
            heap->make_tcp_relay(io, true),
            boost::posix_time::seconds(10),
            boost::posix_time::seconds(10)
        );

        ricochet::endpoint ep = relay->get_endpoint();
        boost::asio::ip::tcp::endpoint mirror(ep.address(), ep.port());

        auto cp = std::make_shared<std::promise<void>>();

        BOOST_REQUIRE_NO_THROW(
            relay->start(
                ricochet::peer(boost::asio::ip::make_address("0.0.0.0"), 0, ricochet::schema::client),
                ricochet::peer(boost::asio::ip::make_address("0.0.0.0"), 0, ricochet::schema::client),
                [cp]() { cp->set_value(); }
            )
        );

        mock_tcp_client_peer left(io, mirror);
        mock_tcp_client_peer right(io, mirror);

        BOOST_REQUIRE_NO_THROW(left.connect());
        BOOST_REQUIRE_NO_THROW(right.connect());

        std::vector<uint8_t> data = { 1, 2, 3, 4, 5 };

        BOOST_REQUIRE_NO_THROW(left.send(data));
        BOOST_REQUIRE_NO_THROW(right.send(data));
        BOOST_REQUIRE_NO_THROW(BOOST_CHECK(left.receive(data.size()) == data));
        BOOST_REQUIRE_NO_THROW(BOOST_CHECK(right.receive(data.size()) == data));

        mock_tcp_client_peer extra(io, mirror);
        BOOST_REQUIRE_THROW(extra.connect(), boost::system::system_error);

        BOOST_REQUIRE_NO_THROW(left.close());
        BOOST_REQUIRE_NO_THROW(right.close());
        BOOST_REQUIRE_NO_THROW(relay->close());

        BOOST_CHECK(cp->get_future().wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready);
        BOOST_TEST_MESSAGE("TCP4 clien-client relay test passed");
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("TCP4 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }
}

BOOST_AUTO_TEST_CASE(tcp6_client_client_relay)
{
    try
    {
        auto relay = std::make_shared<ricochet::tcp_relay>(
            io,
            heap->make_tcp_relay(io, false),
            boost::posix_time::seconds(10),
            boost::posix_time::seconds(10)
        );

        ricochet::endpoint ep = relay->get_endpoint();
        boost::asio::ip::tcp::endpoint mirror(ep.address(), ep.port());

        auto cp = std::make_shared<std::promise<void>>();

        BOOST_REQUIRE_NO_THROW(
            relay->start(
                ricochet::peer(boost::asio::ip::make_address("::"), 0, ricochet::schema::client),
                ricochet::peer(boost::asio::ip::make_address("::"), 0, ricochet::schema::client),
                [cp]() { cp->set_value(); }
            )
        );

        mock_tcp_client_peer left(io, mirror);
        mock_tcp_client_peer right(io, mirror);

        BOOST_REQUIRE_NO_THROW(left.connect());
        BOOST_REQUIRE_NO_THROW(right.connect());

        std::vector<uint8_t> data = { 1, 2, 3, 4, 5 };

        BOOST_REQUIRE_NO_THROW(left.send(data));
        BOOST_REQUIRE_NO_THROW(right.send(data));
        BOOST_REQUIRE_NO_THROW(BOOST_CHECK(left.receive(data.size()) == data));
        BOOST_REQUIRE_NO_THROW(BOOST_CHECK(right.receive(data.size()) == data));

        mock_tcp_client_peer extra(io, mirror);
        BOOST_REQUIRE_THROW(extra.connect(), boost::system::system_error);

        BOOST_REQUIRE_NO_THROW(left.close());
        BOOST_REQUIRE_NO_THROW(right.close());
        BOOST_REQUIRE_NO_THROW(relay->close());

        BOOST_CHECK(cp->get_future().wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready);
        BOOST_TEST_MESSAGE("TCP6 clien-client relay test passed");
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("TCP6 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }
}

BOOST_AUTO_TEST_CASE(udp4_client_client_relay)
{
    try
    {
        auto relay = std::make_shared<ricochet::udp_relay>(
            io,
            heap->make_udp_relay(io, true),
            boost::posix_time::seconds(10),
            boost::posix_time::seconds(10)
        );

        ricochet::endpoint ep = relay->get_endpoint();
        boost::asio::ip::udp::endpoint mirror(ep.address(), ep.port());

        auto cp = std::make_shared<std::promise<void>>();

        BOOST_REQUIRE_NO_THROW(
            relay->start(
                ricochet::peer(boost::asio::ip::make_address("0.0.0.0"), 0, ricochet::schema::client),
                ricochet::peer(boost::asio::ip::make_address("0.0.0.0"), 0, ricochet::schema::client),
                [cp]() { cp->set_value(); }
            )
        );

        boost::asio::ip::udp::socket left(io);
        left.connect(mirror);

        boost::asio::ip::udp::socket right(io);
        right.connect(mirror);

        boost::asio::ip::udp::socket extra(io);
        extra.connect(mirror);

        std::vector<uint8_t> data = { 1, 2, 3, 4, 5 };
        std::vector<uint8_t> more = { 5, 4, 3, 2, 1 };

        BOOST_REQUIRE_NO_THROW(udp_helper::swap(left, right, data));
        BOOST_REQUIRE_NO_THROW(udp_helper::swap(right, left, data));

        BOOST_REQUIRE_THROW(udp_helper::push(extra, left, more), boost::system::system_error);
        BOOST_REQUIRE_THROW(udp_helper::push(right, extra, more), boost::system::system_error);

        BOOST_REQUIRE_NO_THROW(relay->close());

        BOOST_CHECK(cp->get_future().wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready);
        BOOST_TEST_MESSAGE("UDP4 clien-client relay test passed");
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("UDP4 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }
}

BOOST_AUTO_TEST_CASE(udp6_client_client_relay)
{
    try
    {
        auto relay = std::make_shared<ricochet::udp_relay>(
            io,
            heap->make_udp_relay(io, false),
            boost::posix_time::seconds(10),
            boost::posix_time::seconds(10)
        );

        ricochet::endpoint ep = relay->get_endpoint();
        boost::asio::ip::udp::endpoint mirror(ep.address(), ep.port());

        auto cp = std::make_shared<std::promise<void>>();

        BOOST_REQUIRE_NO_THROW(
            relay->start(
                ricochet::peer(boost::asio::ip::make_address("::"), 0, ricochet::schema::client),
                ricochet::peer(boost::asio::ip::make_address("::"), 0, ricochet::schema::client),
                [cp]() { cp->set_value(); }
            )
        );

        boost::asio::ip::udp::socket left(io);
        left.connect(mirror);

        boost::asio::ip::udp::socket right(io);
        right.connect(mirror);

        boost::asio::ip::udp::socket extra(io);
        extra.connect(mirror);

        std::vector<uint8_t> data = { 1, 2, 3, 4, 5 };
        std::vector<uint8_t> more = { 5, 4, 3, 2, 1 };

        BOOST_REQUIRE_NO_THROW(udp_helper::swap(left, right, data));
        BOOST_REQUIRE_NO_THROW(udp_helper::swap(right, left, data));

        BOOST_REQUIRE_THROW(udp_helper::push(extra, left, more), boost::system::system_error);
        BOOST_REQUIRE_THROW(udp_helper::push(right, extra, more), boost::system::system_error);

        BOOST_REQUIRE_NO_THROW(relay->close());

        BOOST_CHECK(cp->get_future().wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready);
        BOOST_TEST_MESSAGE("UDP6 clien-client relay test passed");
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("UDP6 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }
}

BOOST_AUTO_TEST_CASE(tcp4_client_server_relay)
{
    try
    {
        auto relay = std::make_shared<ricochet::tcp_relay>(
            io,
            heap->make_tcp_relay(io, true),
            boost::posix_time::seconds(10),
            boost::posix_time::seconds(10)
        );

        ricochet::endpoint ep = relay->get_endpoint();
        boost::asio::ip::tcp::endpoint mirror(ep.address(), ep.port());
        boost::asio::ip::tcp::endpoint local(ricochet::get_outgoing_address(io, true), 3000);

        auto cp = std::make_shared<std::promise<void>>();

        BOOST_REQUIRE_NO_THROW(
            relay->start(
                ricochet::peer(boost::asio::ip::make_address("0.0.0.0"), 0, ricochet::schema::client),
                ricochet::peer(local.address(), local.port(), ricochet::schema::server),
                [cp]() { cp->set_value(); }
            )
        );

        mock_tcp_client_peer client(io, mirror);
        BOOST_REQUIRE_NO_THROW(client.connect());

        mock_tcp_server_peer server(io, local, mirror);
        BOOST_REQUIRE_NO_THROW(server.connect());

        std::vector<uint8_t> data = { 1, 2, 3, 4, 5 };

        BOOST_REQUIRE_NO_THROW(client.send(data));
        BOOST_REQUIRE_NO_THROW(server.send(data));
        BOOST_REQUIRE_NO_THROW(BOOST_CHECK(client.receive(data.size()) == data));
        BOOST_REQUIRE_NO_THROW(BOOST_CHECK(server.receive(data.size()) == data));

        mock_tcp_client_peer extra(io, mirror);
        BOOST_REQUIRE_THROW(extra.connect(), boost::system::system_error);

        BOOST_REQUIRE_NO_THROW(client.close());
        BOOST_REQUIRE_NO_THROW(server.close());
        BOOST_REQUIRE_NO_THROW(relay->close());

        BOOST_CHECK(cp->get_future().wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready);
        BOOST_TEST_MESSAGE("TCP4 client-server relay test passed");
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("TCP4 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }
}

BOOST_AUTO_TEST_CASE(tcp6_client_server_relay)
{
    try
    {
        auto relay = std::make_shared<ricochet::tcp_relay>(
            io,
            heap->make_tcp_relay(io, false),
            boost::posix_time::seconds(10),
            boost::posix_time::seconds(10)
        );

        ricochet::endpoint ep = relay->get_endpoint();
        boost::asio::ip::tcp::endpoint mirror(ep.address(), ep.port());
        boost::asio::ip::tcp::endpoint local(ricochet::get_outgoing_address(io, false), 4000);

        auto cp = std::make_shared<std::promise<void>>();

        BOOST_REQUIRE_NO_THROW(
            relay->start(
                ricochet::peer(boost::asio::ip::make_address("::"), 0, ricochet::schema::client),
                ricochet::peer(local.address(), local.port(), ricochet::schema::server),
                [cp]() { cp->set_value(); }
            )
        );

        mock_tcp_client_peer client(io, mirror);
        BOOST_REQUIRE_NO_THROW(client.connect());

        mock_tcp_server_peer server(io, local, mirror);
        BOOST_REQUIRE_NO_THROW(server.connect());

        std::vector<uint8_t> data = { 1, 2, 3, 4, 5 };

        BOOST_REQUIRE_NO_THROW(client.send(data));
        BOOST_REQUIRE_NO_THROW(server.send(data));
        BOOST_REQUIRE_NO_THROW(BOOST_CHECK(client.receive(data.size()) == data));
        BOOST_REQUIRE_NO_THROW(BOOST_CHECK(server.receive(data.size()) == data));

        mock_tcp_client_peer extra(io, mirror);
        BOOST_REQUIRE_THROW(extra.connect(), boost::system::system_error);

        BOOST_REQUIRE_NO_THROW(client.close());
        BOOST_REQUIRE_NO_THROW(server.close());
        BOOST_REQUIRE_NO_THROW(relay->close());

        BOOST_CHECK(cp->get_future().wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready);
        BOOST_TEST_MESSAGE("TCP6 client-server relay test passed");
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("TCP6 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }
}

BOOST_AUTO_TEST_CASE(udp4_client_server_relay)
{
    try
    {
        auto relay = std::make_shared<ricochet::udp_relay>(
            io,
            heap->make_udp_relay(io, true),
            boost::posix_time::seconds(10),
            boost::posix_time::seconds(10)
        );

        ricochet::endpoint ep = relay->get_endpoint();
        boost::asio::ip::udp::endpoint mirror(ep.address(), ep.port());
        boost::asio::ip::udp::endpoint local(ricochet::get_outgoing_address(io, true), 5000);

        auto cp = std::make_shared<std::promise<void>>();

        BOOST_REQUIRE_NO_THROW(
            relay->start(
                ricochet::peer(boost::asio::ip::make_address("0.0.0.0"), 0, ricochet::schema::client),
                ricochet::peer(local.address(), local.port(), ricochet::schema::server),
                [cp]() { cp->set_value(); }
            )
        );

        boost::asio::ip::udp::socket client(io);
        client.connect(mirror);

        boost::asio::ip::udp::socket server(io);
        server.open(local.protocol());
        server.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        server.bind(local);
        server.connect(mirror);

        boost::asio::ip::udp::socket extra(io);
        extra.connect(mirror);

        std::vector<uint8_t> data = { 1, 2, 3, 4, 5 };
        std::vector<uint8_t> more = { 5, 4, 3, 2, 1 };

        BOOST_REQUIRE_NO_THROW(udp_helper::push(client, server, data));
        BOOST_REQUIRE_NO_THROW(udp_helper::push(server, client, data));

        BOOST_REQUIRE_THROW(udp_helper::push(extra, client, more), boost::system::system_error);
        BOOST_REQUIRE_THROW(udp_helper::push(server, extra, more), boost::system::system_error);

        BOOST_REQUIRE_NO_THROW(relay->close());

        BOOST_CHECK(cp->get_future().wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready);
        BOOST_TEST_MESSAGE("UDP4 client-server relay test passed");
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("UDP4 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }
}

BOOST_AUTO_TEST_CASE(udp6_client_server_relay)
{
    try
    {
        auto relay = std::make_shared<ricochet::udp_relay>(
            io,
            heap->make_udp_relay(io, false),
            boost::posix_time::seconds(10),
            boost::posix_time::seconds(10)
        );

        ricochet::endpoint ep = relay->get_endpoint();
        boost::asio::ip::udp::endpoint mirror(ep.address(), ep.port());
        boost::asio::ip::udp::endpoint local(ricochet::get_outgoing_address(io, false), 6000);

        auto cp = std::make_shared<std::promise<void>>();

        BOOST_REQUIRE_NO_THROW(
            relay->start(
                ricochet::peer(boost::asio::ip::make_address("::"), 0, ricochet::schema::client),
                ricochet::peer(local.address(), local.port(), ricochet::schema::server),
                [cp]() { cp->set_value(); }
            )
        );

        boost::asio::ip::udp::socket client(io);
        client.connect(mirror);

        boost::asio::ip::udp::socket server(io);
        server.open(local.protocol());
        server.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        server.bind(local);
        server.connect(mirror);

        boost::asio::ip::udp::socket extra(io);
        extra.connect(mirror);

        std::vector<uint8_t> data = { 1, 2, 3, 4, 5 };
        std::vector<uint8_t> more = { 5, 4, 3, 2, 1 };

        BOOST_REQUIRE_NO_THROW(udp_helper::push(client, server, data));
        BOOST_REQUIRE_NO_THROW(udp_helper::push(server, client, data));

        BOOST_REQUIRE_THROW(udp_helper::push(extra, client, more), boost::system::system_error);
        BOOST_REQUIRE_THROW(udp_helper::push(server, extra, more), boost::system::system_error);

        BOOST_REQUIRE_NO_THROW(relay->close());

        BOOST_CHECK(cp->get_future().wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready);
        BOOST_TEST_MESSAGE("UDP6 client-server relay test passed");
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("UDP6 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }
}

BOOST_AUTO_TEST_CASE(tcp4_server_server_relay)
{
    try
    {
        auto relay = std::make_shared<ricochet::tcp_relay>(
            io,
            heap->make_tcp_relay(io, true),
            boost::posix_time::seconds(10),
            boost::posix_time::seconds(10)
        );

        ricochet::endpoint ep = relay->get_endpoint();
        boost::asio::ip::tcp::endpoint mirror(ep.address(), ep.port());
        boost::asio::ip::tcp::endpoint one(ricochet::get_outgoing_address(io, true), 3001);
        boost::asio::ip::tcp::endpoint two(ricochet::get_outgoing_address(io, true), 3002);

        auto cp = std::make_shared<std::promise<void>>();

        BOOST_REQUIRE_NO_THROW(
            relay->start(
                ricochet::peer(one.address(), one.port(), ricochet::schema::server),
                ricochet::peer(two.address(), two.port(), ricochet::schema::server),
                [cp]() { cp->set_value(); }
            )
        );

        mock_tcp_server_peer red(io, one, mirror);
        BOOST_REQUIRE_NO_THROW(red.connect());

        mock_tcp_server_peer blue(io, two, mirror);
        BOOST_REQUIRE_NO_THROW(blue.connect());

        std::vector<uint8_t> data = { 1, 2, 3, 4, 5 };

        BOOST_REQUIRE_NO_THROW(red.send(data));
        BOOST_REQUIRE_NO_THROW(blue.send(data));
        BOOST_REQUIRE_NO_THROW(BOOST_CHECK(red.receive(data.size()) == data));
        BOOST_REQUIRE_NO_THROW(BOOST_CHECK(blue.receive(data.size()) == data));

        mock_tcp_client_peer extra(io, mirror);
        BOOST_REQUIRE_THROW(extra.connect(), boost::system::system_error);

        BOOST_REQUIRE_NO_THROW(red.close());
        BOOST_REQUIRE_NO_THROW(blue.close());
        BOOST_REQUIRE_NO_THROW(relay->close());

        BOOST_CHECK(cp->get_future().wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready);
        BOOST_TEST_MESSAGE("TCP4 server-server relay test passed");
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("TCP4 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }
}

BOOST_AUTO_TEST_CASE(tcp6_server_server_relay)
{
    try
    {
        auto relay = std::make_shared<ricochet::tcp_relay>(
            io,
            heap->make_tcp_relay(io, false),
            boost::posix_time::seconds(10),
            boost::posix_time::seconds(10)
        );

        ricochet::endpoint ep = relay->get_endpoint();
        boost::asio::ip::tcp::endpoint mirror(ep.address(), ep.port());
        boost::asio::ip::tcp::endpoint one(ricochet::get_outgoing_address(io, false), 4001);
        boost::asio::ip::tcp::endpoint two(ricochet::get_outgoing_address(io, false), 4002);

        auto cp = std::make_shared<std::promise<void>>();

        BOOST_REQUIRE_NO_THROW(
            relay->start(
                ricochet::peer(one.address(), one.port(), ricochet::schema::server),
                ricochet::peer(two.address(), two.port(), ricochet::schema::server),
                [cp]() { cp->set_value(); }
            )
        );

        mock_tcp_server_peer red(io, one, mirror);
        BOOST_REQUIRE_NO_THROW(red.connect());

        mock_tcp_server_peer blue(io, two, mirror);
        BOOST_REQUIRE_NO_THROW(blue.connect());

        std::vector<uint8_t> data = { 1, 2, 3, 4, 5 };

        BOOST_REQUIRE_NO_THROW(red.send(data));
        BOOST_REQUIRE_NO_THROW(blue.send(data));
        BOOST_REQUIRE_NO_THROW(BOOST_CHECK(red.receive(data.size()) == data));
        BOOST_REQUIRE_NO_THROW(BOOST_CHECK(blue.receive(data.size()) == data));

        mock_tcp_client_peer extra(io, mirror);
        BOOST_REQUIRE_THROW(extra.connect(), boost::system::system_error);

        BOOST_REQUIRE_NO_THROW(red.close());
        BOOST_REQUIRE_NO_THROW(blue.close());
        BOOST_REQUIRE_NO_THROW(relay->close());

        BOOST_CHECK(cp->get_future().wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready);
        BOOST_TEST_MESSAGE("TCP6 server-server relay test passed");
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("TCP6 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }
}

BOOST_AUTO_TEST_CASE(udp4_server_server_relay)
{
    try
    {
        auto relay = std::make_shared<ricochet::udp_relay>(
            io,
            heap->make_udp_relay(io, true),
            boost::posix_time::seconds(10),
            boost::posix_time::seconds(10)
        );

        ricochet::endpoint ep = relay->get_endpoint();
        boost::asio::ip::udp::endpoint mirror(ep.address(), ep.port());
        boost::asio::ip::udp::endpoint one(ricochet::get_outgoing_address(io, true), 5001);
        boost::asio::ip::udp::endpoint two(ricochet::get_outgoing_address(io, true), 5002);

        auto cp = std::make_shared<std::promise<void>>();

        BOOST_REQUIRE_NO_THROW(
            relay->start(
                ricochet::peer(one.address(), one.port(), ricochet::schema::server),
                ricochet::peer(two.address(), two.port(), ricochet::schema::server),
                [cp]() { cp->set_value(); }
            )
        );

        boost::asio::ip::udp::socket red(io);
        red.open(one.protocol());
        red.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        red.bind(one);
        red.connect(mirror);

        boost::asio::ip::udp::socket blue(io);
        blue.open(two.protocol());
        blue.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        blue.bind(two);
        blue.connect(mirror);

        boost::asio::ip::udp::socket extra(io);
        extra.connect(mirror);

        std::vector<uint8_t> data = { 1, 2, 3, 4, 5 };
        std::vector<uint8_t> more = { 5, 4, 3, 2, 1 };

        BOOST_REQUIRE_NO_THROW(udp_helper::push(red, blue, data));
        BOOST_REQUIRE_NO_THROW(udp_helper::push(blue, red, data));

        BOOST_REQUIRE_THROW(udp_helper::push(extra, red, more), boost::system::system_error);
        BOOST_REQUIRE_THROW(udp_helper::push(blue, extra, more), boost::system::system_error);

        BOOST_REQUIRE_NO_THROW(relay->close());

        BOOST_CHECK(cp->get_future().wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready);
        BOOST_TEST_MESSAGE("UDP4 server-server relay test passed");
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("UDP4 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }
}

BOOST_AUTO_TEST_CASE(udp6_server_server_relay)
{
    try
    {
        auto relay = std::make_shared<ricochet::udp_relay>(
            io,
            heap->make_udp_relay(io, false),
            boost::posix_time::seconds(10),
            boost::posix_time::seconds(10)
        );

        ricochet::endpoint ep = relay->get_endpoint();
        boost::asio::ip::udp::endpoint mirror(ep.address(), ep.port());
        boost::asio::ip::udp::endpoint one(ricochet::get_outgoing_address(io, false), 6001);
        boost::asio::ip::udp::endpoint two(ricochet::get_outgoing_address(io, false), 6002);

        auto cp = std::make_shared<std::promise<void>>();

        BOOST_REQUIRE_NO_THROW(
            relay->start(
                ricochet::peer(one.address(), one.port(), ricochet::schema::server),
                ricochet::peer(two.address(), two.port(), ricochet::schema::server),
                [cp]() { cp->set_value(); }
            )
        );

        boost::asio::ip::udp::socket red(io);
        red.open(one.protocol());
        red.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        red.bind(one);
        red.connect(mirror);

        boost::asio::ip::udp::socket blue(io);
        blue.open(two.protocol());
        blue.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        blue.bind(two);
        blue.connect(mirror);

        boost::asio::ip::udp::socket extra(io);
        extra.connect(mirror);

        std::vector<uint8_t> data = { 1, 2, 3, 4, 5 };
        std::vector<uint8_t> more = { 5, 4, 3, 2, 1 };

        BOOST_REQUIRE_NO_THROW(udp_helper::push(red, blue, data));
        BOOST_REQUIRE_NO_THROW(udp_helper::push(blue, red, data));

        BOOST_REQUIRE_THROW(udp_helper::push(extra, red, more), boost::system::system_error);
        BOOST_REQUIRE_THROW(udp_helper::push(blue, extra, more), boost::system::system_error);

        BOOST_REQUIRE_NO_THROW(relay->close());

        BOOST_CHECK(cp->get_future().wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready);
        BOOST_TEST_MESSAGE("UDP6 server-server relay test passed");
    }
    catch (const ricochet::unavailable_proto& e)
    {
        BOOST_TEST_MESSAGE("UDP6 unavailable: " + std::string(e.what()));
        BOOST_TEST(true);
    }
}

BOOST_AUTO_TEST_SUITE_END()
