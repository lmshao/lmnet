/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025-2026 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#endif

#include "../test_framework.h"
#include "lmnet/unix_client.h"
#include "lmnet/unix_server.h"

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

TEST(UnixTest, ConnectFailureDoesNotNotifyListener)
{
    const std::string socket_path = "/tmp/lmnet_missing_socket.sock";
    std::atomic<int> receiveCount{0};
    std::atomic<int> closeCount{0};
    std::atomic<int> errorCount{0};

    class FailureListener : public IClientListener {
    public:
        FailureListener(std::atomic<int> &receiveCount, std::atomic<int> &closeCount, std::atomic<int> &errorCount)
            : receiveCount_(receiveCount), closeCount_(closeCount), errorCount_(errorCount)
        {
        }

        void OnReceive(int, std::shared_ptr<DataBuffer>) override { ++receiveCount_; }
        void OnClose(int) override { ++closeCount_; }
        void OnError(int, const std::string &) override { ++errorCount_; }

    private:
        std::atomic<int> &receiveCount_;
        std::atomic<int> &closeCount_;
        std::atomic<int> &errorCount_;
    };

    std::remove(socket_path.c_str());

    auto client = UnixClient::Create(socket_path);
    auto listener = std::make_shared<FailureListener>(receiveCount, closeCount, errorCount);
    client->SetListener(listener);

    EXPECT_TRUE(client->Init());
    EXPECT_FALSE(client->Connect());

    EXPECT_FALSE(
        WaitFor([&] { return receiveCount.load() > 0 || closeCount.load() > 0 || errorCount.load() > 0; }, 1000));
    EXPECT_EQ(0, receiveCount.load());
    EXPECT_EQ(0, closeCount.load());
    EXPECT_EQ(0, errorCount.load());

    client->Close();
    std::remove(socket_path.c_str());
}

TEST(UnixTest, ConnectFailureAllowsRetry)
{
    const std::string socket_path = "/tmp/lmnet_retry_socket.sock";
    std::atomic<bool> accepted{false};

    class ServerListener : public IServerListener {
    public:
        explicit ServerListener(std::atomic<bool> &accepted) : accepted_(accepted) {}

        void OnAccept(std::shared_ptr<Session>) override { accepted_ = true; }
        void OnReceive(std::shared_ptr<Session>, std::shared_ptr<DataBuffer>) override {}
        void OnClose(std::shared_ptr<Session>) override {}
        void OnError(std::shared_ptr<Session>, const std::string &) override {}

    private:
        std::atomic<bool> &accepted_;
    };

    class ClientListener : public IClientListener {
    public:
        void OnReceive(int, std::shared_ptr<DataBuffer>) override {}
        void OnClose(int) override {}
        void OnError(int, const std::string &) override {}
    };

    std::remove(socket_path.c_str());

    auto client = UnixClient::Create(socket_path);
    auto clientListener = std::make_shared<ClientListener>();
    client->SetListener(clientListener);
    EXPECT_TRUE(client->Init());
    EXPECT_FALSE(client->Connect());

    auto server = UnixServer::Create(socket_path);
    auto serverListener = std::make_shared<ServerListener>(accepted);
    server->SetListener(serverListener);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    EXPECT_TRUE(client->Connect());
    EXPECT_TRUE(WaitFor([&] { return accepted.load(); }, 2000));

    client->Close();
    server->Stop();
    std::remove(socket_path.c_str());
}

TEST(UnixTest, InitBindFailureInvalidatesServerSocket)
{
    const std::string socket_path = "/tmp/lmnet_missing_dir/test_unix_server.sock";
    std::remove(socket_path.c_str());

    auto server = UnixServer::Create(socket_path);
    EXPECT_TRUE(!server->Init());
    EXPECT_EQ(INVALID_SOCKET, server->GetSocketFd());

    EXPECT_TRUE(server->Stop());
    std::remove(socket_path.c_str());
}

TEST(UnixTest, LargeClientSend)
{
    const std::string socket_path = "/tmp/test_unix_large_send";
    const size_t payload_size = 2 * 1024 * 1024;

    std::string payload(payload_size, '\0');
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>('a' + (i % 23));
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

        void OnError(std::shared_ptr<Session>, const std::string &) override {}
        void OnClose(std::shared_ptr<Session>) override {}
        void OnAccept(std::shared_ptr<Session>) override {}
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
        void OnReceive(int, std::shared_ptr<DataBuffer>) override {}
        void OnClose(int) override {}
        void OnError(int, const std::string &) override {}
    };

    std::remove(socket_path.c_str());
    auto server = UnixServer::Create(socket_path);
    auto server_listener = std::make_shared<ServerListener>(payload, server_mutex, server_data, server_done, server_ok);
    server->SetListener(server_listener);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    auto client = UnixClient::Create(socket_path);
    auto client_listener = std::make_shared<ClientListener>();
    client->SetListener(client_listener);
    EXPECT_TRUE(client->Init());
    EXPECT_TRUE(client->Connect());

    EXPECT_TRUE(client->Send(payload));

    for (int i = 0; i < 200 && !server_done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    EXPECT_TRUE(server_done.load());
    EXPECT_TRUE(server_ok.load());

    client->Close();
    server->Stop();
    std::remove(socket_path.c_str());
}

TEST(UnixTest, CloseAfterInitInvalidatesSocket)
{
    auto client = UnixClient::Create("/tmp/lmnet_close_after_init.sock");

    EXPECT_TRUE(client->Init());
    EXPECT_TRUE(client->GetSocketFd() != INVALID_SOCKET);

    client->Close();

    EXPECT_EQ(INVALID_SOCKET, client->GetSocketFd());
}

TEST(UnixTest, ConcurrentClientSendBurst)
{
    const std::string socket_path = "/tmp/test_unix_concurrent_client_send";
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
            oss << "client-thread=" << thread_index << ";msg=" << message_index
                << ";payload=abcdefghijklmnopqrstuvwxyz\n";
            expected_messages.insert(oss.str());
        }
    }

    class ServerListener : public IServerListener {
    public:
        ServerListener(std::mutex &mutex, std::string &streamBuffer, std::set<std::string> &receivedMessages,
                       const std::set<std::string> &expectedMessages, std::atomic<bool> &done, std::atomic<bool> &ok)
            : mutex_(mutex), streamBuffer_(streamBuffer), receivedMessages_(receivedMessages),
              expectedMessages_(expectedMessages), done_(done), ok_(ok)
        {
        }

        void OnError(std::shared_ptr<Session>, const std::string &) override {}
        void OnClose(std::shared_ptr<Session>) override {}
        void OnAccept(std::shared_ptr<Session>) override {}
        void OnReceive(std::shared_ptr<Session>, std::shared_ptr<DataBuffer> data) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            streamBuffer_.append(reinterpret_cast<const char *>(data->Data()), data->Size());

            size_t newlinePos = std::string::npos;
            while ((newlinePos = streamBuffer_.find('\n')) != std::string::npos) {
                std::string message = streamBuffer_.substr(0, newlinePos + 1);
                streamBuffer_.erase(0, newlinePos + 1);
                receivedMessages_.insert(message);
            }

            if (receivedMessages_.size() == expectedMessages_.size()) {
                ok_ = (receivedMessages_ == expectedMessages_);
                done_ = true;
            }
        }

    private:
        std::mutex &mutex_;
        std::string &streamBuffer_;
        std::set<std::string> &receivedMessages_;
        const std::set<std::string> &expectedMessages_;
        std::atomic<bool> &done_;
        std::atomic<bool> &ok_;
    };

    class ClientListener : public IClientListener {
    public:
        void OnReceive(int, std::shared_ptr<DataBuffer>) override {}
        void OnClose(int) override {}
        void OnError(int, const std::string &) override {}
    };

    std::remove(socket_path.c_str());
    auto server = UnixServer::Create(socket_path);
    auto serverListener = std::make_shared<ServerListener>(server_mutex, stream_buffer, received_messages,
                                                           expected_messages, server_done, server_ok);
    server->SetListener(serverListener);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    auto client = UnixClient::Create(socket_path);
    auto clientListener = std::make_shared<ClientListener>();
    client->SetListener(clientListener);
    EXPECT_TRUE(client->Init());
    EXPECT_TRUE(client->Connect());

    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    std::atomic<bool> send_failed{false};
    for (int thread_index = 0; thread_index < thread_count; ++thread_index) {
        threads.emplace_back([client, thread_index, messages_per_thread, &send_failed]() {
            for (int message_index = 0; message_index < messages_per_thread; ++message_index) {
                std::ostringstream oss;
                oss << "client-thread=" << thread_index << ";msg=" << message_index
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
    for (int i = 0; i < 200 && !server_done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    EXPECT_TRUE(server_done.load());
    EXPECT_TRUE(server_ok.load());

    client->Close();
    server->Stop();
    std::remove(socket_path.c_str());
}

TEST(UnixTest, ConcurrentServerSendBurst)
{
    const std::string socket_path = "/tmp/test_unix_concurrent_server_send";
    const int thread_count = 4;
    const int messages_per_thread = 200;

    std::mutex session_mutex;
    std::shared_ptr<Session> accepted_session;
    std::atomic<bool> accepted{false};
    std::atomic<bool> client_done{false};
    std::atomic<bool> client_ok{false};
    std::atomic<bool> send_failed{false};
    std::mutex client_mutex;
    std::string stream_buffer;
    std::set<std::string> received_messages;
    std::set<std::string> expected_messages;

    for (int thread_index = 0; thread_index < thread_count; ++thread_index) {
        for (int message_index = 0; message_index < messages_per_thread; ++message_index) {
            std::ostringstream oss;
            oss << "server-thread=" << thread_index << ";msg=" << message_index
                << ";payload=abcdefghijklmnopqrstuvwxyz\n";
            expected_messages.insert(oss.str());
        }
    }

    class ServerListener : public IServerListener {
    public:
        ServerListener(std::mutex &mutex, std::shared_ptr<Session> &session, std::atomic<bool> &accepted)
            : mutex_(mutex), session_(session), accepted_(accepted)
        {
        }

        void OnAccept(std::shared_ptr<Session> session) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session_ = std::move(session);
            accepted_ = true;
        }

        void OnError(std::shared_ptr<Session>, const std::string &) override {}
        void OnClose(std::shared_ptr<Session>) override {}
        void OnReceive(std::shared_ptr<Session>, std::shared_ptr<DataBuffer>) override {}

    private:
        std::mutex &mutex_;
        std::shared_ptr<Session> &session_;
        std::atomic<bool> &accepted_;
    };

    class ClientListener : public IClientListener {
    public:
        ClientListener(std::mutex &mutex, std::string &streamBuffer, std::set<std::string> &receivedMessages,
                       const std::set<std::string> &expectedMessages, std::atomic<bool> &done, std::atomic<bool> &ok)
            : mutex_(mutex), streamBuffer_(streamBuffer), receivedMessages_(receivedMessages),
              expectedMessages_(expectedMessages), done_(done), ok_(ok)
        {
        }

        void OnReceive(int, std::shared_ptr<DataBuffer> data) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            streamBuffer_.append(reinterpret_cast<const char *>(data->Data()), data->Size());

            size_t newlinePos = std::string::npos;
            while ((newlinePos = streamBuffer_.find('\n')) != std::string::npos) {
                std::string message = streamBuffer_.substr(0, newlinePos + 1);
                streamBuffer_.erase(0, newlinePos + 1);
                receivedMessages_.insert(message);
            }

            if (receivedMessages_.size() == expectedMessages_.size()) {
                ok_ = (receivedMessages_ == expectedMessages_);
                done_ = true;
            }
        }

        void OnClose(int) override {}
        void OnError(int, const std::string &) override {}

    private:
        std::mutex &mutex_;
        std::string &streamBuffer_;
        std::set<std::string> &receivedMessages_;
        const std::set<std::string> &expectedMessages_;
        std::atomic<bool> &done_;
        std::atomic<bool> &ok_;
    };

    std::remove(socket_path.c_str());
    auto server = UnixServer::Create(socket_path);
    auto serverListener = std::make_shared<ServerListener>(session_mutex, accepted_session, accepted);
    server->SetListener(serverListener);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    auto client = UnixClient::Create(socket_path);
    auto clientListener = std::make_shared<ClientListener>(client_mutex, stream_buffer, received_messages,
                                                           expected_messages, client_done, client_ok);
    client->SetListener(clientListener);
    EXPECT_TRUE(client->Init());
    EXPECT_TRUE(client->Connect());

    for (int i = 0; i < 100 && !accepted.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    EXPECT_TRUE(accepted.load());

    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(session_mutex);
        session = accepted_session;
    }
    EXPECT_TRUE(static_cast<bool>(session));

    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (int thread_index = 0; thread_index < thread_count; ++thread_index) {
        threads.emplace_back([session, thread_index, messages_per_thread, &send_failed]() {
            for (int message_index = 0; message_index < messages_per_thread; ++message_index) {
                std::ostringstream oss;
                oss << "server-thread=" << thread_index << ";msg=" << message_index
                    << ";payload=abcdefghijklmnopqrstuvwxyz\n";
                if (!session->Send(oss.str())) {
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
    for (int i = 0; i < 200 && !client_done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    EXPECT_TRUE(client_done.load());
    EXPECT_TRUE(client_ok.load());

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
