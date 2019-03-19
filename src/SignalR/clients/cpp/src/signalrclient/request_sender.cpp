// Copyright (c) .NET Foundation. All rights reserved.
// Licensed under the Apache License, Version 2.0. See License.txt in the project root for license information.

#include "stdafx.h"
#include "request_sender.h"
#include "http_sender.h"
#include "url_builder.h"
#include "signalrclient/signalr_exception.h"

namespace signalr
{
    namespace request_sender
    {
        pplx::task<negotiation_response> negotiate(web_request_factory& request_factory, const std::string& base_url,
            const signalr_client_config& signalr_client_config)
        {
            auto negotiate_url = url_builder::build_negotiate(base_url);

            return http_sender::post(request_factory, negotiate_url, signalr_client_config)
                .then([](std::string body)
            {
                auto negotiation_response_json = web::json::value::parse(utility::conversions::to_string_t(body));

                negotiation_response response;

                if (negotiation_response_json.has_field(_XPLATSTR("error")))
                {
                    response.error = utility::conversions::to_utf8string(negotiation_response_json[_XPLATSTR("error")].as_string());
                    return std::move(response);
                }

                if (negotiation_response_json.has_field(_XPLATSTR("connectionId")))
                {
                    response.connectionId = utility::conversions::to_utf8string(negotiation_response_json[_XPLATSTR("connectionId")].as_string());
                }

                if (negotiation_response_json.has_field(_XPLATSTR("availableTransports")))
                {
                    for (auto transportData : negotiation_response_json[_XPLATSTR("availableTransports")].as_array())
                    {
                        available_transport transport;
                        transport.transport = utility::conversions::to_utf8string(transportData[_XPLATSTR("transport")].as_string());

                        for (auto format : transportData[_XPLATSTR("transferFormats")].as_array())
                        {
                            transport.transfer_formats.push_back(utility::conversions::to_utf8string(format.as_string()));
                        }

                        response.availableTransports.push_back(transport);
                    }
                }

                if (negotiation_response_json.has_field(_XPLATSTR("url")))
                {
                    response.url = utility::conversions::to_utf8string(negotiation_response_json[_XPLATSTR("url")].as_string());

                    if (negotiation_response_json.has_field(_XPLATSTR("accessToken")))
                    {
                        response.accessToken = utility::conversions::to_utf8string(negotiation_response_json[_XPLATSTR("accessToken")].as_string());
                    }
                }

                if (negotiation_response_json.has_field(_XPLATSTR("ProtocolVersion")))
                {
                    throw signalr_exception("Detected a connection attempt to an ASP.NET SignalR Server. This client only supports connecting to an ASP.NET Core SignalR Server. See https://aka.ms/signalr-core-differences for details.");
                }

                return std::move(response);
            });
        }
    }
}
