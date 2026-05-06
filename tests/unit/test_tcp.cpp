/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025-2026 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _WIN32
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

#include "../test_framework.h"
#include "lmnet/tcp_client.h"
#include "lmnet/tcp_server.h"

#ifdef __APPLE__
#include "../../src/platforms/darwin/event_reactor.h"
#endif

#ifdef _WIN32
#include "../../src/platforms/windows/iocp_manager.h"
#endif

using namespace lmshao::lmnet;

namespace {
uint16_t GetIdleTcpPort()
{
    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET) {
        return 0;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
#ifdef _WIN32
        closesocket(fd);
#else
        close(fd);
#endif
        return 0;
    }

    socklen_t len = sizeof(addr);
    if (getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len) != 0) {
#ifdef _WIN32
        closesocket(fd);
#else
        close(fd);
#endif
        return 0;
    }

#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif

    return ntohs(addr.sin_port);
}

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

int SetSendBufferSize(socket_t socket, int send_buffer_size)
{
#ifdef _WIN32
    return setsockopt(socket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char *>(&send_buffer_size),
                      static_cast<int>(sizeof(send_buffer_size)));
#else
    return setsockopt(socket, SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size));
#endif
}

#ifdef __APPLE__
class BlockingEventHandler final : public EventHandler {
public:
    explicit BlockingEventHandler(socket_t fd) : fd_(fd) {}

    void HandleRead(socket_t) override {}
    void HandleWrite(socket_t) override {}
    void HandleError(socket_t) override {}
    void HandleClose(socket_t) override {}
    int GetHandle() const override { return fd_; }
    int GetEvents() const override { return static_cast<int>(EventType::READ); }

private:
    socket_t fd_;
};
#endif
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

TEST(TcpTest, CloseAfterInitInvalidatesSocket)
{
    auto client = TcpClient::Create("127.0.0.1", 6553);

    EXPECT_TRUE(client->Init());
    EXPECT_TRUE(client->GetSocketFd() != INVALID_SOCKET);

    client->Close();

    EXPECT_EQ(INVALID_SOCKET, client->GetSocketFd());
}

TEST(TcpTest, InitBindFailureInvalidatesClientSocket)
{
    uint16_t localPort = GetIdleTcpPort();
    EXPECT_TRUE(localPort != 0);

    auto firstClient = TcpClient::Create("127.0.0.1", 6553, "127.0.0.1", localPort);
    EXPECT_TRUE(firstClient->Init());
    EXPECT_TRUE(firstClient->GetSocketFd() != INVALID_SOCKET);

    auto secondClient = TcpClient::Create("127.0.0.1", 6553, "127.0.0.1", localPort);
    EXPECT_TRUE(!secondClient->Init());
    EXPECT_EQ(INVALID_SOCKET, secondClient->GetSocketFd());

    firstClient->Close();
    secondClient->Close();
}

TEST(TcpTest, StopAfterInitInvalidatesServerSocket)
{
    auto server = TcpServer::Create("127.0.0.1", 12347);

    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->GetSocketFd() != INVALID_SOCKET);

    EXPECT_TRUE(server->Stop());

    EXPECT_EQ(INVALID_SOCKET, server->GetSocketFd());
}

TEST(TcpTest, InitBindFailureInvalidatesServerSocket)
{
    uint16_t port = GetIdleTcpPort();
    EXPECT_TRUE(port != 0);

    auto firstServer = TcpServer::Create("127.0.0.1", port);
    EXPECT_TRUE(firstServer->Init());
    EXPECT_TRUE(firstServer->GetSocketFd() != INVALID_SOCKET);

    auto secondServer = TcpServer::Create("127.0.0.1", port);
    EXPECT_TRUE(!secondServer->Init());
    EXPECT_EQ(INVALID_SOCKET, secondServer->GetSocketFd());

    EXPECT_TRUE(firstServer->Stop());
    EXPECT_TRUE(secondServer->Stop());
}

TEST(TcpTest, InitInvalidIpInvalidatesServerSocket)
{
    auto server = TcpServer::Create("invalid-ip", 12348);

    EXPECT_TRUE(!server->Init());
    EXPECT_EQ(INVALID_SOCKET, server->GetSocketFd());

    EXPECT_TRUE(server->Stop());
}

TEST(TcpTest, ConnectFailureDoesNotNotifyListener)
{
    const uint16_t port = 65432;
    std::atomic<int> receiveCount{0};
    std::atomic<int> closeCount{0};
    std::atomic<int> errorCount{0};

    class FailureListener : public IClientListener {
    public:
        FailureListener(std::atomic<int> &receiveCount, std::atomic<int> &closeCount, std::atomic<int> &errorCount)
            : receiveCount_(receiveCount), closeCount_(closeCount), errorCount_(errorCount)
        {
        }

        void OnReceive(socket_t, std::shared_ptr<DataBuffer>) override { ++receiveCount_; }
        void OnClose(socket_t) override { ++closeCount_; }
        void OnError(socket_t, const std::string &) override { ++errorCount_; }

    private:
        std::atomic<int> &receiveCount_;
        std::atomic<int> &closeCount_;
        std::atomic<int> &errorCount_;
    };

    auto client = TcpClient::Create("127.0.0.1", port);
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
}

TEST(TcpTest, ConnectFailureAllowsRetry)
{
    const uint16_t port = 65433;
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
        void OnReceive(socket_t, std::shared_ptr<DataBuffer>) override {}
        void OnClose(socket_t) override {}
        void OnError(socket_t, const std::string &) override {}
    };

    auto client = TcpClient::Create("127.0.0.1", port);
    auto clientListener = std::make_shared<ClientListener>();
    client->SetListener(clientListener);
    EXPECT_TRUE(client->Init());
    EXPECT_FALSE(client->Connect());

    auto server = TcpServer::Create("0.0.0.0", port);
    auto serverListener = std::make_shared<ServerListener>(accepted);
    server->SetListener(serverListener);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    EXPECT_TRUE(client->Connect());
    EXPECT_TRUE(WaitFor([&] { return accepted.load(); }, 2000));

    client->Close();
    server->Stop();
}

#ifdef __APPLE__
TEST(TcpTest, ConnectRegisterFailureInvalidatesSocket)
{
    const uint16_t port = 12349;

    auto server = TcpServer::Create("127.0.0.1", port);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    auto client = TcpClient::Create("127.0.0.1", port);
    EXPECT_TRUE(client->Init());
    EXPECT_TRUE(client->GetSocketFd() != INVALID_SOCKET);

    auto blocker = std::make_shared<BlockingEventHandler>(client->GetSocketFd());
    EXPECT_TRUE(EventReactor::GetInstance().RegisterHandler(blocker));

    EXPECT_FALSE(client->Connect());
    EXPECT_EQ(INVALID_SOCKET, client->GetSocketFd());

    EXPECT_TRUE(EventReactor::GetInstance().RemoveHandler(blocker->GetHandle()));

    client->Close();
    server->Stop();
}
#endif

TEST(TcpTest, ExplicitCloseNotifiesOnceWithoutError)
{
    const uint16_t port = 12346;
    std::atomic<bool> accepted{false};
    std::atomic<int> closeCount{0};
    std::atomic<int> errorCount{0};

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
        ClientListener(std::atomic<int> &closeCount, std::atomic<int> &errorCount)
            : closeCount_(closeCount), errorCount_(errorCount)
        {
        }

        void OnReceive(socket_t, std::shared_ptr<DataBuffer>) override {}
        void OnClose(socket_t) override { ++closeCount_; }
        void OnError(socket_t, const std::string &) override { ++errorCount_; }

    private:
        std::atomic<int> &closeCount_;
        std::atomic<int> &errorCount_;
    };

    auto server = TcpServer::Create("0.0.0.0", port);
    auto serverListener = std::make_shared<ServerListener>(accepted);
    server->SetListener(serverListener);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    auto client = TcpClient::Create("127.0.0.1", port);
    auto clientListener = std::make_shared<ClientListener>(closeCount, errorCount);
    client->SetListener(clientListener);
    EXPECT_TRUE(client->Init());
    EXPECT_TRUE(client->Connect());
    EXPECT_TRUE(WaitFor([&] { return accepted.load(); }, 2000));

    client->Close();
    client->Close();

    EXPECT_TRUE(WaitFor([&] { return closeCount.load() == 1; }, 2000));
    EXPECT_EQ(1, closeCount.load());
    EXPECT_EQ(0, errorCount.load());
    EXPECT_FALSE(WaitFor([&] { return closeCount.load() > 1 || errorCount.load() > 0; }, 500));
    EXPECT_EQ(INVALID_SOCKET, client->GetSocketFd());

    server->Stop();
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
    EXPECT_EQ(0, SetSendBufferSize(client->GetSocketFd(), send_buffer_size));

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
    auto server_listener = std::make_shared<ServerListener>(server_mutex, stream_buffer, received_messages,
                                                            expected_messages, server_done, server_ok);
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
                oss << "thread=" << thread_index << ";msg=" << message_index << ";payload=abcdefghijklmnopqrstuvwxyz\n";
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

TEST(TcpTest, LargeServerReplySend)
{
    const uint16_t port = 12349;
    const size_t payload_size = 2 * 1024 * 1024;
    const std::string request = "send-large-reply";

    std::string payload(payload_size, '\0');
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>('a' + (i % 19));
    }

    int send_buffer_size = 4096;
    std::atomic<bool> send_ok{false};
    std::atomic<bool> client_done{false};
    std::atomic<bool> client_ok{false};
    std::mutex client_mutex;
    std::string client_data;
    client_data.reserve(payload.size());

    class ServerListener : public IServerListener {
    public:
        ServerListener(const std::string &expectedRequest, const std::string &payload, int sendBufferSize,
                       std::atomic<bool> &sendOk)
            : expectedRequest_(expectedRequest), payload_(payload), sendBufferSize_(sendBufferSize), sendOk_(sendOk)
        {
        }

        void OnAccept(std::shared_ptr<Session> session) override
        {
            int buffer_size = sendBufferSize_;
            (void)SetSendBufferSize(session->NativeHandle(), buffer_size);
        }
        void OnClose(std::shared_ptr<Session>) override {}
        void OnError(std::shared_ptr<Session>, const std::string &) override {}

        void OnReceive(std::shared_ptr<Session> session, std::shared_ptr<DataBuffer> data) override
        {
            if (data->ToString() == expectedRequest_) {
                sendOk_ = session->Send(payload_.data(), payload_.size());
            }
        }

    private:
        const std::string &expectedRequest_;
        const std::string &payload_;
        int sendBufferSize_;
        std::atomic<bool> &sendOk_;
    };

    class ClientListener : public IClientListener {
    public:
        ClientListener(const std::string &expected, std::mutex &mutex, std::string &received, std::atomic<bool> &done,
                       std::atomic<bool> &ok)
            : expected_(expected), mutex_(mutex), received_(received), done_(done), ok_(ok)
        {
        }

        void OnReceive(socket_t, std::shared_ptr<DataBuffer> data) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            received_.append(reinterpret_cast<const char *>(data->Data()), data->Size());
            if (received_.size() >= expected_.size()) {
                ok_ = (received_ == expected_);
                done_ = true;
            }
        }

        void OnClose(socket_t) override {}
        void OnError(socket_t, const std::string &) override {}

    private:
        const std::string &expected_;
        std::mutex &mutex_;
        std::string &received_;
        std::atomic<bool> &done_;
        std::atomic<bool> &ok_;
    };

    auto server = TcpServer::Create("0.0.0.0", port);
    auto server_listener = std::make_shared<ServerListener>(request, payload, send_buffer_size, send_ok);
    server->SetListener(server_listener);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    auto client = TcpClient::Create("127.0.0.1", port);
    auto client_listener = std::make_shared<ClientListener>(payload, client_mutex, client_data, client_done, client_ok);
    client->SetListener(client_listener);
    EXPECT_TRUE(client->Init());
    EXPECT_TRUE(client->Connect());

    EXPECT_TRUE(client->Send(request));
    EXPECT_TRUE(WaitFor([&] { return send_ok.load(); }, 5000));
    EXPECT_TRUE(WaitFor([&] { return client_done.load(); }, 10000));
    EXPECT_TRUE(client_ok.load());

    client->Close();
    server->Stop();
}

TEST(TcpTest, ConcurrentServerSendBurst)
{
    const uint16_t port = 12350;
    const int thread_count = 4;
    const int messages_per_thread = 200;

    std::mutex session_mutex;
    std::shared_ptr<Session> accepted_session;
    std::atomic<bool> accepted{false};
    std::atomic<bool> send_failed{false};
    std::atomic<bool> client_done{false};
    std::atomic<bool> client_ok{false};
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

        void OnReceive(std::shared_ptr<Session>, std::shared_ptr<DataBuffer>) override {}
        void OnClose(std::shared_ptr<Session>) override {}
        void OnError(std::shared_ptr<Session>, const std::string &) override {}

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

        void OnReceive(socket_t, std::shared_ptr<DataBuffer> data) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            streamBuffer_.append(reinterpret_cast<const char *>(data->Data()), data->Size());

            size_t newline_pos = std::string::npos;
            while ((newline_pos = streamBuffer_.find('\n')) != std::string::npos) {
                std::string message = streamBuffer_.substr(0, newline_pos + 1);
                streamBuffer_.erase(0, newline_pos + 1);
                receivedMessages_.insert(message);
            }

            if (receivedMessages_.size() == expectedMessages_.size()) {
                ok_ = (receivedMessages_ == expectedMessages_);
                done_ = true;
            }
        }

        void OnClose(socket_t) override {}
        void OnError(socket_t, const std::string &) override {}

    private:
        std::mutex &mutex_;
        std::string &streamBuffer_;
        std::set<std::string> &receivedMessages_;
        const std::set<std::string> &expectedMessages_;
        std::atomic<bool> &done_;
        std::atomic<bool> &ok_;
    };

    auto server = TcpServer::Create("0.0.0.0", port);
    auto server_listener = std::make_shared<ServerListener>(session_mutex, accepted_session, accepted);
    server->SetListener(server_listener);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    auto client = TcpClient::Create("127.0.0.1", port);
    auto client_listener = std::make_shared<ClientListener>(client_mutex, stream_buffer, received_messages,
                                                            expected_messages, client_done, client_ok);
    client->SetListener(client_listener);
    EXPECT_TRUE(client->Init());
    EXPECT_TRUE(client->Connect());

    EXPECT_TRUE(WaitFor([&] { return accepted.load(); }, 5000));

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
    EXPECT_TRUE(WaitFor([&] { return client_done.load(); }, 10000));
    EXPECT_TRUE(client_ok.load());

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
