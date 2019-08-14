cmake_minimum_required (VERSION 3.8)

project (netsim LANGUAGES CXX)

add_executable (netsim main.cpp router.cpp event.cpp)
target_compile_features(netsim PUBLIC cxx_std_17)
target_compile_options(netsim PRIVATE -Wall -Wextra -fno-omit-frame-pointer
    -fsanitize=address -fcolor-diagnostics)
target_link_libraries(netsim PRIVATE -fno-omit-frame-pointer -fsanitize=address)

set (CMAKE_EXPORT_COMPILE_COMMANDS ON)
