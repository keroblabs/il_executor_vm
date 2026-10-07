cmake_minimum_required(VERSION 3.13)

function(run_checked working_directory)
    execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${working_directory}"
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Command failed (${result}): ${ARGN}\n${output}\n${errors}")
    endif()
endfunction()

foreach(variant debug release standalone)
    set(build_directory "${OBJECT_HANDLER_MATRIX}/${variant}")
    set(prefix "${build_directory}/installed")
    set(consumer_directory "${build_directory}/consumer")
    file(MAKE_DIRECTORY "${build_directory}" "${consumer_directory}")
    set(source_directory "${OBJECT_HANDLER_ROOT}")
    set(component_directory "${build_directory}/source/object_handler")
    set(build_type Debug)
    set(build_configuration Debug)
    set(consumer_type Release)
    set(sanitize OFF)
    set(standard 99)
    if(variant STREQUAL debug)
        set(sanitize ON)
    elseif(variant STREQUAL release)
        set(build_type Release)
        set(build_configuration Release)
        set(consumer_type Debug)
        set(standard 11)
    else()
        set(source_directory "${OBJECT_HANDLER_ROOT}/source/object_handler")
        set(component_directory "${build_directory}")
        set(build_type "")
    endif()

    run_checked("${build_directory}" "${CMAKE_COMMAND}"
        -S "${source_directory}" -B "${build_directory}"
        -G "${OBJECT_HANDLER_GENERATOR}"
        "-DCMAKE_BUILD_TYPE=${build_type}"
        "-DCMAKE_C_COMPILER=${OBJECT_HANDLER_COMPILER}"
        "-DOBJECT_HANDLER_C_STANDARD=${standard}"
        "-DOBJECT_HANDLER_SANITIZE=${sanitize}"
        -DOBJECT_HANDLER_TEST_BUILD_MATRIX=OFF)
    run_checked("${build_directory}" "${CMAKE_COMMAND}" --build "${build_directory}"
        --config "${build_configuration}" --target test_objects test_heap test_array_string
        test_value test_spaces test_instances test_stack test_consumer_config)
    run_checked("${build_directory}" "${CMAKE_CTEST_COMMAND}"
        -C "${build_configuration}" --output-on-failure)
    set(install_configuration "${build_type}")
    if(OBJECT_HANDLER_GENERATOR MATCHES "Multi-Config|Visual Studio|Xcode")
        set(install_configuration "${build_configuration}")
    endif()
    run_checked("${build_directory}" "${CMAKE_COMMAND}"
        "-DCMAKE_INSTALL_PREFIX=${prefix}"
        "-DCMAKE_INSTALL_CONFIG_NAME=${install_configuration}"
        -P "${component_directory}/cmake_install.cmake")

    run_checked("${consumer_directory}" "${CMAKE_COMMAND}"
        -S "${OBJECT_HANDLER_TESTS}/consumer" -B "${consumer_directory}"
        -G "${OBJECT_HANDLER_GENERATOR}"
        "-DCMAKE_PREFIX_PATH=${prefix}"
        "-DCMAKE_BUILD_TYPE=${consumer_type}"
        "-DCMAKE_C_COMPILER=${OBJECT_HANDLER_COMPILER}")
    run_checked("${consumer_directory}" "${CMAKE_COMMAND}" --build "${consumer_directory}"
        --config "${consumer_type}")
    run_checked("${consumer_directory}" "${CMAKE_CTEST_COMMAND}"
        -C "${consumer_type}" --output-on-failure)
    message(STATUS "${variant}: library suite, installed opposite-NDEBUG consumer and override checks passed")
endforeach()