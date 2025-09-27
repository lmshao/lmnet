# io_uring 后端开发状态

本文档概述了 `io_uring` 后端实现的当前状态和后续任务。

## 当前状态

`io_uring` 后端目前正在积极开发中，旨在为现有的 `epoll` 后端提供一个高性能的异步替代方案。

1.  **核心组件**:
    *   `IoUringManager`: 管理 `io_uring` 提交和完成队列的中心单例。
    *   TCP 实现 (`tcp_server_impl.cpp`, `tcp_client_impl.cpp`): 初始版本已就绪。正在进行修复编译错误并与核心网络接口对齐的工作。
    *   UDP 实现 (`udp_server_impl.cpp`, `udp_client_impl.cpp`): 初始版本已就绪，类似的调试和对齐工作正在进行中。

2.  **Unix域套接字**:
    *   `unix_server_impl.cpp` 和 `unix_client_impl.cpp` 已添加到 `io_uring` 目录中。
    *   这些文件目前正在从基于 `epoll` 的对应文件中重构，以与 `IoUringManager` 集成。

3.  **主要挑战和正在进行的修复**:
    *   **接口不匹配**: 解决与未能正确 `override` 基类接口（例如 `IUnixServer`, `IClientListener`）的函数签名相关的编译错误。
    *   **回调参数**: 更新监听器回调（`OnAccept`, `OnReceive`, `OnClose`, `OnError`），以使用 `std::shared_ptr<Session>` 而不是原始文件描述符 (`int`)，这是重构后设计的核心要求。
    *   **构建系统**: `CMakeLists.txt` 已更新以包含新的 `io_uring` 源文件，但工厂方法（`Create`）的移除导致了 `unix_server.cpp` 和 `unix_client.cpp` 中的链接错误。

## 下一步计划

1.  **修复 `unix_server_impl` 和 `unix_client_impl`**:
    *   **修正继承**: 包含完整的基类头文件（`unix_server.h`, `unix_client.h`）以解决不完整类型错误和 `override` 问题。
    *   **实现会话管理**: 在 `OnAccept` 中创建 `std::shared_ptr<Session>`，并将其传递给 `IServerListener` 的回调函数，以符合接口定义。
    *   **更新回调**: 确保所有回调（`OnError`, `OnClose`）都使用 `Session` 对象而不是文件描述符。

2.  **修复 `unix_server.cpp` 和 `unix_client.cpp`**:
    *   **移除 `Create` 调用**: 修改构造函数，使用 `std::make_shared<UnixServerImpl>(...)` 或 `std::make_unique<UnixClientImpl>(...)` 来实例化实现，而不是调用已被移除的静态 `Create` 方法。

3.  **完成构建**:
    *   在完成上述修复后，再次运行 `make` 以识别并解决 `io_uring` 后端中任何剩余的编译或链接错误。

4.  **测试**:
    *   一旦构建成功，运行示例和测试，以验证 `io_uring` 后端在 TCP、UDP 和 Unix 域套接字上的功能是否正常。
