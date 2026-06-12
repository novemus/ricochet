/*
 * Copyright (c) 2026 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include "client.h"
#include "agent.h"

namespace ricochet {

class relay_agent : public agent
{
    client m_client;

    template<class result> result perform_request(boost::asio::yield_context yield, ricochet::query req)
    {
        ricochet::reply res(4096);

        m_client.write_query(yield, req);
        m_client.read_reply(yield, res);

        if (res.type() == reply::kind::mistake)
        {
            m_client.shutdown(yield);

            auto error = std::get<ricochet::failure>(res.payload());
            switch(error)
            {
                case ricochet::failure::server_error:
                    throw server_error("server error");
                case ricochet::failure::malformed_message:
                    throw malformed_message("malformed message");
                case ricochet::failure::unavailable_proto:
                    throw unavailable_proto("unavailable proto");
                case ricochet::failure::limit_reached:
                    throw limit_reached("limit reached");
                default:
                    throw server_error("unknown error");
            }
        }

        return std::get<result>(res.payload());
    }

public:

    relay_agent(const boost::asio::ip::tcp::endpoint& server,
                const std::filesystem::path& cert,
                const std::filesystem::path& key,
                const std::filesystem::path& ca) : m_client(server, cert, key, ca)
    {
    }

    void assign_relay(boost::asio::yield_context yield, protocol proto, endpoint& relay) override
    {
        m_client.connect(yield);
        relay = perform_request<endpoint>(yield, query::make_provide_query(proto));
    }

    void launch_relay(boost::asio::yield_context yield, const peer& red, const peer& blue) override
    {
        perform_request<bool>(yield, query::make_connect_query(red, blue));
        m_client.shutdown(yield);
    }
};

std::shared_ptr<agent> create_agent(const boost::asio::ip::tcp::endpoint& server,
                                    const std::filesystem::path& cert,
                                    const std::filesystem::path& key,
                                    const std::filesystem::path& ca)
{
    return std::make_shared<relay_agent>(server, cert, key, ca);
}

} // namespace ricochet
