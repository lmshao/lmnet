# Lmnet

[![English](https://img.shields.io/badge/Language-English-blue)](README.md)
[![简体中文](https://img.shields.io/badge/语言-简体中文-red)](README.zh-CN.md)

Cross-platform asynchronous networking library for C++17.

Lmnet provides a unified API for TCP, UDP, and Unix domain socket communication while using the native I/O model of each platform:

- Linux: epoll or io_uring
- macOS: kqueue
- Windows: IOCP

It is intended for learning, prototypes, and application-level network components that need the same API across platforms.

## Overview

- TCP client and server APIs
- UDP client and server APIs
- Unix domain socket client and server APIs on Linux and macOS
- Platform-specific backends selected by CMake
- Callback-based session and buffer handling
- Examples and unit tests in the repository

## Platform Support

| Capability | Linux | macOS | Windows | Notes |
| --- | --- | --- | --- | --- |
| TCP client/server | Yes | Yes | Yes | Uses the platform backend selected at build time |
| UDP client/server | Yes | Yes | Yes | Datagram APIs share the same listener model |
| Unix domain sockets | Yes | Yes | No | Exposed through `UnixClient` and `UnixServer` |
| Linux backend selection | epoll / io_uring | N/A | N/A | `LMNET_LINUX_BACKEND=AUTO|EPOLL|IOURING` |

## Requirements

- C++17 compiler
- CMake 3.10 or later
- [lmcore](https://github.com/lmshao/lmcore)
- On Linux, `liburing` is required only when building with the io_uring backend
- On Windows, Visual Studio 2019 or later is recommended

## Getting Started

### 1. Provide `lmcore`

Lmnet first tries `find_package(lmcore)`. If that fails, it looks for a sibling directory named `../lmcore`.

Recommended local layout:

```text
workspace/
|- lmcore/
`- lmnet/
```

Example:

```bash
git clone https://github.com/lmshao/lmcore.git
git clone https://github.com/lmshao/lmnet.git
```

### 2. Configure and build

Linux and macOS:

```bash
cmake -S . -B build
cmake --build build --parallel
```

Windows:

```powershell
cmake -S . -B build -G "Visual Studio 16 2019" -A x64
cmake --build build --config Debug
```

Useful CMake options:

- `-DBUILD_EXAMPLES=OFF`
- `-DBUILD_TESTS=OFF`
- `-DBUILD_STATIC_LIBS=OFF`
- `-DBUILD_SHARED_LIBS=OFF`

### 3. Select the Linux backend

Linux builds accept these values:

- `AUTO`: prefer io_uring when `liburing` is available, otherwise fall back to epoll
- `EPOLL`: force the epoll backend
- `IOURING`: require io_uring and fail configuration if `liburing` is unavailable

Examples:

```bash
cmake -S . -B build -DLMNET_LINUX_BACKEND=AUTO
cmake -S . -B build -DLMNET_LINUX_BACKEND=EPOLL
cmake -S . -B build -DLMNET_LINUX_BACKEND=IOURING
```

### 4. Install

Install and CMake package export targets are generated on Unix-like platforms:

```bash
cmake --install build
```

Windows does not currently provide install or uninstall targets; use the build output directly.

## Quick Start

Minimal TCP echo server:

```cpp
#include <chrono>
#include <lmnet/tcp_server.h>

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

## Examples

The repository includes runnable examples in [examples](examples):

- `tcp_echo_server`
- `tcp_client`
- `tcp_benchmark`
- `udp_echo_server`
- `udp_client`
- `udp_stream`
- `unix_echo_server`
- `unix_client`
- `network_utils_demo`

The public headers are under [include/lmnet](include/lmnet).

## Testing

Builds enable tests by default. Run them after building:

Unix-like platforms:

```bash
ctest --test-dir build --output-on-failure
```

Windows:

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

## Repository Layout

```text
include/lmnet/      Public headers
src/                Common implementation
src/platforms/      Platform backends
examples/           Sample programs
tests/              Unit tests
cmake/              Package config and uninstall helpers
```

## Contributing

Contributions are welcome! Please open issues or pull requests for bug reports, feature requests, or improvements.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.