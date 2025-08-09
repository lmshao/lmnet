# Unit tests for individual components

# DataBuffer unit tests
add_executable(test_data_buffer test_data_buffer.cpp)
target_link_libraries(test_data_buffer network)
add_test(NAME DataBufferTests COMMAND $<TARGET_FILE:test_data_buffer>)
set_tests_properties(DataBufferTests PROPERTIES LABELS "unit")

# TaskQueue unit tests
add_executable(test_task_queue test_task_queue.cpp)
target_link_libraries(test_task_queue network)
add_test(NAME TaskQueueTests COMMAND $<TARGET_FILE:test_task_queue>)
set_tests_properties(TaskQueueTests PROPERTIES LABELS "unit")

# Log unit tests
add_executable(test_log test_log.cpp)
target_link_libraries(test_log network)
add_test(NAME LogTests COMMAND $<TARGET_FILE:test_log>)
set_tests_properties(LogTests PROPERTIES LABELS "unit")

# ThreadPool unit tests
add_executable(test_thread_pool test_thread_pool.cpp)
target_link_libraries(test_thread_pool network pthread)
add_test(NAME ThreadPoolTests COMMAND $<TARGET_FILE:test_thread_pool>)
set_tests_properties(ThreadPoolTests PROPERTIES LABELS "unit")

# TCP unit tests
add_executable(test_tcp test_tcp.cpp)
target_link_libraries(test_tcp network pthread)
add_test(NAME TcpTests COMMAND $<TARGET_FILE:test_tcp>)
set_tests_properties(TcpTests PROPERTIES LABELS "unit")

# UDP unit tests
add_executable(test_udp test_udp.cpp)
target_link_libraries(test_udp network pthread)
add_test(NAME UdpTests COMMAND $<TARGET_FILE:test_udp>)
set_tests_properties(UdpTests PROPERTIES LABELS "unit")

# UNIX socket unit tests (only on Unix-like systems)
if(NOT WIN32)
    add_executable(test_unix test_unix.cpp)
    target_link_libraries(test_unix network pthread)
    add_test(NAME UnixTests COMMAND $<TARGET_FILE:test_unix>)
    set_tests_properties(UnixTests PROPERTIES LABELS "unit")
endif()