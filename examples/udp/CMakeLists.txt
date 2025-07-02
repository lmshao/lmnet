# UDP Examples
file(GLOB UDP_EXAMPLES "*.cxx")
foreach(src IN LISTS UDP_EXAMPLES)
    string(REGEX REPLACE "^.*/([^/.]+).cxx" "\\1" target ${src})
    message(STATUS "UDP Example target: " ${target})
    add_executable(${target} ${src})
    target_link_libraries(${target} network)
endforeach(src)
