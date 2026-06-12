/*
 * Copyright (c) 2026 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#pragma once

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <filesystem>
#include "export.h"
#include "proto.h"

namespace ricochet {

struct agent
{
    virtual ~agent() {}
    virtual void assign_relay(boost::asio::yield_context yield, protocol proto, endpoint& relay) = 0;
    virtual void launch_relay(boost::asio::yield_context yield, const peer& red, const peer& blue) = 0;
};

LIBRICOCHET_EXPORT
std::shared_ptr<agent> create_agent(const boost::asio::ip::tcp::endpoint& server,
                                    const std::filesystem::path& cert,
                                    const std::filesystem::path& key,
                                    const std::filesystem::path& ca);
} // namespace ricochet
