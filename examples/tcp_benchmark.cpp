
/**
 * TCP Performance Benchmark Tool
 *
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 */

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "lmnet/tcp_client.h"
#include "lmnet/tcp_server.h"

using namespace lmshao::lmnet;

// Benchmark configuration
struct BenchmarkConfig {
    int num_clients = 1;       // Number of concurrent clients
    int message_size = 1024;   // Message size in bytes
    int duration_seconds = 10; // Test duration in seconds
    int num_messages = 0;      // Total messages to send (0 = continuous)
    std::string server_ip = "127.0.0.1";
    uint16_t server_port = 8888;
    bool latency_test = false; // Enable latency measurement
};

// Benchmark statistics
struct BenchmarkStats {
    std::atomic<uint64_t> messages_sent{0};
    std::atomic<uint64_t> messages_received{0};
    std::atomic<uint64_t> bytes_sent{0};
    std::atomic<uint64_t> bytes_received{0};
    std::atomic<uint64_t> total_latency_us{0}; // Total latency in microseconds
    std::atomic<uint64_t> min_latency_us{UINT64_MAX};
    std::atomic<uint64_t> max_latency_us{0};

    void UpdateLatency(uint64_t latency_us)
    {
        total_latency_us += latency_us;

        uint64_t current_min = min_latency_us.load();
        while (latency_us < current_min && !min_latency_us.compare_exchange_weak(current_min, latency_us))
            ;

        uint64_t current_max = max_latency_us.load();
        while (latency_us > current_max && !max_latency_us.compare_exchange_weak(current_max, latency_us))
            ;
    }

    void Print(double elapsed_seconds)
    {
        std::cout << "\n============== Performance Statistics ==============\n";
        std::cout << "Duration: " << elapsed_seconds << " seconds\n";
        std::cout << "Messages Sent: " << messages_sent << "\n";
        std::cout << "Messages Received: " << messages_received << "\n";
        std::cout << "Bytes Sent: " << bytes_sent << " (" << (bytes_sent / 1024.0 / 1024.0) << " MB)\n";
        std::cout << "Bytes Received: " << bytes_received << " (" << (bytes_received / 1024.0 / 1024.0) << " MB)\n";
        std::cout << "\n--- Throughput ---\n";
        std::cout << "Messages/sec: " << (messages_received / elapsed_seconds) << "\n";
        std::cout << "Throughput (MB/s): " << (bytes_received / elapsed_seconds / 1024.0 / 1024.0) << "\n";

        if (messages_received > 0 && total_latency_us > 0) {
            std::cout << "\n--- Latency ---\n";
            std::cout << "Average Latency: " << (total_latency_us / messages_received) << " us\n";
            std::cout << "Min Latency: " << min_latency_us << " us\n";
            std::cout << "Max Latency: " << max_latency_us << " us\n";
        }
        std::cout << "====================================================\n";
    }
};

// Echo server implementation
class EchoServerListener : public IServerListener {
public:
    void OnAccept(std::shared_ptr<Session> session) override
    {
        std::cout << "Client connected: " << session->host << ":" << session->port << "\n";
    }

    void OnReceive(std::shared_ptr<Session> session, std::shared_ptr<DataBuffer> buffer) override
    {
        // Echo back
        session->Send(buffer);
    }

    void OnClose(std::shared_ptr<Session> session) override
    {
        std::cout << "Client disconnected: " << session->host << ":" << session->port << "\n";
    }

    void OnError(std::shared_ptr<Session> session, const std::string &errorInfo) override
    {
        std::cerr << "Session error " << session->host << ":" << session->port << " - " << errorInfo << "\n";
    }
};

// Benchmark client implementation
class BenchmarkClient : public IClientListener, public std::enable_shared_from_this<BenchmarkClient> {
public:
    BenchmarkClient(const BenchmarkConfig &config, BenchmarkStats &stats, int client_id)
        : config_(config), stats_(stats), client_id_(client_id)
    {
        test_data_.resize(config_.message_size, 'A' + (client_id % 26));
    }

    bool Start()
    {
        client_ = TcpClient::Create(config_.server_ip, config_.server_port);
        if (!client_) {
            std::cerr << "Client " << client_id_ << ": Failed to create client\n";
            return false;
        }

        client_->SetListener(shared_from_this());

        if (!client_->Init()) {
            std::cerr << "Client " << client_id_ << ": Failed to initialize\n";
            return false;
        }

        if (!client_->Connect()) {
            std::cerr << "Client " << client_id_ << ": Failed to connect\n";
            return false;
        }

        return true;
    }

    void OnReceive(socket_t fd, std::shared_ptr<DataBuffer> buffer) override
    {
        stats_.messages_received++;
        stats_.bytes_received += buffer->Size();

        if (config_.latency_test && buffer->Size() >= sizeof(uint64_t)) {
            // Extract timestamp from message
            uint64_t send_time;
            std::memcpy(&send_time, buffer->Data(), sizeof(uint64_t));
            auto now = std::chrono::steady_clock::now();
            auto recv_time = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
            stats_.UpdateLatency(recv_time - send_time);
        }

        // Continue sending if test is running
        if (is_running_) {
            SendMessage();
        }
    }

    void OnClose(socket_t fd) override
    {
        std::cout << "Client " << client_id_ << " closed\n";
        is_running_ = false;
    }

    void OnError(socket_t fd, const std::string &errorInfo) override
    {
        std::cerr << "Client " << client_id_ << " error: " << errorInfo << "\n";
    }

    void SendMessage()
    {
        if (!is_running_ || !client_) {
            return;
        }

        if (config_.latency_test) {
            // Add timestamp to message for latency measurement
            auto now = std::chrono::steady_clock::now();
            uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
            std::memcpy(test_data_.data(), &timestamp, sizeof(uint64_t));
        }

        if (client_->Send(test_data_.data(), test_data_.size())) {
            stats_.messages_sent++;
            stats_.bytes_sent += test_data_.size();
        }
    }

    void Run(int duration_seconds)
    {
        is_running_ = true;

        // Start sending
        for (int i = 0; i < 10; ++i) { // Send initial burst
            SendMessage();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Wait for duration
        std::this_thread::sleep_for(std::chrono::seconds(duration_seconds));
        is_running_ = false;
    }

    void Stop()
    {
        is_running_ = false;
        if (client_) {
            client_->Close();
        }
    }

private:
    const BenchmarkConfig &config_;
    BenchmarkStats &stats_;
    int client_id_;
    std::shared_ptr<TcpClient> client_;
    std::vector<char> test_data_;
    std::atomic<bool> is_running_{false};
};

void PrintUsage(const char *program_name)
{
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "  TCP Performance Benchmark Tool\n";
    std::cout << "  Version: 1.0.0\n";
    std::cout << "  Author: SHAO Liming <lmshao@163.com>\n";
    std::cout << "============================================================\n\n";

    std::cout << "Usage: " << program_name << " <mode> [options]\n\n";

    std::cout << "Modes:\n";
    std::cout << "  --server      Run as echo server\n";
    std::cout << "  --client      Run as benchmark client (default if other options provided)\n";
    std::cout << "  --help        Show this help message\n\n";

    std::cout << "Server Options:\n";
    std::cout << "  -p <port>     Server listening port (default: 8888)\n\n";

    std::cout << "Client Options:\n";
    std::cout << "  -c <num>      Number of concurrent clients (default: 1)\n";
    std::cout << "  -s <size>     Message size in bytes (default: 1024)\n";
    std::cout << "  -t <seconds>  Test duration in seconds (default: 10)\n";
    std::cout << "  -h <host>     Server host address (default: 127.0.0.1)\n";
    std::cout << "  -p <port>     Server port (default: 8888)\n";
    std::cout << "  -l            Enable latency measurement\n\n";

    std::cout << "Common Examples:\n\n";
    std::cout << "  1. Start Echo Server\n";
    std::cout << "     " << program_name << " --server -p 8888\n\n";

    std::cout << "  2. Basic Throughput Test (single client)\n";
    std::cout << "     " << program_name << " -c 1 -s 1024 -t 10\n\n";

    std::cout << "  3. Concurrent Test (10 clients)\n";
    std::cout << "     " << program_name << " -c 10 -s 1024 -t 30\n\n";

    std::cout << "  4. Latency Test\n";
    std::cout << "     " << program_name << " -c 1 -s 256 -t 10 -l\n\n";

    std::cout << "  5. High Concurrency Test (100 clients)\n";
    std::cout << "     " << program_name << " -c 100 -s 1024 -t 60\n\n";

    std::cout << "  6. Large Packet Test (64KB)\n";
    std::cout << "     " << program_name << " -c 10 -s 65536 -t 30\n\n";

    std::cout << "  7. Connect to Remote Server\n";
    std::cout << "     " << program_name << " -c 10 -h 192.168.1.100 -p 8888 -t 30\n\n";

    std::cout << "Performance Metrics:\n";
    std::cout << "  - Messages/sec    : Message throughput rate\n";
    std::cout << "  - Throughput MB/s : Data transfer bandwidth\n";
    std::cout << "  - Latency (us)    : Round-trip time (with -l option)\n\n";

    std::cout << "Notes:\n";
    std::cout << "  * Use Ctrl+C to stop the server or client\n";
    std::cout << "  * For best performance, use Release build\n";
    std::cout << "  * Server must be started before running client tests\n\n";
}

int main(int argc, char *argv[])
{
    // Show help if no arguments provided
    if (argc == 1) {
        PrintUsage(argv[0]);
        return 0;
    }

    BenchmarkConfig config;
    bool run_server = false;
    bool run_client = false;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            PrintUsage(argv[0]);
            return 0;
        } else if (arg == "--server") {
            run_server = true;
        } else if (arg == "--client") {
            run_client = true;
        } else if (arg == "-c" && i + 1 < argc) {
            config.num_clients = std::atoi(argv[++i]);
            run_client = true; // Implicitly enable client mode
        } else if (arg == "-s" && i + 1 < argc) {
            config.message_size = std::atoi(argv[++i]);
            run_client = true;
        } else if (arg == "-t" && i + 1 < argc) {
            config.duration_seconds = std::atoi(argv[++i]);
            run_client = true;
        } else if (arg == "-h" && i + 1 < argc) {
            config.server_ip = argv[++i];
            run_client = true;
        } else if (arg == "-p" && i + 1 < argc) {
            config.server_port = std::atoi(argv[++i]);
        } else if (arg == "-l") {
            config.latency_test = true;
            run_client = true;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            PrintUsage(argv[0]);
            return 1;
        }
    }

    // Check mode
    if (run_server && run_client) {
        std::cerr << "Error: Cannot run as both server and client\n\n";
        PrintUsage(argv[0]);
        return 1;
    }

    if (!run_server && !run_client) {
        std::cerr << "Error: Must specify either --server or client options\n\n";
        PrintUsage(argv[0]);
        return 1;
    }

    if (run_server) {
        // Run echo server
        std::cout << "Starting echo server on port " << config.server_port << "...\n";

        auto server = TcpServer::Create(config.server_port);
        auto listener = std::make_shared<EchoServerListener>();
        server->SetListener(listener);

        if (!server->Init() || !server->Start()) {
            std::cerr << "Failed to start server\n";
            return 1;
        }

        std::cout << "Server started. Press Ctrl+C to stop.\n";

        // Wait for Ctrl+C
        std::this_thread::sleep_for(std::chrono::hours(24));

    } else {
        // Run benchmark clients
        std::cout << "========== TCP Benchmark ==========\n";
        std::cout << "Clients: " << config.num_clients << "\n";
        std::cout << "Message Size: " << config.message_size << " bytes\n";
        std::cout << "Duration: " << config.duration_seconds << " seconds\n";
        std::cout << "Server: " << config.server_ip << ":" << config.server_port << "\n";
        std::cout << "Latency Test: " << (config.latency_test ? "Yes" : "No") << "\n";
        std::cout << "===================================\n\n";

        BenchmarkStats stats;
        std::vector<std::shared_ptr<BenchmarkClient>> clients;
        std::vector<std::thread> threads;

        // Create and start clients
        for (int i = 0; i < config.num_clients; ++i) {
            auto client = std::make_shared<BenchmarkClient>(config, stats, i);
            if (!client->Start()) {
                std::cerr << "Failed to start client " << i << "\n";
                continue;
            }
            clients.push_back(client);
        }

        std::cout << "Connected " << clients.size() << " clients\n";
        std::cout << "Running benchmark...\n";

        auto start_time = std::chrono::steady_clock::now();

        // Start benchmark
        for (auto &client : clients) {
            threads.emplace_back([&client, &config]() { client->Run(config.duration_seconds); });
        }

        // Wait for completion
        for (auto &thread : threads) {
            thread.join();
        }

        auto end_time = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(end_time - start_time).count();

        // Stop all clients
        for (auto &client : clients) {
            client->Stop();
        }

        // Print statistics
        stats.Print(elapsed);
    }

    return 0;
}
