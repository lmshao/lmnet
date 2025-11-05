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

#ifdef _WIN32
#include "../../src/platforms/windows/iocp_manager.h"
#endif

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
    if (!server->Init()) {
        std::cout << "IPv6 not available on this host, skipping TCP IPv6 test." << std::endl;
        return;
    }
    EXPECT_TRUE(server->Start());

    // Start client
    auto client = TcpClient::Create("127.0.0.1", port);
    auto client_listener = std::make_shared<ClientListener>(client_received, client_recv_data);
    client->SetListener(client_listener);
    if (!client->Init()) {
        std::cout << "IPv6 client init failed, skipping TCP IPv6 test." << std::endl;
        server->Stop();
        return;
    }
    if (!client->Connect()) {
        std::cout << "IPv6 client connect failed, skipping TCP IPv6 test." << std::endl;
        server->Stop();
        return;
    }

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

    // Give extra time for cleanup
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// IPv6 loopback TCP server-client test
TEST(TcpTest, IPv6LoopbackServerClientSendRecv)
{
    const uint16_t port = 12356;
    const std::string test_msg = "hello tcp v6";
    std::atomic<bool> server_received{false};
    std::atomic<bool> client_received{false};
    std::string server_recv_data, client_recv_data;

    // Server listener (IPv6)
    class ServerListenerV6 : public IServerListener {
    public:
        std::atomic<bool> &received;
        std::string &recv_data;
        ServerListenerV6(std::atomic<bool> &r, std::string &d) : received(r), recv_data(d) {}
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

    class ClientListenerV6 : public IClientListener {
    public:
        std::atomic<bool> &received;
        std::string &recv_data;
        ClientListenerV6(std::atomic<bool> &r, std::string &d) : received(r), recv_data(d) {}
        void OnReceive(socket_t fd, std::shared_ptr<DataBuffer> data) override
        {
            recv_data = data->ToString();
            received = true;
        }
        void OnClose(socket_t fd) override {}
        void OnError(socket_t fd, const std::string &) override {}
    };

    // Start IPv6 server (bind to ::)
    auto server = TcpServer::Create("::", port);
    auto server_listener = std::make_shared<ServerListenerV6>(server_received, server_recv_data);
    server->SetListener(server_listener);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    // Start IPv6 client (connect to ::1)
    auto client = TcpClient::Create("::1", port);
    auto client_listener = std::make_shared<ClientListenerV6>(client_received, client_recv_data);
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

    // Give extra time for cleanup
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

int main()
{
    int result = TestRunner::getInstance().runAllTests();

#ifdef _WIN32
    // Explicitly stop IOCP manager before exit
    lmshao::lmnet::IocpManager::GetInstance().Exit();
#endif

    return result;
}
