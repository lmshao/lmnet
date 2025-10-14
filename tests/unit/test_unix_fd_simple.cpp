/**
 * @file test_unix_fd_simple.cpp
 * @brief Simple Unix Domain Socket FD Transfer Test
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "../test_framework.h"
#include "lmnet/unix_client.h"
#include "lmnet/unix_server.h"

using namespace lmshao::lmnet;
using namespace lmshao::lmcore;

#if defined(__linux__)

namespace {
// Create a temporary file for testing
std::pair<int, std::string> CreateTestFile(const std::string &content)
{
    char temp_name[] = "/tmp/lmnet_simple_XXXXXX";
    int fd = mkstemp(temp_name);
    if (fd >= 0 && !content.empty()) {
        write(fd, content.c_str(), content.length());
        lseek(fd, 0, SEEK_SET);
    }
    return {fd, std::string(temp_name)};
}

// Read file content by FD
std::string ReadFileByFd(int fd)
{
    lseek(fd, 0, SEEK_SET);
    char buffer[1024] = {0};
    ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
    return bytes > 0 ? std::string(buffer, bytes) : "";
}

// Simple wait with timeout
template <typename Condition>
bool WaitFor(Condition condition, int timeout_ms = 1000)
{
    for (int i = 0; i < timeout_ms / 10; ++i) {
        if (condition())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
} // namespace

// ========== Simple FD-Only Transfer Test ==========
TEST(UnixFdSimple, BasicFdTransfer)
{
    const std::string socket_path = "/tmp/test_simple_fd";
    const std::string file_content = "Simple FD test content";

    std::atomic<bool> fd_received{false};
    int received_fd = -1;

    // Server listener that only cares about FDs
    class SimpleServerListener : public IServerListener {
    public:
        SimpleServerListener(std::atomic<bool> &received, int &fd) : received_(received), fd_(fd) {}

        void OnReceiveUnixMessage(std::shared_ptr<Session>, const UnixMessage &message) override
        {
            if (message.HasFds()) {
                fd_ = message.fds[0];
                received_ = true;
                // Close extra fds if any
                for (size_t i = 1; i < message.fds.size(); ++i) {
                    close(message.fds[i]);
                }
            }
        }

        void OnError(std::shared_ptr<Session>, const std::string &) override {}
        void OnClose(std::shared_ptr<Session>) override {}
        void OnAccept(std::shared_ptr<Session>) override {}
        void OnReceive(std::shared_ptr<Session>, std::shared_ptr<DataBuffer>) override {}

    private:
        std::atomic<bool> &received_;
        int &fd_;
    };

    // Minimal client listener
    class SimpleClientListener : public IClientListener {
    public:
        void OnReceive(int, std::shared_ptr<DataBuffer>) override {}
        void OnClose(int) override {}
        void OnError(int, const std::string &) override {}
    };

    // Create test file
    auto [test_fd, test_file] = CreateTestFile(file_content);
    EXPECT_GT(test_fd, -1);

    // Setup server
    std::remove(socket_path.c_str());
    auto server = UnixServer::Create(socket_path);
    auto server_listener = std::make_shared<SimpleServerListener>(fd_received, received_fd);
    server->SetListener(server_listener);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    // Setup client
    auto client = UnixClient::Create(socket_path);
    client->SetListener(std::make_shared<SimpleClientListener>());
    EXPECT_TRUE(client->Init());
    EXPECT_TRUE(client->Connect());

    // Send FD
    EXPECT_TRUE(client->SendFds({test_fd}));

    // Wait for reception
    EXPECT_TRUE(WaitFor([&] { return fd_received.load(); }));
    EXPECT_GT(received_fd, -1);

    // Verify FD content
    std::string received_content = ReadFileByFd(received_fd);
    EXPECT_EQ(file_content, received_content);

    // Cleanup
    close(test_fd);
    close(received_fd);
    unlink(test_file.c_str());

    client->Close();
    server->Stop();
    std::remove(socket_path.c_str());
}

// ========== Mixed Data + FD Transfer Test ==========
TEST(UnixFdSimple, MixedTransfer)
{
    const std::string socket_path = "/tmp/test_simple_mixed";
    const std::string protocol_data = "COMMAND=SEND_FILE";
    const std::string file_content = "Mixed test file content";

    std::atomic<bool> message_received{false};
    std::string received_data;
    int received_fd = -1;

    // Server listener using unified callback
    class MixedServerListener : public IServerListener {
    public:
        MixedServerListener(std::atomic<bool> &received, std::string &data, int &fd)
            : received_(received), data_(data), fd_(fd)
        {
        }

        // Traditional callback for debugging (data only)
        void OnReceive(std::shared_ptr<Session> session, std::shared_ptr<DataBuffer> buffer) override
        {
            printf("[DEBUG] OnReceive called with data: %s\n", buffer->ToString().c_str());
        }

        // Unified callback handles both data and FDs
        void OnReceiveUnixMessage(std::shared_ptr<Session> session, const UnixMessage &message) override
        {
            printf("[DEBUG] OnReceiveUnixMessage called - hasData: %s, hasFds: %s\n",
                   message.HasData() ? "true" : "false", message.HasFds() ? "true" : "false");
            if (message.HasData() && message.HasFds()) {
                data_ = message.data->ToString();
                fd_ = message.fds[0];
                received_ = true;
                printf("[DEBUG] Mixed message processed - data: %s, fd: %d\n", data_.c_str(), fd_);
                // Close extra fds
                for (size_t i = 1; i < message.fds.size(); ++i) {
                    close(message.fds[i]);
                }
            }
        }

        void OnError(std::shared_ptr<Session>, const std::string &) override {}
        void OnClose(std::shared_ptr<Session>) override {}
        void OnAccept(std::shared_ptr<Session>) override {}

    private:
        std::atomic<bool> &received_;
        std::string &data_;
        int &fd_;
    };

    class SimpleClientListener : public IClientListener {
    public:
        void OnReceive(int, std::shared_ptr<DataBuffer>) override {}
        void OnClose(int) override {}
        void OnError(int, const std::string &) override {}
    };

    // Create test file
    auto [test_fd, test_file] = CreateTestFile(file_content);
    EXPECT_GT(test_fd, -1);

    // Setup server
    std::remove(socket_path.c_str());
    auto server = UnixServer::Create(socket_path);
    auto server_listener = std::make_shared<MixedServerListener>(message_received, received_data, received_fd);
    server->SetListener(server_listener);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    // Setup client
    auto client = UnixClient::Create(socket_path);
    client->SetListener(std::make_shared<SimpleClientListener>());
    EXPECT_TRUE(client->Init());
    EXPECT_TRUE(client->Connect());

    // Send mixed data + FD
    auto buffer = DataBuffer::PoolAlloc(protocol_data.length());
    buffer->Assign(protocol_data.data(), protocol_data.length());
    EXPECT_TRUE(client->SendWithFds(buffer, {test_fd}));

    // Wait for reception
    EXPECT_TRUE(WaitFor([&] { return message_received.load(); }, 2000));
    EXPECT_EQ(protocol_data, received_data);
    EXPECT_GT(received_fd, -1);

    // Verify FD content - add debug info
    std::string received_content = ReadFileByFd(received_fd);
    printf("[DEBUG] Expected: '%s', Got: '%s'\n", file_content.c_str(), received_content.c_str());
    EXPECT_EQ(file_content, received_content);

    // Cleanup
    close(test_fd);
    close(received_fd);
    unlink(test_file.c_str());

    client->Close();
    server->Stop();
    std::remove(socket_path.c_str());
}

#else
TEST(UnixFdSimple, BasicFdTransfer)
{
    std::puts("Unix FD transfer tests are only supported on Linux. Skipping test.");
    EXPECT_TRUE(true);
}

TEST(UnixFdSimple, MixedTransfer)
{
    std::puts("Unix FD transfer tests are only supported on Linux. Skipping test.");
    EXPECT_TRUE(true);
}
#endif

RUN_ALL_TESTS()
