#pragma once

// Paths of an installed game (TASK-059). An installed game runs from its own folder: the shaders sit in
// <executable folder>\shaders and the GS package in <executable folder>\gs. No path of the build machine is needed.

#include <filesystem>

namespace gt4::install {

// The folder of the running executable, or an empty path when Windows cannot report it.
std::filesystem::path executable_directory();

// <executable folder>\shaders when that folder exists, else the build-time shader folder (development builds).
std::filesystem::path choose_shader_directory(const std::filesystem::path &build_time_directory);

// <executable folder>\gs\gt4-gs.exe when that file exists, else an empty path. GT4_GS_PROCESS overrides it at the caller.
std::filesystem::path installed_gs_process();

// `gt4vulkan.exe --portable-check`: prints the paths the game would use and returns 0 when the required folders are
// present, 1 otherwise. Nothing is loaded and no window is created.
int run_portable_check(const std::filesystem::path &build_time_shaders);

} // namespace gt4::install
