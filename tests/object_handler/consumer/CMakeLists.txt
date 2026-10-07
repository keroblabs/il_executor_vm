cmake_minimum_required(VERSION 3.13)
project(object_handler_consumer C)

find_package(object_handler CONFIG REQUIRED)
add_executable(object_handler_consumer ../test_objects.c ../fake_embedder.c ../harness.c)
target_link_libraries(object_handler_consumer PRIVATE object_handler::object_handler)
set_target_properties(object_handler_consumer PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(object_handler_consumer PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/configuration_probe.c"
     "#include \"obj_handler.h\"\nint main(void) { return 0; }\n")
foreach(debug_switch OBJECT_DEBUG_REFS HEAP_DEBUG HEAP_POISON HEAP_VERIFY_EVERY_SAFEPOINT STACK_DEBUG)
    try_compile(matching "${CMAKE_CURRENT_BINARY_DIR}/matching_${debug_switch}"
        "${CMAKE_CURRENT_BINARY_DIR}/configuration_probe.c"
        COMPILE_DEFINITIONS "-D${debug_switch}=OBJ_HANDLER_BUILD_${debug_switch}"
        LINK_LIBRARIES object_handler::object_handler OUTPUT_VARIABLE diagnostics)
    if(NOT matching)
        message(FATAL_ERROR "Matching ${debug_switch} override failed:\n${diagnostics}")
    endif()
    try_compile(conflicting "${CMAKE_CURRENT_BINARY_DIR}/conflicting_${debug_switch}"
        "${CMAKE_CURRENT_BINARY_DIR}/configuration_probe.c"
        COMPILE_DEFINITIONS "-D${debug_switch}=(1 - OBJ_HANDLER_BUILD_${debug_switch})"
        LINK_LIBRARIES object_handler::object_handler OUTPUT_VARIABLE diagnostics)
    string(FIND "${diagnostics}"
           "${debug_switch} conflicts with the ObjectHandler library configuration" position)
    if(conflicting OR position EQUAL -1)
        message(FATAL_ERROR "Conflicting ${debug_switch} override was not rejected:\n${diagnostics}")
    endif()
endforeach()

enable_testing()
add_test(NAME consumer_configuration_matches_library
         COMMAND object_handler_consumer consumer_configuration_matches_library)