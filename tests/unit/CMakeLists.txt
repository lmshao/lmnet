# Unit tests for individual components

# TCP unit tests
add_executable(test_tcp test_tcp.cpp)
target_link_libraries(test_tcp lmnet ${PLATFORM_LIBS})
add_test(NAME TcpTests COMMAND $<TARGET_FILE:test_tcp>)
set_tests_properties(TcpTests PROPERTIES LABELS "unit")

# UDP unit tests
add_executable(test_udp test_udp.cpp)
target_link_libraries(test_udp lmnet ${PLATFORM_LIBS})
add_test(NAME UdpTests COMMAND $<TARGET_FILE:test_udp>)
set_tests_properties(UdpTests PROPERTIES LABELS "unit")

# UNIX socket unit tests (only on Unix-like systems)
if(NOT WIN32)
    add_executable(test_unix test_unix.cpp)
    target_link_libraries(test_unix lmnet ${PLATFORM_LIBS})
    add_test(NAME UnixTests COMMAND $<TARGET_FILE:test_unix>)
    set_tests_properties(UnixTests PROPERTIES LABELS "unit")
    
    # Enhanced UNIX FD transfer tests
    add_executable(test_unix_fd_enhanced test_unix_fd_enhanced.cpp)
    target_link_libraries(test_unix_fd_enhanced lmnet ${PLATFORM_LIBS})
    add_test(NAME UnixFdEnhancedTests COMMAND $<TARGET_FILE:test_unix_fd_enhanced>)
    set_tests_properties(UnixFdEnhancedTests PROPERTIES LABELS "unit;fd_transfer")
    
    # Simple UNIX FD transfer tests (for debugging)
    add_executable(test_unix_fd_simple test_unix_fd_simple.cpp)
    target_link_libraries(test_unix_fd_simple lmnet ${PLATFORM_LIBS})
    add_test(NAME UnixFdSimpleTests COMMAND $<TARGET_FILE:test_unix_fd_simple>)
    set_tests_properties(UnixFdSimpleTests PROPERTIES LABELS "unit;fd_transfer;simple")
endif()