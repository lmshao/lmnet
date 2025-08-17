# Unit tests for individual components

# TCP unit tests
add_executable(test_tcp test_tcp.cpp)
target_link_libraries(test_tcp network ${PLATFORM_LIBS})
add_test(NAME TcpTests COMMAND $<TARGET_FILE:test_tcp>)
set_tests_properties(TcpTests PROPERTIES LABELS "unit")

# UDP unit tests
add_executable(test_udp test_udp.cpp)
target_link_libraries(test_udp network ${PLATFORM_LIBS})
add_test(NAME UdpTests COMMAND $<TARGET_FILE:test_udp>)
set_tests_properties(UdpTests PROPERTIES LABELS "unit")

# UNIX socket unit tests (only on Unix-like systems)
if(NOT WIN32)
    add_executable(test_unix test_unix.cpp)
    target_link_libraries(test_unix network ${PLATFORM_LIBS})
    add_test(NAME UnixTests COMMAND $<TARGET_FILE:test_unix>)
    set_tests_properties(UnixTests PROPERTIES LABELS "unit")
endif()