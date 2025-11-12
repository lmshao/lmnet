/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <lmnet/udp_client.h>

#include <iostream>
#include <string>
#include <thread>

using namespace lmshao::lmnet;

bool gExit = false;
class MyListener : public IClientListener {
public:
    void OnReceive(socket_t fd, std::shared_ptr<DataBuffer> buffer) override
    {
        std::cout << "----------" << std::endl;
        std::cout << "pid: " << std::this_thread::get_id() << std::endl;
        std::cout << "OnReceive " << buffer->Size() << " bytes:" << std::endl;
        std::cout << buffer->Data() << std::endl;
        std::cout << "----------" << std::endl;
    }

    void OnClose(socket_t fd) override
    {
        std::cout << "----------" << std::endl;
        std::cout << "pid: " << std::this_thread::get_id() << std::endl;
        std::cout << "Server close connection" << std::endl;
        std::cout << "----------" << std::endl;
        gExit = true;
    }

    void OnError(socket_t fd, const std::string &errorInfo) override
    {
        std::cout << "----------" << std::endl;
        std::cout << "pid: " << std::this_thread::get_id() << std::endl;
        std::cout << "OnError: '" << errorInfo << "'" << std::endl;
        std::cout << "----------" << std::endl;
        gExit = true;
    }
};

int main(int argc, char **argv)
{
    printf("Built at %s on %s.\n", __TIME__, __DATE__);
    // Unified endpoint parser (same as TCP example)
    auto parseEndpoint = [&](std::string &ip, uint16_t &port) -> bool {
        ip = "127.0.0.1";
        port = 0;
        auto usage = [&]() {
            printf("Usage:\n");
            printf("  %s <ip> <port>\n", argv[0]);
            printf("  %s <port>\n", argv[0]);
            printf("  %s [IPv6]:port\n", argv[0]);
            printf("  %s IPv4:port\n", argv[0]);
            printf("Notes:\n");
            printf("  - Link-local IPv6 requires scope id: [fe80::xxxx%%<index>]:port\n");
            printf("    Example: %s [fe80::1ecc:8972:8892:91ff%%12]:7777 (%%12 is interface index)\n", argv[0]);
        };

        if (argc == 2) {
            std::string arg(argv[1]);
            if (!arg.empty() && arg.front() == '[') {
                auto rb = arg.find(']');
                auto colon = arg.find(':', rb != std::string::npos ? rb : 0);
                if (rb == std::string::npos || colon == std::string::npos) {
                    usage();
                    return false;
                }
                ip = arg.substr(1, rb - 1);
                port = static_cast<uint16_t>(std::atoi(arg.substr(colon + 1).c_str()));
                return true;
            }
            auto colon = arg.find(':');
            if (colon != std::string::npos) {
                ip = arg.substr(0, colon);
                port = static_cast<uint16_t>(std::atoi(arg.substr(colon + 1).c_str()));
                return true;
            } else {
                port = static_cast<uint16_t>(std::atoi(arg.c_str()));
                return true;
            }
        } else if (argc == 3) {
            ip = argv[1];
            port = static_cast<uint16_t>(std::atoi(argv[2]));
            return true;
        } else {
            usage();
            return false;
        }
    };

    std::string remoteIp;
    uint16_t remotePort;
    if (!parseEndpoint(remoteIp, remotePort)) {
        return -1;
    }

    auto udpClient = UdpClient::Create(remoteIp, remotePort, "", 0);
    auto listener = std::make_shared<MyListener>();
    udpClient->SetListener(listener);
    bool res = false;
    res = udpClient->Init();
    assert(res);

    printf("----\n");
    char sendbuf[1024];
    printf("Input:\n----------\n");

    while (!gExit && fgets(sendbuf, sizeof(sendbuf), stdin)) {
        if (udpClient->Send(sendbuf, strlen(sendbuf))) {
            std::cout << "Send scuccess, length: " << strlen(sendbuf) << std::endl;
        } else {
            std::cout << "Send error" << std::endl;
        }

        memset(sendbuf, 0, sizeof(sendbuf));
        printf("Input:\n----------\n");
    }

    udpClient->Close();
    return 0;
}