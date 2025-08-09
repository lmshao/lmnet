/**
 * @file udp_stream.cxx
 * @brief UDP Stream Example
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include <getopt.h>

#include <cctype>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "network/udp_client.h"
#include "network/udp_server.h"

using namespace lmshao::network;

class StreamServerListener : public IServerListener {
public:
    StreamServerListener()
        : lastReportTime(std::chrono::steady_clock::now()), bytesReceived(0), lastSeq(0), totalPackets(0),
          lostPackets(0), outOfOrderPackets(0)
    {
    }

    void OnAccept(std::shared_ptr<Session> session) override
    {
        printf("[server] Accept: %s\n", session->ClientInfo().c_str());
    }

    void OnReceive(std::shared_ptr<Session> session, std::shared_ptr<DataBuffer> buffer) override
    {
        uint32_t seq = *(uint32_t *)buffer->Data();
        size_t size = buffer->Size();
        bytesReceived += size;
        totalPackets++;
        if (seq != lastSeq + 1 && lastSeq != 0) {
            if (seq > lastSeq + 1)
                lostPackets += (seq - lastSeq - 1);
            else
                outOfOrderPackets++;
        }
        lastSeq = seq;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastReportTime).count();
        if (elapsed >= 1) {
            double mbps = bytesReceived * 8.0 / 1024.0 / 1024.0 / elapsed;
            double lossRate = totalPackets ? (double)lostPackets / (totalPackets + lostPackets) : 0.0;
            double outOfOrderRate = totalPackets ? (double)outOfOrderPackets / totalPackets : 0.0;
            printf("[server] Bandwidth: %.2f Mbps, Loss: %.2f%%, OutOfOrder: %.2f%%\n", mbps, lossRate * 100,
                   outOfOrderRate * 100);
            char statMsg[128];
            snprintf(statMsg, sizeof(statMsg), "[server-stat] Bandwidth: %.2f Mbps, Loss: %.2f%%, OutOfOrder: %.2f%%",
                     mbps, lossRate * 100, outOfOrderRate * 100);
            session->Send((const uint8_t *)statMsg, strlen(statMsg));

            bytesReceived = 0;
            lastReportTime = now;
            totalPackets = 0;
            lostPackets = 0;
            outOfOrderPackets = 0;
        }
    }

    void OnClose(std::shared_ptr<Session> session) override
    {
        printf("[server] Close: %s\n", session->ClientInfo().c_str());
    }

    void OnError(std::shared_ptr<Session> session, const std::string &errorInfo) override
    {
        printf("[server] Error: %s, %s\n", session->ClientInfo().c_str(), errorInfo.c_str());
    }

private:
    std::chrono::steady_clock::time_point lastReportTime;
    size_t bytesReceived;
    uint32_t lastSeq;
    size_t totalPackets;
    size_t lostPackets;
    size_t outOfOrderPackets;
};

class StreamClientListener : public IClientListener {
public:
    void OnReceive(int fd, std::shared_ptr<DataBuffer> buffer) override
    {
        std::string msg = buffer->ToString();
        if (msg.find("[server-stat]") == 0) {
            printf("%s\n", msg.c_str());
        } else {
            printf("[client] Receive: %s\n", msg.c_str());
        }
    }

    void OnClose(int fd) override { printf("[client] Connection closed, fd: %d\n", fd); }

    void OnError(int fd, const std::string &errorInfo) override
    {
        printf("[client] Error: %s, fd: %d\n", errorInfo.c_str(), fd);
    }
};

void PrintUsage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("  -s, --server        Run as server mode\n");
    printf("  -c, --client        Run as client mode\n");
    printf("  -i, --ip <ip>       Specify IP address (default: 127.0.0.1)\n");
    printf("  -p, --port <port>   Specify port number (default: 10621)\n");
    printf("  -z, --size <bytes>  Send packet size (bytes, default: 1024)\n");
    printf("  -b, --bitrate <bps> Send at bitrate (bits per second, default: 2M)\n");
    printf("  -t, --interval <ms> Send batch interval (ms, default: 10)\n");
}

struct ProgramOptions {
    bool isServer = false;
    std::string ip = "127.0.0.1";
    int port = 10621;
    int pktSize = 1024;
    int bitrate = 2 * 1024 * 1024;
    int interval = 10;
    std::string bitrateStr;
};

void ParseArgs(int argc, char *argv[], ProgramOptions &opts)
{
    static struct option long_options[] = {
        {"server", no_argument, 0, 's'},         {"client", no_argument, 0, 'c'},
        {"ip", required_argument, 0, 'i'},       {"port", required_argument, 0, 'p'},
        {"size", required_argument, 0, 'z'},     {"bitrate", required_argument, 0, 'b'},
        {"interval", required_argument, 0, 't'}, {0, 0, 0, 0}};

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "sci:p:z:b:t:", long_options, &option_index)) != -1) {
        switch (opt) {
            case 's':
                opts.isServer = true;
                break;
            case 'c':
                opts.isServer = false;
                break;
            case 'i':
                opts.ip = optarg;
                break;
            case 'p':
                opts.port = std::stoi(optarg);
                break;
            case 'z':
                opts.pktSize = std::stoi(optarg);
                break;
            case 'b':
                opts.bitrateStr = optarg;
                break;
            case 't':
                opts.interval = std::stoi(optarg);
                break;
            default:
                PrintUsage(argv[0]);
                exit(1);
        }
    }

    if (!opts.bitrateStr.empty()) {
        size_t len = opts.bitrateStr.length();
        int factor = 1;
        if (len > 1 && std::isalpha(opts.bitrateStr[len - 1])) {
            char unit = std::toupper(opts.bitrateStr[len - 1]);
            if (unit == 'K' || unit == 'k') {
                factor = 1024;
            } else if (unit == 'M' || unit == 'm') {
                factor = 1024 * 1024;
            } else if (unit == 'G' || unit == 'g') {
                factor = 1024 * 1024 * 1024;
                opts.bitrateStr = opts.bitrateStr.substr(0, len - 1);
            }
            opts.bitrate = std::stoi(opts.bitrateStr) * factor;
        }
    }
}

int main(int argc, char *argv[])
{
    printf("Build time: %s %s\n", __DATE__, __TIME__);
    signal(SIGPIPE, SIG_IGN);
    ProgramOptions opts;
    ParseArgs(argc, argv, opts);

    bool isServer = opts.isServer;
    std::string ip = opts.ip;
    int port = opts.port;
    int pktSize = opts.pktSize;
    int bitrate = opts.bitrate;
    int interval = opts.interval;

    if (pktSize < 4 || pktSize > 1400) {
        printf("Error: packet size must be between 4 and 1400 bytes.\n");
        pktSize = 1024;
        printf("Using default packet size: %d bytes\n", pktSize);
    }

    if (isServer) {
        auto server = UdpServer::Create(ip, port);
        auto listener = std::make_shared<StreamServerListener>();
        server->SetListener(listener);

        if (!server->Init()) {
            printf("Failed to init udp server\n");
            return 1;
        }

        if (!server->Start()) {
            printf("Failed to start udp server\n");
            return 1;
        }
        printf("UDP Stream Server started at %s:%d\n", ip.c_str(), port);
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    } else {
        auto client = UdpClient::Create(ip, port);
        auto listener = std::make_shared<StreamClientListener>();

        client->SetListener(listener);

        if (!client->Init()) {
            printf("Failed to init udp client\n");
            return 1;
        }

        printf("UDP Stream Client started, target %s:%d, packet size: %d\n", ip.c_str(), port, pktSize);
        auto buffer = DataBuffer::Create(pktSize);
        buffer->SetSize(pktSize);
        uint32_t *seq = (uint32_t *)buffer->Data();
        memset(buffer->Data() + sizeof(uint32_t), 'x', pktSize - sizeof(uint32_t));
        *seq = 0;

        size_t bytesSent = 0;
        size_t packetsSent = 0;
        auto lastReportTime = std::chrono::steady_clock::now();
        auto next = std::chrono::steady_clock::now();
        int packetsPerBatch = (int)((bitrate / 8.0 * (interval / 1000.0)) / pktSize);
        if (packetsPerBatch < 1)
            packetsPerBatch = 1;
        while (true) {
            for (int i = 0; i < packetsPerBatch; ++i) {
                client->Send(buffer->Data(), buffer->Size());
                (*seq)++;
                bytesSent += buffer->Size();
                packetsSent++;
            }
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastReportTime).count();
            if (elapsed >= 1) {
                double mbps = bytesSent * 8.0 / 1024.0 / 1024.0 / elapsed;
                printf("[client] Bandwidth: %.2f Mbps, Packets: %zu\n", mbps, packetsSent);
                bytesSent = 0;
                packetsSent = 0;
                lastReportTime = now;
            }
            next += std::chrono::milliseconds(interval);
            std::this_thread::sleep_until(next);
        }
    }
    return 0;
}