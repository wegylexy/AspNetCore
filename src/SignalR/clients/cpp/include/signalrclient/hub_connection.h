// Copyright (c) .NET Foundation. All rights reserved.
// Licensed under the Apache License, Version 2.0. See License.txt in the project root for license information.

#pragma once

#include "_exports.h"
#include <memory>
#include <functional>
#include "pplx/pplxtasks.h"
#include "cpprest/json.h"
#include "connection_state.h"
#include "trace_level.h"
#include "log_writer.h"
#include "signalr_client_config.h"

namespace signalr
{
    class hub_connection_impl;

    class hub_connection
    {
    public:
        typedef std::function<void __cdecl (const web::json::value&)> method_invoked_handler;

        SIGNALRCLIENT_API explicit hub_connection(const std::string& url, trace_level trace_level = trace_level::all,
            std::shared_ptr<log_writer> log_writer = nullptr);

        SIGNALRCLIENT_API ~hub_connection();

        hub_connection(const hub_connection&) = delete;

        hub_connection& operator=(const hub_connection&) = delete;

        SIGNALRCLIENT_API pplx::task<void> __cdecl start();
        SIGNALRCLIENT_API pplx::task<void> __cdecl stop();

        SIGNALRCLIENT_API connection_state __cdecl get_connection_state() const;
        SIGNALRCLIENT_API std::string __cdecl get_connection_id() const;

        SIGNALRCLIENT_API void __cdecl set_disconnected(const std::function<void __cdecl()>& disconnected_callback);

        SIGNALRCLIENT_API void __cdecl set_client_config(const signalr_client_config& config);

        SIGNALRCLIENT_API void __cdecl on(const std::string& event_name, const method_invoked_handler& handler);

        SIGNALRCLIENT_API pplx::task<web::json::value> invoke(const std::string& method_name, const web::json::value& arguments = web::json::value::array());

        SIGNALRCLIENT_API pplx::task<void> send(const std::string& method_name, const web::json::value& arguments = web::json::value::array());

    private:
        std::shared_ptr<hub_connection_impl> m_pImpl;
    };
}
