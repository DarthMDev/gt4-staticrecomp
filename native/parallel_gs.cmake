# TASK-012 / slice 162: paraLLEl-GS library, headless backend and fixtures.
# Included from native/CMakeLists.txt only when GT4_PARALLEL_GS=ON (default OFF).
# The source is the pinned checkout made by scripts/fetch_parallel_gs.py; it is verified
# against docs/inputs/parallel-gs-pin.json at configure time and is never modified.

set(GT4_PARALLEL_GS_PIN "${GT4_ROOT}/docs/inputs/parallel-gs-pin.json")
set(GT4_PARALLEL_GS_SOURCE "${GT4_ASSETS_ROOT}/private/references/parallel-gs" CACHE PATH
    "Pinned paraLLEl-GS checkout (scripts/fetch_parallel_gs.py)")
set(GT4_PARALLEL_GS_PYTHON "${GT4_ASSETS_ROOT}/private/tooling-venv/Scripts/python.exe" CACHE FILEPATH
    "Python used for the stdlib-only pin verification")
if(NOT EXISTS "${GT4_PARALLEL_GS_PYTHON}")
    message(FATAL_ERROR "Missing ${GT4_PARALLEL_GS_PYTHON}; see docs/BUILD.md")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GT4_PARALLEL_GS_PIN}")
execute_process(
    COMMAND "${GT4_PARALLEL_GS_PYTHON}" "${GT4_ROOT}/scripts/fetch_parallel_gs.py"
            --manifest "${GT4_PARALLEL_GS_PIN}" --verify-only --dest "${GT4_PARALLEL_GS_SOURCE}"
    RESULT_VARIABLE parallel_gs_verify_result
    OUTPUT_VARIABLE parallel_gs_verify_output
    ERROR_VARIABLE parallel_gs_verify_error)
if(NOT parallel_gs_verify_result EQUAL 0)
    message(FATAL_ERROR "paraLLEl-GS checkout does not match its pin: ${parallel_gs_verify_error}"
            "Run scripts/fetch_parallel_gs.py into a fresh directory (docs/BUILD.md, slice 162).")
endif()

# The vendor CMake forces /MT unless the runtime is set first; GT4Recomp links /MD (lld-link
# /failifmismatch RuntimeLibrary). Set before add_subdirectory so every vendor target inherits it.
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# Library-only paraLLEl-GS: no SDL, no runtime GLSL compiler, prebuilt SPIR-V, no DXGI interop,
# no RenderDoc, no vendor tools or install rules. CACHE FORCE matches the vendor's own options.
set(PARALLEL_GS_STANDALONE ON CACHE BOOL "" FORCE)
set(GRANITE_VULKAN_DXGI_INTEROP OFF CACHE BOOL "" FORCE)
set(GRANITE_RENDERDOC_CAPTURE OFF CACHE BOOL "" FORCE)
set(GRANITE_TOOLS OFF CACHE BOOL "" FORCE)
set(GRANITE_INSTALL_TARGETS OFF CACHE BOOL "" FORCE)
# The vendor project declares LANGUAGES C as well (volk). Use the same clang-cl for C as for C++.
if(NOT CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER "${CMAKE_CXX_COMPILER}")
endif()
enable_language(C)
add_subdirectory("${GT4_PARALLEL_GS_SOURCE}" parallel-gs EXCLUDE_FROM_ALL)

# Vendor code is third-party: keep its diagnostics out of the GT4Recomp logs. Our own
# targets keep /W4 and are unaffected because these flags are added only to vendor targets.
function(gt4_silence_vendor_targets directory)
    get_property(vendor_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(vendor_target IN LISTS vendor_targets)
        get_target_property(vendor_type ${vendor_target} TYPE)
        if(NOT vendor_type STREQUAL "INTERFACE_LIBRARY")
            target_compile_options(${vendor_target} PRIVATE -w)
        endif()
    endforeach()
    get_property(vendor_children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(vendor_child IN LISTS vendor_children)
        gt4_silence_vendor_targets("${vendor_child}")
    endforeach()
endfunction()
gt4_silence_vendor_targets("${GT4_PARALLEL_GS_SOURCE}")

# Slice 167 local patch. The pinned checkout stays clean; the patched copy lives in the build tree
# and replaces gs_interface.cpp in the vendor target. Reason (Confirmed by source reading and by the
# headless smoke, docs/PARALLEL-GS-PLAN.md section 9): GSInterface::gif_transfer sizes the optimized
# PACKED handler run with `size / nreg`, where `size` is the whole transfer, not the qwords left
# after index `i`. Our front end delivers GIF data per DMA chunk (path 3), per VIF DIRECT (path 2)
# and per XGKICK (path 1), so a GIF packet often spans several calls; the handler then reads past
# the caller's buffer and advances NLOOP too far. With fewer than NREG qwords left it would never
# advance. The patch counts the remaining qwords and sends a partial loop down the generic
# per-register path. Each anchor must occur exactly once in the pinned source.
function(gt4_replace_once text_var anchor replacement)
    string(FIND "${${text_var}}" "${anchor}" first_hit)
    string(FIND "${${text_var}}" "${anchor}" last_hit REVERSE)
    if(first_hit EQUAL -1 OR NOT first_hit EQUAL last_hit)
        message(FATAL_ERROR "paraLLEl-GS patch anchor not found exactly once: ${anchor}")
    endif()
    string(REPLACE "${anchor}" "${replacement}" patched "${${text_var}}")
    set(${text_var} "${patched}" PARENT_SCOPE)
endfunction()
set(GT4_PARALLEL_GS_VENDOR_INTERFACE "${GT4_PARALLEL_GS_SOURCE}/gs/gs_interface.cpp")
set(GT4_PARALLEL_GS_PATCHED "${CMAKE_CURRENT_BINARY_DIR}/parallel-gs-patched/gs_interface.cpp")
file(READ "${GT4_PARALLEL_GS_VENDOR_INTERFACE}" parallel_gs_interface_text)
gt4_replace_once(parallel_gs_interface_text
    "if (path.reg == 0 && optimized_draw_handler[path_index])"
    "if (path.reg == 0 && optimized_draw_handler[path_index] && size - i >= nreg) // GT4Recomp patch")
gt4_replace_once(parallel_gs_interface_text
    "std::min<uint32_t>(size / nreg, path.tag.NLOOP - path.loop)"
    "std::min<uint32_t>(uint32_t((size - i) / nreg), path.tag.NLOOP - path.loop) /* GT4Recomp patch */")
file(WRITE "${GT4_PARALLEL_GS_PATCHED}.new" "${parallel_gs_interface_text}")
file(COPY_FILE "${GT4_PARALLEL_GS_PATCHED}.new" "${GT4_PARALLEL_GS_PATCHED}" ONLY_IF_DIFFERENT)
file(REMOVE "${GT4_PARALLEL_GS_PATCHED}.new")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GT4_PARALLEL_GS_VENDOR_INTERFACE}")
get_target_property(parallel_gs_sources parallel-gs SOURCES)
list(TRANSFORM parallel_gs_sources REPLACE "^gs_interface\\.cpp$" "${GT4_PARALLEL_GS_PATCHED}")
if(NOT "${GT4_PARALLEL_GS_PATCHED}" IN_LIST parallel_gs_sources)
    message(FATAL_ERROR "paraLLEl-GS target no longer lists gs_interface.cpp; review the slice 167 patch")
endif()
set_property(TARGET parallel-gs PROPERTY SOURCES ${parallel_gs_sources})

# Granite's Vulkan headers differ from the PCSX2-tree headers used by ps2runtime_vulkan. Only this
# library and its fixture see them, through the backend translation unit.
add_library(gt4_parallel_gs STATIC parallel_gs_backend.cpp)
# /W3 rather than /W4: Granite's own headers trigger -Wmissing-field-initializers and -Wsign-compare
# when included from this unit, and those diagnostics are not ours to fix.
target_compile_options(gt4_parallel_gs PRIVATE /W3)
target_link_libraries(gt4_parallel_gs PRIVATE parallel-gs)

# SetGsCrt to SMODE1/SMODE2 mapping: pure, no GPU types, shared by the front end and the fixtures.
add_library(gt4_gs_video_mode STATIC gs_video_mode.cpp)
target_compile_options(gt4_gs_video_mode PRIVATE /W4)

add_executable(gs_video_mode_fixture gs_video_mode_fixture.cpp)
target_compile_options(gs_video_mode_fixture PRIVATE /W4)
target_link_libraries(gs_video_mode_fixture PRIVATE gt4_gs_video_mode)

add_executable(parallel_gs_fixture parallel_gs_fixture.cpp)
target_compile_options(parallel_gs_fixture PRIVATE /W4)
target_link_libraries(parallel_gs_fixture PRIVATE gt4_parallel_gs gt4_gs_video_mode)
