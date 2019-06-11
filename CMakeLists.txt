cmake_minimum_required (VERSION 2.6)
project (cpu)

include_directories ("${PROJECT_BINARY_DIR}")

add_definitions (-std=c++17)
set (CMAKE_BUILD_TYPE Debug)
set (CMAKE_EXPORT_COMPILE_COMMANDS ON)
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fno-omit-frame-pointer -fsanitize=address")
set (CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fno-omit-frame-pointer -fsanitize=address")
# set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fno-omit-frame-pointer")
# set (CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fno-omit-frame-pointer")

# Compiler invocation switch
option (COMPILER_VERBOSE "Print verbose compiler invocation" OFF)

if (COMPILER_VERBOSE)
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -v")
endif ()

# Force colored output for Ninja
if (CMAKE_GENERATOR MATCHES "Ninja")
  if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_definitions (-fcolor-diagnostics)
  elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_definitions (-fdiagnostics-color=always)
  endif ()
endif ()

set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Creates executable target.
add_executable (cpu main.cpp decode.cpp cpu.cpp memory.cpp event.cpp)

if (APPLE)
  # Should be below the add_executable () line to avoid complaining about
  # nonexistent target.
  add_custom_command(TARGET cpu
    POST_BUILD
    COMMAND "dsymutil" $<TARGET_FILE:cpu>)
endif ()
