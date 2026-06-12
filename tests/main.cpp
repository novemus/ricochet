/*
 * Copyright (c) 2026 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

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
