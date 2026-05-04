#include <boost/test/unit_test.hpp>
#include <ricochet.h>
#include <vector>

using namespace ricochet;

BOOST_AUTO_TEST_CASE(default_constructor)
{
    buffer buf;
    BOOST_CHECK_EQUAL(buf.size(), 0);
    // For empty buffer, data() may return nullptr, which is valid
}

BOOST_AUTO_TEST_CASE(size_constructor)
{
    buffer buf(42);
    BOOST_CHECK_EQUAL(buf.size(), 42);
    BOOST_CHECK(buf.data() != nullptr);
}

BOOST_AUTO_TEST_CASE(vector_constructor)
{
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    buffer buf(data);
    BOOST_CHECK_EQUAL(buf.size(), 5);
    BOOST_CHECK(buf.data() != nullptr);
    
    for (size_t i = 0; i < data.size(); ++i)
    {
        BOOST_CHECK_EQUAL(buf.data()[i], data[i]);
    }
}

BOOST_AUTO_TEST_CASE(resize)
{
    buffer buf;
    BOOST_CHECK_EQUAL(buf.size(), 0);
    
    buf.resize(10);
    BOOST_CHECK_EQUAL(buf.size(), 10);
    BOOST_CHECK(buf.data() != nullptr);
    
    buf.resize(5);
    BOOST_CHECK_EQUAL(buf.size(), 5);
    BOOST_CHECK(buf.data() != nullptr);
}

BOOST_AUTO_TEST_CASE(data_access)
{
    std::vector<uint8_t> data = {10, 20, 30};
    buffer buf(data);
    
    const uint8_t* const_data = buf.data();
    BOOST_CHECK(const_data != nullptr);
    BOOST_CHECK_EQUAL(const_data[0], 10);
    BOOST_CHECK_EQUAL(const_data[1], 20);
    BOOST_CHECK_EQUAL(const_data[2], 30);
    
    uint8_t* mutable_data = buf.data();
    BOOST_CHECK(mutable_data != nullptr);
    mutable_data[0] = 99;
    BOOST_CHECK_EQUAL(buf.data()[0], 99);
}

BOOST_AUTO_TEST_CASE(const_data_access)
{
    const std::vector<uint8_t> data = {10, 20, 30};
    const buffer buf(data);
    
    const uint8_t* const_data = buf.data();
    BOOST_CHECK(const_data != nullptr);
    BOOST_CHECK_EQUAL(const_data[0], 10);
    BOOST_CHECK_EQUAL(const_data[1], 20);
    BOOST_CHECK_EQUAL(const_data[2], 30);
}

BOOST_AUTO_TEST_CASE(empty_buffer)
{
    buffer buf;
    BOOST_CHECK_EQUAL(buf.size(), 0);
    // For empty buffer, data() may return nullptr, which is valid
}
