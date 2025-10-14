/**
 * @file test_unix.cpp
 * @brief Unix Domain Socket Unit Tests
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#endif

#include "../test_framework.h"
#include "lmnet/unix_client.h"
#include "lmnet/unix_server.h"

using namespace lmshao::lmnet;

#if defined(__linux__) || defined(__APPLE__)
// Simple UNIX socket server-client test
TEST(UnixTest, ServerClientSendRecv)
{
    const std::string socket_path = "/tmp/test_unix_socket";
    const std::string test_msg = "hello unix";
    std::atomic<bool> server_received{false};
    std::atomic<bool> client_received{false};
    std::string server_recv_data, client_recv_data;
    int client_fd = -1;

    // Server listener
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

#if defined(__linux__) && !defined(LMNET_LINUX_BACKEND_IOURING)
TEST(UnixTest, FileDescriptorTransfer)
{
    const std::string socket_path = "/tmp/test_unix_fd";
    std::atomic<bool> server_sent_fd{false};
    std::atomic<bool> server_send_failed{false};
    std::atomic<bool> client_received_fd{false};

    int pipe_write_end = -1;
    int received_fd = -1;

    class ServerListenerWithFd : public IServerListener {
    public:
        ServerListenerWithFd(std::atomic<bool> &sent, std::atomic<bool> &failed, int &write_end)
            : sent_(sent), failed_(failed), write_end_(write_end)
        {
        }

        void OnError(std::shared_ptr<Session>, const std::string &) override {}
        void OnClose(std::shared_ptr<Session>) override {}

        void OnAccept(std::shared_ptr<Session> session) override
        {
            int pipefd[2];
            if (pipe(pipefd) != 0) {
                failed_ = true;
                return;
            }

            write_end_ = pipefd[1];
            std::vector<int> fds = {pipefd[0]};
            if (session->SendFds(fds)) {
                sent_ = true;
            } else {
                failed_ = true;
            }
            close(pipefd[0]);
        }

        void OnReceive(std::shared_ptr<Session>, std::shared_ptr<DataBuffer>) override {}

    private:
        std::atomic<bool> &sent_;
        std::atomic<bool> &failed_;
        int &write_end_;
    };

    class ClientListenerWithFd : public IClientListener {
    public:
        ClientListenerWithFd(std::atomic<bool> &received, int &fd_ref) : received_(received), fd_ref_(fd_ref) {}

        void OnReceive(int, std::shared_ptr<DataBuffer>) override {}

        void OnReceiveUnixMessage(socket_t, const UnixMessage &message) override
        {
            if (message.HasFds()) {
                fd_ref_ = message.fds.front();
                received_ = true;
                // Close remaining FDs
                for (size_t i = 1; i < message.fds.size(); ++i) {
                    if (message.fds[i] >= 0) {
                        close(message.fds[i]);
                    }
                }
            }
        }

        void OnClose(int) override {}
        void OnError(int, const std::string &) override {}

    private:
        std::atomic<bool> &received_;
        int &fd_ref_;
    };

    std::remove(socket_path.c_str());
    auto server = UnixServer::Create(socket_path);
    auto server_listener = std::make_shared<ServerListenerWithFd>(server_sent_fd, server_send_failed, pipe_write_end);
    server->SetListener(server_listener);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    auto client = UnixClient::Create(socket_path);
    auto client_listener = std::make_shared<ClientListenerWithFd>(client_received_fd, received_fd);
    client->SetListener(client_listener);
    EXPECT_TRUE(client->Init());
    EXPECT_TRUE(client->Connect());

    for (int i = 0; i < 40 && !server_sent_fd && !server_send_failed; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    EXPECT_TRUE(server_sent_fd);
    EXPECT_FALSE(server_send_failed);
    EXPECT_GT(pipe_write_end, -1);

    for (int i = 0; i < 40 && !client_received_fd; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    EXPECT_TRUE(client_received_fd);
    EXPECT_GT(received_fd, -1);

    const std::string fd_payload = "payload-through-fd";
    ssize_t written = write(pipe_write_end, fd_payload.data(), fd_payload.size());
    EXPECT_EQ(static_cast<ssize_t>(fd_payload.size()), written);

    char buffer[64] = {0};
    ssize_t read_bytes = read(received_fd, buffer, sizeof(buffer));
    EXPECT_EQ(static_cast<ssize_t>(fd_payload.size()), read_bytes);
    std::string received(buffer, buffer + read_bytes);
    EXPECT_EQ(fd_payload, received);

    if (pipe_write_end >= 0) {
        close(pipe_write_end);
    }
    if (received_fd >= 0) {
        close(received_fd);
    }

    client->Close();
    server->Stop();
    std::remove(socket_path.c_str());
}
#else
TEST(UnixTest, FileDescriptorTransfer)
{
    std::puts("File descriptor transfer test skipped on this platform/backend.");
    EXPECT_TRUE(true);
}
#endif
#else
TEST(UnixTest, ServerClientSendRecv)
{
    std::puts("Unix domain sockets are not supported on this platform. Skipping test.");
    EXPECT_TRUE(true);
}
#endif

RUN_ALL_TESTS()
