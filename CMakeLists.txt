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

# Build tuning candidates (TASK-010). Defaults keep the qualified code generation unchanged.
# Each option applies to every target of this tree (runtime, generated VU1 code, tests) so the
# executables and the tables they link agree. Pinned EE game library (ps2game.lib) is not affected.
option(GT4_FP_CONTRACT_OFF "Build with -ffp-contract=off (control: no fused multiply-add)" OFF)
option(GT4_X86_64_V3 "Build for x86-64-v3 (AVX2, FMA); implies GT4_FP_CONTRACT_OFF" OFF)
if(GT4_FP_CONTRACT_OFF OR GT4_X86_64_V3)
    add_compile_options(/clang:-ffp-contract=off)
endif()
if(GT4_X86_64_V3)
    add_compile_options(/clang:-march=x86-64-v3)
endif()
add_subdirectory(native)
add_subdirectory(tools/vu_recomp)

enable_testing()
set(GT4_PYTHON "${CMAKE_SOURCE_DIR}/private/tooling-venv/Scripts/python.exe"
    CACHE FILEPATH "Python environment with scripts/requirements.txt installed")
if(NOT EXISTS "${GT4_PYTHON}")
    message(FATAL_ERROR "Missing GT4_PYTHON environment; see docs/BUILD.md")
endif()
add_test(NAME native_vulkan_pixels COMMAND $<TARGET_FILE:vulkan_fixture>)
add_test(NAME native_vulkan_pixels_sync COMMAND $<TARGET_FILE:vulkan_fixture>)
set_tests_properties(native_vulkan_pixels_sync PROPERTIES RUN_SERIAL TRUE TIMEOUT 120
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "GT4_GS_SYNC=1")
add_test(NAME native_gs_submit_ring COMMAND $<TARGET_FILE:gs_submit_ring_fixture>)
add_test(NAME native_vulkan_async_stress_async
    COMMAND $<TARGET_FILE:vulkan_async_stress> "${CMAKE_BINARY_DIR}/async-stress-async.bin")
add_test(NAME native_vulkan_async_stress_sync
    COMMAND $<TARGET_FILE:vulkan_async_stress> "${CMAKE_BINARY_DIR}/async-stress-sync.bin")
add_test(NAME native_vulkan_async_stress_compare
    COMMAND ${CMAKE_COMMAND} -E compare_files "${CMAKE_BINARY_DIR}/async-stress-async.bin" "${CMAKE_BINARY_DIR}/async-stress-sync.bin")
set_tests_properties(native_gs_submit_ring native_vulkan_async_stress_async native_vulkan_async_stress_sync
    PROPERTIES RUN_SERIAL TRUE TIMEOUT 600 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(native_vulkan_async_stress_async PROPERTIES ENVIRONMENT "GT4_GS_SYNC=0")
set_tests_properties(native_vulkan_async_stress_sync PROPERTIES ENVIRONMENT "GT4_GS_SYNC=1")
set_tests_properties(native_vulkan_async_stress_compare PROPERTIES DEPENDS
    "native_vulkan_async_stress_async;native_vulkan_async_stress_sync")
add_test(NAME native_audio_input COMMAND $<TARGET_FILE:host_io_fixture>)
add_test(NAME native_pacing COMMAND $<TARGET_FILE:pacing_fixture>)
add_test(NAME native_quickfix COMMAND $<TARGET_FILE:quickfix_fixture>)
add_test(NAME native_race_display COMMAND $<TARGET_FILE:race_display_fixture>)
add_test(NAME native_shape_observer COMMAND $<TARGET_FILE:shape_observer_fixture>)
add_test(NAME native_performance_accounting COMMAND $<TARGET_FILE:perf_accounting_fixture>)
add_test(NAME native_performance_probe COMMAND $<TARGET_FILE:perf_probe_fixture>)
# TASK-011 step 0: GIF scanner, worker overlap model, interval counters, report lines, GT4_MTVU_MEASURE switch.
add_test(NAME native_mtvu_measure COMMAND $<TARGET_FILE:mtvu_measure_fixture>)
# TASK-011 step 1: SPSC ring and ordered journal (docs/MTVU-PLAN.md section 5).
add_test(NAME native_mtvu_ring COMMAND $<TARGET_FILE:mtvu_ring_fixture>)
add_test(NAME native_mtvu_journal COMMAND $<TARGET_FILE:mtvu_journal_fixture>)
add_test(NAME native_shape_capture_export COMMAND $<TARGET_FILE:shape_capture_export_fixture>)
add_test(NAME native_shape_capture_control COMMAND $<TARGET_FILE:shape_capture_control_fixture>)
add_test(NAME python_native_contracts
    COMMAND "${GT4_PYTHON}" -m unittest discover -s "${CMAKE_SOURCE_DIR}/tests/python")
set_tests_properties(native_mtvu_measure native_mtvu_ring native_mtvu_journal native_quickfix native_race_display native_vulkan_pixels native_audio_input native_shape_observer native_performance_accounting native_performance_probe native_shape_capture_export native_shape_capture_control python_native_contracts
    PROPERTIES RUN_SERIAL TRUE TIMEOUT 120
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "GT4_NATIVE_BUILD=${CMAKE_BINARY_DIR}")
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
if(TARGET gt4_vu1_aot_registry_test)
    add_test(NAME vu1_aot_registry COMMAND $<TARGET_FILE:gt4_vu1_aot_registry_test>)
    set_tests_properties(vu1_aot_registry PROPERTIES RUN_SERIAL TRUE TIMEOUT 1800
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "GT4_VU_CORPUS_DIR=${GT4_VU_CORPUS_DIR}")
endif()
add_test(NAME vu0_emitter COMMAND $<TARGET_FILE:gt4_vu0_emitter_tests>)
set_tests_properties(vu0_emitter PROPERTIES RUN_SERIAL TRUE TIMEOUT 300 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
if(TARGET gt4_vu0_aot_registry_test)
    add_test(NAME vu0_aot_registry COMMAND $<TARGET_FILE:gt4_vu0_aot_registry_test> "${GT4_VU0_IMAGE_FILE}")
    set_tests_properties(vu0_aot_registry PROPERTIES RUN_SERIAL TRUE TIMEOUT 1800 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME vu0_aot_differential COMMAND $<TARGET_FILE:gt4_vu0_differential_tests> "${GT4_VU0_IMAGE_FILE}")
    set_tests_properties(vu0_aot_differential PROPERTIES RUN_SERIAL TRUE TIMEOUT 3600 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()
