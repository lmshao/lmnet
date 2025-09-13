/**
 * @file test_tcp.cpp
 * @brief TCP Unit Tests
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include <atomic>
#include <chrono>
#include <thread>

#include "../test_framework.h"
#include "lmnet/tcp_client.h"
#include "lmnet/tcp_server.h"

using namespace lmshao::lmnet;

// Simple TCP server-client test
TEST(TcpTest, ServerClientSendRecv)
{
    const uint16_t port = 12345;
    const std::string test_msg = "hello tcp";
    std::atomic<bool> server_received{false};
    std::atomic<bool> client_received{false};
    std::string server_recv_data, client_recv_data;

    // Server listener
    class ServerListener : public IServerListener {
    public:
        std::atomic<bool> &received;
        std::string &recv_data;
        ServerListener(std::atomic<bool> &r, std::string &d) : received(r), recv_data(d) {}
        void OnError(std::shared_ptr<Session>, const std::string &) override {}
        void OnClose(std::shared_ptr<Session>) override {}
        void OnAccept(std::shared_ptr<Session>) override {}
        void OnReceive(std::shared_ptr<Session> session, std::shared_ptr<DataBuffer> data) override
        {
            recv_data = data->ToString();
            received = true;
            session->Send("world");
        }
    };

    class ClientListener : public IClientListener {
    public:
        std::atomic<bool> &received;
        std::string &recv_data;
        ClientListener(std::atomic<bool> &r, std::string &d) : received(r), recv_data(d) {}
        void OnReceive(socket_t fd, std::shared_ptr<DataBuffer> data) override
        {
            recv_data = data->ToString();
            received = true;
        }
        void OnClose(socket_t fd) override {}
        void OnError(socket_t fd, const std::string &) override {}
    };

    // Start server
    auto server = TcpServer::Create("0.0.0.0", port);
    auto server_listener = std::make_shared<ServerListener>(server_received, server_recv_data);
    server->SetListener(server_listener);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    // Start client
    auto client = TcpClient::Create("127.0.0.1", port);
    auto client_listener = std::make_shared<ClientListener>(client_received, client_recv_data);
    client->SetListener(client_listener);
    EXPECT_TRUE(client->Init());
    EXPECT_TRUE(client->Connect());

    // client send data
    EXPECT_TRUE(client->Send(test_msg));

    // wait for server to receive
    for (int i = 0; i < 20 && !server_received; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(server_received);
    EXPECT_TRUE(server_recv_data == test_msg);

    // wait for client to receive reply
    for (int i = 0; i < 20 && !client_received; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(client_received);
    EXPECT_TRUE(client_recv_data == "world");

    client->Close();
    server->Stop();
}

RUN_ALL_TESTS()
