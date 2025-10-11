# Lmnet

A modern C++ cross-platform asynchronous lmnet library with high performance. It provides TCP, UDP, and UNIX domain socket support across Linux, macOS, and Windows platforms, focusing on event-driven programming, resource management, and scalable network applications. 

Suitable for learning, prototyping, and building real-world network services.

## Features

- **Cross-platform support**: Linux (epoll/io_uring), macOS (kqueue), and Windows (IOCP)
- **High-performance I/O**: Event-driven asynchronous I/O with platform-optimized implementations
- **Multiple Linux backends**: Choose between traditional epoll or modern io_uring for optimal performance
- **Comprehensive socket support**: TCP/UDP client & server, UNIX domain sockets (Linux/macOS)
- **Centralized resource management**: Unified IOCP manager on Windows for optimal resource utilization
- **Thread pool integration**: Efficient task queue and worker thread management
- **Session management**: Advanced connection lifecycle handling
- **Customizable event handlers**: Flexible callbacks for read/write/error/close events
- **Resource-safe shutdown**: Graceful exit mechanisms with proper resource cleanup
- **Production-ready**: Comprehensive unit tests and real-world examples

## Installation

### Prerequisites

Lmnet depends on the [lmcore library](https://github.com/lmshao/lmcore). You have two options to satisfy this dependency:

#### Option 1: System Installation (Recommended)

Install lmcore to your system first:

```bash
# Clone and install lmcore
git clone https://github.com/lmshao/lmcore.git
cd lmcore
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install  # Install to /usr/local
```

Then build lmnet:

```bash
git clone https://github.com/lmshao/lmnet.git
cd lmnet
mkdir build && cd build
cmake ..
make -j$(nproc)
```

#### Option 2: Local Development Setup

Place lmcore and lmnet in the same parent directory:

```bash
# Directory structure should be:
# parent_directory/
# ├── lmcore/     (clone lmcore here)
# └── lmnet/      (clone lmnet here)

git clone https://github.com/lmshao/lmcore.git
git clone https://github.com/lmshao/lmnet.git

cd lmnet
mkdir build && cd build
cmake ..  # Will automatically find lmcore in sibling directory
make -j$(nproc)
```

### Build Configuration

The CMake build system will automatically:
1. First try to find system-installed lmcore (`find_package(lmcore)`)
2. If not found, look for lmcore in the sibling directory `../lmcore`
3. If neither is found, display helpful error messages with installation instructions

### Linux Backend Selection

Lmnet supports two high-performance I/O backends on Linux:

- **io_uring** (default when available): Linux's modern asynchronous I/O interface for maximum performance
- **epoll** (fallback): Traditional Linux event notification using epoll + eventfd

The build system automatically detects system capabilities:
- If Linux kernel 5.1+ and liburing are available, io_uring is used by default
- Otherwise, falls back to epoll backend

To explicitly force epoll backend:

```bash
cmake .. -DLMNET_LINUX_BACKEND_IOURING=OFF
make -j$(nproc)
```

**io_uring advantages:**
- Zero-copy I/O operations
- Reduced system call overhead  
- Better performance for high-throughput applications
- Completion-based notifications vs event-based

**Requirements for io_uring:**
- Linux kernel 5.1+ (recommended 5.6+)
- liburing library installed

### Linux & macOS

Build with CMake:

```bash
cd lmnet  # After setting up lmcore dependency as described above
mkdir build && cd build
cmake ..
cmake --build . --parallel
```

### Windows

**Prerequisites:**
- Visual Studio 2019 or later (with C++17 support)
- CMake 3.16 or later

**Build steps:**

```powershell
cd lmnet  # After setting up lmcore dependency as described above
mkdir build
cd build
cmake .. -G "Visual Studio 16 2019" -A x64
cmake --build . --config Debug
# or
cmake --build . --config Release
```

### Platform-Specific Features

**Linux**: 

Choose between two high-performance backends (auto-detected):
- **io_uring**: Uses Linux's modern asynchronous I/O interface for maximum performance (default when available)
- **epoll**: Uses `epoll` for event-driven I/O with `eventfd` for graceful shutdown (fallback)

**macOS**: Uses native `kqueue` for efficient event notification:
- Single kqueue-driven reactor shared by all Darwin network components
- Integrates with lmcore task queues for callback execution
- Supports TCP/UDP/Unix domain sockets with consistent APIs

**Windows**: Uses `IOCP` (I/O Completion Ports) with a centralized manager for optimal resource utilization:
- Single IOCP instance shared across all network components
- Configurable worker thread pool (defaults to CPU core count)
- Automatic socket registration/deregistration
- Efficient completion event handling

## Quick Start

Create a simple TCP echo server:

```cpp
#include <lmnet/tcp_server.h>

#include <iostream>
#include <memory>
#include <thread>

class MyListener : public IServerListener {
public:
    void OnError(std::shared_ptr<Session> clientSession, const std::string &errorInfo) override {}
    void OnClose(std::shared_ptr<Session> clientSession) override {}
    void OnAccept(std::shared_ptr<Session> clientSession) override
    {
        std::cout << "OnAccept: from " << clientSession->ClientInfo() << std::endl;
    }
    void OnReceive(std::shared_ptr<Session> clientSession, std::shared_ptr<DataBuffer> buffer) override
    {
        if (clientSession->Send(buffer)) {
            std::cout << "send echo data ok." << std::endl;
        }
    }
};

int main(int argc, char **argv)
{
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

More examples can be found in the [`examples/`](examples/) directory.

## API Reference

- See header files in [`include/lmnet/`](include/lmnet/) for detailed API documentation.
- Key classes: `TcpServer`, `TcpClient`, `UdpServer`, `UdpClient`, `EventReactor`, `Session`, etc.

## Testing

Run unit tests after building:

### Linux Testing

```bash
cd build
ctest
```

### Windows Testing

```powershell
cd build
# Run all tests
ctest -C Debug
# or run specific tests
.\tests\unit\Debug\test_tcp.exe
.\tests\unit\Debug\test_udp.exe
```

## Development & Debugging

### Windows Development

For development on Windows, you can use Visual Studio IDE:

1. Open Visual Studio
2. Select "Open a local folder" and choose the project root directory
3. Visual Studio will automatically detect CMake and configure the project
4. Set startup project to one of the test executables or examples
5. Use F5 to run with debugging

**Debugging Tips:**
- Set breakpoints in your event handlers (`OnAccept`, `OnReceive`, etc.)
- Monitor IOCP worker threads in the debugger
- Use Visual Studio's diagnostic tools to analyze performance
- Check the Output window for lmnet library logs

**Windows-specific considerations:**
- IOCP manager automatically initializes when the first network component starts
- All network components share the same IOCP instance for efficiency
- Worker thread count is automatically set to CPU core count (configurable)
- Use Windows Event Viewer for system-level network debugging

## Architecture

```mermaid
graph TB
    subgraph "User Application"
        YourApp[Your Application]
    end
    
    subgraph "Lmnet Library"
        TcpServer[TCP Server]
        TcpClient[TCP Client]
        UdpServer[UDP Server]
        UdpClient[UDP Client]
        UnixServer[Unix Server]
        UnixClient[Unix Client]
        
        subgraph "I/O Backend Layer"
            subgraph "Linux"
                LinuxEpoll[epoll + eventfd]
                LinuxIoUring[io_uring]
            end
            subgraph "macOS"
                MacOSKqueue[kqueue]
            end
            subgraph "Windows"
                WindowsIOCP[IOCP + Worker Threads]
            end
        end
        
        TaskQueue[Task Queue]
        ThreadPool[Thread Pool]
        Session[Session Management]
        DataBuffer[Data Buffer]
        
        TcpServer --> LinuxEpoll
        TcpClient --> LinuxEpoll
        UdpServer --> LinuxEpoll
        UdpClient --> LinuxEpoll
        UnixServer --> LinuxEpoll
        UnixClient --> LinuxEpoll
        
        TcpServer --> LinuxIoUring
        TcpClient --> LinuxIoUring
        UdpServer --> LinuxIoUring
        UdpClient --> LinuxIoUring
        UnixServer --> LinuxIoUring
        UnixClient --> LinuxIoUring
        
    TcpServer --> MacOSKqueue
    TcpClient --> MacOSKqueue
    UdpServer --> MacOSKqueue
    UdpClient --> MacOSKqueue
    UnixServer --> MacOSKqueue
    UnixClient --> MacOSKqueue

        TcpServer --> WindowsIOCP
        TcpClient --> WindowsIOCP
        UdpServer --> WindowsIOCP
        UdpClient --> WindowsIOCP
        
        LinuxEpoll --> TaskQueue
        LinuxIoUring --> TaskQueue
    MacOSKqueue --> TaskQueue
        WindowsIOCP --> TaskQueue
        
        LinuxEpoll --> ThreadPool
        LinuxIoUring --> ThreadPool
    MacOSKqueue --> ThreadPool
        WindowsIOCP --> ThreadPool
        
        LinuxEpoll --> Session
        LinuxIoUring --> Session
    MacOSKqueue --> Session
        WindowsIOCP --> Session
        
        Session --> DataBuffer
    end
    
    YourApp -.-> TcpServer
    YourApp -.-> TcpClient
    YourApp -.-> UdpServer
    YourApp -.-> UdpClient
    YourApp -.-> UnixServer
    YourApp -.-> UnixClient
    
    classDef userApp fill:#e1f5fe,stroke:#01579b,stroke-width:2px
    classDef networkComp fill:#f3e5f5,stroke:#4a148c,stroke-width:2px
    classDef linuxBackend fill:#e8f5e8,stroke:#1b5e20,stroke-width:3px
    classDef macBackend fill:#e8eaf6,stroke:#283593,stroke-width:3px
    classDef windowsBackend fill:#fff3e0,stroke:#e65100,stroke-width:3px
    classDef coreComp fill:#fce4ec,stroke:#880e4f,stroke-width:2px
    
    class YourApp userApp
    class TcpServer,TcpClient,UdpServer,UdpClient,UnixServer,UnixClient networkComp
    class LinuxEpoll,LinuxIoUring linuxBackend
    class MacOSKqueue macBackend
    class WindowsIOCP windowsBackend
    class TaskQueue,ThreadPool,Session,DataBuffer coreComp
```

**Platform-specific I/O Backends:**
- **Linux**: Choose between **epoll** (traditional) or **io_uring** (modern) - auto-detected
- **macOS**: Native **kqueue** reactor with shared event loop
- **Windows**: **IOCP** (I/O Completion Ports) with Worker Threads
- **Cross-platform**: Unified abstraction layer for seamless development

## Contributing

Contributions are welcome! Please open issues or pull requests for bug reports, feature requests, or improvements.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.