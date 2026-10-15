# HWR, the MIT hardware GS renderer (TASK-027). Chapter 12.10: built only with GT4_HW_GS=ON
# (default OFF until qualified). Included from native/CMakeLists.txt.
#
# Layout for later milestones: add sources to gt4_hw_backend (or a new sub-library with
# target_link_libraries(... gt4_hw_backend)). Only hw_gs_backend.cpp and the backend-internal
# files may include Vulkan headers; the public headers stay Vulkan-free.

set(GT4_HW_VULKAN_SDK "$ENV{VULKAN_SDK}" CACHE PATH "Vulkan SDK root with Include/ and Lib/vulkan-1.lib")
if(NOT EXISTS "${GT4_HW_VULKAN_SDK}/Lib/vulkan-1.lib")
    message(FATAL_ERROR "GT4_HW_GS needs a Vulkan SDK: set GT4_HW_VULKAN_SDK or VULKAN_SDK")
endif()

# Pure option parsing (GT4_RENDERER, GT4_HW_*): no Vulkan, no Windows types.
add_library(gt4_hw_options STATIC hw_gs_options.cpp)
target_compile_options(gt4_hw_options PRIVATE /W4)
target_compile_definitions(gt4_hw_options PRIVATE _CRT_SECURE_NO_WARNINGS)

# Backend, dispatch front end and device code. Vulkan is linked from the SDK import library.
add_library(gt4_hw_backend STATIC hw_gs_backend.cpp hw_gs_dispatch.cpp hw_gs_vulkan_loader.cpp)
target_include_directories(gt4_hw_backend PRIVATE "${GT4_HW_VULKAN_SDK}/Include")
target_link_libraries(gt4_hw_backend PUBLIC gt4_hw_options gt4_pacing "${GT4_HW_VULKAN_SDK}/Lib/vulkan-1.lib")
target_compile_options(gt4_hw_backend PRIVATE /W4)
target_compile_definitions(gt4_hw_backend PRIVATE _CRT_SECURE_NO_WARNINGS)

add_executable(hw_options_fixture hw_options_fixture.cpp)
target_link_libraries(hw_options_fixture PRIVATE gt4_hw_options)
target_compile_options(hw_options_fixture PRIVATE /W4)

# The shared presentation_rect lives in native_vulkan.cpp. The fixture compiles that file directly
# (it loads the Vulkan loader at run time) instead of linking the donor runtime library.
add_executable(hw_present_rect_fixture hw_present_rect_fixture.cpp "${CMAKE_CURRENT_SOURCE_DIR}/../native_vulkan.cpp")
target_include_directories(hw_present_rect_fixture PRIVATE "${GT4_HW_VULKAN_SDK}/Include")
target_link_libraries(hw_present_rect_fixture PRIVATE gt4_pacing gt4_perf_probe user32)
target_compile_definitions(hw_present_rect_fixture PRIVATE GT4_VULKAN_SHADER_DIR="")
target_compile_options(hw_present_rect_fixture PRIVATE /W4)

add_executable(hw_gs_smoke hw_gs_smoke.cpp)
target_include_directories(hw_gs_smoke PRIVATE "${GT4_HW_VULKAN_SDK}/Include")
target_link_libraries(hw_gs_smoke PRIVATE gt4_hw_backend user32)
target_compile_options(hw_gs_smoke PRIVATE /W4)
target_compile_definitions(hw_gs_smoke PRIVATE _CRT_SECURE_NO_WARNINGS)
