/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include <lmnet/common.h>
#include <lmnet/iserver_listener.h>
#include <lmnet/lmnet_logger.h>
#include <lmnet/network_utils.h>
#include <lmnet/udp_server.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace lmshao::lmnet;

class MyListener : public IServerListener {
public:
    void OnError(std::shared_ptr<Session> clientSession, const std::string &errorInfo) override
    {
        std::cout << "----------" << std::endl;
        std::cout << "pid: " << std::this_thread::get_id() << std::endl;
        std::cout << "OnError: '" << errorInfo << "' from " << clientSession->ClientInfo() << std::endl;
        std::cout << "----------" << std::endl;
    }
    void OnClose(std::shared_ptr<Session> clientSession) override
    {
        std::cout << "----------" << std::endl;
        std::cout << "pid: " << std::this_thread::get_id() << std::endl;
        std::cout << "OnClose: from " << clientSession->ClientInfo() << std::endl;
        std::cout << "----------" << std::endl;
    }
    void OnAccept(std::shared_ptr<Session> clientSession) override {}
    void OnReceive(std::shared_ptr<Session> clientSession, std::shared_ptr<DataBuffer> buffer) override
    {
        std::cout << "----------" << std::endl;
        std::cout << "pid: " << std::this_thread::get_id() << std::endl;
        std::cout << "OnReceive " << buffer->Size() << " bytes from " << clientSession->ClientInfo() << std::endl;
        std::cout << buffer->Data() << std::endl;
        std::cout << "----------" << std::endl;
        if (clientSession->Send(buffer)) {
            std::cout << "send echo data ok." << std::endl;
        } else {
            std::cout << "send echo data failed." << std::endl;
        }
        std::cout << "." << std::endl;
    }
};

static bool IsIPv6Address(const std::string &ip)
{
    return ip.find(':') != std::string::npos;
}

static bool IsAnyAddress(const std::string &ip)
{
    return ip == "::" || ip == "0.0.0.0";
}

static std::vector<std::string> GetAllLocalIPs()
{
    std::vector<std::string> ips;
    std::set<std::string> unique;
    auto interfaces = NetworkUtils::GetAllInterfaces();
    for (const auto &iface : interfaces) {
        if (!iface.isUp) {
            continue;
        }
        if (!iface.ipv4.empty()) {
            if (unique.insert(iface.ipv4).second) {
                ips.emplace_back(iface.ipv4);
            }
        }
        if (!iface.ipv6.empty()) {
            if (unique.insert(iface.ipv6).second) {
                ips.emplace_back(iface.ipv6);
            }
        }
    }
    return ips;
}

int main(int argc, char **argv)
{
    printf("Built at %s on %s.\n", __TIME__, __DATE__);
    lmshao::lmnet::InitLmnetLogger(lmshao::lmcore::LogLevel::kInfo);
    std::string ip = "0.0.0.0";
    uint16_t port = 7777;
    bool ip_specified = false;
    if (argc == 2) {
        // Only port provided
        port = static_cast<uint16_t>(strtoul(argv[1], nullptr, 10));
    } else if (argc >= 3) {
        // ip + port provided
        ip = argv[1];
        port = static_cast<uint16_t>(strtoul(argv[2], nullptr, 10));
        ip_specified = true;
    } else {
        printf("Usage:\n  %s [ip] <port>\n  ip: default 0.0.0.0, use '::' for IPv6\n  port: default 7777\n", argv[0]);
    }

    auto udp_server = UdpServer::Create(ip, port);
    auto listener = std::make_shared<MyListener>();
    udp_server->SetListener(listener);
    udp_server->Init();
    udp_server->Start();
    if (ip_specified && !IsAnyAddress(ip)) {
        if (IsIPv6Address(ip)) {
            printf("Listen on [%s]:%d\n", ip.c_str(), port);
        } else {
            printf("Listen on %s:%d\n", ip.c_str(), port);
        }
    } else {
        // When binding to any-address (0.0.0.0 or ::) or no IP specified, enumerate all interfaces
        if (ip_specified && IsAnyAddress(ip)) {
            if (IsIPv6Address(ip)) {
                printf("Listen on [%s]:%d\n", ip.c_str(), port);
            } else {
                printf("Listen on %s:%d\n", ip.c_str(), port);
            }
        }
        printf("Listen on all interfaces, port %d\n", port);
        auto all_ips = GetAllLocalIPs();
        for (const auto &addr : all_ips) {
            if (IsIPv6Address(addr)) {
                printf(" - [%s]:%d\n", addr.c_str(), port);
            } else {
                printf(" - %s:%d\n", addr.c_str(), port);
            }
        }
    }

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
    printf("Exit...\n");
    return 0;
}