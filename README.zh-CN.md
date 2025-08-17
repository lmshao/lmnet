# Network（网络库）

一个现代 C++ 跨平台异步网络库，具备高性能特性。支持 Linux 和 Windows 平台上的 TCP、UDP 和 UNIX 域套接字，专注于事件驱动编程、资源管理和可扩展的网络应用。适合学习、原型开发和实际服务搭建。

## 特性

- **跨平台支持**：Linux（epoll）和 Windows（IOCP）
- **高性能 I/O**：事件驱动异步 I/O，针对不同平台优化实现
- **全面的套接字支持**：TCP/UDP 客户端与服务端，UNIX 域套接字（Linux）
- **集中式资源管理**：Windows 上统一的 IOCP 管理器，优化资源利用
- **线程池集成**：高效的任务队列与工作线程管理
- **会话（Session）管理**：完善的连接生命周期处理
- **可自定义事件处理器**：灵活的读/写/错误/关闭事件回调
- **资源安全释放**：优雅的退出机制和适当的资源清理
- **生产就绪**：全面的单元测试与真实示例

## 安装

### 克隆仓库和依赖

**推荐方式**：使用 `--recursive` 参数一次性获取所有代码和依赖：

```bash
git clone --recursive https://github.com/lmshao/network.git
```

**或者分步操作**：如果已经克隆了仓库，需要单独下载子模块：

```bash
git clone https://github.com/lmshao/network.git
cd network
git submodule update --init --recursive
```

### Linux

使用 CMake 构建：

```bash
cd network
mkdir build && cd build
cmake ..
make
```

### Windows

**前提条件：**
- Visual Studio 2019 或更高版本（支持 C++17）
- CMake 3.16 或更高版本

**构建步骤：**

```powershell
cd network
mkdir build
cd build
cmake .. -G "Visual Studio 16 2019" -A x64
cmake --build . --config Debug
# 或者
cmake --build . --config Release
```

### 平台特定功能

**Linux**：使用 `epoll` 实现高性能事件驱动 I/O，配合 `eventfd` 实现优雅关闭。

**Windows**：使用 `IOCP`（I/O 完成端口）和集中式管理器实现最优资源利用：
- 所有网络组件共享单一 IOCP 实例
- 可配置工作线程池（默认为 CPU 核心数）
- 自动套接字注册/注销
- 高效的完成事件处理

## 快速开始

创建一个简单的 TCP echo 服务器：

```cpp
#include <network/tcp_server.h>
#include <iostream>
#include <memory>
#include <thread>

class MyListener : public IServerListener {
public:
    void OnError(std::shared_ptr<Session> clientSession, const std::string &errorInfo) override {}
    void OnClose(std::shared_ptr<Session> clientSession) override {}
    void OnAccept(std::shared_ptr<Session> clientSession) override {
        std::cout << "OnAccept: from " << clientSession->ClientInfo() << std::endl;
    }
    void OnReceive(std::shared_ptr<Session> clientSession, std::shared_ptr<DataBuffer> buffer) override {
        if (clientSession->Send(buffer)) {
            std::cout << "send echo data ok." << std::endl;
        }
    }
};

int main(int argc, char **argv) {
    uint16_t port = 7777;
    auto tcp_server = TcpServer::Create("0.0.0.0", port);
    auto listener = std::make_shared<MyListener>();
    tcp_server->SetListener(listener);
    tcp_server->Init();
    tcp_server->Start();
    std::cout << "Listen on port 0.0.0.0:" << port << std::endl;
    while (true) {
        std::this_thread::sleep_for(std::chrono::hours(24));
    }
    return 0;
}
```

更多示例见 [`examples/`](examples/) 目录。

## API 参考

- 详细 API 文档见 [`include/network/`](include/network/) 头文件。
- 主要类：`TcpServer`、`TcpClient`、`UdpServer`、`UdpClient`、`EventReactor`、`Session` 等。

## 测试

构建后运行单元测试：

### Linux

```bash
cd build
ctest
```

### Windows

```powershell
cd build
# 运行所有测试
ctest -C Debug
# 或运行特定测试
.\tests\unit\Debug\test_tcp.exe
.\tests\unit\Debug\test_udp.exe
```

## 开发与调试

### Windows 开发

在 Windows 平台使用 Visual Studio 开发：

1. 打开 Visual Studio
2. 选择"打开本地文件夹"，选择项目根目录
3. Visual Studio 会自动检测 CMake 并配置项目
4. 将启动项目设为测试可执行文件或示例之一
5. 使用 F5 进行调试运行

**调试技巧：**

- 在事件处理器中设置断点（`OnAccept`、`OnReceive` 等）
- 在调试器中监控 IOCP 工作线程
- 使用 Visual Studio 的诊断工具分析性能
- 查看输出窗口中的网络库日志

**Windows 特定注意事项：**

- IOCP 管理器在第一个网络组件启动时自动初始化
- 所有网络组件共享同一个 IOCP 实例以提高效率
- 工作线程数量自动设为 CPU 核心数（可配置）
- 使用 Windows 事件查看器进行系统级网络调试

## 架构

![Network Architecture](https://www.plantuml.com/plantuml/svg/bP9Dxzem38Vl-HJU_xHfWjrxG32s0z9Us02X2VKGMfT6JUF8INZHjE--K52s2LLqFRNruv_TFfawBzc7LiCNz2VYvfFQi-JBZ8jQUInyO1IlV8qjWjLI6I6iB1fvG7ZPE6IOMQjL8RqYym8_1SHhzuC2n_Uv-FKNVS-7-u04hmkxGZuYvp0QJM3tyOZ6VqTPUE9t2ohiPsda8POJFjSg8iFmJPdIX-7_bsxKkJS-Xon-h0bd8AClVnhIW04DFGkaUMlfccy10-R3y9fHbaiYFOnVeNDHx0rEJxVQ1-db-r1Lo9td91t6uya_LZC83jq3a3ps0CbSF_0a8Tb94V2w-xzl7iYavO4KXZm2gqFjWQfd9w9hoW0jQGrl0QzmLNQbhJ8bdLm6Yw_pPQnjYHjak2bn7J9dScvajVGLSGjoxpp7GOgCG5F2THCXfl4sjFet)

<details>
<summary>PlantUML 源码</summary>

```plantuml
@startuml NetworkLibraryArchitecture
!theme plain

package "User Application" {
    [YourApp]
}

package "Network Library" {
    [TcpServer] --> [EventReactor/IocpManager]
    [TcpClient] --> [EventReactor/IocpManager]
    [UdpServer] --> [EventReactor/IocpManager]
    [UdpClient] --> [EventReactor/IocpManager]
    [UnixServer] --> [EventReactor] : Linux only
    [UnixClient] --> [EventReactor] : Linux only
    [EventReactor/IocpManager] --> [TaskQueue]
    [EventReactor/IocpManager] --> [ThreadPool]
    [EventReactor/IocpManager] --> [Session]
    [Session] --> [DataBuffer]
}

[YourApp] ..> [TcpServer] : uses
[YourApp] ..> [TcpClient] : uses
[YourApp] ..> [UdpServer] : uses
[YourApp] ..> [UdpClient] : uses
[YourApp] ..> [UnixServer] : uses (Linux)
[YourApp] ..> [UnixClient] : uses (Linux)

note right of [EventReactor/IocpManager]
    Linux: epoll + eventfd
    Windows: IOCP + Worker Threads
    Cross-platform abstraction
end note

@enduml
```
</details>

## 贡献

欢迎提交 issue 或 pull request 反馈 bug、需求或改进建议。

## 许可证

本项目采用 MIT 许可证。详情请查看 [LICENSE](LICENSE) 文件。
