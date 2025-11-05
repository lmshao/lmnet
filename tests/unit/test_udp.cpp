/**
 * @file test_udp.cpp
 * @brief UDP Unit Tests
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
#include "lmnet/udp_client.h"
#include "lmnet/udp_server.h"

#ifdef _WIN32
#include "../../src/platforms/windows/iocp_manager.h"
#endif

using namespace lmshao::lmnet;

// Simple UDP server-client test
TEST(UdpTest, ServerClientSendRecv)
{
    const uint16_t port = 12346;
    const std::string test_msg = "hello udp";
    std::atomic<bool> server_received{false};
    std::atomic<bool> client_received{false};
    std::string server_recv_data, client_recv_data;
    int client_fd = -1;

    class ServerListener : public IServerListener {
    public:
        std::atomic<bool> &received;
        std::string &recv_data;
        int &client_fd;
        ServerListener(std::atomic<bool> &r, std::string &d, int &fd) : received(r), recv_data(d), client_fd(fd) {}
        void OnAccept(std::shared_ptr<Session> session) override
        {
            std::cout << "[UDP Server] OnAccept: " << session->ClientInfo() << std::endl;
        }
        void OnReceive(std::shared_ptr<Session> session, std::shared_ptr<DataBuffer> data) override
        {
            std::cout << "[UDP Server] OnReceive " << data->Size() << " bytes from " << session->ClientInfo()
                      << std::endl;
            recv_data = data->ToString();
            received = true;
            if (session->Send("world")) {
                std::cout << "[UDP Server] send reply ok." << std::endl;
            } else {
                std::cout << "[UDP Server] send reply failed." << std::endl;
            }
        }
        void OnClose(std::shared_ptr<Session> session) override
        {
            std::cout << "[UDP Server] OnClose: " << session->ClientInfo() << std::endl;
        }
        void OnError(std::shared_ptr<Session> session, const std::string &reason) override
        {
            std::cout << "[UDP Server] OnError: '" << reason << "' from " << session->ClientInfo() << std::endl;
        }
    };

    class ClientListener : public IClientListener {
    public:
        std::atomic<bool> &received;
        std::string &recv_data;
        ClientListener(std::atomic<bool> &r, std::string &d) : received(r), recv_data(d) {}
        void OnReceive(socket_t, std::shared_ptr<DataBuffer> data) override
        {
            recv_data = data->ToString();
            received = true;
        }
        void OnClose(socket_t) override {}
        void OnError(socket_t, const std::string &) override {}
    };

    // Start server
    auto server = UdpServer::Create("0.0.0.0", port);
    auto server_listener = std::make_shared<ServerListener>(server_received, server_recv_data, client_fd);
    server->SetListener(server_listener);
    if (!server->Init()) {
        std::cout << "IPv6 not available on this host, skipping UDP IPv6 test." << std::endl;
        return;
    }
    EXPECT_TRUE(server->Start());

    // Start client
    auto client = UdpClient::Create("127.0.0.1", port);
    auto client_listener = std::make_shared<ClientListener>(client_received, client_recv_data);
    client->SetListener(client_listener);
    if (!client->Init()) {
        std::cout << "IPv6 client init failed, skipping UDP IPv6 test." << std::endl;
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

    printf("UDP server-client test completed successfully.\n");
}

// IPv6 loopback UDP server-client test
TEST(UdpTest, IPv6LoopbackServerClientSendRecv)
{
    const uint16_t port = 12357;
    const std::string test_msg = "hello udp v6";
    std::atomic<bool> server_received{false};
    std::atomic<bool> client_received{false};
    std::string server_recv_data, client_recv_data;

    class ServerListenerV6 : public IServerListener {
    public:
        std::atomic<bool> &received;
        std::string &recv_data;
        ServerListenerV6(std::atomic<bool> &r, std::string &d) : received(r), recv_data(d) {}
        void OnAccept(std::shared_ptr<Session> session) override {}
        void OnReceive(std::shared_ptr<Session> session, std::shared_ptr<DataBuffer> data) override
        {
            recv_data = data->ToString();
            received = true;
            session->Send("world");
        }
        void OnClose(std::shared_ptr<Session>) override {}
        void OnError(std::shared_ptr<Session>, const std::string &) override {}
    };

    class ClientListenerV6 : public IClientListener {
    public:
        std::atomic<bool> &received;
        std::string &recv_data;
        ClientListenerV6(std::atomic<bool> &r, std::string &d) : received(r), recv_data(d) {}
        void OnReceive(socket_t, std::shared_ptr<DataBuffer> data) override
        {
            recv_data = data->ToString();
            received = true;
        }
        void OnClose(socket_t) override {}
        void OnError(socket_t, const std::string &) override {}
    };

    // Start IPv6 server (bind to ::)
    auto server = UdpServer::Create("::", port);
    auto server_listener = std::make_shared<ServerListenerV6>(server_received, server_recv_data);
    server->SetListener(server_listener);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    // Start IPv6 client (connect/send to ::1)
    auto client = UdpClient::Create("::1", port);
    auto client_listener = std::make_shared<ClientListenerV6>(client_received, client_recv_data);
    client->SetListener(client_listener);
    EXPECT_TRUE(client->Init());

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

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// Test GetIdlePort functionality
TEST(UdpTest, GetIdlePortTest)
{
    printf("Testing GetIdlePort function...\n");

    // Test GetIdlePort
    uint16_t port1 = UdpServer::GetIdlePort();
    uint16_t port2 = UdpServer::GetIdlePort();
    uint16_t port3 = UdpServer::GetIdlePort();

    printf("GetIdlePort results:\n");
    printf("  Port 1: %d\n", port1);
    printf("  Port 2: %d\n", port2);
    printf("  Port 3: %d\n", port3);

    // Test GetIdlePortPair
    printf("Testing GetIdlePortPair function...\n");
    uint16_t pairPort = UdpServer::GetIdlePortPair();
    printf("GetIdlePortPair result: %d\n", pairPort);

    // Verify ports are valid and increasing
    EXPECT_TRUE(port1 > 0);
    EXPECT_TRUE(port2 > port1);
    EXPECT_TRUE(port3 > port2);
    EXPECT_TRUE(pairPort > 0);

    printf("Port discovery test completed successfully.\n");
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
