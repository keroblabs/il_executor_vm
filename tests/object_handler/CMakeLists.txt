cmake_minimum_required(VERSION 3.13)

# Harness (layer 1) and fake embedder (layer 2) of doc 18 section 2.
add_library(object_handler_harness STATIC harness.c fake_embedder.c)
target_include_directories(object_handler_harness PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(object_handler_harness PUBLIC object_handler)

function(object_handler_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} object_handler_harness)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${name}.c)

    # One ctest, and so one process, per TEST_CASE (doc 18 section 2.2).
    file(STRINGS ${name}.c cases REGEX "^TEST_CASE\\([a-z0-9_]+\\)")
    foreach(line ${cases})
        string(REGEX REPLACE "^TEST_CASE\\(([a-z0-9_]+)\\).*" "\\1" case "${line}")
        add_test(NAME ${name}.${case} COMMAND ${name} ${case})
        set_tests_properties(${name}.${case} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()
endfunction()

object_handler_test(test_objects)
object_handler_test(test_heap)
object_handler_test(test_array_string)
object_handler_test(test_value)
object_handler_test(test_spaces)
object_handler_test(test_instances)
object_handler_test(test_stack)

add_executable(test_consumer_config test_objects.c)
target_link_libraries(test_consumer_config object_handler_harness)
target_compile_definitions(test_consumer_config PRIVATE OBJECT_HANDLER_TEST_OPPOSITE_NDEBUG)
add_test(NAME test_consumer_config.opposite_ndebug
         COMMAND test_consumer_config consumer_configuration_matches_library)

option(OBJECT_HANDLER_TEST_BUILD_MATRIX "Test build variants and installed consumers" ON)
if(OBJECT_HANDLER_TEST_BUILD_MATRIX)
    add_test(NAME test_build_config.consumer_matrix
        COMMAND ${CMAKE_COMMAND}
            "-DOBJECT_HANDLER_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/../.."
            "-DOBJECT_HANDLER_TESTS=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DOBJECT_HANDLER_MATRIX=${CMAKE_CURRENT_BINARY_DIR}/build_config"
            "-DOBJECT_HANDLER_COMPILER=${CMAKE_C_COMPILER}"
            "-DOBJECT_HANDLER_GENERATOR=${CMAKE_GENERATOR}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/test_build_config.cmake)
endif()

foreach(target object_handler_harness test_objects test_heap test_array_string test_value test_spaces
               test_instances test_stack test_consumer_config)
    set_target_properties(${target} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -pedantic)
    endif()
endforeach()
