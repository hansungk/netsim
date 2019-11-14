cmake_minimum_required (VERSION 3.8)

project (netsim LANGUAGES C)

add_executable (netsim main.c sim.c router.c event.c queue.c mem.c pqueue.c)
# target_compile_features(netsim PUBLIC c_std_99)

target_compile_options(netsim PRIVATE -Wall -Wextra -Wno-unused-parameter -fno-omit-frame-pointer
    -fsanitize=address,leak,undefined -fcolor-diagnostics)
target_link_libraries(netsim PRIVATE -fno-omit-frame-pointer
    -fsanitize=address,leak,undefined)
# target_compile_options(netsim PRIVATE -Wall -Wextra -Wno-unused-parameter -fno-omit-frame-pointer
#     -fcolor-diagnostics)
# target_link_libraries(netsim PRIVATE -fno-omit-frame-pointer)

set (CMAKE_EXPORT_COMPILE_COMMANDS ON)
