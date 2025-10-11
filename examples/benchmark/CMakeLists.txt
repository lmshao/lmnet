# TCP Benchmark Tool
add_executable(tcp_benchmark tcp_benchmark.cpp)
target_link_libraries(tcp_benchmark PRIVATE lmnet_static lmcore-static)

if(WIN32)
    target_link_libraries(tcp_benchmark PRIVATE ws2_32)
endif()

