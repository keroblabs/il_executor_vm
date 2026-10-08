add_executable(test_executor test_executor.c)
target_link_libraries(test_executor PRIVATE il_executor object_handler_harness)
target_include_directories(test_executor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../source/methods)
set_target_properties(test_executor PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_executor PRIVATE
        -Wall -Wextra -Werror -pedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes)
endif()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS test_executor.c)
file(STRINGS test_executor.c cases REGEX "^TEST_CASE\\([a-z0-9_]+\\)")
foreach(line ${cases})
    string(REGEX REPLACE "^TEST_CASE\\(([a-z0-9_]+)\\).*" "\\1" case "${line}")
    add_test(NAME test_executor.${case} COMMAND test_executor ${case})
    set_tests_properties(test_executor.${case} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()
