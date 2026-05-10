# Lmnet（网络库）

[![English](https://img.shields.io/badge/Language-English-blue)](README.md)
[![简体中文](https://img.shields.io/badge/语言-简体中文-red)](README.zh-CN.md)

一个面向 C++17 的跨平台异步网络库。

Lmnet 提供统一的 TCP、UDP 和 Unix 域套接字 API，并在不同平台使用对应的原生 I/O 模型：

- Linux：epoll 或 io_uring
- macOS：kqueue
- Windows：IOCP

它适合学习、原型开发，以及需要统一跨平台网络接口的应用层组件。

## 概览

- TCP 客户端与服务端 API
- UDP 客户端与服务端 API
- Linux 和 macOS 上的 Unix 域套接字 API
- 由 CMake 选择的平台后端实现
- 基于回调的会话和缓冲区处理方式
- 仓库内包含示例程序和单元测试

## 平台支持

| 能力 | Linux | macOS | Windows | 说明 |
| --- | --- | --- | --- | --- |
| TCP 客户端/服务端 | 是 | 是 | 是 | 使用构建时选中的平台后端 |
| UDP 客户端/服务端 | 是 | 是 | 是 | Datagram API 使用同一套监听接口 |
| Unix 域套接字 | 是 | 是 | 否 | 通过 `UnixClient` 和 `UnixServer` 暴露 |
| Linux 后端选择 | epoll / io_uring | 不适用 | 不适用 | `LMNET_LINUX_BACKEND=AUTO|EPOLL|IOURING` |

## 依赖要求

- 支持 C++17 的编译器
- CMake 3.10 或更高版本
- [lmcore](https://github.com/lmshao/lmcore)
- Linux 上仅在使用 io_uring 后端时需要 `liburing`
- Windows 上建议使用 Visual Studio 2019 或更新版本

## 开始使用

### 1. 提供 `lmcore`

Lmnet 会优先尝试 `find_package(lmcore)`。如果失败，会继续查找同级目录 `../lmcore`。

推荐的本地目录结构：

```text
workspace/
|- lmcore/
`- lmnet/
```

示例：

```bash
git clone https://github.com/lmshao/lmcore.git
git clone https://github.com/lmshao/lmnet.git
```

### 2. 配置并构建

Linux 和 macOS：

```bash
cmake -S . -B build
cmake --build build --parallel
```

Windows：

```powershell
cmake -S . -B build -G "Visual Studio 16 2019" -A x64
cmake --build build --config Debug
```

常用 CMake 选项：

- `-DBUILD_EXAMPLES=OFF`
- `-DBUILD_TESTS=OFF`
- `-DBUILD_STATIC_LIBS=OFF`
- `-DBUILD_SHARED_LIBS=OFF`

### 3. 选择 Linux 后端

Linux 构建支持以下取值：

- `AUTO`：优先使用 io_uring，找不到 `liburing` 时回退到 epoll
- `EPOLL`：强制使用 epoll 后端
- `IOURING`：强制使用 io_uring，若缺少 `liburing` 则配置失败

示例：

```bash
cmake -S . -B build -DLMNET_LINUX_BACKEND=AUTO
cmake -S . -B build -DLMNET_LINUX_BACKEND=EPOLL
cmake -S . -B build -DLMNET_LINUX_BACKEND=IOURING
```

### 4. 安装

Unix-like 平台会生成安装和 CMake 包导出目标：

```bash
cmake --install build
```

Windows 当前不提供 install 或 uninstall 目标，直接使用构建输出即可。

## 快速开始

最小 TCP echo 服务端示例：

```cpp
#include <lmnet/tcp_server.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

using namespace lmshao::lmnet;

class MyListener : public IServerListener {
public:
    void OnAccept(std::shared_ptr<Session> session) override
    {
        std::cout << "accepted: " << session->ClientInfo() << std::endl;
    }

    void OnReceive(std::shared_ptr<Session> session, std::shared_ptr<DataBuffer> buffer) override
    {
        session->Send(buffer);
    }

    void OnClose(std::shared_ptr<Session>) override {}
    void OnError(std::shared_ptr<Session>, const std::string &) override {}
};

int main()
{
    uint16_t port = 7777;
    auto tcp_server = TcpServer::Create("0.0.0.0", port);
    auto listener = std::make_shared<MyListener>();

    tcp_server->SetListener(listener);
    if (!tcp_server->Init() || !tcp_server->Start()) {
        return 1;
    }

    std::cout << "listening on 0.0.0.0:" << port << std::endl;
    while (true) {
        std::this_thread::sleep_for(std::chrono::hours(24));
    }
}
```

## 示例程序

仓库自带的示例位于 [examples](examples)：

- `tcp_echo_server`
- `tcp_client`
- `tcp_benchmark`
- `udp_echo_server`
- `udp_client`
- `udp_stream`
- `unix_echo_server`
- `unix_client`
- `network_utils_demo`

公共头文件位于 [include/lmnet](include/lmnet)。

## 测试

测试默认随构建启用。构建完成后可执行：

Unix-like 平台：

```bash
ctest --test-dir build --output-on-failure
```

### Windows

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

## 仓库结构

```text
include/lmnet/      公共头文件
src/                通用实现
src/platforms/      平台后端
examples/           示例程序
tests/              单元测试
cmake/              包配置与卸载辅助脚本
```

## 贡献

欢迎提交 issue 或 pull request 反馈 bug、需求或改进建议。

## 许可证

本项目采用 MIT 许可证。详情请查看 [LICENSE](LICENSE) 文件。
