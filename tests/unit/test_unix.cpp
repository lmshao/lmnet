//
// Copyright © 2025 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include "../test_framework.h"
#include "network/unix_client.h"
#include "network/unix_server.h"

using namespace lmshao::network;

// Simple UNIX socket server-client test
TEST(UnixTest, ServerClientSendRecv)
{
    const std::string socket_path = "/tmp/test_unix_socket";
    const std::string test_msg = "hello unix";
    std::atomic<bool> server_received{false};
    std::atomic<bool> client_received{false};
    std::string server_recv_data, client_recv_data;
    int client_fd = -1;

    // Server 监听器
    class ServerListener : public IServerListener {
    public:
        std::atomic<bool> &received;
        std::string &recv_data;
        int &client_fd;
        ServerListener(std::atomic<bool> &r, std::string &d, int &fd) : received(r), recv_data(d), client_fd(fd) {}
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
        void OnReceive(int, std::shared_ptr<DataBuffer> data) override
        {
            recv_data = data->ToString();
            received = true;
        }
        void OnClose(int) override {}
        void OnError(int, const std::string &) override {}
    };

    // Start server
    std::remove(socket_path.c_str());
    auto server = UnixServer::Create(socket_path);
    auto server_listener = std::make_shared<ServerListener>(server_received, server_recv_data, client_fd);
    server->SetListener(server_listener);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    // Start client
    auto client = UnixClient::Create(socket_path);
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
    std::remove(socket_path.c_str());
}

RUN_ALL_TESTS()
