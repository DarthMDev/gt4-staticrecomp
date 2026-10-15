cmake_minimum_required(VERSION 3.24)
project(GT4Recomp VERSION 0.2.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The qualified target is Windows x86-64")
endif()
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR NOT MSVC)
    message(FATAL_ERROR "Use clang-cl with the Visual Studio x64 environment")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_subdirectory(native)
add_subdirectory(tools/vu_recomp)

enable_testing()
set(GT4_PYTHON "${CMAKE_SOURCE_DIR}/private/tooling-venv/Scripts/python.exe"
    CACHE FILEPATH "Python environment with scripts/requirements.txt installed")
if(NOT EXISTS "${GT4_PYTHON}")
    message(FATAL_ERROR "Missing GT4_PYTHON environment; see docs/BUILD.md")
endif()
add_test(NAME native_vulkan_pixels COMMAND $<TARGET_FILE:vulkan_fixture>)
add_test(NAME native_audio_input COMMAND $<TARGET_FILE:host_io_fixture>)
add_test(NAME native_shape_observer COMMAND $<TARGET_FILE:shape_observer_fixture>)
add_test(NAME native_performance_accounting COMMAND $<TARGET_FILE:perf_accounting_fixture>)
add_test(NAME native_performance_probe COMMAND $<TARGET_FILE:perf_probe_fixture>)
add_test(NAME native_shape_capture_export COMMAND $<TARGET_FILE:shape_capture_export_fixture>)
add_test(NAME native_shape_capture_control COMMAND $<TARGET_FILE:shape_capture_control_fixture>)
add_test(NAME python_native_contracts
    COMMAND "${GT4_PYTHON}" -m unittest discover -s "${CMAKE_SOURCE_DIR}/tests/python")
set_tests_properties(native_vulkan_pixels native_audio_input native_shape_observer native_performance_accounting native_performance_probe native_shape_capture_export native_shape_capture_control python_native_contracts
    PROPERTIES RUN_SERIAL TRUE TIMEOUT 120
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "GT4_ASPECT=16:9;GT4_NATIVE_BUILD=${CMAKE_BINARY_DIR}")
if(TARGET shape_runtime_fixture)
    add_test(NAME native_shape_runtime COMMAND $<TARGET_FILE:shape_runtime_fixture>)
    set_tests_properties(native_shape_runtime PROPERTIES RUN_SERIAL TRUE TIMEOUT 120
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_property(TEST python_native_contracts APPEND PROPERTY ENVIRONMENT "GT4_SHAPE_RUNTIME_TESTS=1")
endif()
add_test(NAME vu_recomp_core COMMAND $<TARGET_FILE:gt4_vu_recomp_core_tests>)
add_test(NAME vu_recomp_differential COMMAND $<TARGET_FILE:gt4_vu_differential_tests>)
add_test(NAME vu_recomp_corpus COMMAND $<TARGET_FILE:gt4_vu_corpus_differential>)
set_tests_properties(vu_recomp_core vu_recomp_differential vu_recomp_corpus PROPERTIES RUN_SERIAL TRUE TIMEOUT 1800
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(vu_recomp_corpus PROPERTIES ENVIRONMENT "GT4_VU_CORPUS_DIR=${GT4_VU_CORPUS_DIR}")
