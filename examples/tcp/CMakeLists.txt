# TCP Examples
file(GLOB TCP_EXAMPLES "*.cxx")
foreach(src IN LISTS TCP_EXAMPLES)
    string(REGEX REPLACE "^.*/([^/.]+).cxx" "\\1" target ${src})
    message(STATUS "TCP Example target: " ${target})
    add_executable(${target} ${src})
    target_link_libraries(${target} network)
endforeach(src)
