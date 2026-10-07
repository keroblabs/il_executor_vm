cmake_minimum_required(VERSION 3.13)
project(object_handler C)

include(GNUInstallDirs)

set(OBJECT_HANDLER_C_STANDARD 99 CACHE STRING "C standard for ObjectHandler: 99, or 11")
option(OBJECT_HANDLER_SANITIZE "Build ObjectHandler and its tests with ASan and UBSan" OFF)

string(TOUPPER "${CMAKE_BUILD_TYPE}" object_handler_build_type)
if(object_handler_build_type MATCHES "^(RELEASE|RELWITHDEBINFO|MINSIZEREL)$")
    set(OBJ_HANDLER_DEBUG_DEFAULT 0)
else()
    set(OBJ_HANDLER_DEBUG_DEFAULT 1)
endif()

foreach(debug_switch OBJECT_DEBUG_REFS HEAP_DEBUG HEAP_POISON HEAP_VERIFY_EVERY_SAFEPOINT STACK_DEBUG)
    option(OBJECT_HANDLER_${debug_switch} "Enable ${debug_switch} in ObjectHandler"
           ${OBJ_HANDLER_DEBUG_DEFAULT})
    if(OBJECT_HANDLER_${debug_switch})
        set(OBJ_HANDLER_BUILD_${debug_switch} 1)
    else()
        set(OBJ_HANDLER_BUILD_${debug_switch} 0)
    endif()
endforeach()

configure_file(obj_handler_build_config.h.in include/obj_handler_build_config.h @ONLY)

add_library(object_handler STATIC
    obj_handler.c
    objects.c
    heap.c
    gc.c
    stack.c
    array.c
    obj_string.c
    value.c
)
add_library(object_handler::object_handler ALIAS object_handler)

target_include_directories(object_handler PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/object_handler>
)
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

install(TARGETS object_handler EXPORT object_handlerTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES
    obj_handler.h obj_handler_config.h objects_types.h objects.h heap.h gc.h
    stack.h array.h obj_string.h value.h
    ${CMAKE_CURRENT_BINARY_DIR}/include/obj_handler_build_config.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/object_handler)
install(EXPORT object_handlerTargets FILE object_handlerConfig.cmake
    NAMESPACE object_handler:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/object_handler)

# Configured on its own, this directory is the standalone build of doc 14 section 1.1.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../tests/object_handler
                     ${CMAKE_CURRENT_BINARY_DIR}/tests)
endif()
