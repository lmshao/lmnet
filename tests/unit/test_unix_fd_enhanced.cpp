/**
 * @file test_unix_fd_enhanced.cpp
 * @brief Enhanced Unix Domain Socket FD Transfer Tests
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
#include <cstdio>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "../test_framework.h"
#include "lmnet/unix_client.h"
#include "lmnet/unix_message.h"
#include "lmnet/unix_server.h"

using namespace lmshao::lmnet;
using namespace lmshao::lmcore;

#if defined(__linux__)

namespace {
// Helper function to create temporary files for testing
std::pair<int, std::string> CreateTestFile(const std::string &content)
{
    char temp_name[] = "/tmp/lmnet_test_XXXXXX";
    int fd = mkstemp(temp_name);
    if (fd >= 0 && !content.empty()) {
        write(fd, content.c_str(), content.length());
        lseek(fd, 0, SEEK_SET); // Reset to beginning
    }
    return {fd, std::string(temp_name)};
}

// Helper function to read file content by FD
std::string ReadFileByFd(int fd)
{
    lseek(fd, 0, SEEK_SET);
    char buffer[1024] = {0};
    ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
    return bytes > 0 ? std::string(buffer, bytes) : "";
}

// Wait helper with timeout
template <typename Condition>
bool WaitFor(Condition condition, int timeout_ms = 2000, int check_interval_ms = 50)
{
    for (int i = 0; i < timeout_ms / check_interval_ms; ++i) {
        if (condition())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
    }
    return false;
}

// Simple client listener for tests
class DummyClientListener : public IClientListener {
public:
    void OnReceive(int, std::shared_ptr<DataBuffer>) override {}
    void OnClose(int) override {}
    void OnError(int, const std::string &) override {}
};
} // namespace

// ========== Test 1: Basic Data-Only Transfer (Backward Compatibility) ==========
TEST(UnixFdEnhanced, BasicDataTransfer)
{
    const std::string socket_path = "/tmp/test_basic_data";
    const std::string test_message = "Hello World!";

    std::atomic<bool> data_received{false};
    std::string received_data;

    class TestServerListener : public IServerListener {
    public:
        TestServerListener(std::atomic<bool> &received, std::string &data) : received_(received), data_(data) {}

        void OnReceive(std::shared_ptr<Session> session, std::shared_ptr<DataBuffer> buffer) override
        {
            data_ = buffer->ToString();
            received_ = true;
        }

        void OnError(std::shared_ptr<Session>, const std::string &) override {}
        void OnClose(std::shared_ptr<Session>) override {}
        void OnAccept(std::shared_ptr<Session>) override {}

    private:
        std::atomic<bool> &received_;
        std::string &data_;
    };

    // Setup server
    std::remove(socket_path.c_str());
    auto server = UnixServer::Create(socket_path);
    auto server_listener = std::make_shared<TestServerListener>(data_received, received_data);
    server->SetListener(server_listener);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    // Setup client
    auto client = UnixClient::Create(socket_path);
    client->SetListener(std::make_shared<DummyClientListener>());
    EXPECT_TRUE(client->Init());
    EXPECT_TRUE(client->Connect());

    // Send data
    EXPECT_TRUE(client->Send(test_message));

    // Verify reception
    EXPECT_TRUE(WaitFor([&] { return data_received.load(); }));
    EXPECT_EQ(test_message, received_data);

    // Cleanup
    client->Close();
    server->Stop();
    std::remove(socket_path.c_str());
}

// ========== Test 2: FD-Only Transfer ==========
TEST(UnixFdEnhanced, FdOnlyTransfer)
{
    const std::string socket_path = "/tmp/test_fd_only";
    const std::string test_content = "File content for FD transfer";

    std::atomic<bool> fd_received{false};
    int received_fd = -1;

    class TestServerListener : public IServerListener {
    public:
        TestServerListener(std::atomic<bool> &received, int &fd) : received_(received), fd_(fd) {}

        void OnReceiveUnixMessage(std::shared_ptr<Session>, const UnixMessage &message) override
        {
            if (message.HasFds()) {
                fd_ = message.fds[0];
                received_ = true;
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

    // Create test file
    auto [test_fd, test_file] = CreateTestFile(test_content);
    EXPECT_GT(test_fd, -1);

    // Setup server
    std::remove(socket_path.c_str());
    auto server = UnixServer::Create(socket_path);
    auto server_listener = std::make_shared<TestServerListener>(fd_received, received_fd);
    server->SetListener(server_listener);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    // Setup client
    auto client = UnixClient::Create(socket_path);
    client->SetListener(std::make_shared<DummyClientListener>());
    EXPECT_TRUE(client->Init());
    EXPECT_TRUE(client->Connect());

    // Send FD only
    EXPECT_TRUE(client->SendFds({test_fd}));

    // Verify FD reception
    EXPECT_TRUE(WaitFor([&] { return fd_received.load(); }));
    EXPECT_GT(received_fd, -1);
    EXPECT_NE(received_fd, test_fd); // Should be different FD (duplicated)

    // Verify FD content
    std::string received_content = ReadFileByFd(received_fd);
    EXPECT_EQ(test_content, received_content);

    // Cleanup
    close(test_fd);
    close(received_fd);
    unlink(test_file.c_str());
    client->Close();
    server->Stop();
    std::remove(socket_path.c_str());
}

// ========== Test 3: Mixed Data + FD Transfer ==========
TEST(UnixFdEnhanced, MixedDataAndFdTransfer)
{
    const std::string socket_path = "/tmp/test_mixed_transfer";
    const std::string protocol_data = "REQUEST_ID=12345,CMD=OPEN_FILE";
    const std::string file_content = "Mixed transfer test content";

    std::atomic<bool> message_received{false};
    std::string received_protocol;
    int received_fd = -1;

    class TestServerListener : public IServerListener {
    public:
        TestServerListener(std::atomic<bool> &received, std::string &protocol, int &fd)
            : received_(received), protocol_(protocol), fd_(fd)
        {
        }

        // Use new unified callback for atomic data+FD handling
        void OnReceiveUnixMessage(std::shared_ptr<Session> session, const UnixMessage &message) override
        {
            printf("[DEBUG MixedTransfer] OnReceiveUnixMessage called - HasData:%d, HasFds:%d\n", message.HasData(),
                   message.HasFds());
            if (message.HasData()) {
                printf("[DEBUG MixedTransfer] Data size: %zu\n", message.data->Size());
            }
            if (message.HasFds()) {
                printf("[DEBUG MixedTransfer] FD count: %zu\n", message.fds.size());
            }
            if (message.HasData() && message.HasFds()) {
                protocol_ = message.data->ToString();
                fd_ = message.fds[0];
                printf("[DEBUG MixedTransfer] Set protocol='%s', fd=%d\n", protocol_.c_str(), fd_);
                received_ = true;
            }
        }

        void OnError(std::shared_ptr<Session>, const std::string &) override {}
        void OnClose(std::shared_ptr<Session>) override {}
        void OnAccept(std::shared_ptr<Session>) override {}
        void OnReceive(std::shared_ptr<Session>, std::shared_ptr<DataBuffer>) override {}

    private:
        std::atomic<bool> &received_;
        std::string &protocol_;
        int &fd_;
    };

    // Create test file
    auto [test_fd, test_file] = CreateTestFile(file_content);
    EXPECT_GT(test_fd, -1);

    // Setup server
    std::remove(socket_path.c_str());
    auto server = UnixServer::Create(socket_path);
    auto server_listener = std::make_shared<TestServerListener>(message_received, received_protocol, received_fd);
    server->SetListener(server_listener);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    // Setup client
    auto client = UnixClient::Create(socket_path);
    client->SetListener(std::make_shared<DummyClientListener>());
    EXPECT_TRUE(client->Init());
    EXPECT_TRUE(client->Connect());

    // Send mixed data + FD using SendWithFds interface
    auto protocol_buffer = DataBuffer::PoolAlloc(protocol_data.length());
    protocol_buffer->Assign(protocol_data.data(), protocol_data.length());
    EXPECT_TRUE(client->SendWithFds(protocol_buffer, {test_fd}));

    // Verify atomic reception
    EXPECT_TRUE(WaitFor([&] { return message_received.load(); }));
    EXPECT_EQ(protocol_data, received_protocol);
    EXPECT_GT(received_fd, -1);
    EXPECT_NE(received_fd, test_fd);

    // Verify FD content matches protocol expectation
    printf("[DEBUG] About to read from received_fd=%d\n", received_fd);
    printf("[DEBUG] Checking if FD is valid with fcntl: %d\n", fcntl(received_fd, F_GETFD));
    std::string received_content = ReadFileByFd(received_fd);
    printf("[DEBUG] Read content: '%s' (length=%zu)\n", received_content.c_str(), received_content.length());
    EXPECT_EQ(file_content, received_content);

    // Cleanup
    close(test_fd);
    close(received_fd);
    unlink(test_file.c_str());
    client->Close();
    server->Stop();
    std::remove(socket_path.c_str());
}

// ========== Test 4: Multiple FDs Transfer ==========
TEST(UnixFdEnhanced, MultipleFdsTransfer)
{
    const std::string socket_path = "/tmp/test_multiple_fds";
    const std::string content1 = "First file content";
    const std::string content2 = "Second file content";
    const std::string content3 = "Third file content";

    std::atomic<bool> fds_received{false};
    std::vector<int> received_fds;

    class TestServerListener : public IServerListener {
    public:
        TestServerListener(std::atomic<bool> &received, std::vector<int> &fds) : received_(received), fds_(fds) {}

        void OnReceiveUnixMessage(std::shared_ptr<Session>, const UnixMessage &message) override
        {
            if (message.HasFds()) {
                fds_ = message.fds;
                received_ = true;
            }
        }

        void OnError(std::shared_ptr<Session>, const std::string &) override {}
        void OnClose(std::shared_ptr<Session>) override {}
        void OnAccept(std::shared_ptr<Session>) override {}
        void OnReceive(std::shared_ptr<Session>, std::shared_ptr<DataBuffer>) override {}

    private:
        std::atomic<bool> &received_;
        std::vector<int> &fds_;
    };

    // Create test files
    auto [fd1, file1] = CreateTestFile(content1);
    auto [fd2, file2] = CreateTestFile(content2);
    auto [fd3, file3] = CreateTestFile(content3);
    EXPECT_GT(fd1, -1);
    EXPECT_GT(fd2, -1);
    EXPECT_GT(fd3, -1);

    // Setup server
    std::remove(socket_path.c_str());
    auto server = UnixServer::Create(socket_path);
    auto server_listener = std::make_shared<TestServerListener>(fds_received, received_fds);
    server->SetListener(server_listener);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    // Setup client
    auto client = UnixClient::Create(socket_path);
    client->SetListener(std::make_shared<DummyClientListener>());
    EXPECT_TRUE(client->Init());
    EXPECT_TRUE(client->Connect());

    // Send multiple FDs
    EXPECT_TRUE(client->SendFds({fd1, fd2, fd3}));

    // Verify reception
    EXPECT_TRUE(WaitFor([&] { return fds_received.load(); }));
    EXPECT_EQ(3, received_fds.size());

    // Verify content of each received FD
    EXPECT_EQ(content1, ReadFileByFd(received_fds[0]));
    EXPECT_EQ(content2, ReadFileByFd(received_fds[1]));
    EXPECT_EQ(content3, ReadFileByFd(received_fds[2]));

    // Cleanup
    close(fd1);
    close(fd2);
    close(fd3);
    for (int fd : received_fds)
        close(fd);
    unlink(file1.c_str());
    unlink(file2.c_str());
    unlink(file3.c_str());
    client->Close();
    server->Stop();
    std::remove(socket_path.c_str());
}

// ========== Test 5: Error Handling - Invalid FDs ==========
TEST(UnixFdEnhanced, ErrorHandlingInvalidFds)
{
    const std::string socket_path = "/tmp/test_invalid_fds";

    std::atomic<bool> connection_established{false};

    class TestServerListener : public IServerListener {
    public:
        TestServerListener(std::atomic<bool> &established) : established_(established) {}

        void OnAccept(std::shared_ptr<Session>) override { established_ = true; }
        void OnError(std::shared_ptr<Session>, const std::string &) override {}
        void OnClose(std::shared_ptr<Session>) override {}
        void OnReceive(std::shared_ptr<Session>, std::shared_ptr<DataBuffer>) override {}

    private:
        std::atomic<bool> &established_;
    };

    // Setup server
    std::remove(socket_path.c_str());
    auto server = UnixServer::Create(socket_path);
    auto server_listener = std::make_shared<TestServerListener>(connection_established);
    server->SetListener(server_listener);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    // Setup client
    auto client = UnixClient::Create(socket_path);
    client->SetListener(std::make_shared<DummyClientListener>());
    EXPECT_TRUE(client->Init());
    EXPECT_TRUE(client->Connect());

    // Wait for connection
    EXPECT_TRUE(WaitFor([&] { return connection_established.load(); }));

    // Try to send invalid FDs - should fail gracefully
    std::vector<int> invalid_fds = {-1, -5, 99999};

    // This should return false but not crash
    bool result = client->SendFds(invalid_fds);
    EXPECT_FALSE(result); // Should fail for invalid FDs

    // Give time for any potential error callbacks
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Cleanup
    client->Close();
    server->Stop();
    std::remove(socket_path.c_str());
}

// ========== Test 6: Backward Compatibility ==========
TEST(UnixFdEnhanced, BackwardCompatibility)
{
    const std::string socket_path = "/tmp/test_backward_compat";
    const std::string test_data = "Compatibility test data";
    const std::string file_content = "Compatibility FD content";

    std::atomic<bool> data_received{false};
    std::atomic<bool> fd_received{false};
    std::atomic<bool> unified_received{false};
    std::string received_data;
    int received_fd = -1;

    class TestServerListener : public IServerListener {
    public:
        TestServerListener(std::atomic<bool> &data_recv, std::atomic<bool> &fd_recv, std::atomic<bool> &unified_recv,
                           std::string &data, int &fd)
            : data_recv_(data_recv), fd_recv_(fd_recv), unified_recv_(unified_recv), data_(data), fd_(fd)
        {
        }

        // Test that all three callbacks are called for mixed message
        void OnReceive(std::shared_ptr<Session>, std::shared_ptr<DataBuffer> buffer) override
        {
            data_ = buffer->ToString();
            data_recv_ = true;
        }

        void OnReceiveUnixMessage(std::shared_ptr<Session>, const UnixMessage &message) override
        {
            // Unified callback handles both data and FDs
            if (message.HasFds() && !message.fds.empty()) {
                fd_ = message.fds[0];
                fd_recv_ = true;
            }
            if (message.HasData() && message.HasFds()) {
                unified_recv_ = true;
            }
        }

        void OnError(std::shared_ptr<Session>, const std::string &) override {}
        void OnClose(std::shared_ptr<Session>) override {}
        void OnAccept(std::shared_ptr<Session>) override {}

    private:
        std::atomic<bool> &data_recv_;
        std::atomic<bool> &fd_recv_;
        std::atomic<bool> &unified_recv_;
        std::string &data_;
        int &fd_;
    };

    // Create test file
    auto [test_fd, test_file] = CreateTestFile(file_content);
    EXPECT_GT(test_fd, -1);

    // Setup server
    std::remove(socket_path.c_str());
    auto server = UnixServer::Create(socket_path);
    auto server_listener =
        std::make_shared<TestServerListener>(data_received, fd_received, unified_received, received_data, received_fd);
    server->SetListener(server_listener);
    EXPECT_TRUE(server->Init());
    EXPECT_TRUE(server->Start());

    // Setup client
    auto client = UnixClient::Create(socket_path);
    client->SetListener(std::make_shared<DummyClientListener>());
    EXPECT_TRUE(client->Init());
    EXPECT_TRUE(client->Connect());

    // Send mixed data + FD
    auto buffer = DataBuffer::PoolAlloc(test_data.length());
    buffer->Assign(test_data.data(), test_data.length());
    EXPECT_TRUE(client->SendWithFds(buffer, {test_fd}));

    // Verify all callbacks are called (backward compatibility)
    EXPECT_TRUE(WaitFor([&] { return data_received.load() && fd_received.load() && unified_received.load(); }));

    EXPECT_EQ(test_data, received_data);
    EXPECT_GT(received_fd, -1);
    EXPECT_EQ(file_content, ReadFileByFd(received_fd));

    // Cleanup
    close(test_fd);
    close(received_fd);
    unlink(test_file.c_str());
    client->Close();
    server->Stop();
    std::remove(socket_path.c_str());
}

#else
// Placeholder tests for non-Linux platforms
TEST(UnixFdEnhanced, BasicDataTransfer)
{
    std::puts("Unix FD transfer tests are only supported on Linux. Skipping test.");
    EXPECT_TRUE(true);
}

TEST(UnixFdEnhanced, FdOnlyTransfer)
{
    std::puts("Unix FD transfer tests are only supported on Linux. Skipping test.");
    EXPECT_TRUE(true);
}

TEST(UnixFdEnhanced, MixedDataAndFdTransfer)
{
    std::puts("Unix FD transfer tests are only supported on Linux. Skipping test.");
    EXPECT_TRUE(true);
}

TEST(UnixFdEnhanced, MultipleFdsTransfer)
{
    std::puts("Unix FD transfer tests are only supported on Linux. Skipping test.");
    EXPECT_TRUE(true);
}

TEST(UnixFdEnhanced, ErrorHandlingInvalidFds)
{
    std::puts("Unix FD transfer tests are only supported on Linux. Skipping test.");
    EXPECT_TRUE(true);
}

TEST(UnixFdEnhanced, BackwardCompatibility)
{
    std::puts("Unix FD transfer tests are only supported on Linux. Skipping test.");
    EXPECT_TRUE(true);
}
#endif

RUN_ALL_TESTS()