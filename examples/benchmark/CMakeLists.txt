# TCP Benchmark Tool
add_executable(tcp_benchmark tcp_benchmark.cpp)

set(LMCORE_BENCHMARK_TARGET "")
if(TARGET lmcore::static)
    set(LMCORE_BENCHMARK_TARGET lmcore::static)
elseif(TARGET lmcore-static)
    set(LMCORE_BENCHMARK_TARGET lmcore-static)
elseif(TARGET lmcore)
    set(LMCORE_BENCHMARK_TARGET lmcore)
else()
    message(FATAL_ERROR "tcp_benchmark requires lmcore; build lmcore locally or install it")
endif()

target_link_libraries(tcp_benchmark PRIVATE lmnet_static ${LMCORE_BENCHMARK_TARGET})

if(WIN32)
    target_link_libraries(tcp_benchmark PRIVATE ws2_32)
endif()

