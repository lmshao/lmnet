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
#include <mutex>
#include <set>
#include <sstream>
#include <sys/socket.h>
#include <thread>
#include <vector>

#include "../test_framework.h"
#include "lmnet/tcp_client.h"
#include "lmnet/tcp_server.h"

#ifdef _WIN32
#include "../../src/platforms/windows/iocp_manager.h"
#endif

using namespace lmshao::lmnet;

namespace {
template <typename Condition>
bool WaitFor(Condition condition, int timeout_ms = 5000, int check_interval_ms = 25)
{
    for (int i = 0; i < timeout_ms / check_interval_ms; ++i) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
    }
    return false;
}
} // namespace

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

    // Give extra time for cleanup
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

TEST(TcpTest, LargePayloadSend)
{
    const uint16_t port = 12347;
    const size_t payload_size = 2 * 1024 * 1024;

    std::string payload(payload_size, '\0');
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>('A' + (i % 23));
    }

    std::atomic<bool> server_done{false};
    std::atomic<bool> server_ok{false};
    std::mutex server_mutex;
    std::string server_data;
    server_data.reserve(payload.size());

    class ServerListener : public IServerListener {
    public:
        ServerListener(const std::string &expected, std::mutex &mutex, std::string &received, std::atomic<bool> &done,
                       std::atomic<bool> &ok)
            : expected_(expected), mutex_(mutex), received_(received), done_(done), ok_(ok)
        {
        }

        void OnAccept(std::shared_ptr<Session>) override {}
        void OnClose(std::shared_ptr<Session>) override {}
        void OnError(std::shared_ptr<Session>, const std::string &) override {}

        void OnReceive(std::shared_ptr<Session>, std::shared_ptr<DataBuffer> data) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            received_.append(reinterpret_cast<const char *>(data->Data()), data->Size());
            if (received_.size() >= expected_.size()) {
                ok_ = (received_ == expected_);
                done_ = true;
            }
        }

    private:
        const std::string &expected_;
        std::mutex &mutex_;
        std::string &received_;
        std::atomic<bool> &done_;
        std::atomic<bool> &ok_;
    };

    class ClientListener : public IClientListener {
    public:
        void OnReceive(socket_t, std::shared_ptr<DataBuffer>) override {}
        void OnClose(socket_t) override {}
        void OnError(socket_t, const std::string &) override {}
    };

    auto server = TcpServer::Create("0.0.0.0", port);
    auto server_listener = std::make_shared<ServerListener>(payload, server_mutex, server_data, server_done, server_ok);
    server->SetListener(server_listener);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    auto client = TcpClient::Create("127.0.0.1", port);
    client->SetListener(std::make_shared<ClientListener>());
    EXPECT_TRUE(client->Init());
    EXPECT_TRUE(client->Connect());

    int send_buffer_size = 4096;
    EXPECT_EQ(0, setsockopt(client->GetSocketFd(), SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size)));

    EXPECT_TRUE(client->Send(payload.data(), payload.size()));
    EXPECT_TRUE(WaitFor([&] { return server_done.load(); }, 10000));
    EXPECT_TRUE(server_ok.load());

    client->Close();
    server->Stop();
}

TEST(TcpTest, ConcurrentSendBurst)
{
    const uint16_t port = 12348;
    const int thread_count = 4;
    const int messages_per_thread = 200;

    std::atomic<bool> server_done{false};
    std::atomic<bool> server_ok{false};
    std::mutex server_mutex;
    std::string stream_buffer;
    std::set<std::string> received_messages;
    std::set<std::string> expected_messages;

    for (int thread_index = 0; thread_index < thread_count; ++thread_index) {
        for (int message_index = 0; message_index < messages_per_thread; ++message_index) {
            std::ostringstream oss;
            oss << "thread=" << thread_index << ";msg=" << message_index << ";payload=abcdefghijklmnopqrstuvwxyz\n";
            expected_messages.insert(oss.str());
        }
    }

    class ServerListener : public IServerListener {
    public:
        ServerListener(std::mutex &mutex, std::string &stream_buffer, std::set<std::string> &received_messages,
                       const std::set<std::string> &expected_messages, std::atomic<bool> &done, std::atomic<bool> &ok)
            : mutex_(mutex), stream_buffer_(stream_buffer), received_messages_(received_messages),
              expected_messages_(expected_messages), done_(done), ok_(ok)
        {
        }

        void OnAccept(std::shared_ptr<Session>) override {}
        void OnClose(std::shared_ptr<Session>) override {}
        void OnError(std::shared_ptr<Session>, const std::string &) override {}

        void OnReceive(std::shared_ptr<Session>, std::shared_ptr<DataBuffer> data) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stream_buffer_.append(reinterpret_cast<const char *>(data->Data()), data->Size());

            size_t newline_pos = std::string::npos;
            while ((newline_pos = stream_buffer_.find('\n')) != std::string::npos) {
                std::string message = stream_buffer_.substr(0, newline_pos + 1);
                stream_buffer_.erase(0, newline_pos + 1);
                received_messages_.insert(message);
            }

            if (received_messages_.size() == expected_messages_.size()) {
                ok_ = (received_messages_ == expected_messages_);
                done_ = true;
            }
        }

    private:
        std::mutex &mutex_;
        std::string &stream_buffer_;
        std::set<std::string> &received_messages_;
        const std::set<std::string> &expected_messages_;
        std::atomic<bool> &done_;
        std::atomic<bool> &ok_;
    };

    class ClientListener : public IClientListener {
    public:
        void OnReceive(socket_t, std::shared_ptr<DataBuffer>) override {}
        void OnClose(socket_t) override {}
        void OnError(socket_t, const std::string &) override {}
    };

    auto server = TcpServer::Create("0.0.0.0", port);
    auto server_listener =
        std::make_shared<ServerListener>(server_mutex, stream_buffer, received_messages, expected_messages, server_done,
                                         server_ok);
    server->SetListener(server_listener);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    auto client = TcpClient::Create("127.0.0.1", port);
    client->SetListener(std::make_shared<ClientListener>());
    EXPECT_TRUE(client->Init());
    EXPECT_TRUE(client->Connect());

    std::atomic<bool> send_failed{false};
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (int thread_index = 0; thread_index < thread_count; ++thread_index) {
        threads.emplace_back([client, thread_index, messages_per_thread, &send_failed]() {
            for (int message_index = 0; message_index < messages_per_thread; ++message_index) {
                std::ostringstream oss;
                oss << "thread=" << thread_index << ";msg=" << message_index
                    << ";payload=abcdefghijklmnopqrstuvwxyz\n";
                if (!client->Send(oss.str())) {
                    send_failed = true;
                    return;
                }
            }
        });
    }

    for (auto &thread : threads) {
        thread.join();
    }

    EXPECT_FALSE(send_failed.load());
    EXPECT_TRUE(WaitFor([&] { return server_done.load(); }, 10000));
    EXPECT_TRUE(server_ok.load());

    client->Close();
    server->Stop();
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
