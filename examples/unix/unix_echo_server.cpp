/**
 * @file unix_echo_server.cxx
 * @brief Unix Domain Socket Echo Server Example
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include <unistd.h>

#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "network/iserver_listener.h"
#include "network/session.h"
#include "network/unix_server.h"

using namespace lmshao::network;

class EchoServerListener : public IServerListener {
public:
    void OnAccept(std::shared_ptr<Session> session) override
    {
        std::cout << "----------" << std::endl;
        std::cout << "pid: " << std::this_thread::get_id() << std::endl;
        std::cout << "OnAccept: from " << session->ClientInfo() << std::endl;
        std::cout << "----------" << std::endl;
    }
    void OnReceive(std::shared_ptr<Session> session, std::shared_ptr<DataBuffer> data) override
    {
        std::cout << "----------" << std::endl;
        std::cout << "pid: " << std::this_thread::get_id() << std::endl;
        std::cout << "OnReceive " << data->Size() << " bytes from " << session->ClientInfo() << std::endl;
        std::cout << (char *)data->Data() << std::endl;
        std::cout << "----------" << std::endl;
        if (session->Send(data)) {
            std::cout << "send echo data ok." << std::endl;
        } else {
            std::cout << "send echo data failed." << std::endl;
        }
        std::cout << "----------" << std::endl;
    }
    void OnClose(std::shared_ptr<Session> session) override
    {
        std::cout << "----------" << std::endl;
        std::cout << "pid: " << std::this_thread::get_id() << std::endl;
        std::cout << "OnClose: from " << session->ClientInfo() << std::endl;
        std::cout << "----------" << std::endl;
    }
    void OnError(std::shared_ptr<Session> session, const std::string &reason) override
    {
        std::cout << "----------" << std::endl;
        std::cout << "pid: " << std::this_thread::get_id() << std::endl;
        std::cout << "OnError: '" << reason << "' from " << session->ClientInfo() << std::endl;
        std::cout << "----------" << std::endl;
    }
};

int main()
{
    std::string socketPath = "/tmp/unix_echo.sock";
    auto server = UnixServer::Create(socketPath);
    auto listener = std::make_shared<EchoServerListener>();
    server->SetListener(listener);
    if (!server->Init()) {
        std::cerr << "Server init failed!" << std::endl;
        return 1;
    }
    if (!server->Start()) {
        std::cerr << "Server start failed!" << std::endl;
        return 1;
    }
    std::cout << "UNIX Echo Server started at " << socketPath << std::endl;
    while (true) {
        sleep(1);
    }
    return 0;
}
