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