// Copyright (c) .NET Foundation. All rights reserved.
// Licensed under the Apache License, Version 2.0. See License.txt in the project root for license information.

#include "stdafx.h"
#include "default_websocket_client.h"

namespace signalr
{
    namespace
    {
        static web::websockets::client::websocket_client_config create_client_config(const signalr_client_config& signalr_client_config) noexcept
        {
            auto websocket_client_config = signalr_client_config.get_websocket_client_config();
            websocket_client_config.headers() = signalr_client_config.get_http_headers();

            return websocket_client_config;
        }
    }

    default_websocket_client::default_websocket_client(const signalr_client_config& signalr_client_config) noexcept
        : m_underlying_client(create_client_config(signalr_client_config))
    { }

    pplx::task<void> default_websocket_client::connect(const std::string& url)
    {
        return m_underlying_client.connect(utility::conversions::to_string_t(url));
    }

    pplx::task<void> default_websocket_client::send(const std::string &message)
    {
        web::websockets::client::websocket_outgoing_message msg;
        msg.set_utf8_message(message);
        return m_underlying_client.send(msg);
    }

    pplx::task<std::string> default_websocket_client::receive()
    {
        // the caller is responsible for observing exceptions
        return m_underlying_client.receive()
            .then([](web::websockets::client::websocket_incoming_message msg)
            {
                return msg.extract_string();
            });
    }

    pplx::task<void> default_websocket_client::close()
    {
        return m_underlying_client.close();
    }
}
