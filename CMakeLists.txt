cmake_minimum_required (VERSION 3.8)

project (netsim LANGUAGES CXX C)

add_executable (netsim main.cpp sim.cpp router.cpp topology.cpp event.cpp
    queue.cpp pqueue.c)
target_compile_features(netsim PUBLIC cxx_std_14)

set(default_build_type "Debug")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
  set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE
      STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

target_compile_options(netsim PRIVATE -Wall -Wextra -Wno-unused-parameter -fno-omit-frame-pointer)
target_link_libraries(netsim PRIVATE -fno-omit-frame-pointer)

if (CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(netsim PRIVATE -fcolor-diagnostics)
elseif (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(netsim PRIVATE -fdiagnostics-color)
endif()

# target_link_libraries(netsim PRIVATE yaml)

# Comment this out to disable AddressSanitizer.
target_compile_options(netsim PRIVATE -fsanitize=address,leak,undefined)
target_link_libraries(netsim PRIVATE -fsanitize=address,leak,undefined)

set (CMAKE_EXPORT_COMPILE_COMMANDS ON)
