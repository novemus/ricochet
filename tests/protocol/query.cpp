/*
 * Copyright (c) 2026 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include <boost/test/unit_test.hpp>
#include <boost/asio.hpp>
#include <vector>
#include "proto.h"

using namespace ricochet;

BOOST_AUTO_TEST_CASE(query_default_constructor)
{
    query q;
    BOOST_CHECK_EQUAL(q.size(), 0);
}

BOOST_AUTO_TEST_CASE(query_buffer_constructor)
{
    std::vector<uint8_t> data = {0x00, 0x01, 0x00, 0x00, 0x02, 0x00}; // provide query with TCP protocol
    query q(data);
    
    BOOST_CHECK_EQUAL(q.size(), data.size());
    BOOST_CHECK(q.data() != nullptr);
}

BOOST_AUTO_TEST_CASE(query_provide_type)
{
    query q = query::make_provide_query(protocol::tcp4);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(q.type()), 0); // provide kind
}

BOOST_AUTO_TEST_CASE(query_connect_type)
{
    boost::asio::ip::address addr1 = boost::asio::ip::make_address("192.168.1.1");
    boost::asio::ip::address addr2 = boost::asio::ip::make_address("192.168.1.2");
    
    query q = query::make_connect_query(peer(addr1, 8080, schema::client), 
                                        peer(addr2, 8081, schema::server));
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(q.type()), 1); // connect kind
}

BOOST_AUTO_TEST_CASE(query_provide_payload)
{
    query q = query::make_provide_query(protocol::udp6);
    query::value payload = q.payload();
    
    BOOST_CHECK(std::holds_alternative<protocol>(payload));
    BOOST_CHECK_EQUAL(std::get<protocol>(payload), protocol::udp6);
}

BOOST_AUTO_TEST_CASE(query_connect_payload)
{
    boost::asio::ip::address addr1 = boost::asio::ip::make_address("127.0.0.1");
    boost::asio::ip::address addr2 = boost::asio::ip::make_address("127.0.0.1");
    
    query q = query::make_connect_query(peer(addr1, 1234, schema::client), 
                                        peer(addr2, 5678, schema::server));
    query::value payload = q.payload();
    
    BOOST_CHECK(std::holds_alternative<couple>(payload));
}

BOOST_AUTO_TEST_CASE(query_length)
{
    query q = query::make_provide_query(protocol::tcp4);
    uint32_t length = q.length();
    BOOST_CHECK(length > 0);
}

BOOST_AUTO_TEST_CASE(query_inheritance_from_buffer)
{
    std::vector<uint8_t> data = {0x00, 0x01, 0x00, 0x00, 0x02, 0x00};
    query q(data);
    
    // Test buffer functionality
    q.resize(10);
    BOOST_CHECK_EQUAL(q.size(), 10);
}

BOOST_AUTO_TEST_CASE(query_make_provide_all_protocols)
{
    query q_udp4 = query::make_provide_query(protocol::udp4);
    query q_tcp4 = query::make_provide_query(protocol::tcp4);
    query q_udp6 = query::make_provide_query(protocol::udp6);
    query q_tcp6 = query::make_provide_query(protocol::tcp6);
    
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(q_udp4.type()), 0);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(q_tcp4.type()), 0);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(q_udp6.type()), 0);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(q_tcp6.type()), 0);
    
    query::value payload_udp4 = q_udp4.payload();
    query::value payload_tcp4 = q_tcp4.payload();
    query::value payload_udp6 = q_udp6.payload();
    query::value payload_tcp6 = q_tcp6.payload();
    
    BOOST_CHECK(std::get<protocol>(payload_udp4) == protocol::udp4);
    BOOST_CHECK(std::get<protocol>(payload_tcp4) == protocol::tcp4);
    BOOST_CHECK(std::get<protocol>(payload_udp6) == protocol::udp6);
    BOOST_CHECK(std::get<protocol>(payload_tcp6) == protocol::tcp6);
}
