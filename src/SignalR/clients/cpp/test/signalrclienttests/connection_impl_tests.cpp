// Copyright (c) .NET Foundation. All rights reserved.
// Licensed under the Apache License, Version 2.0. See License.txt in the project root for license information.

#include "stdafx.h"
#include "test_utils.h"
#include "test_web_request_factory.h"
#include "test_websocket_client.h"
#include "test_transport_factory.h"
#include "connection_impl.h"
#include "signalrclient/trace_level.h"
#include "trace_log_writer.h"
#include "memory_log_writer.h"
#include "cpprest/ws_client.h"
#include "signalrclient/signalr_exception.h"
#include "signalrclient/web_exception.h"

using namespace signalr;

static std::shared_ptr<connection_impl> create_connection(std::shared_ptr<websocket_client> websocket_client = create_test_websocket_client(),
    std::shared_ptr<log_writer> log_writer = std::make_shared<trace_log_writer>(), trace_level trace_level = trace_level::all)
{
    return connection_impl::create(create_uri(), trace_level, log_writer, create_test_web_request_factory(),
        std::make_unique<test_transport_factory>(websocket_client));
}

TEST(connection_impl_connection_state, initial_connection_state_is_disconnected)
{
    auto connection =
        connection_impl::create(create_uri(), trace_level::none, std::make_shared<trace_log_writer>());

    ASSERT_EQ(connection_state::disconnected, connection->get_connection_state());
}

TEST(connection_impl_start, cannot_start_non_disconnected_exception)
{
    auto websocket_client = create_test_websocket_client(
        /* receive function */ []() { return pplx::task_from_result(std::string("{ }\x1e")); });
    auto connection = create_connection(websocket_client);

    connection->start().wait();

    try
    {
        connection->start().get();
        ASSERT_TRUE(false); // exception not thrown
    }
    catch (const signalr_exception& e)
    {
        ASSERT_STREQ("cannot start a connection that is not in the disconnected state", e.what());
    }
}

TEST(connection_impl_start, connection_state_is_connecting_when_connection_is_being_started)
{
    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());

    auto websocket_client = create_test_websocket_client(
        /* receive function */ []() { return pplx::task_from_exception<std::string>(std::runtime_error("should not be invoked")); },
        /* send function */ [](const std::string){ return pplx::task_from_exception<void>(std::runtime_error("should not be invoked"));  },
        /* connect function */[](const std::string&)
        {
            return pplx::task_from_exception<void>(web::websockets::client::websocket_exception(_XPLATSTR("connecting failed")));
        });

    auto connection = create_connection(websocket_client, writer, trace_level::errors);

    auto startTask = connection->start()
        // this test is not set up to connect successfully so we have to observe exceptions otherwise
        // other tests may fail due to an unobserved exception from the outstanding start task
        .then([](pplx::task<void> start_task)
        {
            try
            {
                start_task.get();
            }
            catch (...)
            { }
        });

    ASSERT_EQ(connection->get_connection_state(), connection_state::connecting);
    startTask.get();
}

TEST(connection_impl_start, connection_state_is_connected_when_connection_established_succesfully)
{
    auto websocket_client = create_test_websocket_client(
        /* receive function */ []() { return pplx::task_from_result(std::string("{ }\x1e")); });
    auto connection = create_connection(websocket_client);
    connection->start().get();
    ASSERT_EQ(connection->get_connection_state(), connection_state::connected);
}

TEST(connection_impl_start, connection_state_is_disconnected_when_connection_cannot_be_established)
{
    auto web_request_factory = std::make_unique<test_web_request_factory>([](const std::string&) -> std::unique_ptr<web_request>
    {
        return std::unique_ptr<web_request>(new web_request_stub((unsigned short)404, "Bad request", ""));
    });

    auto connection =
        connection_impl::create(create_uri(), trace_level::none, std::make_shared<trace_log_writer>(),
        std::move(web_request_factory), std::make_unique<transport_factory>());

    try
    {
        connection->start().get();
    }
    catch (...)
    { }

    ASSERT_EQ(connection->get_connection_state(), connection_state::disconnected);
}

TEST(connection_impl_start, throws_for_invalid_uri)
{
    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());

    auto websocket_client = create_test_websocket_client(
        /* receive function */ []() { return pplx::task_from_result(std::string("{ }\x1e")); });

    auto connection = connection_impl::create(":1\t ä bad_uri&a=b", trace_level::errors, writer, create_test_web_request_factory(), std::make_unique<test_transport_factory>(websocket_client));

    try
    {
        connection->start().get();
        ASSERT_TRUE(false);
    }
    catch (const std::exception&)
    {
        // We shouldn't check the exact exception as it would be specific to the http library being used
    }

    ASSERT_EQ(connection->get_connection_state(), connection_state::disconnected);
}

TEST(connection_impl_start, start_sets_id_query_string)
{
    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());
    std::string query_string;

    auto websocket_client = create_test_websocket_client(
        /* receive function */ []() { return pplx::task_from_exception<std::string>(std::runtime_error("should not be invoked")); },
        /* send function */ [](const std::string&) { return pplx::task_from_exception<void>(std::runtime_error("should not be invoked"));  },
        /* connect function */[&query_string](const std::string& url)
    {
        query_string = utility::conversions::to_utf8string(url.substr(url.find('?') + 1));
        return pplx::task_from_exception<void>(web::websockets::client::websocket_exception("connecting failed"));
    });

    auto connection = connection_impl::create(create_uri(""), trace_level::errors, writer, create_test_web_request_factory(), std::make_unique<test_transport_factory>(websocket_client));

    try
    {
        connection->start().get();
    }
    catch (...)
    {
    }

    ASSERT_EQ("id=f7707523-307d-4cba-9abf-3eef701241e8", query_string);
}

TEST(connection_impl_start, start_appends_id_query_string)
{
    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());
    std::string query_string;

    auto websocket_client = create_test_websocket_client(
        /* receive function */ []() { return pplx::task_from_exception<std::string>(std::runtime_error("should not be invoked")); },
        /* send function */ [](const std::string) { return pplx::task_from_exception<void>(std::runtime_error("should not be invoked"));  },
        /* connect function */[&query_string](const std::string& url)
    {
        query_string = utility::conversions::to_utf8string(url.substr(url.find('?') + 1));
        return pplx::task_from_exception<void>(web::websockets::client::websocket_exception(_XPLATSTR("connecting failed")));
    });

    auto connection = connection_impl::create(create_uri("a=b&c=d"), trace_level::errors, writer, create_test_web_request_factory(), std::make_unique<test_transport_factory>(websocket_client));

    try
    {
        connection->start().get();
    }
    catch (...)
    {
    }

    ASSERT_EQ("a=b&c=d&id=f7707523-307d-4cba-9abf-3eef701241e8", query_string);
}

TEST(connection_impl_start, start_logs_exceptions)
{
    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());

    auto web_request_factory = std::make_unique<test_web_request_factory>([](const std::string&) -> std::unique_ptr<web_request>
    {
        return std::unique_ptr<web_request>(new web_request_stub((unsigned short)404, "Bad request", ""));
    });

    auto connection =
        connection_impl::create(create_uri(), trace_level::errors, writer,
            std::move(web_request_factory), std::make_unique<transport_factory>());

    try
    {
        connection->start().get();
    }
    catch (...)
    { }

    auto log_entries = std::dynamic_pointer_cast<memory_log_writer>(writer)->get_log_entries();
    ASSERT_FALSE(log_entries.empty());

    auto entry = remove_date_from_log_entry(log_entries[0]);
    ASSERT_EQ("[error       ] connection could not be started due to: web exception - 404 Bad request\n", entry);
}


TEST(connection_impl_start, start_propagates_exceptions_from_negotiate)
{
    auto web_request_factory = std::make_unique<test_web_request_factory>([](const std::string&) -> std::unique_ptr<web_request>
    {
        return std::unique_ptr<web_request>(new web_request_stub((unsigned short)404, "Bad request", ""));
    });

    auto connection =
        connection_impl::create(create_uri(), trace_level::none, std::make_shared<trace_log_writer>(),
        std::move(web_request_factory), std::make_unique<transport_factory>());

    try
    {
        connection->start().get();
        ASSERT_TRUE(false); // exception not thrown
    }
    catch (const std::exception &e)
    {
        ASSERT_EQ(_XPLATSTR("web exception - 404 Bad request"), utility::conversions::to_string_t(e.what()));
    }
}

TEST(connection_impl_start, start_fails_if_transport_connect_throws)
{
    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());

    auto websocket_client = create_test_websocket_client(
        /* receive function */ []() { return pplx::task_from_exception<std::string>(std::runtime_error("should not be invoked")); },
        /* send function */ [](const std::string){ return pplx::task_from_exception<void>(std::runtime_error("should not be invoked"));  },
        /* connect function */[](const std::string&)
        {
            return pplx::task_from_exception<void>(web::websockets::client::websocket_exception(_XPLATSTR("connecting failed")));
        });

    auto connection = create_connection(websocket_client, writer, trace_level::errors);

    try
    {
        connection->start().get();
        ASSERT_TRUE(false); // exception not thrown
    }
    catch (const std::exception &e)
    {
        ASSERT_EQ(_XPLATSTR("connecting failed"), utility::conversions::to_string_t(e.what()));
    }

    auto log_entries = std::dynamic_pointer_cast<memory_log_writer>(writer)->get_log_entries();
    ASSERT_TRUE(log_entries.size() > 1);

    auto entry = remove_date_from_log_entry(log_entries[1]);
    ASSERT_EQ("[error       ] transport could not connect due to: connecting failed\n", entry);
}

#if defined(_WIN32)   //  https://github.com/aspnet/SignalR-Client-Cpp/issues/131

TEST(connection_impl_send, send_fails_if_transport_fails_when_receiving_messages)
{
    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());

    auto websocket_client = create_test_websocket_client([]() { return pplx::task_from_result(std::string("")); },
        /* send function */ [](const std::string &)
        {
            return pplx::task_from_exception<void>(std::runtime_error("send error"));
        });

    auto connection = create_connection(websocket_client, writer, trace_level::errors);

    connection->start().get();

    try
    {
        connection->send("message").get();
        ASSERT_TRUE(false); // exception not thrown
    }
    catch (const std::exception &e)
    {
        ASSERT_STREQ("send error", e.what());
    }

    auto log_entries = std::dynamic_pointer_cast<memory_log_writer>(writer)->get_log_entries();
    ASSERT_TRUE(log_entries.size() == 1) << dump_vector(log_entries);

    auto entry = remove_date_from_log_entry(log_entries[0]);
    ASSERT_EQ("[error       ] error sending data: send error\n", entry) << dump_vector(log_entries);
}

#endif

TEST(connection_impl_start, start_fails_if_negotiate_request_fails)
{
    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());

    auto web_request_factory = std::make_unique<test_web_request_factory>([](const std::string&)
    {
        return std::unique_ptr<web_request>(new web_request_stub((unsigned short)400, "Bad Request"));
    });

    auto websocket_client = std::make_shared<test_websocket_client>();
    websocket_client->set_receive_function([]()->pplx::task<std::string>
    {
        return pplx::task_from_result(std::string("{ }\x1e"));
    });

    auto connection =
        connection_impl::create(create_uri(), trace_level::messages, writer,
        std::move(web_request_factory), std::make_unique<test_transport_factory>(websocket_client));

    try
    {
        connection->start().get();
        ASSERT_TRUE(false); // exception not thrown
    }
    catch (const web_exception &e)
    {
        ASSERT_STREQ("web exception - 400 Bad Request", e.what());
    }
}

TEST(connection_impl_start, start_fails_if_negotiate_response_has_error)
{
    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());

    auto web_request_factory = std::make_unique<test_web_request_factory>([](const std::string& url)
    {
        auto response_body =
            url.find("/negotiate") != std::string::npos
            ? "{ \"error\": \"bad negotiate\" }"
            : "";

        return std::unique_ptr<web_request>(new web_request_stub((unsigned short)200, "OK", response_body));
    });

    pplx::task_completion_event<void> tce;
    auto websocket_client = std::make_shared<test_websocket_client>();
    websocket_client->set_connect_function([tce](const std::string&) mutable
    {
        return pplx::task<void>(tce);
    });

    auto connection =
        connection_impl::create(create_uri(), trace_level::messages, writer,
            std::move(web_request_factory), std::make_unique<test_transport_factory>(websocket_client));

    try
    {
        connection->start().get();
        ASSERT_TRUE(false); // exception not thrown
    }
    catch (const signalr_exception & e)
    {
        ASSERT_STREQ("bad negotiate", e.what());
    }
}

TEST(connection_impl_start, start_fails_if_negotiate_response_does_not_have_websockets)
{
    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());

    auto web_request_factory = std::make_unique<test_web_request_factory>([](const std::string& url)
    {
        auto response_body =
            url.find("/negotiate") != std::string::npos
            ? "{ \"availableTransports\": [ { \"transport\": \"ServerSentEvents\", \"transferFormats\": [ \"Text\" ] } ] }"
            : "";

        return std::unique_ptr<web_request>(new web_request_stub((unsigned short)200, "OK", response_body));
    });

    pplx::task_completion_event<void> tce;
    auto websocket_client = std::make_shared<test_websocket_client>();
    websocket_client->set_connect_function([tce](const std::string&) mutable
    {
        return pplx::task<void>(tce);
    });

    auto connection =
        connection_impl::create(create_uri(), trace_level::messages, writer,
            std::move(web_request_factory), std::make_unique<test_transport_factory>(websocket_client));

    try
    {
        connection->start().get();
        ASSERT_TRUE(false); // exception not thrown
    }
    catch (const signalr_exception & e)
    {
        ASSERT_STREQ("The server does not support WebSockets which is currently the only transport supported by this client.", e.what());
    }
}

TEST(connection_impl_start, start_fails_if_negotiate_response_does_not_have_transports)
{
    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());

    auto web_request_factory = std::make_unique<test_web_request_factory>([](const std::string& url)
    {
        auto response_body =
            url.find("/negotiate") != std::string::npos
            ? "{ \"availableTransports\": [ ] }"
            : "";

        return std::unique_ptr<web_request>(new web_request_stub((unsigned short)200, "OK", response_body));
    });

    pplx::task_completion_event<void> tce;
    auto websocket_client = std::make_shared<test_websocket_client>();
    websocket_client->set_connect_function([tce](const std::string&) mutable
    {
        return pplx::task<void>(tce);
    });

    auto connection =
        connection_impl::create(create_uri(), trace_level::messages, writer,
            std::move(web_request_factory), std::make_unique<test_transport_factory>(websocket_client));

    try
    {
        connection->start().get();
        ASSERT_TRUE(false); // exception not thrown
    }
    catch (const signalr_exception & e)
    {
        ASSERT_STREQ("The server does not support WebSockets which is currently the only transport supported by this client.", e.what());
    }
}

TEST(connection_impl_start, start_fails_if_negotiate_response_is_invalid)
{
    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());

    auto web_request_factory = std::make_unique<test_web_request_factory>([](const std::string& url)
    {
        auto response_body =
            url.find("/negotiate") != std::string::npos
            ? "{ \"availableTransports\": [ "
            : "";

        return std::unique_ptr<web_request>(new web_request_stub((unsigned short)200, "OK", response_body));
    });

    pplx::task_completion_event<void> tce;
    auto websocket_client = std::make_shared<test_websocket_client>();
    websocket_client->set_connect_function([tce](const std::string&) mutable
    {
        return pplx::task<void>(tce);
    });

    auto connection =
        connection_impl::create(create_uri(), trace_level::messages, writer,
            std::move(web_request_factory), std::make_unique<test_transport_factory>(websocket_client));

    try
    {
        connection->start().get();
        ASSERT_TRUE(false); // exception not thrown
    }
    catch (const std::exception & e)
    {
        ASSERT_STREQ("* Line 1, Column 28 Syntax error: Malformed token", e.what());
    }
}

TEST(connection_impl_start, negotiate_follows_redirect)
{
    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());

    auto web_request_factory = std::make_unique<test_web_request_factory>([](const std::string& url)
    {
        std::string response_body = "";
        if (url.find("/negotiate") != std::string::npos)
        {
            if (url.find("redirected") != std::string::npos)
            {
                response_body = "{\"connectionId\" : \"f7707523-307d-4cba-9abf-3eef701241e8\", "
                    "\"availableTransports\" : [ { \"transport\": \"WebSockets\", \"transferFormats\": [ \"Text\", \"Binary\" ] } ] }";
            }
            else
            {
                response_body = "{ \"url\": \"http://redirected\" }";
            }
        }

        return std::unique_ptr<web_request>(new web_request_stub((unsigned short)200, "OK", response_body));
    });

    auto websocket_client = std::make_shared<test_websocket_client>();

    std::string connectUrl;
    websocket_client->set_connect_function([&connectUrl](const std::string& url)
    {
        connectUrl = url;
        return pplx::task_from_result();
    });

    auto connection =
        connection_impl::create(create_uri(), trace_level::messages, writer,
            std::move(web_request_factory), std::make_unique<test_transport_factory>(websocket_client));

    connection->start().get();

    ASSERT_EQ("ws://redirected/?id=f7707523-307d-4cba-9abf-3eef701241e8", connectUrl);
}

TEST(connection_impl_start, negotiate_redirect_uses_accessToken)
{
    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());
    std::string accessToken;

    auto web_request_factory = std::make_unique<test_web_request_factory>([&accessToken](const std::string& url)
    {
        std::string response_body = "";
        if (url.find("/negotiate") != std::string::npos)
        {
            if (url.find("redirected") != std::string::npos)
            {
                response_body = "{\"connectionId\" : \"f7707523-307d-4cba-9abf-3eef701241e8\", "
                    "\"availableTransports\" : [ { \"transport\": \"WebSockets\", \"transferFormats\": [ \"Text\", \"Binary\" ] } ] }";
            }
            else
            {
                response_body = "{ \"url\": \"http://redirected\", \"accessToken\": \"secret\" }";
            }
        }

        auto request = new web_request_stub((unsigned short)200, "OK", response_body);
        request->on_get_response = [&accessToken](web_request_stub& stub)
        {
            accessToken = utility::conversions::to_utf8string(stub.m_signalr_client_config.get_http_headers()[_XPLATSTR("Authorization")]);
        };
        return std::unique_ptr<web_request>(request);
    });

    auto websocket_client = std::make_shared<test_websocket_client>();

    std::string connectUrl;
    websocket_client->set_connect_function([&connectUrl](const std::string& url)
    {
        connectUrl = url;
        return pplx::task_from_result();
    });

    auto connection =
        connection_impl::create(create_uri(), trace_level::messages, writer,
            std::move(web_request_factory), std::make_unique<test_transport_factory>(websocket_client));

    connection->start().get();

    ASSERT_EQ("ws://redirected/?id=f7707523-307d-4cba-9abf-3eef701241e8", connectUrl);
    ASSERT_EQ("Bearer secret", accessToken);
}

TEST(connection_impl_start, negotiate_fails_after_too_many_redirects)
{
    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());

    auto web_request_factory = std::make_unique<test_web_request_factory>([](const std::string& url)
    {
        std::string response_body = "";
        if (url.find("/negotiate") != std::string::npos)
        {
            // infinite redirect
            response_body = "{ \"url\": \"http://redirected\" }";
        }

        return std::unique_ptr<web_request>(new web_request_stub((unsigned short)200, "OK", response_body));
    });

    auto websocket_client = std::make_shared<test_websocket_client>();

    auto connection =
        connection_impl::create(create_uri(), trace_level::messages, writer,
            std::move(web_request_factory), std::make_unique<test_transport_factory>(websocket_client));

    try
    {
        connection->start().get();
    }
    catch (signalr_exception e)
    {
        ASSERT_STREQ("Negotiate redirection limit exceeded.", e.what());
    }
}

TEST(connection_impl_start, negotiate_fails_if_ProtocolVersion_in_response)
{
    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());

    auto web_request_factory = std::make_unique<test_web_request_factory>([](const std::string& url)
    {
        std::string response_body = "";
        if (url.find("/negotiate") != std::string::npos)
        {
            response_body = "{\"ProtocolVersion\" : \"\" }";
        }

        return std::unique_ptr<web_request>(new web_request_stub((unsigned short)200, "OK", response_body));
    });

    auto websocket_client = std::make_shared<test_websocket_client>();

    auto connection =
        connection_impl::create(create_uri(), trace_level::messages, writer,
            std::move(web_request_factory), std::make_unique<test_transport_factory>(websocket_client));

    try
    {
        connection->start().get();
    }
    catch (signalr_exception e)
    {
        ASSERT_STREQ("Detected a connection attempt to an ASP.NET SignalR Server. This client only supports connecting to an ASP.NET Core SignalR Server. See https://aka.ms/signalr-core-differences for details.", e.what());
    }
}

TEST(connection_impl_start, negotiate_redirect_does_not_overwrite_url)
{
    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());
    int redirectCount = 0;

    auto web_request_factory = std::make_unique<test_web_request_factory>([&redirectCount](const std::string& url)
    {
        std::string response_body = "";
        if (url.find("/negotiate") != std::string::npos)
        {
            if (url.find("redirected") != std::string::npos)
            {
                response_body = "{\"connectionId\" : \"f7707523-307d-4cba-9abf-3eef701241e8\", "
                    "\"availableTransports\" : [ { \"transport\": \"WebSockets\", \"transferFormats\": [ \"Text\", \"Binary\" ] } ] }";
            }
            else
            {
                response_body = "{ \"url\": \"http://redirected\" }";
                redirectCount++;
            }
        }

        return std::unique_ptr<web_request>(new web_request_stub((unsigned short)200, "OK", response_body));
    });

    auto websocket_client = std::make_shared<test_websocket_client>();

    auto connection =
        connection_impl::create(create_uri(), trace_level::messages, writer,
            std::move(web_request_factory), std::make_unique<test_transport_factory>(websocket_client));

    connection->start().get();
    ASSERT_EQ(1, redirectCount);
    connection->stop().get();
    connection->start().get();
    ASSERT_EQ(2, redirectCount);
}

TEST(connection_impl_start, negotiate_redirect_uses_own_query_string)
{
    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());
    std::string query_string;

    auto websocket_client = create_test_websocket_client(
        /* receive function */ []() { return pplx::task_from_exception<std::string>(std::runtime_error("should not be invoked")); },
        /* send function */ [](const std::string) { return pplx::task_from_exception<void>(std::runtime_error("should not be invoked"));  },
        /* connect function */[&query_string](const std::string& url)
    {
        query_string = url.substr(url.find('?') + 1);
        return pplx::task_from_exception<void>(web::websockets::client::websocket_exception(_XPLATSTR("connecting failed")));
    });

    auto web_request_factory = std::make_unique<test_web_request_factory>([](const std::string& url)
    {
        std::string response_body = "";
        if (url.find("/negotiate") != std::string::npos)
        {
            if (url.find("redirected") != std::string::npos)
            {
                response_body = "{\"connectionId\" : \"f7707523-307d-4cba-9abf-3eef701241e8\", "
                    "\"availableTransports\" : [ { \"transport\": \"WebSockets\", \"transferFormats\": [ \"Text\", \"Binary\" ] } ] }";
            }
            else
            {
                response_body = "{ \"url\": \"http://redirected?customQuery=1\" }";
            }
        }

        return std::unique_ptr<web_request>(new web_request_stub((unsigned short)200, "OK", response_body));
    });

    auto connection = connection_impl::create(create_uri("a=b&c=d"), trace_level::errors, writer, std::move(web_request_factory), std::make_unique<test_transport_factory>(websocket_client));

    try
    {
        connection->start().get();
    }
    catch (...)
    {
    }

    ASSERT_EQ("customQuery=1&id=f7707523-307d-4cba-9abf-3eef701241e8", query_string);
}

TEST(connection_impl_start, start_fails_if_connect_request_times_out)
{
    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());

    auto web_request_factory = create_test_web_request_factory();

    pplx::task_completion_event<void> tce;
    auto websocket_client = std::make_shared<test_websocket_client>();
    websocket_client->set_connect_function([tce](const std::string&) mutable
    {
        return pplx::task<void>(tce);
    });

    auto connection =
        connection_impl::create(create_uri(), trace_level::messages, writer,
        std::move(web_request_factory), std::make_unique<test_transport_factory>(websocket_client));

    try
    {
        connection->start().get();
        ASSERT_TRUE(false); // exception not thrown
    }
    catch (const signalr_exception &e)
    {
        ASSERT_STREQ("transport timed out when trying to connect", e.what());
    }
}

TEST(connection_impl_process_response, process_response_logs_messages)
{
    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());
    auto wait_receive = std::make_shared<event>();
    auto websocket_client = create_test_websocket_client(
        /* receive function */ [wait_receive]()
        {
            wait_receive->set();
            return pplx::task_from_result(std::string("{ }"));
        });
    auto connection = create_connection(websocket_client, writer, trace_level::messages);

    connection->start().get();
    // Need to give the receive loop time to run
    std::make_shared<event>()->wait(1000);

    auto log_entries = std::dynamic_pointer_cast<memory_log_writer>(writer)->get_log_entries();
    ASSERT_FALSE(log_entries.empty());

    auto entry = remove_date_from_log_entry(log_entries[0]);
    ASSERT_EQ("[message     ] processing message: { }\n", entry);
}

TEST(connection_impl_send, message_sent)
{
    std::string actual_message;

    auto websocket_client = create_test_websocket_client(
        /* receive function */ []() { return pplx::task_from_result(std::string("{ }\x1e")); },
        /* send function */ [&actual_message](const std::string& message)
    {
        actual_message = message;
        return pplx::task_from_result();
    });

    auto connection = create_connection(websocket_client);

    const std::string message{ "Test message" };

    connection->start()
        .then([connection, message]()
        {
            return connection->send(message);
        }).get();

    ASSERT_EQ(message, actual_message);
}

TEST(connection_impl_send, send_throws_if_connection_not_connected)
{
    auto connection =
        connection_impl::create(create_uri(), trace_level::none, std::make_shared<trace_log_writer>());

    try
    {
        connection->send("whatever").get();
        ASSERT_TRUE(false); // exception expected but not thrown
    }
    catch (const signalr_exception &e)
    {
        ASSERT_STREQ("cannot send data when the connection is not in the connected state. current connection state: disconnected", e.what());
    }
}

TEST(connection_impl_send, exceptions_from_send_logged_and_propagated)
{
    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());
    auto websocket_client = create_test_websocket_client(
        /* receive function */ []()
        {
            return pplx::task_from_result(std::string("{}"));
        },
        /* send function */ [](const std::string&)
        {
            return pplx::task_from_exception<void>(std::runtime_error("error"));
        });

    auto connection = create_connection(websocket_client, writer, trace_level::errors);

    try
    {
        connection->start()
            .then([connection]()
        {
            return connection->send("Test message");
        }).get();

        ASSERT_TRUE(false); // exception expected but not thrown
    }
    catch (const std::runtime_error &e)
    {
        ASSERT_STREQ("error", e.what());
    }

    auto log_entries = std::dynamic_pointer_cast<memory_log_writer>(writer)->get_log_entries();
    ASSERT_FALSE(log_entries.empty());

    auto entry = remove_date_from_log_entry(log_entries[0]);
    ASSERT_EQ("[error       ] error sending data: error\n", entry);
}

TEST(connection_impl_set_message_received, callback_invoked_when_message_received)
{
    int call_number = -1;
    auto websocket_client = create_test_websocket_client(
        /* receive function */ [call_number]()
        mutable {
        std::string responses[]
        {
            "Test",
            "release",
            "{}"
        };

        call_number = std::min(call_number + 1, 2);

        return pplx::task_from_result(responses[call_number]);
    });

    auto connection = create_connection(websocket_client);

    auto message = std::make_shared<std::string>();

    auto message_received_event = std::make_shared<event>();
    connection->set_message_received([message, message_received_event](const std::string &m)
    {
        if (m == "Test")
        {
            *message = m;
        }

        if (m == "release")
        {
            message_received_event->set();
        }
    });

    connection->start().get();

    ASSERT_FALSE(message_received_event->wait(5000));

    ASSERT_EQ("Test", *message);
}

TEST(connection_impl_set_message_received, exception_from_callback_caught_and_logged)
{
    int call_number = -1;
    auto websocket_client = create_test_websocket_client(
        /* receive function */ [call_number]()
        mutable {
        std::string responses[]
        {
            "throw",
            "release",
            "{}"
        };

        call_number = std::min(call_number + 1, 2);

        return pplx::task_from_result(responses[call_number]);
    });

    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());
    auto connection = create_connection(websocket_client, writer, trace_level::errors);

    auto message_received_event = std::make_shared<event>();
    connection->set_message_received([message_received_event](const std::string &m)
    {
        if (m == "throw")
        {
            throw std::runtime_error("oops");
        }

        if (m == "release")
        {
            message_received_event->set();
        }
    });

    connection->start().get();

    ASSERT_FALSE(message_received_event->wait(5000));

    auto log_entries = std::dynamic_pointer_cast<memory_log_writer>(writer)->get_log_entries();
    ASSERT_FALSE(log_entries.empty());

    auto entry = remove_date_from_log_entry(log_entries[0]);
    ASSERT_EQ("[error       ] message_received callback threw an exception: oops\n", entry);
}

TEST(connection_impl_set_message_received, non_std_exception_from_callback_caught_and_logged)
{
    int call_number = -1;
    auto websocket_client = create_test_websocket_client(
        /* receive function */ [call_number]()
        mutable {
        std::string responses[]
        {
            "throw",
            "release",
            "{}"
        };

        call_number = std::min(call_number + 1, 2);

        return pplx::task_from_result(responses[call_number]);
    });

    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());
    auto connection = create_connection(websocket_client, writer, trace_level::errors);

    auto message_received_event = std::make_shared<event>();
    connection->set_message_received([message_received_event](const std::string &m)
    {
        if (m == "throw")
        {
            throw 42;
        }

        if (m == "release")
        {
            message_received_event->set();
        }
    });

    connection->start().get();

    ASSERT_FALSE(message_received_event->wait(5000));

    auto log_entries = std::dynamic_pointer_cast<memory_log_writer>(writer)->get_log_entries();
    ASSERT_FALSE(log_entries.empty());

    auto entry = remove_date_from_log_entry(log_entries[0]);
    ASSERT_EQ("[error       ] message_received callback threw an unknown exception\n", entry);
}

void can_be_set_only_in_disconnected_state(std::function<void(connection_impl *)> callback, const char* expected_exception_message)
{
    auto websocket_client = create_test_websocket_client(
        /* receive function */ []() { return pplx::task_from_result(std::string("{ }\x1e")); });
    auto connection = create_connection(websocket_client);

    connection->start().get();

    try
    {
        callback(connection.get());
        ASSERT_TRUE(false); // exception expected but not thrown
    }
    catch (const signalr_exception &e)
    {
        ASSERT_STREQ(expected_exception_message, e.what());
    }
}

TEST(connection_impl_set_configuration, set_message_received_callback_can_be_set_only_in_disconnected_state)
{
    can_be_set_only_in_disconnected_state(
        [](connection_impl* connection) { connection->set_message_received([](const std::string&){}); },
        "cannot set the callback when the connection is not in the disconnected state. current connection state: connected");
}

TEST(connection_impl_set_configuration, set_disconnected_callback_can_be_set_only_in_disconnected_state)
{
    can_be_set_only_in_disconnected_state(
        [](connection_impl* connection) { connection->set_disconnected([](){}); },
        "cannot set the disconnected callback when the connection is not in the disconnected state. current connection state: connected");
}

TEST(connection_impl_stop, stopping_disconnected_connection_is_no_op)
{
    std::shared_ptr<log_writer> writer{ std::make_shared<memory_log_writer>() };
    auto connection = connection_impl::create(create_uri(), trace_level::all, writer);
    connection->stop().get();

    ASSERT_EQ(connection_state::disconnected, connection->get_connection_state());

    auto log_entries = std::dynamic_pointer_cast<memory_log_writer>(writer)->get_log_entries();
    ASSERT_EQ(2U, log_entries.size());
    ASSERT_EQ("[info        ] stopping connection\n", remove_date_from_log_entry(log_entries[0]));
    ASSERT_EQ("[info        ] acquired lock in shutdown()\n", remove_date_from_log_entry(log_entries[1]));
}

TEST(connection_impl_stop, stopping_disconnecting_connection_returns_cancelled_task)
{
    event close_event;
    auto writer = std::shared_ptr<log_writer>{std::make_shared<memory_log_writer>()};

    auto websocket_client = create_test_websocket_client(
        /* receive function */ []() { return pplx::task_from_result(std::string("{ }\x1e")); },
        /* send function */ [](const std::string){ return pplx::task_from_exception<void>(std::runtime_error("should not be invoked")); },
        /* connect function */ [&close_event](const std::string&) { return pplx::task_from_result(); },
        /* close function */ [&close_event]()
        {
            return pplx::create_task([&close_event]()
            {
                close_event.wait();
            });
        });

    auto connection = create_connection(websocket_client, writer, trace_level::state_changes);

    connection->start().get();
    auto stop_task = connection->stop();

    try
    {
        connection->stop().get();
        ASSERT_FALSE(true); // exception expected but not thrown
    }
    catch (const pplx::task_canceled&)
    { }

    close_event.set();
    stop_task.get();

    ASSERT_EQ(connection_state::disconnected, connection->get_connection_state());

    auto log_entries = std::dynamic_pointer_cast<memory_log_writer>(writer)->get_log_entries();
    ASSERT_EQ(4U, log_entries.size());
    ASSERT_EQ("[state change] disconnected -> connecting\n", remove_date_from_log_entry(log_entries[0]));
    ASSERT_EQ("[state change] connecting -> connected\n", remove_date_from_log_entry(log_entries[1]));
    ASSERT_EQ("[state change] connected -> disconnecting\n", remove_date_from_log_entry(log_entries[2]));
    ASSERT_EQ("[state change] disconnecting -> disconnected\n", remove_date_from_log_entry(log_entries[3]));
}

TEST(connection_impl_stop, can_start_and_stop_connection)
{
    auto writer = std::shared_ptr<log_writer>{std::make_shared<memory_log_writer>()};

    auto websocket_client = create_test_websocket_client(
        /* receive function */ []() { return pplx::task_from_result(std::string("{ }\x1e")); });
    auto connection = create_connection(websocket_client, writer, trace_level::state_changes);

    connection->start()
        .then([connection]()
        {
            return connection->stop();
        }).get();

    auto log_entries = std::dynamic_pointer_cast<memory_log_writer>(writer)->get_log_entries();
    ASSERT_EQ(4U, log_entries.size());
    ASSERT_EQ("[state change] disconnected -> connecting\n", remove_date_from_log_entry(log_entries[0]));
    ASSERT_EQ("[state change] connecting -> connected\n", remove_date_from_log_entry(log_entries[1]));
    ASSERT_EQ("[state change] connected -> disconnecting\n", remove_date_from_log_entry(log_entries[2]));
    ASSERT_EQ("[state change] disconnecting -> disconnected\n", remove_date_from_log_entry(log_entries[3]));
}

TEST(connection_impl_stop, can_start_and_stop_connection_multiple_times)
{
    auto writer = std::shared_ptr<log_writer>{std::make_shared<memory_log_writer>()};

    {
        auto websocket_client = create_test_websocket_client(
            /* receive function */ []() { return pplx::task_from_result(std::string("{ }\x1e")); });
        auto connection = create_connection(websocket_client, writer, trace_level::state_changes);

        connection->start()
            .then([connection]()
        {
            return connection->stop();
        })
        .then([connection]()
        {
            return connection->start();
        }).get();
    }

    auto memory_writer = std::dynamic_pointer_cast<memory_log_writer>(writer);

    // The connection_impl will be destroyed when the last reference to shared_ptr holding is released. This can happen
    // on a different thread in which case the dtor will be invoked on a different thread so we need to wait for this
    // to happen and if it does not the test will fail
    for (int wait_time_ms = 5; wait_time_ms < 100 && memory_writer->get_log_entries().size() < 8; wait_time_ms <<= 1)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(wait_time_ms));
    }

    auto log_entries = memory_writer->get_log_entries();
    ASSERT_EQ(8U, log_entries.size());
    ASSERT_EQ("[state change] disconnected -> connecting\n", remove_date_from_log_entry(log_entries[0]));
    ASSERT_EQ("[state change] connecting -> connected\n", remove_date_from_log_entry(log_entries[1]));
    ASSERT_EQ("[state change] connected -> disconnecting\n", remove_date_from_log_entry(log_entries[2]));
    ASSERT_EQ("[state change] disconnecting -> disconnected\n", remove_date_from_log_entry(log_entries[3]));
    ASSERT_EQ("[state change] disconnected -> connecting\n", remove_date_from_log_entry(log_entries[4]));
    ASSERT_EQ("[state change] connecting -> connected\n", remove_date_from_log_entry(log_entries[5]));
    ASSERT_EQ("[state change] connected -> disconnecting\n", remove_date_from_log_entry(log_entries[6]));
    ASSERT_EQ("[state change] disconnecting -> disconnected\n", remove_date_from_log_entry(log_entries[7]));
}

TEST(connection_impl_stop, dtor_stops_the_connection)
{
    auto writer = std::shared_ptr<log_writer>{std::make_shared<memory_log_writer>()};

    {
        auto websocket_client = create_test_websocket_client(
            /* receive function */ []() 
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                return pplx::task_from_result(std::string("{ }\x1e"));
            });
        auto connection = create_connection(websocket_client, writer, trace_level::state_changes);

        connection->start().get();
    }

    auto memory_writer = std::dynamic_pointer_cast<memory_log_writer>(writer);

    // The connection_impl will be destroyed when the last reference to shared_ptr holding is released. This can happen
    // on a different thread in which case the dtor will be invoked on a different thread so we need to wait for this
    // to happen and if it does not the test will fail
    for (int wait_time_ms = 5; wait_time_ms < 100 && memory_writer->get_log_entries().size() < 4; wait_time_ms <<= 1)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(wait_time_ms));
    }

    auto log_entries = memory_writer->get_log_entries();
    ASSERT_EQ(4U, log_entries.size());
    ASSERT_EQ("[state change] disconnected -> connecting\n", remove_date_from_log_entry(log_entries[0]));
    ASSERT_EQ("[state change] connecting -> connected\n", remove_date_from_log_entry(log_entries[1]));
    ASSERT_EQ("[state change] connected -> disconnecting\n", remove_date_from_log_entry(log_entries[2]));
    ASSERT_EQ("[state change] disconnecting -> disconnected\n", remove_date_from_log_entry(log_entries[3]));
}

TEST(connection_impl_stop, stop_cancels_ongoing_start_request)
{
    auto disconnect_completed_event = std::make_shared<event>();

    auto websocket_client = create_test_websocket_client(
        /* receive function */ [disconnect_completed_event]()
        {
            disconnect_completed_event->wait();
            return pplx::task_from_result(std::string("{ }\x1e"));
        });

    auto writer = std::shared_ptr<log_writer>{std::make_shared<memory_log_writer>()};
    auto connection = create_connection(std::make_shared<test_websocket_client>(), writer, trace_level::all);

    auto start_task = connection->start();
    connection->stop().get();
    disconnect_completed_event->set();

    start_task.then([](pplx::task<void> t)
    {
        try
        {
            t.get();
            ASSERT_TRUE(false); // exception expected but not thrown
        }
        catch (const pplx::task_canceled &)
        { }
    }).get();

    ASSERT_EQ(connection_state::disconnected, connection->get_connection_state());

    auto log_entries = std::dynamic_pointer_cast<memory_log_writer>(writer)->get_log_entries();
    ASSERT_EQ(5U, log_entries.size());
    ASSERT_EQ("[state change] disconnected -> connecting\n", remove_date_from_log_entry(log_entries[0]));
    ASSERT_EQ("[info        ] stopping connection\n", remove_date_from_log_entry(log_entries[1]));
    ASSERT_EQ("[info        ] acquired lock in shutdown()\n", remove_date_from_log_entry(log_entries[2]));
    ASSERT_EQ("[info        ] starting the connection has been canceled.\n", remove_date_from_log_entry(log_entries[3]));
    ASSERT_EQ("[state change] connecting -> disconnected\n", remove_date_from_log_entry(log_entries[4]));
}

TEST(connection_impl_stop, ongoing_start_request_canceled_if_connection_stopped_before_init_message_received)
{
    auto web_request_factory = std::make_unique<test_web_request_factory>([](const std::string& url)
    {
        auto response_body =
            url.find("/negotiate") != std::string::npos
            ? "{ \"connectionId\" : \"f7707523-307d-4cba-9abf-3eef701241e8\", "
              "\"availableTransports\" : [ { \"transport\": \"WebSockets\", \"transferFormats\": [ \"Text\", \"Binary\" ] } ] }"
            : "";

        return std::unique_ptr<web_request>(new web_request_stub((unsigned short)200, "OK", response_body));
    });

    auto websocket_client = create_test_websocket_client(/*receive function*/ []()
    {
        return pplx::task_from_result<std::string>("{}");
    });

    auto writer = std::shared_ptr<log_writer>{std::make_shared<memory_log_writer>()};
    auto connection = connection_impl::create(create_uri(), trace_level::all, writer,
        std::move(web_request_factory), std::make_unique<test_transport_factory>(websocket_client));

    auto start_task = connection->start();
    connection->stop().get();

    start_task.then([](pplx::task<void> t)
    {
        try
        {
            t.get();
            ASSERT_TRUE(false); // exception expected but not thrown
        }
        catch (const pplx::task_canceled &)
        { }
    }).get();

    auto log_entries = std::dynamic_pointer_cast<memory_log_writer>(writer)->get_log_entries();
    ASSERT_EQ(5U, log_entries.size()) << dump_vector(log_entries);
    ASSERT_EQ("[state change] disconnected -> connecting\n", remove_date_from_log_entry(log_entries[0]));
    ASSERT_EQ("[info        ] stopping connection\n", remove_date_from_log_entry(log_entries[1]));
    ASSERT_EQ("[info        ] acquired lock in shutdown()\n", remove_date_from_log_entry(log_entries[2]));
    ASSERT_EQ("[info        ] starting the connection has been canceled.\n", remove_date_from_log_entry(log_entries[3]));
    ASSERT_EQ("[state change] connecting -> disconnected\n", remove_date_from_log_entry(log_entries[4]));
}

TEST(connection_impl_stop, stop_invokes_disconnected_callback)
{
    auto websocket_client = create_test_websocket_client(
        /* receive function */ []() { return pplx::task_from_result(std::string("{ }\x1e")); });
    auto connection = create_connection(websocket_client);

    auto disconnected_invoked = false;
    connection->set_disconnected([&disconnected_invoked](){ disconnected_invoked = true; });

    connection->start()
        .then([connection]()
        {
            return connection->stop();
        }).get();

    ASSERT_TRUE(disconnected_invoked);
}

TEST(connection_impl_stop, std_exception_for_disconnected_callback_caught_and_logged)
{
    auto writer = std::shared_ptr<log_writer>{std::make_shared<memory_log_writer>()};

    int call_number = -1;
    auto websocket_client = create_test_websocket_client(
        /* receive function */ [call_number]()
        mutable {
            std::string responses[]
            {
                "{ }\x1e",
                "{}"
            };

            call_number = std::min(call_number + 1, 1);

            return pplx::task_from_result(responses[call_number]);
        });
    auto connection = create_connection(websocket_client, writer, trace_level::errors);

    connection->set_disconnected([](){ throw std::runtime_error("exception from disconnected"); });

    connection->start()
        .then([connection]()
        {
            return connection->stop();
        }).get();

    auto log_entries = std::dynamic_pointer_cast<memory_log_writer>(writer)->get_log_entries();
    ASSERT_EQ(1U, log_entries.size());
    ASSERT_EQ("[error       ] disconnected callback threw an exception: exception from disconnected\n", remove_date_from_log_entry(log_entries[0]));
}

TEST(connection_impl_stop, exception_for_disconnected_callback_caught_and_logged)
{
    auto writer = std::shared_ptr<log_writer>{std::make_shared<memory_log_writer>()};

    int call_number = -1;
    auto websocket_client = create_test_websocket_client(
        /* receive function */ [call_number]()
        mutable {
            std::string responses[]
            {
                "{ }\x1e",
                "{}"
            };

            call_number = std::min(call_number + 1, 1);

            return pplx::task_from_result(responses[call_number]);
        });
    auto connection = create_connection(websocket_client, writer, trace_level::errors);

    connection->set_disconnected([](){ throw 42; });

    connection->start()
        .then([connection]()
        {
            return connection->stop();
        }).get();

    auto log_entries = std::dynamic_pointer_cast<memory_log_writer>(writer)->get_log_entries();
    ASSERT_EQ(1U, log_entries.size());
    ASSERT_EQ("[error       ] disconnected callback threw an unknown exception\n", remove_date_from_log_entry(log_entries[0]));
}

TEST(connection_impl_config, custom_headers_set_in_requests)
{
    auto writer = std::shared_ptr<log_writer>{std::make_shared<memory_log_writer>()};

    auto web_request_factory = std::make_unique<test_web_request_factory>([](const std::string& url)
    {
            auto response_body =
            url.find("/negotiate") != std::string::npos
            ? "{\"connectionId\" : \"f7707523-307d-4cba-9abf-3eef701241e8\", "
            "\"availableTransports\" : [ { \"transport\": \"WebSockets\", \"transferFormats\": [ \"Text\", \"Binary\" ] } ] }"
            : "";

        auto request = new web_request_stub((unsigned short)200, "OK", response_body);
        request->on_get_response = [](web_request_stub& request)
        {
            auto http_headers = request.m_signalr_client_config.get_http_headers();
            ASSERT_EQ(1U, http_headers.size());
            ASSERT_EQ(_XPLATSTR("42"), http_headers[_XPLATSTR("Answer")]);
        };

        return std::unique_ptr<web_request>(request);
    });

    auto websocket_client = create_test_websocket_client(
        /* receive function */ []() { return pplx::task_from_result(std::string("{ }\x1e")); });

    auto connection =
        connection_impl::create(create_uri(), trace_level::state_changes,
        writer, std::move(web_request_factory), std::make_unique<test_transport_factory>(websocket_client));

    signalr::signalr_client_config signalr_client_config{};
    auto http_headers = signalr_client_config.get_http_headers();
    http_headers[_XPLATSTR("Answer")] = _XPLATSTR("42");
    signalr_client_config.set_http_headers(http_headers);
    connection->set_client_config(signalr_client_config);

    connection->start()
        .then([connection]()
    {
        return connection->stop();
    }).get();

    ASSERT_EQ(connection_state::disconnected, connection->get_connection_state());
}

TEST(connection_impl_set_config, config_can_be_set_only_in_disconnected_state)
{
    can_be_set_only_in_disconnected_state(
        [](connection_impl* connection) 
        {
            signalr::signalr_client_config signalr_client_config;
            connection->set_client_config(signalr_client_config);
        },"cannot set client config when the connection is not in the disconnected state. current connection state: connected");
}

TEST(connection_impl_change_state, change_state_logs)
{
    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());
    auto websocket_client = create_test_websocket_client(
        /* receive function */ []() { return pplx::task_from_result(std::string("{ }\x1e")); });
    auto connection = create_connection(websocket_client, writer, trace_level::state_changes);

    connection->start().wait();

    auto log_entries = std::dynamic_pointer_cast<memory_log_writer>(writer)->get_log_entries();
    ASSERT_FALSE(log_entries.empty());

    auto entry = remove_date_from_log_entry(log_entries[0]);
    ASSERT_EQ("[state change] disconnected -> connecting\n", entry);
}

TEST(connection_id, connection_id_is_set_if_start_fails_but_negotiate_request_succeeds)
{
    std::shared_ptr<log_writer> writer(std::make_shared<memory_log_writer>());

    auto websocket_client = create_test_websocket_client(
        /* receive function */ [](){ return pplx::task_from_exception<std::string>(std::runtime_error("should not be invoked")); },
        /* send function */ [](const std::string){ return pplx::task_from_exception<void>(std::runtime_error("should not be invoked"));  },
        /* connect function */[](const std::string&)
        {
            return pplx::task_from_exception<void>(web::websockets::client::websocket_exception(_XPLATSTR("connecting failed")));
        });

    auto connection = create_connection(websocket_client, writer, trace_level::errors);

    auto start_task = connection->start()
        // this test is not set up to connect successfully so we have to observe exceptions otherwise
        // other tests may fail due to an unobserved exception from the outstanding start task
        .then([](pplx::task<void> start_task)
        {
            try
            {
                start_task.get();
            }
            catch (...)
            {
            }
        });

    ASSERT_EQ("", connection->get_connection_id());
    start_task.get();
    ASSERT_EQ("f7707523-307d-4cba-9abf-3eef701241e8", connection->get_connection_id());
}

TEST(connection_id, can_get_connection_id_when_connection_in_connected_state)
{
    auto writer = std::shared_ptr<log_writer>{ std::make_shared<memory_log_writer>() };

    auto websocket_client = create_test_websocket_client(
        /* receive function */ [](){ return pplx::task_from_result(std::string("{ }\x1e")); });
    auto connection = create_connection(websocket_client, writer, trace_level::state_changes);

    std::string connection_id;
    connection->start()
        .then([connection, &connection_id]()
        mutable {
            connection_id = connection->get_connection_id();
            return connection->stop();
        }).get();

    ASSERT_EQ("f7707523-307d-4cba-9abf-3eef701241e8", connection_id);
}

TEST(connection_id, can_get_connection_id_after_connection_has_stopped)
{
    auto writer = std::shared_ptr<log_writer>{ std::make_shared<memory_log_writer>() };

    auto websocket_client = create_test_websocket_client(
        /* receive function */ [](){ return pplx::task_from_result(std::string("{ }\x1e")); });
    auto connection = create_connection(websocket_client, writer, trace_level::state_changes);

    connection->start()
        .then([connection]()
        {
            return connection->stop();
        }).get();

    ASSERT_EQ("f7707523-307d-4cba-9abf-3eef701241e8", connection->get_connection_id());
}

TEST(connection_id, connection_id_reset_when_starting_connection)
{
    auto fail_http_requests = false;

    auto writer = std::shared_ptr<log_writer>{ std::make_shared<memory_log_writer>() };

    auto websocket_client = create_test_websocket_client(
        /* receive function */ [](){ return pplx::task_from_result(std::string("{ }\x1e")); });

    auto web_request_factory = std::make_unique<test_web_request_factory>([&fail_http_requests](const std::string &url) -> std::unique_ptr<web_request>
    {
        if (!fail_http_requests) {
            auto response_body =
                url.find("/negotiate") != std::string::npos
                ? "{\"connectionId\" : \"f7707523-307d-4cba-9abf-3eef701241e8\", "
                "\"availableTransports\" : [ { \"transport\": \"WebSockets\", \"transferFormats\": [ \"Text\", \"Binary\" ] } ] }"
                : "";

            return std::unique_ptr<web_request>(new web_request_stub((unsigned short)200, "OK", response_body));
        }

        return std::unique_ptr<web_request>(new web_request_stub((unsigned short)500, "Internal Server Error", ""));
    });

    auto connection =
        connection_impl::create(create_uri(), trace_level::none, std::make_shared<trace_log_writer>(),
            std::move(web_request_factory), std::make_unique<test_transport_factory>(websocket_client));

    connection->start()
        .then([connection]()
        {
            return connection->stop();
        }).get();

    ASSERT_EQ("f7707523-307d-4cba-9abf-3eef701241e8", connection->get_connection_id());

    fail_http_requests = true;

    connection->start()
        // this test is not set up to connect successfully so we have to observe exceptions otherwise
        // other tests may fail due to an unobserved exception from the outstanding start task
        .then([](pplx::task<void> start_task)
        {
            try
            {
                start_task.get();
            }
            catch (...)
            {
            }
        }).get();

    ASSERT_EQ("", connection->get_connection_id());
}
