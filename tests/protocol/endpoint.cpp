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

BOOST_AUTO_TEST_CASE(endpoint_vector_constructor)
{
    std::vector<uint8_t> data = {4, 192, 168, 1, 1, 0x1F, 0x90}; // IPv4: 192.168.1.1:8080
    endpoint ep(data);
    
    BOOST_CHECK_EQUAL(ep.size(), data.size());
    BOOST_CHECK(ep.data() != nullptr);
}

BOOST_AUTO_TEST_CASE(inheritance_from_buffer)
{
    std::vector<uint8_t> data = {4, 127, 0, 0, 1, 0x04, 0xD2}; // IPv4: 127.0.0.1:1234
    endpoint ep(data);
    
    BOOST_CHECK_EQUAL(ep.size(), 7);
    BOOST_CHECK(ep.data() != nullptr);
    
    // Test buffer functionality
    ep.resize(8);
    BOOST_CHECK_EQUAL(ep.size(), 8);
}

BOOST_AUTO_TEST_CASE(address_extraction)
{
    // IPv4 address test
    std::vector<uint8_t> ipv4_data = {4, 192, 168, 1, 100, 0x1F, 0x90}; // IPv4: 192.168.1.100:8080
    endpoint ep_ipv4(ipv4_data);
    
    boost::asio::ip::address addr = ep_ipv4.address();
    BOOST_CHECK(addr.is_v4());
    BOOST_CHECK_EQUAL(addr.to_string(), "192.168.1.100");
}

BOOST_AUTO_TEST_CASE(port_extraction)
{
    std::vector<uint8_t> data = {4, 192, 168, 1, 1, 0x1F, 0x90}; // IPv4: Port 8080
    endpoint ep(data);
    
    uint16_t port = ep.port();
    BOOST_CHECK_EQUAL(port, 8080);
}

BOOST_AUTO_TEST_CASE(empty_data)
{
    std::vector<uint8_t> empty_data;
    endpoint ep(empty_data);
    
    BOOST_CHECK_EQUAL(ep.size(), 0);
}

BOOST_AUTO_TEST_CASE(minimal_ipv4_data)
{
    std::vector<uint8_t> min_data = {4, 127, 0, 0, 1, 0x00, 0x50}; // IPv4: 127.0.0.1:80
    endpoint ep(min_data);
    
    BOOST_CHECK_EQUAL(ep.size(), 7);
    BOOST_CHECK_EQUAL(ep.port(), 80);
}

BOOST_AUTO_TEST_CASE(ipv6_address_extraction)
{
    // IPv6 address test: ::1 (localhost)
    std::vector<uint8_t> ipv6_data = {
        6, // IPv6 type
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x1F, 0x90 // Port 8080
    };
    endpoint ep_ipv6(ipv6_data);
    
    boost::asio::ip::address addr = ep_ipv6.address();
    BOOST_CHECK(addr.is_v6());
    BOOST_CHECK_EQUAL(addr.to_string(), "::1");
    BOOST_CHECK_EQUAL(ep_ipv6.port(), 8080);
}

BOOST_AUTO_TEST_CASE(ipv6_full_address)
{
    // Full IPv6 address test: 2001:db8::1
    std::vector<uint8_t> ipv6_data = {
        6, // IPv6 type
        0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x04, 0xD2 // Port 1234
    };
    endpoint ep_ipv6(ipv6_data);
    
    boost::asio::ip::address addr = ep_ipv6.address();
    BOOST_CHECK(addr.is_v6());
    BOOST_CHECK_EQUAL(addr.to_string(), "2001:db8::1");
    BOOST_CHECK_EQUAL(ep_ipv6.port(), 1234);
}

BOOST_AUTO_TEST_CASE(ipv6_size)
{
    std::vector<uint8_t> ipv6_data = {
        6, // IPv6 type
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x1F, 0x90
    };
    endpoint ep_ipv6(ipv6_data);
    
    BOOST_CHECK_EQUAL(ep_ipv6.size(), 19); // 1 byte type + 16 bytes IPv6 + 2 bytes port
}
