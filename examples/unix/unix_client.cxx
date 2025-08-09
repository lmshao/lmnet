/**
 * @file unix_client.cxx
 * @brief Unix Domain Socket Client Example
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "unix_client.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "network/iclient_listener.h"

using namespace lmshao::network;

bool gExit = false;
class EchoClientListener : public IClientListener {
public:
    void OnReceive(int fd, std::shared_ptr<DataBuffer> data) override
    {
        std::string msg((char *)data->Data(), data->Size());
        std::cout << "Received from server: " << msg << std::endl;
    }

    void OnClose(int fd) override
    {
        std::cout << "Connection closed: fd=" << fd << std::endl;
        gExit = true;
    }

    void OnError(int fd, const std::string &reason) override
    {
        std::cout << "Error: fd=" << fd << ", reason=" << reason << std::endl;
        gExit = true;
    }
};

int main()
{
    std::string socketPath = "/tmp/unix_echo.sock";
    auto client = UnixClient::Create(socketPath);
    auto listener = std::make_shared<EchoClientListener>();
    client->SetListener(listener);
    if (!client->Init()) {
        std::cerr << "Client init failed!" << std::endl;
        return 1;
    }
    if (!client->Connect()) {
        std::cerr << "Client connect failed!" << std::endl;
        return 1;
    }

    char sendbuf[1024];
    printf("Input:\n----------\n");
    while (!gExit && fgets(sendbuf, sizeof(sendbuf), stdin)) {
        if (client->Send(sendbuf, strlen(sendbuf))) {
            std::cout << "Send scuccess, length: " << strlen(sendbuf) << std::endl;
        } else {
            std::cout << "Send error" << std::endl;
        }

        memset(sendbuf, 0, sizeof(sendbuf));
        printf("Input:\n----------\n");
    }

    client->Close();
    return 0;
}
