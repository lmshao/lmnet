# UDP Examples
file(GLOB UDP_EXAMPLES "*.cpp")
foreach(src IN LISTS UDP_EXAMPLES)
    string(REGEX REPLACE "^.*/([^/.]+).cpp" "\\1" target ${src})
    message(STATUS "UDP Example target: " ${target})
    add_executable(${target} ${src})
    target_link_libraries(${target} lmnet)
endforeach(src)
