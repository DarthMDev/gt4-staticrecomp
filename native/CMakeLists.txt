cmake_minimum_required(VERSION 3.24)
project(GT4NativeVulkan LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(GT4_ROOT ".." ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(DONOR "${GT4_ROOT}/private/references/ps2-web-recomp")
# Experiment producers use their own derived adapter and shader directories so
# a candidate never overwrites the frozen slice114 output. The defaults keep
# the qualified baseline selection when the caller overrides nothing.
set(GT4_PORT_DIR "${GT4_ROOT}/private/slice114-native-base/port" CACHE PATH
    "Directory with the derived donor GPU adapter sources")
set(GT4_SHADER_DIR "${GT4_ROOT}/private/slice114-native-base/shaders" CACHE PATH
    "Directory with the qualified SPIR-V modules")
set(PORT "${GT4_PORT_DIR}")
set(SHADERS "${GT4_SHADER_DIR}")
set(VULKAN_HEADERS "${GT4_ROOT}/private/tooling/pcsx2-slice101-source/3rdparty/vulkan/include")
set(GAME_LIBRARY "${GT4_ROOT}/private/reference-acceleration-study/runtime-build/ps2game.lib")
if(NOT EXISTS "${GAME_LIBRARY}")
    message(FATAL_ERROR "Missing private AOT game library; see docs/BUILD.md")
endif()
file(SHA256 "${GAME_LIBRARY}" game_library_hash)
if(NOT game_library_hash STREQUAL "c837d13dae027f377aecdb0c39834ede2e9463eabf7f767fda6a3c5a0762de1f")
    message(FATAL_ERROR "AOT game library differs from the qualified USA v2.00 producer")
endif()
set(RUNTIME_SOURCES memory dispatch system kernel hw cop disc dmac vif vu_micro vu_jit vu_jit_web)
set(RUNTIME_FILES)
foreach(source IN LISTS RUNTIME_SOURCES)
    list(APPEND RUNTIME_FILES "${DONOR}/runtime/src/${source}.cpp")
endforeach()
foreach(source IN ITEMS ipu/ipu gs/gif gs/queue gs/raster
                        iop/cpu iop/kernel iop/libs iop/hw iop/spu2 iop/sio2
                        iop/io iop/cdvd iop/sif iop/cdvdfsv)
    list(APPEND RUNTIME_FILES "${DONOR}/runtime/src/${source}.cpp")
endforeach()
add_library(ps2runtime_vulkan STATIC ${RUNTIME_FILES} "${PORT}/gs.cpp" "${PORT}/gpu.cpp" native_vulkan.cpp)
target_include_directories(ps2runtime_vulkan PUBLIC "${DONOR}/runtime/include" "${DONOR}/runtime/src"
                           "${DONOR}/runtime/src/gs" "${CMAKE_CURRENT_SOURCE_DIR}" "${VULKAN_HEADERS}")
target_compile_definitions(ps2runtime_vulkan PUBLIC PS2_GPU _CRT_SECURE_NO_WARNINGS
                           GT4_VULKAN_SHADER_DIR="${SHADERS}")
target_compile_options(ps2runtime_vulkan PRIVATE /W4)
add_library(native_host_io STATIC host_io.cpp)
target_link_libraries(native_host_io PUBLIC winmm xinput user32)
target_compile_options(native_host_io PRIVATE /W4)
add_executable(gt4vulkan "${PORT}/main.cpp" window.cpp)
target_include_directories(gt4vulkan PRIVATE "${PORT}")
target_compile_definitions(gt4vulkan PRIVATE PS2_WINDOW)
target_link_libraries(gt4vulkan PRIVATE
    "${GAME_LIBRARY}"
    ps2runtime_vulkan native_host_io user32)
add_executable(vulkan_fixture fixture.cpp)
target_include_directories(vulkan_fixture PRIVATE "${PORT}")
target_link_libraries(vulkan_fixture PRIVATE ps2runtime_vulkan user32)
target_compile_options(gt4vulkan PRIVATE /W4)
target_compile_options(vulkan_fixture PRIVATE /W4)
add_executable(host_io_fixture host_io_fixture.cpp)
target_link_libraries(host_io_fixture PRIVATE native_host_io)
target_compile_options(host_io_fixture PRIVATE /W4)
# Host-only diagnostics: no Vulkan device, no guest code.
add_executable(perf_probe_fixture perf_probe_fixture.cpp)
target_compile_options(perf_probe_fixture PRIVATE /W4)
