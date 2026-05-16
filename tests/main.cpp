#define BOOST_TEST_MODULE ricochet_tests
#include <boost/test/unit_test.hpp>
#include "logging.h"

struct LoggingFixture 
{
    LoggingFixture()
    {
        ricochet::logging::init_console_logging(boost::log::trivial::info);
    }

    ~LoggingFixture()
    {
        ricochet::logging::cleanup();
    }
};

BOOST_TEST_GLOBAL_FIXTURE(LoggingFixture);
