cmake_minimum_required(VERSION 3.13)
project(object_handler C)

set(OBJECT_HANDLER_C_STANDARD 99 CACHE STRING "C standard for ObjectHandler: 99, or 11")
option(OBJECT_HANDLER_SANITIZE "Build ObjectHandler and its tests with ASan and UBSan" OFF)

add_library(object_handler STATIC
    obj_handler.c
    objects.c
    heap.c
    gc.c
    array.c
    obj_string.c
    value.c
)

# The library's include path is its own directory only (doc 18 D3).
target_include_directories(object_handler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(object_handler PROPERTIES
    C_STANDARD ${OBJECT_HANDLER_C_STANDARD}
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(object_handler PRIVATE
        -Wall -Wextra -Werror -pedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes)
    if(OBJECT_HANDLER_SANITIZE)
        # GCC's undefined group leaves out float-cast-overflow, which value_convert must never trip.
        target_compile_options(object_handler PUBLIC
            -fsanitize=address,undefined,float-cast-overflow
            -fno-sanitize-recover=undefined,float-cast-overflow -fno-omit-frame-pointer)
        target_link_options(object_handler PUBLIC -fsanitize=address,undefined,float-cast-overflow)
    endif()
endif()

# Configured on its own, this directory is the standalone build of doc 14 section 1.1.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../tests/object_handler
                     ${CMAKE_CURRENT_BINARY_DIR}/tests)
endif()
